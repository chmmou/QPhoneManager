#include "ui/AppController.h"
#include "ui/PhoneNumberText.h"
#include "ui/Settings.h"
#include "ui/calls/CallListModel.h"
#include "ui/calls/CallsArea.h"
#include "ui/messages/ConversationListModel.h"
#include "ui/messages/MessageComposer.h"
#include "ui/messages/MessageListModel.h"
#include "ui/messages/MessagesArea.h"
#include "ui/messages/ScheduledListModel.h"

#include "core/codec/CsvTable.h"
#include "core/codec/MessageExport.h"
#include "core/codec/PhoneTime.h"
#include "core/codec/PhoneVCard.h"
#include "core/sync/IrmcSync.h"
#include "platform/SecretStore.h"

#include "FakePhone.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>
#include <QUrl>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::ui::AppController;
using qpm::ui::ConversationListModel;
using qpm::ui::MessageComposer;
using qpm::ui::MessageListModel;
using qpm::ui::MessagesArea;
using qpm::ui::Settings;

namespace {

constexpr int timeoutMs = 15000;

QByteArray sms(quint32 luid, bool incoming, const QString &number, const QString &text,
               const QDateTime &when, bool read = true)
{
    QByteArray data = "BEGIN:VMESSAGE\r\nTYP:SMS;" + QByteArray(incoming ? "IN" : "OUT") + "\r\n";
    data += incoming ? "BOX:INBOX\r\nREAD:" + QByteArray(read ? "1" : "0") + "\r\n"
                     : QByteArray("BOX:SENT\r\n");
    data += "DATE:" + when.toUTC().toString(QStringLiteral("yyyyMMddTHHmmssZ")).toLatin1() + "\r\n";
    data += "NUMBER:" + number.toUtf8() + "\r\n";
    data += "BODY;CHARSET=UTF-8:" + text.toUtf8() + "\r\n";
    data += "X-IRMC-LUID:" + sync::IrmcSync::luidText(luid).toLatin1() + "\r\nEND:VMESSAGE\r\n";
    return data;
}

QByteArray photoMessage()
{
    return "BEGIN:VMESSAGE\r\nTYP:MMS;IN\r\nBOX:INBOX\r\nREAD:1\r\nDATE:20260101T100000Z\r\n"
           "NUMBER:+49 40 2000006\r\n"
           "ATT;NAME=..=2F..=2F.bashrc;TYPE=image/png;SIZE=3;ENCODING=BASE64:UE5H\r\n\r\n"
           "ATT;NAME=film.mp4;TYPE=video/mp4;SIZE=20971520;URI=content=3A//mms/part/7:NULL\r\n"
           "X-IRMC-LUID:000010000009\r\nEND:VMESSAGE\r\n";
}

Contact erika()
{
    Contact contact;
    contact.luid = 1;
    contact.familyName = QStringLiteral("Mustermann");
    contact.givenName = QStringLiteral("Erika");
    contact.displayName = QStringLiteral("Erika Mustermann");
    ContactValue phone;
    phone.dataId = 11;
    phone.type = phoneType::mobile;
    phone.value = QStringLiteral("+49 151 1234567");
    contact.phones = {phone};
    return contact;
}

QDateTime hoursAgo(int hours)
{
    return QDateTime::currentDateTime().addSecs(-3600LL * hours);
}

QString role(const QAbstractItemModel *model, int row, const QByteArray &name)
{
    return model->data(model->index(row, 0), model->roleNames().key(name)).toString();
}

QVariant roleValue(const QAbstractItemModel *model, int row, const QByteArray &name)
{
    return model->data(model->index(row, 0), model->roleNames().key(name));
}

} // namespace

class TestMessagesArea : public QObject
{
    Q_OBJECT

public:
    explicit TestMessagesArea(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void conversationsShowContactNamesNewestFirst();
    void searchFindsNamesNumbersAndText();
    void conversationShowsItsMessagesOldestFirstWithParts();
    void composerCountsCharactersAndMessages();
    void composerTakesOnlyNumbersAndSendsToEach();
    void numberHandedOverOpensTheComposer();
    void scheduledMessageIsPlannedChangedAndListed();
    void scheduleNeedsATimeAhead();
    void scheduleDateKeepsItsCentury();
    void refusedDeletionPointsToTheArchive();
    void archivedConversationIsMarked();
    void readStateRefusalIsExplained();
    void exportWritesEveryFormatAndTheArchiveReadsBack();
    void attachmentIsSavedWhereTheUserChose();
    void allAttachmentsGoIntoTheFolderWithoutOverwriting();
    void partLeftOnThePhoneIsExplained();
    void namesFromThePhoneBecomeSafeFileNames_data();
    void namesFromThePhoneBecomeSafeFileNames();
    void newMessageIsAnnounced();
    void sendingNeedsAConnection();
    void numbersAreWrittenInOneForm_data();
    void numbersAreWrittenInOneForm();
    void numberLooksAlikeInConversationRecipientAndCallList();

private:
    [[nodiscard]] MessagesArea *area() const { return m_controller->messages(); }

    QTranslator m_translator;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<AppController> m_controller;
};

void TestMessagesArea::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    // The English catalogue holds the singular and plural forms of the texts.
    QVERIFY(m_translator.load(QStringLiteral(QPM_TRANSLATIONS_DIR "/qphonemanager_en.qm")));
    QCoreApplication::installTranslator(&m_translator);
}

void TestMessagesArea::cleanupTestCase()
{
    QCoreApplication::removeTranslator(&m_translator);
}

void TestMessagesArea::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setDataDirectory(m_dir->filePath(QStringLiteral("profiles")));
    m_settings->setAdbPath(m_dir->filePath(QStringLiteral("no-adb")));
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->config().scriptedReplies.insert(
        QStringLiteral("AT*SIM"), {QByteArrayLiteral("AT*SIM\r\n*SIM: 0,,Privat,Beispielnetz,VS\r\n"
                                                     "*SIM: 1,,Arbeit,Beispielnetz,\r\nOK\r\n")});
    m_phone->setObjects(QStringLiteral("pb"), {{1, encodeAsPhoneSendsIt(erika())}});
    m_phone->setObjects(
        QStringLiteral("msg"),
        {{11, sms(11, true, QStringLiteral("+491511234567"), QStringLiteral("Hallo, wie geht's?"),
                  hoursAgo(3), false)},
         {12, sms(12, false, QStringLiteral("0151 1234567"), QStringLiteral("Gut, danke!"),
                  hoursAgo(2))},
         {13, sms(13, true, QStringLiteral("Paketdienst"), QStringLiteral("Ihr Paket kommt heute."),
                  QDateTime(QDate::currentDate().addDays(-1), QTime(12, 0)))},
         {0x10000009, photoMessage()}});

    AppController::Environment environment;
    environment.listenPort = 0;
    environment.listenAddress = QHostAddress(QHostAddress::LocalHost);
    environment.searchNetwork = false;
    environment.searchBluetooth = false;
    m_controller = std::make_unique<AppController>(m_settings.get(), nullptr, environment);
    QVERIFY(m_controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port())));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->isConnected(), timeoutMs);
    QVERIFY(area()->hasPhone());
    QVERIFY(area()->canEdit());
    const QFuture<Result<store::ContactChanges>> contacts = m_controller->contacts()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(contacts.isFinished(), timeoutMs);
    QVERIFY(contacts.result().hasValue());
    const QFuture<Result<store::MessageChanges>> messages = area()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(messages.isFinished(), timeoutMs);
    QVERIFY(messages.result().hasValue());
    QCOMPARE(area()->conversations()->totalCount(), 3);
}

void TestMessagesArea::cleanup()
{
    m_controller.reset();
    m_phone.reset();
    m_settings.reset();
    m_dir.reset();
}

void TestMessagesArea::conversationsShowContactNamesNewestFirst()
{
    const ConversationListModel *list = area()->conversations();

    QCOMPARE(role(list, 0, "title"), QStringLiteral("Erika Mustermann"));
    QCOMPARE(role(list, 0, "snippet"), QStringLiteral("Gut, danke!"));
    QCOMPARE(roleValue(list, 0, "unread").toInt(), 1);
    QCOMPARE(roleValue(list, 0, "messageCount").toInt(), 2);
    QCOMPARE(role(list, 0, "initials"), QStringLiteral("EM"));
    QCOMPARE(role(list, 1, "title"), QStringLiteral("Paketdienst"));
    QCOMPARE(role(list, 1, "dateText"), QStringLiteral("Yesterday"));
    QCOMPARE(role(list, 1, "initials"), QStringLiteral("#"));
    QCOMPARE(role(list, 2, "title"), QStringLiteral("+49 40 2000006"));
    QVERIFY(roleValue(list, 2, "hasAttachments").toBool());
}

void TestMessagesArea::searchFindsNamesNumbersAndText()
{
    const ConversationListModel *list = area()->conversations();

    area()->setSearchText(QStringLiteral("mustermann"));
    QCOMPARE(list->count(), 1);
    area()->setSearchText(QStringLiteral("402000"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(role(list, 0, "title"), QStringLiteral("+49 40 2000006"));
    area()->setSearchText(QStringLiteral("paket heute"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(role(list, 0, "title"), QStringLiteral("Paketdienst"));
    area()->setSearchText(QStringLiteral("nirgends"));
    QCOMPARE(list->count(), 0);
    QCOMPARE(list->totalCount(), 3);
    area()->setSearchText(QString());
    QCOMPARE(list->count(), 3);
}

void TestMessagesArea::conversationShowsItsMessagesOldestFirstWithParts()
{
    QSignalSpy changed(area(), &MessagesArea::currentChanged);

    area()->setCurrentThread(area()->conversations()->threadKeyAt(0));

    QVERIFY(changed.count() >= 1);
    QVERIFY(area()->hasCurrent());
    QCOMPARE(area()->currentTitle(), QStringLiteral("Erika Mustermann"));
    // The newest message of the conversation names the number.
    QCOMPARE(area()->currentNumber(), QStringLiteral("01511234567"));
    QCOMPARE(area()->currentUnread(), 1);
    const MessageListModel *messages = area()->messages();
    QCOMPARE(messages->count(), 2);
    QCOMPARE(role(messages, 0, "body"), QStringLiteral("Hallo, wie geht's?"));
    QVERIFY(!roleValue(messages, 0, "outgoing").toBool());
    QVERIFY(!roleValue(messages, 0, "read").toBool());
    QVERIFY(roleValue(messages, 1, "outgoing").toBool());
    QCOMPARE(messages->luidAt(1), 12);
    QVERIFY(messages->unreadAt(0));

    area()->setCurrentThread(area()->conversations()->threadKeyAt(2));
    QCOMPARE(messages->count(), 1);
    const QVariantList parts = roleValue(messages, 0, "attachments").toList();
    QCOMPARE(parts.size(), 2);
    const QVariantMap picture = parts.at(0).toMap();
    QCOMPARE(picture.value(QStringLiteral("name")).toString(), QStringLiteral("../../.bashrc"));
    QVERIFY(picture.value(QStringLiteral("available")).toBool());
    QVERIFY(picture.value(QStringLiteral("imageUrl"))
                .toString()
                .startsWith(QLatin1StringView("image://qpm/part/")));
    QVERIFY(!parts.at(1).toMap().value(QStringLiteral("available")).toBool());
    QCOMPARE(messages->savableAttachmentsAt(0), 1);
    QCOMPARE(area()->currentNumber(), QStringLiteral("+49402000006"));
}

void TestMessagesArea::composerCountsCharactersAndMessages()
{
    MessageComposer *composer = area()->composer();

    composer->setText(QString());
    QCOMPARE(composer->remainingText(), QStringLiteral("160 characters left"));
    QCOMPARE(composer->partsText(), QString());
    composer->setText(QString(161, u'a'));
    QCOMPARE(composer->parts(), 2);
    QCOMPARE(composer->partsText(), QStringLiteral("2 messages"));
    QCOMPARE(composer->remainingText(), QStringLiteral("145 characters left"));
    QVERIFY(!composer->isUnicode());
    composer->setText(QStringLiteral("Grüße 😀"));
    QVERIFY(composer->isUnicode());
    QCOMPARE(composer->parts(), 1);
    QCOMPARE(composer->remainingText(), QStringLiteral("62 characters left"));
    QCOMPARE(composer->simChoices(),
             (QStringList{QStringLiteral("Default SIM card"), QStringLiteral("SIM 1: Privat"),
                          QStringLiteral("SIM 2: Arbeit")}));
}

void TestMessagesArea::composerTakesOnlyNumbersAndSendsToEach()
{
    MessageComposer *composer = area()->composer();
    QSignalSpy failed(composer, &MessageComposer::errorOccurred);
    QSignalSpy info(area(), &MessagesArea::infoMessage);
    QSignalSpy finished(composer, &MessageComposer::finished);
    area()->composeNew();
    QVERIFY(area()->isComposing());

    composer->setRecipientText(QStringLiteral("Erika"));
    QVERIFY(!composer->addRecipient());
    QCOMPARE(failed.count(), 1);
    composer->setRecipientText(QStringLiteral("+49 (151) 1234567"));
    QVERIFY(composer->addRecipient());
    QCOMPARE(composer->recipientText(), QString());
    composer->setRecipientText(QStringLiteral("030 2000002"));
    QVERIFY(composer->addRecipient());
    QCOMPARE(composer->recipientLabels(),
             (QStringList{QStringLiteral("Erika Mustermann (+49 151 1234567)"),
                          QStringLiteral("030 2000002")}));
    QVERIFY(!composer->canSubmit());
    composer->setText(QStringLiteral("Bis gleich"));
    composer->setSimIndex(2);
    QVERIFY(composer->canSubmit());

    QVERIFY(composer->send());

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeoutMs);
    QCOMPARE(m_phone->sentMessages().size(), 2);
    QCOMPARE(m_phone->sentMessages().at(0).number, QStringLiteral("+491511234567"));
    QCOMPARE(m_phone->sentMessages().at(1).text, QStringLiteral("Bis gleich"));
    QCOMPARE(m_phone->sentMessages().at(1).simSlot, 1);
    QVERIFY(info.count() >= 1);
    QCOMPARE(info.first().first().toString(), QStringLiteral("Message sent to 2 recipients."));
    QVERIFY(!area()->isComposing());
    QVERIFY(composer->recipientLabels().isEmpty());
    QCOMPARE(failed.count(), 1);
}

void TestMessagesArea::numberHandedOverOpensTheComposer()
{
    QSignalSpy failed(area(), &MessagesArea::errorOccurred);

    QVERIFY(area()->composeTo(QStringLiteral("+49 (30) 2000002")));

    QVERIFY(area()->isComposing());
    QCOMPARE(area()->composer()->recipientNumbers(), QStringList{QStringLiteral("+49302000002")});
    QVERIFY(!area()->composeTo(QStringLiteral("Erika")));
    QCOMPARE(failed.count(), 1);
}

void TestMessagesArea::scheduledMessageIsPlannedChangedAndListed()
{
    MessageComposer *composer = area()->composer();
    QSignalSpy finished(composer, &MessageComposer::finished);
    QVERIFY(area()->composeTo(QStringLiteral("0151 1234567")));
    composer->setText(QStringLiteral("Alles Gute"));
    composer->setScheduled(true);
    const QDateTime when = QDateTime::currentDateTime().addDays(3);
    composer->setScheduleDate(QLocale().toString(when.date(), QLocale::ShortFormat));
    composer->setScheduleTime(QStringLiteral("18:30"));
    QCOMPARE(composer->scheduleProblem(), QString());

    QVERIFY(composer->schedule());

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeoutMs);
    QCOMPARE(area()->scheduled()->count(), 1);
    const QAbstractItemModel *scheduled = area()->scheduled();
    QCOMPARE(role(scheduled, 0, "recipientsText"), QStringLiteral("Erika Mustermann"));
    QCOMPARE(role(scheduled, 0, "body"), QStringLiteral("Alles Gute"));
    QCOMPARE(role(scheduled, 0, "statusText"), QStringLiteral("Waiting"));
    QVERIFY(m_phone->syncPuts().constLast().second.contains(
        "DATE:" + QDateTime(when.date(), QTime(18, 30))
                      .toUTC()
                      .toString(QStringLiteral("yyyyMMddTHHmmssZ"))
                      .toLatin1()));
    QVERIFY(m_phone->sentMessages().isEmpty());

    const int luid = area()->scheduled()->luidAt(0);
    QVERIFY(area()->editScheduled(luid));
    QCOMPARE(composer->editingLuid(), luid);
    QVERIFY(composer->isScheduled());
    QCOMPARE(composer->text(), QStringLiteral("Alles Gute"));
    QCOMPARE(composer->scheduleTime(), QStringLiteral("18:30"));
    composer->setText(QStringLiteral("Alles Liebe"));
    QVERIFY(composer->schedule());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, timeoutMs);
    QCOMPARE(area()->scheduled()->count(), 1);
    QCOMPARE(role(scheduled, 0, "body"), QStringLiteral("Alles Liebe"));
}

void TestMessagesArea::scheduleNeedsATimeAhead()
{
    MessageComposer *composer = area()->composer();
    QVERIFY(area()->composeTo(QStringLiteral("0151 1234567")));
    composer->setText(QStringLiteral("Gleich"));
    composer->setScheduled(true);

    composer->setScheduleDate(QStringLiteral("morgen"));
    QCOMPARE(composer->scheduleProblem(),
             QStringLiteral("Enter a date like %1.")
                 .arg(QLocale().toString(QDate::currentDate().addDays(1), QLocale::ShortFormat)));
    composer->setScheduleDate(
        QLocale().toString(QDate::currentDate().addDays(-1), QLocale::ShortFormat));
    QCOMPARE(composer->scheduleProblem(),
             QStringLiteral("Choose a time at least one minute ahead."));
    QVERIFY(!composer->canSubmit());
    QVERIFY(!composer->schedule());
    composer->setScheduled(false);
    QVERIFY(composer->canSubmit());
}

// The short German date has a two-digit year, which Qt reads as 19xx; a
// message planned for next week then lay in the past.
void TestMessagesArea::scheduleDateKeepsItsCentury()
{
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    const auto restore = qScopeGuard(
        []() { QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom)); });
    MessageComposer *composer = area()->composer();
    QVERIFY(area()->composeTo(QStringLiteral("0151 1234567")));
    composer->setText(QStringLiteral("Bis bald"));
    composer->setScheduled(true);

    const QDate tomorrow = QDate::currentDate().addDays(1);
    QCOMPARE(composer->scheduleDate(), tomorrow.toString(QStringLiteral("dd.MM.yyyy")));
    QCOMPARE(composer->scheduledAt().toLocalTime().date(), tomorrow);
    QCOMPARE(composer->scheduleProblem(), QString());
    composer->setScheduleDate(tomorrow.toString(QStringLiteral("dd.MM.yy")));
    QCOMPARE(composer->scheduledAt().toLocalTime().date(), tomorrow);
    QVERIFY(composer->canSubmit());
}

void TestMessagesArea::refusedDeletionPointsToTheArchive()
{
    QSignalSpy failed(area(), &MessagesArea::errorOccurred);
    area()->setCurrentThread(area()->conversations()->threadKeyAt(0));

    QVERIFY(area()->removeCurrentConversation());

    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, timeoutMs);
    QCOMPARE(failed.first().first().toString(),
             QStringLiteral("This version of the phone app does not allow deleting text messages. "
                            "You can archive the message on this computer."));
    QCOMPARE(area()->messages()->count(), 2);

    area()->setCurrentThread(area()->conversations()->threadKeyAt(2));
    QVERIFY(area()->removeCurrentConversation());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, timeoutMs);
    QCOMPARE(failed.last().first().toString(),
             QStringLiteral("The phone app cannot delete multimedia messages. Delete them on the "
                            "phone itself."));
}

void TestMessagesArea::archivedConversationIsMarked()
{
    QSignalSpy info(area(), &MessagesArea::infoMessage);
    area()->setCurrentThread(area()->conversations()->threadKeyAt(0));

    QVERIFY(area()->archiveCurrentConversation());

    QCOMPARE(info.last().first().toString(),
             QStringLiteral("2 messages kept in the archive on this computer."));
    QCOMPARE(roleValue(area()->conversations(), 0, "archived").toInt(), 2);
    QVERIFY(area()->messages()->archivedAt(0));
    QVERIFY(!area()->archiveCurrentConversation());
    QVERIFY(area()->archive({12}, false));
    QVERIFY(!area()->messages()->archivedAt(1));
}

void TestMessagesArea::readStateRefusalIsExplained()
{
    QSignalSpy failed(area(), &MessagesArea::errorOccurred);
    area()->setCurrentThread(area()->conversations()->threadKeyAt(0));

    QVERIFY(area()->markCurrentRead());

    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, timeoutMs);
    QCOMPARE(failed.first().first().toString(),
             QStringLiteral("This version of the phone app does not allow changing the read "
                            "state. On the phone the message stays as it is."));
    QVERIFY(area()->messages()->unreadAt(0));
}

void TestMessagesArea::exportWritesEveryFormatAndTheArchiveReadsBack()
{
    QSignalSpy info(area(), &MessagesArea::infoMessage);
    area()->setCurrentThread(area()->conversations()->threadKeyAt(0));
    const QString csv = m_dir->filePath(QStringLiteral("chat.csv"));
    const QString html = m_dir->filePath(QStringLiteral("chat.html"));
    const QString xml = m_dir->filePath(QStringLiteral("all.xml"));

    QVERIFY(area()->exportConversation(QUrl::fromLocalFile(csv), MessagesArea::ExportFormat::Csv));
    QVERIFY(
        area()->exportConversation(QUrl::fromLocalFile(html), MessagesArea::ExportFormat::Html));
    QVERIFY(area()->exportAll(QUrl::fromLocalFile(xml), MessagesArea::ExportFormat::Xml));

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 3, timeoutMs);
    QFile csvFile(csv);
    QVERIFY(csvFile.open(QIODevice::ReadOnly));
    const Result<CsvRows> rows = readCsv(csvFile.readAll());
    QVERIFY(rows.hasValue());
    QCOMPARE(rows.value().size(), 3);
    QCOMPARE(rows.value().at(1).at(3), QStringLiteral("Erika Mustermann"));
    QCOMPARE(rows.value().at(1).at(4), QStringLiteral("+491511234567"));
    QFile htmlFile(html);
    QVERIFY(htmlFile.open(QIODevice::ReadOnly));
    const QString page = QString::fromUtf8(htmlFile.readAll());
    QVERIFY(page.contains(QLatin1StringView("<h1>Erika Mustermann</h1>")));
    QVERIFY(page.contains(QLatin1StringView("wie geht&#39;s?")));
    QFile xmlFile(xml);
    QVERIFY(xmlFile.open(QIODevice::ReadOnly));
    const Result<QList<Message>> archive = readMessagesXml(xmlFile.readAll());
    QVERIFY(archive.hasValue());
    QCOMPARE(archive.value().size(), 4);

    // Read back into the same phone, the archive brings nothing twice.
    m_controller->disconnectPhone();
    QSignalSpy imported(area(), &MessagesArea::infoMessage);
    QVERIFY(area()->importArchive(QUrl::fromLocalFile(xml)));
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, timeoutMs);
    QCOMPARE(imported.first().first().toString(),
             QStringLiteral("The archive holds no message that is not here already."));
}

void TestMessagesArea::attachmentIsSavedWhereTheUserChose()
{
    QSignalSpy info(area(), &MessagesArea::infoMessage);
    area()->setCurrentThread(area()->conversations()->threadKeyAt(2));
    const int luid = area()->messages()->luidAt(0);
    QCOMPARE(area()->suggestedFileName(luid, 0), QStringLiteral("bashrc"));
    const QString target = m_dir->filePath(QStringLiteral("bild.png"));
    QCOMPARE(area()->suggestedFileUrl(luid, 0, QUrl::fromLocalFile(m_dir->path())),
             QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("bashrc"))));

    QVERIFY(area()->saveAttachment(luid, 0, QUrl::fromLocalFile(target)));

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("PNG"));
    QVERIFY(!area()->saveAttachment(luid, 0, QUrl(QStringLiteral("https://example.org/a.png"))));
}

void TestMessagesArea::allAttachmentsGoIntoTheFolderWithoutOverwriting()
{
    QSignalSpy info(area(), &MessagesArea::infoMessage);
    area()->setCurrentThread(area()->conversations()->threadKeyAt(2));
    const int luid = area()->messages()->luidAt(0);
    const QString folder = m_dir->filePath(QStringLiteral("parts"));
    QVERIFY(QDir().mkpath(folder));
    QFile existing(QDir(folder).filePath(QStringLiteral("bashrc")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QVERIFY(existing.write("old") == 3);
    existing.close();

    QVERIFY(area()->saveAllAttachments(luid, QUrl::fromLocalFile(folder)));

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QCOMPARE(info.first().first().toString(), QStringLiteral("1 attachment saved."));
    QFile kept(QDir(folder).filePath(QStringLiteral("bashrc")));
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("old"));
    QFile saved(QDir(folder).filePath(QStringLiteral("bashrc (2)")));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), QByteArray("PNG"));
    QCOMPARE(QDir(folder).entryList(QDir::Files).size(), 2);
    QVERIFY(!QFile::exists(m_dir->filePath(QStringLiteral(".bashrc"))));
}

void TestMessagesArea::partLeftOnThePhoneIsExplained()
{
    QSignalSpy failed(area(), &MessagesArea::errorOccurred);
    area()->setCurrentThread(area()->conversations()->threadKeyAt(2));

    QVERIFY(!area()->saveAttachment(area()->messages()->luidAt(0), 1,
                                    QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("f.mp4")))));

    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().toString(),
             QStringLiteral("This attachment stayed on the phone because it is large. Save it on "
                            "the phone itself."));
}

void TestMessagesArea::namesFromThePhoneBecomeSafeFileNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("mime");
    QTest::addColumn<QString>("safe");

    QTest::newRow("plain") << QStringLiteral("Foto 1.jpg") << QStringLiteral("image/jpeg")
                           << QStringLiteral("Foto 1.jpg");
    QTest::newRow("path") << QStringLiteral("../../etc/passwd") << QString()
                          << QStringLiteral("passwd");
    QTest::newRow("windows path") << QStringLiteral("C:\\Users\\x\\a.txt") << QString()
                                  << QStringLiteral("a.txt");
    QTest::newRow("hidden") << QStringLiteral(".bashrc") << QString() << QStringLiteral("bashrc");
    QTest::newRow("option") << QStringLiteral("-rf") << QString() << QStringLiteral("rf");
    QTest::newRow("reserved") << QStringLiteral("a:b*c?<d>|\"e\".jpg") << QString()
                              << QStringLiteral("a_b_c__d___e_.jpg");
    QTest::newRow("control") << QStringLiteral("a\nb\u202Ec.jpg") << QString()
                             << QStringLiteral("a_b_c.jpg");
    QTest::newRow("empty with type")
        << QString() << QStringLiteral("application/pdf") << QStringLiteral("Attachment 1.pdf");
    QTest::newRow("only dots") << QStringLiteral("..") << QString()
                               << QStringLiteral("Attachment 1");
    QTest::newRow("long") << QString(300, u'ä') + QStringLiteral(".jpg") << QString()
                          << QString(98, u'ä') + QStringLiteral(".jpg");
}

void TestMessagesArea::namesFromThePhoneBecomeSafeFileNames()
{
    QFETCH(QString, name);
    QFETCH(QString, mime);
    QFETCH(QString, safe);

    const QString result = MessagesArea::safeFileName(name, mime, 0);

    QCOMPARE(result, safe);
    QVERIFY(result.toUtf8().size() <= 200);
}

void TestMessagesArea::newMessageIsAnnounced()
{
    QSignalSpy info(area(), &MessagesArea::infoMessage);
    m_phone->addObject(QStringLiteral("msg"), 14,
                       sms(14, true, QStringLiteral("0151 1234567"), QStringLiteral("Bist du da?"),
                           QDateTime::currentDateTime(), false));

    m_phone->sendUnsolicited("+CMTI: \"ME\",14");

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QCOMPARE(info.first().first().toString(), QStringLiteral("New message from Erika Mustermann."));
    QCOMPARE(role(area()->conversations(), 0, "snippet"), QStringLiteral("Bist du da?"));
    QCOMPARE(roleValue(area()->conversations(), 0, "unread").toInt(), 2);
}

void TestMessagesArea::sendingNeedsAConnection()
{
    QVERIFY(area()->composeTo(QStringLiteral("0301234567")));
    area()->composer()->setText(QStringLiteral("Hi"));
    QVERIFY(area()->composer()->canSubmit());

    m_controller->disconnectPhone();

    QVERIFY(!area()->canEdit());
    QVERIFY(!area()->composer()->canSubmit());
    QVERIFY(!area()->composer()->send());
    QVERIFY(area()->hasPhone());
    QCOMPARE(area()->conversations()->totalCount(), 3);
    QVERIFY(m_phone->sentMessages().isEmpty());
}

void TestMessagesArea::numbersAreWrittenInOneForm_data()
{
    QTest::addColumn<QString>("number");
    QTest::addColumn<QString>("shown");

    QTest::newRow("German mobile") << QStringLiteral("+491512000001")
                                   << QStringLiteral("+49 151 2000001");
    QTest::newRow("German mobile, national") << QStringLiteral("01512000001")
                                             << QStringLiteral("0151 2000001");
    QTest::newRow("Berlin") << QStringLiteral("0302000002") << QStringLiteral("030 2000002");
    QTest::newRow("Berlin, international with 00") << QStringLiteral("00493020000002")
                                                   << QStringLiteral("+49 30 20000002");
    QTest::newRow("Austrian mobile") << QStringLiteral("+436641234567")
                                     << QStringLiteral("+43 664 1234567");
    QTest::newRow("Swiss mobile") << QStringLiteral("+41791234567")
                                  << QStringLiteral("+41 79 1234567");
    QTest::newRow("other country") << QStringLiteral("+12025550123")
                                   << QStringLiteral("+1 2025550123");
    QTest::newRow("own grouping kept") << QStringLiteral("040-200  0006")
                                       << QStringLiteral("040 200 0006");
    QTest::newRow("sender name") << QStringLiteral(" Paketdienst ") << QStringLiteral("Paketdienst");
    QTest::newRow("short code") << QStringLiteral("2222") << QStringLiteral("2222");
}

void TestMessagesArea::numbersAreWrittenInOneForm()
{
    QFETCH(QString, number);
    QFETCH(QString, shown);

    QCOMPARE(qpm::ui::PhoneNumberText::display(number), shown);
}

// The conversation header and the recipient chips showed a number as the
// phone stored it, while the call list grouped the same number.
void TestMessagesArea::numberLooksAlikeInConversationRecipientAndCallList()
{
    const QString stored = QStringLiteral("+491512000001");
    const QString shown = QStringLiteral("+49 151 2000001");
    m_phone->addObject(QStringLiteral("msg"), 14,
                       sms(14, true, stored, QStringLiteral("Bin da."), hoursAgo(1)));
    qpm::test::FakeCall call;
    call.id = 1;
    call.storage = QStringLiteral("RC");
    call.number = stored;
    call.time = formatCallLogTime(QDateTime::currentDateTime().addSecs(-600));
    m_phone->setCalls({call});
    const QFuture<Result<store::MessageChanges>> messages = area()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(messages.isFinished(), timeoutMs);
    QVERIFY(messages.result().hasValue());
    const QFuture<Result<store::CallChanges>> calls = m_controller->calls()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(calls.isFinished(), timeoutMs);
    QVERIFY(calls.result().hasValue());

    const ConversationListModel *list = area()->conversations();
    QCOMPARE(role(list, 0, "title"), shown);
    area()->setCurrentThread(list->threadKeyAt(0));
    QCOMPARE(area()->currentAddress(), shown);
    QCOMPARE(role(m_controller->calls()->list(), 0, "title"), shown);
    QVERIFY(area()->composeTo(stored));
    QCOMPARE(area()->composer()->recipientLabels(), QStringList{shown});
    QCOMPARE(area()->composer()->recipientNumbers(), QStringList{stored});
}

QTEST_MAIN(TestMessagesArea)

#include "tst_messages.moc"
