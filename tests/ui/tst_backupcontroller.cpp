#include "ui/AppController.h"
#include "ui/Settings.h"
#include "ui/backup/BackupController.h"
#include "ui/backup/BackupSectionModel.h"

#include "core/codec/PhoneVCard.h"
#include "core/codec/VNote.h"
#include "platform/SecretStore.h"

#include "FakePhone.h"

#include <QFile>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>
#include <QUrl>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::backup::BackupSection;
using qpm::test::FakePhone;
using qpm::ui::AppController;
using qpm::ui::BackupController;
using qpm::ui::BackupSectionModel;
using qpm::ui::Settings;

namespace {

constexpr int timeoutMs = 15000;

QByteArray contactCard(quint32 luid, const QString &given, const QString &number)
{
    Contact contact;
    contact.luid = luid;
    contact.givenName = given;
    contact.displayName = given;
    ContactValue phone;
    phone.dataId = luid * 10;
    phone.type = phoneType::mobile;
    phone.value = number;
    contact.phones = {phone};
    return encodeAsPhoneSendsIt(contact);
}

QByteArray textMessage()
{
    return "BEGIN:VMESSAGE\r\nTYP:SMS;IN\r\nBOX:INBOX\r\nREAD:1\r\nDATE:20261110T080000Z\r\n"
           "NUMBER:+4915112345678\r\nBODY;CHARSET=UTF-8:Grüße aus Köln\r\n"
           "X-IRMC-LUID:00000000000B\r\nEND:VMESSAGE\r\n";
}

QString detailOf(const BackupSectionModel *model, BackupSection section)
{
    return model->data(model->index(model->rowOf(section)), BackupSectionModel::DetailRole)
        .toString();
}

bool selectable(const BackupSectionModel *model, BackupSection section)
{
    return model->data(model->index(model->rowOf(section)), BackupSectionModel::SelectableRole)
        .toBool();
}

} // namespace

class TestBackupController : public QObject
{
    Q_OBJECT

public:
    explicit TestBackupController(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void backupIsSavedWithItsCountsAndTheHint();
    void previewOffersWhatCanBeRestored();
    void restoreReportsPerSection();
    void withoutConnectionOnlyMessagesCanBeChosen();
    void fileThatIsNoBackupIsReported();
    void chosenKeysFollowTheChoice();
    void stopWhileReadingKeepsTheComparisonFromStarting();

private:
    [[nodiscard]] BackupController *backup() const { return m_controller->backup(); }
    [[nodiscard]] bool saveBackup(const QString &path);
    [[nodiscard]] bool previewBackup(const QString &path);
    [[nodiscard]] bool syncMessages();

    QTranslator m_translator;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<AppController> m_controller;
};

// The English catalogue gives the counted texts their plural forms.
void TestBackupController::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    QVERIFY(m_translator.load(QStringLiteral(QPM_TRANSLATIONS_DIR "/qphonemanager_en.qm")));
    QCoreApplication::installTranslator(&m_translator);
}

void TestBackupController::cleanupTestCase()
{
    QCoreApplication::removeTranslator(&m_translator);
}

void TestBackupController::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setDataDirectory(m_dir->filePath(QStringLiteral("profiles")));
    m_settings->setAdbPath(m_dir->filePath(QStringLiteral("no-adb")));
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(
        QStringLiteral("pb"),
        {{1, contactCard(1, QStringLiteral("Erika"), QStringLiteral("+4915112345678"))},
         {2, contactCard(2, QStringLiteral("Jörg"), QStringLiteral("+4922120000050"))}});
    Note note;
    note.luid = 1;
    note.body = QStringLiteral("Einkauf");
    m_phone->setObjects(QStringLiteral("notes"), {{1, encodeNoteAsPhoneSendsIt(note)}});
    m_phone->setObjects(QStringLiteral("msg"), {{11, textMessage()}});

    AppController::Environment environment;
    environment.listenPort = 0;
    environment.listenAddress = QHostAddress(QHostAddress::LocalHost);
    environment.searchNetwork = false;
    environment.searchBluetooth = false;
    m_controller = std::make_unique<AppController>(m_settings.get(), nullptr, environment);
    QVERIFY(m_controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port())));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->isConnected(), timeoutMs);
    QVERIFY(backup()->hasPhone());
    QVERIFY(backup()->isConnected());
    const QFuture<Result<store::ContactChanges>> contacts = m_controller->contacts()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(contacts.isFinished(), timeoutMs);
    QVERIFY(contacts.result().hasValue());
    const QFuture<Result<store::OrganizerChanges>> notes = m_controller->notes()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(notes.isFinished(), timeoutMs);
    QVERIFY(notes.result().hasValue());
    QVERIFY(syncMessages());
}

void TestBackupController::cleanup()
{
    m_controller.reset();
    m_phone.reset();
    m_settings.reset();
    m_dir.reset();
}

bool TestBackupController::saveBackup(const QString &path)
{
    if (!backup()->create(QUrl::fromLocalFile(path), backup()->sections()->chosenKeys()))
        return false;
    return QTest::qWaitFor([this]() { return !backup()->isBusy(); }, timeoutMs) &&
           !backup()->statusIsError();
}

bool TestBackupController::syncMessages()
{
    const QFuture<Result<store::MessageChanges>> pulled = m_controller->messages()->sync();
    return QTest::qWaitFor([&pulled]() { return pulled.isFinished(); }, timeoutMs) &&
           pulled.result().hasValue();
}

bool TestBackupController::previewBackup(const QString &path)
{
    if (!backup()->preview(QUrl::fromLocalFile(path)))
        return false;
    return QTest::qWaitFor([this]() { return !backup()->isBusy(); }, timeoutMs) &&
           backup()->hasPreview();
}

void TestBackupController::backupIsSavedWithItsCountsAndTheHint()
{
    QCOMPARE(backup()->sections()->chosenKeys().size(), 6);
    const QUrl suggested = backup()->suggestedFileUrl(QUrl::fromLocalFile(m_dir->path()));
    QVERIFY(suggested.toLocalFile().startsWith(m_dir->path()));
    QVERIFY(suggested.fileName().endsWith(QLatin1StringView(".qpmbackup")));
    QSignalSpy busy(backup(), &BackupController::busyChanged);

    QVERIFY(saveBackup(suggested.toLocalFile()));

    QCOMPARE(busy.size(), 2);
    QVERIFY(QFile::exists(suggested.toLocalFile()));
    QVERIFY(backup()->statusText().contains(QLatin1StringView("not encrypted")));
    QCOMPARE(detailOf(backup()->sections(), BackupSection::Contacts),
             QStringLiteral("2 entries saved"));
    QCOMPARE(detailOf(backup()->sections(), BackupSection::Notes), QStringLiteral("1 entry saved"));
    QCOMPARE(backup()->progress(), -1.0);
}

void TestBackupController::previewOffersWhatCanBeRestored()
{
    const QString path = m_dir->filePath(QStringLiteral("phone.qpmbackup"));
    QVERIFY(saveBackup(path));
    m_phone->removeObject(QStringLiteral("pb"), 2);

    QVERIFY(previewBackup(path));

    QVERIFY(!backup()->statusIsError());
    QVERIFY(backup()->archiveText().contains(QLatin1StringView("Example Phone X1")));
    const BackupSectionModel *sections = backup()->restoreSections();
    QVERIFY(selectable(sections, BackupSection::Contacts));
    QVERIFY(!selectable(sections, BackupSection::Notes));
    QVERIFY(!selectable(sections, BackupSection::Calls));
    QCOMPARE(detailOf(sections, BackupSection::Contacts),
             QStringLiteral("New: 1, changed: 0, unchanged: 1."));
    QCOMPARE(detailOf(sections, BackupSection::Calls),
             QStringLiteral("0 calls. They stay in the backup, because the app on the phone may "
                            "not change the call log."));
    QCOMPARE(sections->chosenKeys(), QStringList{QStringLiteral("contacts")});
}

void TestBackupController::restoreReportsPerSection()
{
    const QString path = m_dir->filePath(QStringLiteral("phone.qpmbackup"));
    QVERIFY(saveBackup(path));
    m_phone->removeObject(QStringLiteral("pb"), 2);
    QVERIFY(previewBackup(path));

    QVERIFY(!backup()->restore(QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("other"))),
                               {QStringLiteral("contacts")}));
    QVERIFY(backup()->statusIsError());
    QVERIFY(
        backup()->restore(QUrl::fromLocalFile(path), backup()->restoreSections()->chosenKeys()));
    QTRY_VERIFY_WITH_TIMEOUT(!backup()->isBusy(), timeoutMs);

    QCOMPARE(backup()->statusText(), QStringLiteral("The restore is done."));
    QCOMPARE(detailOf(backup()->restoreSections(), BackupSection::Contacts),
             QStringLiteral("1 entry restored."));
    QVERIFY(backup()->restoreSections()->chosenKeys().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->contacts()->list()->totalCount(), 2, timeoutMs);
}

void TestBackupController::withoutConnectionOnlyMessagesCanBeChosen()
{
    const QString path = m_dir->filePath(QStringLiteral("phone.qpmbackup"));
    QVERIFY(saveBackup(path));
    m_phone->removeObject(QStringLiteral("pb"), 2);
    m_phone->removeObject(QStringLiteral("msg"), 11);
    QVERIFY(syncMessages());
    m_controller->disconnectPhone();
    QTRY_VERIFY(!backup()->isConnected());

    QVERIFY(previewBackup(path));

    QVERIFY(backup()->statusText().contains(QLatin1StringView("not connected")));
    QVERIFY(!selectable(backup()->restoreSections(), BackupSection::Contacts));
    QVERIFY(selectable(backup()->restoreSections(), BackupSection::Messages));
    QCOMPARE(backup()->restoreSections()->chosenKeys(), QStringList{QStringLiteral("messages")});
    QVERIFY(!backup()->restore(QUrl::fromLocalFile(path), {QStringLiteral("contacts")}));
    QCOMPARE(backup()->statusText(), QStringLiteral("Connect the phone first, then restore."));

    QVERIFY(backup()->restore(QUrl::fromLocalFile(path), {QStringLiteral("messages")}));
    QTRY_VERIFY_WITH_TIMEOUT(!backup()->isBusy(), timeoutMs);
    QCOMPARE(backup()->statusText(), QStringLiteral("The restore is done."));
    QCOMPARE(detailOf(backup()->restoreSections(), BackupSection::Messages),
             QStringLiteral("1 entry restored."));
}

void TestBackupController::fileThatIsNoBackupIsReported()
{
    const QString path = m_dir->filePath(QStringLiteral("text.qpmbackup"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Hallo");
    file.close();

    QVERIFY(backup()->preview(QUrl::fromLocalFile(path)));
    QTRY_VERIFY_WITH_TIMEOUT(!backup()->isBusy(), timeoutMs);

    QVERIFY(backup()->statusIsError());
    QCOMPARE(backup()->statusText(),
             QStringLiteral("This file is not a QPhoneManager backup, or it is damaged. Choose "
                            "another file."));
    QVERIFY(!backup()->hasPreview());
    QVERIFY(!backup()->create(QUrl(QStringLiteral("https://example.org/a.qpmbackup")),
                              {QStringLiteral("contacts")}));
    QCOMPARE(backup()->statusText(), QStringLiteral("Choose a file on this computer."));
}

void TestBackupController::chosenKeysFollowTheChoice()
{
    BackupSectionModel *sections = backup()->sections();
    QSignalSpy chosen(sections, &BackupSectionModel::chosenChanged);

    sections->setChosen(sections->rowOf(BackupSection::Messages), false);
    sections->setChosen(sections->rowOf(BackupSection::Calls), false);

    QCOMPARE(chosen.size(), 2);
    QCOMPARE(sections->chosenKeys(),
             (QStringList{QStringLiteral("contacts"), QStringLiteral("calendar"),
                          QStringLiteral("tasks"), QStringLiteral("notes")}));
    QVERIFY(!backup()->create(QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("x"))), {}));
    QCOMPARE(backup()->statusText(), QStringLiteral("Choose at least one part to back up."));
}

// The file is read on a worker; a stop that arrives before it is done must
// still keep the comparison with the phone from starting.
void TestBackupController::stopWhileReadingKeepsTheComparisonFromStarting()
{
    const QString path = m_dir->filePath(QStringLiteral("phone.qpmbackup"));
    QVERIFY(saveBackup(path));
    m_phone->clearLog();

    QVERIFY(backup()->preview(QUrl::fromLocalFile(path)));
    backup()->cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!backup()->isBusy(), timeoutMs);

    QCOMPARE(backup()->statusText(), QStringLiteral("Stopped."));
    QVERIFY(!backup()->statusIsError());
    QVERIFY(!backup()->hasPreview());
    const QStringList commands = m_phone->receivedCommands();
    for (const QString &command : commands)
        QVERIFY2(!command.startsWith(QLatin1StringView("OBEX")), qPrintable(command));
}

QTEST_MAIN(TestBackupController)

#include "tst_backupcontroller.moc"
