#include "ui/AppController.h"
#include "ui/Settings.h"
#include "ui/contacts/ContactDetailModel.h"
#include "ui/contacts/ContactEditor.h"
#include "ui/contacts/ContactListModel.h"
#include "ui/contacts/ContactValueModel.h"
#include "ui/contacts/ContactsArea.h"
#include "ui/contacts/DuplicateListModel.h"
#include "ui/contacts/PhoneImageProvider.h"

#include "core/codec/PhoneVCard.h"
#include "platform/SecretStore.h"

#include "FakePhone.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::test::FakePhone;
using qpm::ui::AppController;
using qpm::ui::ContactDetailModel;
using qpm::ui::ContactEditor;
using qpm::ui::ContactListModel;
using qpm::ui::ContactsArea;
using qpm::ui::ContactValueModel;
using qpm::ui::PhoneImageProvider;
using qpm::ui::PhotoSource;
using qpm::ui::Settings;

namespace {

constexpr int timeoutMs = 15000;

ContactValue value(quint64 dataId, int type, const QString &text)
{
    ContactValue entry;
    entry.dataId = dataId;
    entry.type = type;
    entry.value = text;
    return entry;
}

QByteArray jpeg(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(QColor(30, 120, 90));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 90);
    return bytes;
}

Contact erika()
{
    Contact contact;
    contact.luid = 1;
    contact.familyName = QStringLiteral("Mustermann");
    contact.givenName = QStringLiteral("Erika");
    contact.displayName = QStringLiteral("Erika Mustermann");
    contact.phones = {value(11, phoneType::mobile, QStringLiteral("+49 151 1234567")),
                      value(12, phoneType::home, QStringLiteral("030 111"))};
    contact.emails = {value(13, emailType::home, QStringLiteral("erika@example.org"))};
    contact.groups = {QStringLiteral("Familie")};
    contact.accountType = QStringLiteral("com.google");
    contact.accountName = QStringLiteral("erika@example.org");
    contact.ringtone = QStringLiteral("content://media/internal/audio/media/107;Tune");
    contact.photo = jpeg(96, 96);
    return contact;
}

Contact zoe()
{
    Contact contact;
    contact.luid = 2;
    contact.familyName = QStringLiteral("Örtel");
    contact.givenName = QStringLiteral("Zoë");
    contact.displayName = QStringLiteral("Zoë Örtel");
    contact.organization = QStringLiteral("Beispiel GmbH");
    contact.phones = {value(21, phoneType::work, QStringLiteral("040 222"))};
    return contact;
}

Contact erikaAgain()
{
    Contact contact;
    contact.luid = 3;
    contact.familyName = QStringLiteral("Musterman");
    contact.givenName = QStringLiteral("Erika");
    contact.displayName = QStringLiteral("Erika Musterman");
    contact.phones = {value(31, phoneType::mobile, QStringLiteral("0151 1234567"))};
    contact.note = QStringLiteral("Zweiter Eintrag");
    return contact;
}

QByteArray metadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<accounts filter_disabled=\"0\">\r\n"
           "<account selected=\"1\" type=\"null\" name=\"null\"  default=\"1\" "
           "title=\"Phone\"/>\r\n"
           "<account selected=\"1\" type=\"com.google\" name=\"erika@example.org\"  "
           "title=\"Google\"/>\r\n</accounts>\r\n<groups>\r\n"
           "<group title=\"Familie\" sourceid=\"1\" account_name=\"erika@example.org\" "
           "account_type=\"com.google\"/>\r\n</groups>\r\n</metadata>";
}

QString role(const QAbstractItemModel *model, int row, const QByteArray &name)
{
    return model->data(model->index(row, 0), model->roleNames().key(name)).toString();
}

// A PNG that claims to be 20000 by 20000 pixels and carries no picture data.
QByteArray hugePngHeader()
{
    QByteArray chunk = QByteArray("IHDR") + QByteArray::fromHex("00004E2000004E200802000000");
    quint32 crc = 0xFFFFFFFFu;
    for (const char byte : std::as_const(chunk)) {
        crc ^= static_cast<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    crc ^= 0xFFFFFFFFu;
    QByteArray png = QByteArray::fromHex("89504E470D0A1A0A0000000D") + chunk;
    for (int shift = 24; shift >= 0; shift -= 8)
        png.append(char((crc >> shift) & 0xFF));
    return png;
}

} // namespace

class TestContacts : public QObject
{
    Q_OBJECT

public:
    explicit TestContacts(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void listShowsContactsSortedUnderTheirLetters();
    void searchAndFiltersNarrowTheList();
    void detailShowsEntriesWithTypeNamesAndActions();
    void editorSendsOnlyTheChangedEntry();
    void emptiedEntryIsDeletedOnThePhone();
    void conflictShowsBothVersionsAndKeepsTheChosenOne();
    void newContactGoesToTheChosenAccountAndGroup();
    void editorRefusesWrongDatesAndEmptyContacts();
    void pictureIsCroppedAndMadeSmallEnoughForThePhone();
    void brokenOrHugePictureIsRefused();
    void duplicatesAreListedAndMerged();
    void contactsAreExportedAndImportedAgain();
    void callSendsTheNumberAndWrongNumbersStayHere();
    void writeMessageHandsTheCheckedNumberOn();
    void imageProviderServesOnlyStoredPhotos();
    void editingNeedsAConnection();
    void syncAllReportsTheContactChanges();

private:
    [[nodiscard]] ContactsArea *area() const { return m_controller->contacts(); }
    [[nodiscard]] bool saveAndWait(ContactEditor *editor);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<AppController> m_controller;
};

void TestContacts::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
}

void TestContacts::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setDataDirectory(m_dir->filePath(QStringLiteral("profiles")));
    m_settings->setAdbPath(m_dir->filePath(QStringLiteral("no-adb")));
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("pb"), {{1, encodeAsPhoneSendsIt(erika())},
                                               {2, encodeAsPhoneSendsIt(zoe())},
                                               {3, encodeAsPhoneSendsIt(erikaAgain())}});
    m_phone->setMetadata(QStringLiteral("pb"), metadata());

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
    const QFuture<Result<store::ContactChanges>> pulled = area()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(pulled.isFinished(), timeoutMs);
    QVERIFY(pulled.result().hasValue());
    QCOMPARE(area()->list()->totalCount(), 3);
}

void TestContacts::cleanup()
{
    m_controller.reset();
    m_phone.reset();
    m_settings.reset();
    m_dir.reset();
}

bool TestContacts::saveAndWait(ContactEditor *editor)
{
    QSignalSpy saved(editor, &ContactEditor::saved);
    QSignalSpy conflict(editor, &ContactEditor::conflictChanged);
    QSignalSpy failed(editor, &ContactEditor::errorOccurred);
    if (!editor->save())
        return false;
    return QTest::qWaitFor(
               [&]() { return !saved.isEmpty() || !conflict.isEmpty() || !failed.isEmpty(); },
               timeoutMs) &&
           !saved.isEmpty();
}

void TestContacts::listShowsContactsSortedUnderTheirLetters()
{
    const ContactListModel *list = area()->list();

    QCOMPARE(list->count(), 3);
    QCOMPARE(role(list, 0, "displayName"), QStringLiteral("Erika Musterman"));
    QCOMPARE(role(list, 1, "displayName"), QStringLiteral("Erika Mustermann"));
    QCOMPARE(role(list, 2, "displayName"), QStringLiteral("Zoë Örtel"));
    QCOMPARE(role(list, 0, "section"), QStringLiteral("E"));
    QCOMPARE(role(list, 2, "section"), QStringLiteral("Z"));
    QCOMPARE(role(list, 1, "initials"), QStringLiteral("EM"));
    QVERIFY(role(list, 1, "photoUrl").startsWith(QStringLiteral("image://qpm/photo/1/")));
    QVERIFY(role(list, 0, "photoUrl").isEmpty());
    QCOMPARE(role(list, 1, "primaryNumber"), QStringLiteral("+49 151 1234567"));
    QCOMPARE(ContactListModel::sectionOf(QStringLiteral("123")), QStringLiteral("#"));
}

void TestContacts::searchAndFiltersNarrowTheList()
{
    ContactListModel *list = area()->list();

    area()->setSearchText(QStringLiteral("ortel"));
    QCOMPARE(list->count(), 1);
    area()->setSearchText(QStringLiteral("1234567"));
    QCOMPARE(list->count(), 2);
    area()->setSearchText(QStringLiteral("beispiel"));
    QCOMPARE(list->count(), 1);
    area()->setSearchText(QString());
    QCOMPARE(list->count(), 3);

    list->setGroupFilter(QStringLiteral("Familie"));
    QCOMPARE(list->count(), 1);
    list->setGroupFilter(QString());
    QCOMPARE(area()->accountKeys().size(), 2);
    list->setAccountFilter(area()->accountKeys().at(1));
    QCOMPARE(list->count(), 1);
    QCOMPARE(role(list, 0, "displayName"), QStringLiteral("Erika Mustermann"));
    list->setAccountFilter(area()->accountKeys().at(0));
    QCOMPARE(list->count(), 2);
    list->setAccountFilter(QString());
    QCOMPARE(list->count(), 3);
}

void TestContacts::detailShowsEntriesWithTypeNamesAndActions()
{
    area()->setCurrentLuid(1);
    const ContactDetailModel *detail = area()->detail();

    QVERIFY(detail->hasContact());
    QCOMPARE(detail->displayName(), QStringLiteral("Erika Mustermann"));
    QCOMPARE(detail->account(), QStringLiteral("Google (erika@example.org)"));
    QCOMPARE(detail->groups(), QStringLiteral("Familie"));
    QVERIFY(!detail->photoUrl().isEmpty());
    QCOMPARE(detail->rowCount(), 4);
    QCOMPARE(role(detail, 0, "section"), QStringLiteral("Phone"));
    QCOMPARE(role(detail, 0, "label"), QStringLiteral("Mobile"));
    QCOMPARE(role(detail, 0, "action"), QStringLiteral("call"));
    QCOMPARE(detail->data(detail->index(0), ContactDetailModel::FirstInSectionRole).toBool(), true);
    QCOMPARE(detail->data(detail->index(1), ContactDetailModel::FirstInSectionRole).toBool(),
             false);
    QCOMPARE(role(detail, 2, "section"), QStringLiteral("Email"));
    QCOMPARE(role(detail, 3, "label"), QStringLiteral("Ringtone"));
    QCOMPARE(role(detail, 3, "value"), QStringLiteral("Tune"));

    area()->setCurrentLuid(0);
    QVERIFY(!detail->hasContact());
}

void TestContacts::editorSendsOnlyTheChangedEntry()
{
    ContactEditor *editor = area()->editor();
    QVERIFY(editor->load(1));
    QCOMPARE(editor->phones()->count(), 2);
    QVERIFY(!editor->isModified());

    QVERIFY(editor->phones()->setText(1, QStringLiteral("value"), QStringLiteral("030 999")));
    QVERIFY(editor->isModified());
    QVERIFY(saveAndWait(editor));

    const QByteArray body = m_phone->syncPuts().constLast().second;
    QVERIFY(body.contains("TEL;HOME;X-SYNCMLREF12:030 999\r\n"));
    QVERIFY(!body.contains("SYNCMLREF11"));
    QVERIFY(!body.contains("EMAIL"));
    QVERIFY(!body.contains("PHOTO"));
    QVERIFY(!body.contains("CATEGORIES"));
    QCOMPARE(
        editor->phones()->data(editor->phones()->index(1), ContactValueModel::ValueRole).toString(),
        QStringLiteral("030 999"));
    QVERIFY(!editor->isModified());
}

void TestContacts::emptiedEntryIsDeletedOnThePhone()
{
    ContactEditor *editor = area()->editor();
    QVERIFY(editor->load(1));
    QVERIFY(editor->emails()->setText(0, QStringLiteral("value"), QString()));
    editor->phones()->remove(0);

    QVERIFY(saveAndWait(editor));

    const QByteArray body = m_phone->syncPuts().constLast().second;
    QVERIFY(body.contains("EMAIL;X-SYNCMLREF13:\r\n"));
    QVERIFY(body.contains("TEL;X-SYNCMLREF11:\r\n"));
    QCOMPARE(editor->emails()->count(), 0);
    QCOMPARE(editor->phones()->count(), 1);
}

void TestContacts::conflictShowsBothVersionsAndKeepsTheChosenOne()
{
    Contact onPhone = zoe();
    onPhone.title = QStringLiteral("Leitung");
    m_phone->editObject(QStringLiteral("pb"), 2, encodeAsPhoneSendsIt(onPhone));
    ContactEditor *editor = area()->editor();
    QVERIFY(editor->load(2));
    editor->setNickname(QStringLiteral("Zo"));
    QSignalSpy conflict(editor, &ContactEditor::conflictChanged);
    const qsizetype putsBefore = m_phone->syncPuts().size();

    QVERIFY(editor->save());
    QTRY_VERIFY_WITH_TIMEOUT(editor->hasConflict(), timeoutMs);

    QCOMPARE(m_phone->syncPuts().size(), putsBefore);
    QVERIFY(editor->ownVersionText().contains(QStringLiteral("Nickname: Zo")));
    QVERIFY(editor->phoneVersionText().contains(QStringLiteral("Job title: Leitung")));
    QVERIFY(!editor->ownVersionText().contains(QStringLiteral("Leitung")));

    QVERIFY(editor->takePhoneVersion());
    QVERIFY(!editor->hasConflict());
    QCOMPARE(editor->jobTitle(), QStringLiteral("Leitung"));
    QCOMPARE(editor->nickname(), QString());

    onPhone.note = QStringLiteral("Wieder geändert");
    m_phone->editObject(QStringLiteral("pb"), 2, encodeAsPhoneSendsIt(onPhone));
    editor->setNickname(QStringLiteral("Zo"));
    QVERIFY(editor->save());
    QTRY_VERIFY_WITH_TIMEOUT(editor->hasConflict(), timeoutMs);
    QSignalSpy saved(editor, &ContactEditor::saved);
    QVERIFY(editor->keepOwnVersion());
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, timeoutMs);
    QCOMPARE(editor->nickname(), QStringLiteral("Zo"));
    QVERIFY(m_phone->syncPuts().constLast().second.contains("NICKNAME:Zo\r\n"));
}

void TestContacts::newContactGoesToTheChosenAccountAndGroup()
{
    ContactEditor *editor = area()->editor();
    editor->newContact();
    QVERIFY(editor->isNew());
    QCOMPARE(editor->accountNames(),
             QStringList({QStringLiteral("Phone"), QStringLiteral("Google (erika@example.org)")}));
    QVERIFY(!editor->canSave());
    editor->setAccountIndex(1);
    editor->setGivenName(QStringLiteral("Neu"));
    QVERIFY(editor->phones()->append());
    QVERIFY(editor->phones()->setText(0, QStringLiteral("value"), QStringLiteral("+4917000")));
    QVERIFY(editor->groupNames().contains(QStringLiteral("Familie")));
    editor->setGroupSelected(QStringLiteral("Familie"), true);
    QVERIFY(editor->addGroup(QStringLiteral("Sport")));
    editor->setBirthday(QStringLiteral("31/12/1990"));
    QVERIFY(editor->datesValid());
    QVERIFY(editor->canSave());

    QVERIFY(saveAndWait(editor));

    const auto put = m_phone->syncPuts().constLast();
    QCOMPARE(put.first, QStringLiteral("telecom/pb/luid/.vcf"));
    QVERIFY(put.second.contains("X-ACCOUNT:com.google;erika@example.org\r\n"));
    QVERIFY(put.second.contains("CATEGORIES:Familie;Sport\r\n"));
    QVERIFY(put.second.contains("BDAY:19901231\r\n"));
    QCOMPARE(area()->currentLuid(), 4);
    QCOMPARE(area()->list()->totalCount(), 4);
    QVERIFY(!editor->isNew());
}

void TestContacts::editorRefusesWrongDatesAndEmptyContacts()
{
    ContactEditor *editor = area()->editor();
    editor->newContact();
    QSignalSpy failed(editor, &ContactEditor::errorOccurred);

    QVERIFY(!editor->save());
    QCOMPARE(failed.count(), 1);

    editor->setGivenName(QStringLiteral("Datum"));
    editor->setAnniversary(QStringLiteral("31.31.2020"));
    QVERIFY(!editor->datesValid());
    QVERIFY(!editor->canSave());
    QVERIFY(!editor->save());
    QCOMPARE(failed.count(), 2);
    QVERIFY(failed.last().first().toString().contains(editor->dateExample()));
    QCOMPARE(editor->dateExample(), QStringLiteral("31/12/1990"));
}

void TestContacts::pictureIsCroppedAndMadeSmallEnoughForThePhone()
{
    QImage noise(3000, 2000, QImage::Format_RGB32);
    for (int y = 0; y < noise.height(); ++y) {
        for (int x = 0; x < noise.width(); ++x)
            noise.setPixel(x, y, qRgb((x * 7 + y * 13) % 256, (x * y) % 256, (x ^ y) % 256));
    }
    const QString path = m_dir->filePath(QStringLiteral("big.png"));
    QVERIFY(noise.save(path));
    ContactEditor *editor = area()->editor();
    QVERIFY(editor->load(2));

    QVERIFY(editor->loadPhotoCandidate(QUrl::fromLocalFile(path)));
    QVERIFY(editor->candidateUrl().startsWith(QStringLiteral("image://qpm/candidate/")));
    QCOMPARE(editor->candidateWidth(), 2048);
    QVERIFY(editor->applyCrop(100, 100, 1200));

    QVERIFY(editor->hasPhoto());
    QVERIFY(editor->candidateUrl().isEmpty());
    QVERIFY(editor->photoUrl().startsWith(QStringLiteral("image://qpm/draft/")));
    const QByteArray photo = editor->edited().photo;
    QVERIFY(photo.startsWith("\xFF\xD8\xFF"));
    QVERIFY(photo.size() <= ContactEditor::maxPhotoBytesForWrite);
    QVERIFY(saveAndWait(editor));
    QVERIFY(m_phone->syncPuts().constLast().second.contains("PHOTO;ENCODING=BASE64;TYPE=JPEG:"));
}

void TestContacts::brokenOrHugePictureIsRefused()
{
    const QString broken = m_dir->filePath(QStringLiteral("broken.jpg"));
    const QString huge = m_dir->filePath(QStringLiteral("huge.png"));
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a picture at all");
    file.close();
    QFile hugeFile(huge);
    QVERIFY(hugeFile.open(QIODevice::WriteOnly));
    hugeFile.write(hugePngHeader());
    hugeFile.close();
    ContactEditor *editor = area()->editor();
    QSignalSpy failed(editor, &ContactEditor::errorOccurred);

    QVERIFY(!editor->loadPhotoCandidate(QUrl::fromLocalFile(broken)));
    QVERIFY(!editor->loadPhotoCandidate(QUrl::fromLocalFile(huge)));
    QVERIFY(!editor->loadPhotoCandidate(QUrl(QStringLiteral("https://example.org/a.jpg"))));
    QCOMPARE(failed.count(), 3);
    QVERIFY(editor->candidateUrl().isEmpty());

    // A header that states the size plainly reaches the size check itself.
    QBuffer buffer;
    buffer.setData("P6\n20000 20000\n255\n");
    buffer.open(QIODevice::ReadOnly);
    QCOMPARE(qpm::ui::readImageSafely(&buffer).error().code(), ErrorCode::LimitExceeded);
}

void TestContacts::duplicatesAreListedAndMerged()
{
    QSignalSpy found(area(), &ContactsArea::duplicatesFound);
    QVERIFY(area()->findDuplicates());
    QTRY_COMPARE_WITH_TIMEOUT(found.count(), 1, timeoutMs);
    const auto *duplicates = area()->duplicates();
    QCOMPARE(duplicates->count(), 1);
    QCOMPARE(role(duplicates, 0, "firstName"), QStringLiteral("Erika Mustermann"));
    QCOMPARE(role(duplicates, 0, "secondName"), QStringLiteral("Erika Musterman"));
    QCOMPARE(role(duplicates, 0, "reason"), QStringLiteral("Same phone number"));

    QSignalSpy info(area(), &ContactsArea::infoMessage);
    QVERIFY(area()->mergeDuplicate(0));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);

    QCOMPARE(duplicates->count(), 0);
    QCOMPARE(area()->list()->totalCount(), 2);
    area()->setCurrentLuid(1);
    QCOMPARE(area()->detail()->note(), QStringLiteral("Zweiter Eintrag"));
}

void TestContacts::contactsAreExportedAndImportedAgain()
{
    QSignalSpy info(area(), &ContactsArea::infoMessage);
    const QString vcf = m_dir->filePath(QStringLiteral("alle.vcf"));
    const QString csv = m_dir->filePath(QStringLiteral("eine.csv"));

    QVERIFY(area()->exportContacts(QUrl::fromLocalFile(vcf), false, {}));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QVERIFY(area()->exportContacts(QUrl::fromLocalFile(csv), true, {2}));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 2, timeoutMs);

    QFile vcard(vcf);
    QVERIFY(vcard.open(QIODevice::ReadOnly));
    const QByteArray cards = vcard.readAll();
    QCOMPARE(cards.count("BEGIN:VCARD\r\nVERSION:3.0\r\n"), 3);
    QFile table(csv);
    QVERIFY(table.open(QIODevice::ReadOnly));
    const QByteArray rows = table.readAll();
    QVERIFY(rows.startsWith("\xEF\xBB\xBF"
                            "First name;"));
    QVERIFY(rows.contains("Zoë"));
    QVERIFY(!rows.contains("Erika"));

    QVERIFY(area()->importContacts(QUrl::fromLocalFile(csv)));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 3, timeoutMs);
    QVERIFY(info.last().first().toString().startsWith(QStringLiteral("1 contact")));
    QCOMPARE(area()->list()->totalCount(), 4);

    QSignalSpy failed(area(), &ContactsArea::errorOccurred);
    QVERIFY(area()->importContacts(
        QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("missing.vcf")))));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, timeoutMs);
}

void TestContacts::callSendsTheNumberAndWrongNumbersStayHere()
{
    QSignalSpy info(area(), &ContactsArea::infoMessage);
    QSignalSpy failed(area(), &ContactsArea::errorOccurred);
    m_phone->clearLog();

    QVERIFY(area()->call(QStringLiteral("+49 151 1234567")));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("ATD+491511234567;")));

    QVERIFY(!area()->call(QStringLiteral("0151;ATA")));
    QCOMPARE(failed.count(), 1);
    const QStringList commands = m_phone->receivedCommands();
    for (const QString &command : commands)
        QVERIFY(!command.contains(QStringLiteral("ATA")));
}

void TestContacts::writeMessageHandsTheCheckedNumberOn()
{
    QSignalSpy requested(area(), &ContactsArea::messageRequested);
    QSignalSpy failed(area(), &ContactsArea::errorOccurred);

    QVERIFY(area()->writeMessage(QStringLiteral("+49 (151) 1234567")));
    QVERIFY(!area()->writeMessage(QStringLiteral("Erika")));

    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.first().first().toString(), QStringLiteral("+491511234567"));
    QCOMPARE(failed.count(), 1);
}

void TestContacts::imageProviderServesOnlyStoredPhotos()
{
    PhoneImageProvider provider(m_controller->photoSource());
    const QString url = role(area()->list(), 1, "photoUrl");
    const QString id = url.mid(QStringLiteral("image://qpm/").size());
    QSize size;

    const QImage photo = provider.requestImage(id, &size, QSize(48, 48));

    QVERIFY(!photo.isNull());
    QCOMPARE(size, QSize(48, 48));
    QVERIFY(provider.requestImage(QStringLiteral("photo/1/../../data.sqlite"), &size, {}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("photo/1/ABCDEF"), &size, {}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("unknown/1"), &size, {}).isNull());
    QVERIFY(size.isEmpty());
}

void TestContacts::editingNeedsAConnection()
{
    ContactEditor *editor = area()->editor();
    QVERIFY(editor->load(2));
    editor->setNickname(QStringLiteral("Offline"));
    QVERIFY(editor->canSave());

    m_controller->disconnectPhone();

    QVERIFY(!area()->canEdit());
    QVERIFY(!editor->canSave());
    QSignalSpy failed(editor, &ContactEditor::errorOccurred);
    QVERIFY(!editor->save());
    QCOMPARE(failed.count(), 1);
    QVERIFY(!area()->removeContacts({2}));
    QCOMPARE(area()->list()->totalCount(), 3);
}

void TestContacts::syncAllReportsTheContactChanges()
{
    Contact changed = zoe();
    changed.note = QStringLiteral("Neu");
    m_phone->editObject(QStringLiteral("pb"), 2, encodeAsPhoneSendsIt(changed));
    m_phone->removeObject(QStringLiteral("pb"), 3);
    QSignalSpy finished(m_controller.get(), &AppController::syncFinished);

    m_controller->syncAll();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeoutMs);
    QCOMPARE(finished.first().first().toString(),
             QStringLiteral("Contacts: 0 new, 1 changed, 1 deleted\n"
                            "Calls: 0 new, 0 changed, 0 deleted"));
    QCOMPARE(area()->list()->totalCount(), 2);
}

QTEST_MAIN(TestContacts)

#include "tst_contacts.moc"
