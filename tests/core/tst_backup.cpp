#include "core/backup/BackupService.h"
#include "core/backup/ZipStore.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/PhoneVCard.h"
#include "core/codec/VNote.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/CalendarService.h"
#include "core/service/CallLogService.h"
#include "core/service/ContactsService.h"
#include "core/service/MessagesService.h"
#include "core/service/NotesService.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::backup;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::CalendarService;
using qpm::core::service::CallLogService;
using qpm::core::service::ContactsService;
using qpm::core::service::MessagesService;
using qpm::core::service::NotesService;
using qpm::core::store::ProfileStore;
using qpm::test::FakeCall;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;
constexpr quint32 meetingLuid = 0x10000001;
constexpr quint32 holidayLuid = 0x10000002;
constexpr quint32 taskLuid = 4;
constexpr quint32 noteLuid = 1;
constexpr quint32 erikaInbox = 11;
constexpr quint32 erikaSent = 12;
constexpr quint32 photoMessage = 0x10000004;
constexpr quint32 plannedMessage = 0x20000001;
constexpr QLatin1StringView accountUrl("anna@example.org@com.google/NULL");

ContactValue value(quint64 dataId, int type, const QString &text)
{
    ContactValue entry;
    entry.dataId = dataId;
    entry.type = type;
    entry.value = text;
    return entry;
}

Contact person(quint32 luid, const QString &given, const QString &family, const QString &number)
{
    Contact contact;
    contact.luid = luid;
    contact.givenName = given;
    contact.familyName = family;
    contact.displayName = given + u' ' + family;
    contact.phones = {value(luid * 10 + 1, phoneType::mobile, number)};
    return contact;
}

Contact erika()
{
    Contact contact = person(1, QStringLiteral("Erika"), QStringLiteral("Mustermann"),
                             QStringLiteral("+49 151 1234567"));
    contact.emails = {value(12, emailType::home, QStringLiteral("erika@example.org"))};
    contact.note = QStringLiteral("Grüße 😀");
    contact.accountType = QStringLiteral("com.google");
    contact.accountName = QStringLiteral("erika@example.org");
    return contact;
}

QList<FakeSyncObject> exampleContacts()
{
    const QList<Contact> contacts = {
        erika(),
        person(2, QStringLiteral("Max"), QStringLiteral("Müller"), QStringLiteral("030 12-34")),
        person(3, QStringLiteral("Jörg"), QStringLiteral("Weiß"),
               QStringLiteral("+49 221 2000005")),
        person(4, QStringLiteral("Zoë"), QStringLiteral("Örtel"), QStringLiteral("+49 40 2000006")),
    };
    QList<FakeSyncObject> objects;
    for (const Contact &contact : contacts)
        objects.append({contact.luid, encodeAsPhoneSendsIt(contact)});
    return objects;
}

QByteArray contactMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<accounts filter_disabled=\"0\">\r\n"
           "<account selected=\"1\" type=\"null\" name=\"null\"  default=\"1\" "
           "title=\"Phone\"/>\r\n"
           "<account selected=\"1\" type=\"com.google\" name=\"erika@example.org\"  "
           "title=\"Google\"/>\r\n</accounts>\r\n<groups>\r\n</groups>\r\n</metadata>";
}

QDateTime utc(int year, int month, int day, int hour = 0, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), QTimeZone::UTC);
}

QByteArray calendarMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<calendars filter_disabled=\"0\">\r\n"
           "<calendar selected=\"1\" default=\"1\" url=\"anna@example.org@com.google/NULL\" "
           "title=\"Familie\" color=\"FF33B679\" hidden=\"0\" timezone=\"Europe/Berlin\" "
           "access_level=\"700\"/>\r\n"
           "<calendar selected=\"1\" url=\"anna@example.org@com.google/NULL\" title=\"Feiertage\" "
           "color=\"FF0B8043\" hidden=\"0\" timezone=\"Europe/Berlin\" access_level=\"200\"/>\r\n"
           "</calendars>\r\n<tasksupport>1</tasksupport>\r\n<task_categories>\r\n"
           "<category name=\"Haushalt\" color=\"FF8800\" id=\"1\"/>\r\n"
           "</task_categories>\r\n</metadata>";
}

CalendarEvent meeting()
{
    CalendarEvent event;
    event.luid = meetingLuid;
    event.summary = QStringLiteral("Elternabend in der Schule");
    event.location = QStringLiteral("Aula");
    event.start = utc(2026, 11, 12, 17);
    event.end = utc(2026, 11, 12, 18, 30);
    event.alarms = {utc(2026, 11, 12, 16, 30)};
    event.calendarUrl = accountUrl;
    event.calendarTitle = QStringLiteral("Familie");
    return event;
}

CalendarTask windows()
{
    CalendarTask task;
    task.luid = taskLuid;
    task.summary = QStringLiteral("Fenster putzen");
    task.due = QDate(2026, 11, 20);
    task.categories = {QStringLiteral("Haushalt")};
    return task;
}

QList<FakeSyncObject> exampleCalendar()
{
    CalendarEvent holiday;
    holiday.luid = holidayLuid;
    holiday.summary = QStringLiteral("Tag der Deutschen Einheit");
    holiday.allDay = true;
    holiday.start = utc(2026, 10, 3);
    holiday.end = utc(2026, 10, 4);
    holiday.calendarUrl = accountUrl;
    holiday.calendarTitle = QStringLiteral("Feiertage");
    return {{meetingLuid, encodeEventAsPhoneSendsIt(meeting())},
            {holidayLuid, encodeEventAsPhoneSendsIt(holiday)},
            {taskLuid, encodeTaskAsPhoneSendsIt(windows())}};
}

Note shoppingList()
{
    Note note;
    note.luid = noteLuid;
    note.body = QStringLiteral("Einkaufsliste:\nMilch, Brot, Äpfel");
    note.lastModified = utc(2026, 11, 10, 8);
    return note;
}

QByteArray noteMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">"
           "<note_categories>\r\n</note_categories>\r\n</metadata>";
}

QByteArray sms(quint32 luid, bool incoming, const QString &number, const QString &text,
               const QString &stamp)
{
    QByteArray data = "BEGIN:VMESSAGE\r\nTYP:SMS;" + QByteArray(incoming ? "IN" : "OUT") + "\r\n";
    data += incoming ? QByteArray("BOX:INBOX\r\nREAD:1\r\n") : QByteArray("BOX:SENT\r\n");
    data += "DATE:" + stamp.toLatin1() + "\r\n";
    data += "NUMBER:" + number.toUtf8() + "\r\n";
    data += "BODY;CHARSET=UTF-8:" + text.toUtf8() + "\r\n";
    data += "X-IRMC-LUID:" + sync::IrmcSync::luidText(luid).toLatin1() + "\r\nEND:VMESSAGE\r\n";
    return data;
}

// The name of the picture tries to leave the folder it is unpacked into.
QByteArray photoMms()
{
    return "BEGIN:VMESSAGE\r\nTYP:MMS;IN\r\nBOX:INBOX\r\nREAD:1\r\nDATE:20261108T100000Z\r\n"
           "NUMBER:+49 170 2000003\r\nSUBJECT:Ausflug\r\n"
           "ATT;NAME=text_0.txt;TYPE=text/plain:Schau mal\r\n"
           "ATT;NAME=..=2F..=2Fbild.jpg;TYPE=image/jpeg;SIZE=4;ENCODING=BASE64:/9j/2Q==\r\n\r\n"
           "X-IRMC-LUID:000010000004\r\nEND:VMESSAGE\r\n";
}

QByteArray plannedSms()
{
    return "BEGIN:VMESSAGE\r\nSIM:0\r\nTYP:SMS;OUT\r\nBOX:SHEDULED\r\n"
           "RECIPIENT:Bernd;+49302000002;SHEDULED;;\r\nDATE:20301224T170000Z\r\n"
           "BODY:Frohes Fest\r\nX-IRMC-LUID:000020000001\r\nEND:VMESSAGE\r\n";
}

QList<FakeSyncObject> exampleMessages()
{
    return {
        {erikaInbox, sms(erikaInbox, true, QStringLiteral("+4915112345678"),
                         QStringLiteral("Grüße aus Köln"), QStringLiteral("20261110T080000Z"))},
        {erikaSent, sms(erikaSent, false, QStringLiteral("015112345678"),
                        QStringLiteral("Danke, Müller"), QStringLiteral("20261110T090000Z"))},
        {photoMessage, photoMms()},
        {plannedMessage, plannedSms()},
    };
}

QList<FakeCall> exampleCalls()
{
    FakeCall dialled;
    dialled.id = 21;
    dialled.storage = QStringLiteral("DC");
    dialled.time = QStringLiteral("26/11/10,08:00:00");
    dialled.number = QStringLiteral("+49 30 12345671");
    dialled.duration = 65;
    FakeCall missed;
    missed.id = 22;
    missed.storage = QStringLiteral("MC");
    missed.time = QStringLiteral("26/11/10,12:00:00");
    missed.number = QStringLiteral("030 12345672");
    return {dialled, missed};
}

QList<BackupSection> everySection()
{
    return QList<BackupSection>(std::begin(allBackupSections), std::end(allBackupSections));
}

QByteArray manifestWith(int version, const QJsonObject &sections,
                        const QString &format = QString(backupFormatName))
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), format);
    root.insert(QStringLiteral("formatVersion"), version);
    root.insert(QStringLiteral("createdAt"), QStringLiteral("2026-11-12T10:00:00Z"));
    root.insert(QStringLiteral("app"), QStringLiteral("QPhoneManager 9.0.0"));
    root.insert(QStringLiteral("deviceId"), QStringLiteral("dev-1"));
    root.insert(QStringLiteral("deviceName"), QStringLiteral("Testhandy"));
    root.insert(QStringLiteral("sections"), sections);
    return QJsonDocument(root).toJson();
}

} // namespace

class TestBackup : public QObject
{
    Q_OBJECT

public:
    explicit TestBackup(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void backupFollowsTheLayoutOfTheContract();
    void backupReadsBackWithEveryEntry();
    void onlyChosenSectionsAreWrittenAndProgressIsReported();
    void previewCountsNewChangedAndUnchangedEntriesOfThePhone();
    void restoreBringsBackDeletedAndChangedEntries();
    void messagesReturnOnlyToTheArchiveOnThisComputer();
    void callLogStaysInTheBackup();
    void unreadableArchiveIsRefused_data();
    void unreadableArchiveIsRefused();
    void newerFormatVersionIsNotSupported();
    void manifestAboveTheLimitIsRefused();
    void sectionThePhoneRefusesIsReportedWhileTheOthersAreRestored();
    void cancelStopsTheRestoreBetweenEntries();
    void withoutConnectionThePreviewComparesWithThisComputer();
    void secondOperationWhileOneRunsIsRefused();
    void backupOfAnotherPhoneOnlyAdds();
    void archiveImportStaysWithTheStoreItStartedWith();
    void partNamesStayUniqueAndPortable();

private:
    template <typename T> [[nodiscard]] Result<T> wait(const QFuture<Result<T>> &future)
    {
        if (!waitFor(future, timeoutMs))
            return Error(ErrorCode::Timeout, QStringLiteral("did not finish"));
        return future.result();
    }
    [[nodiscard]] bool syncAll();
    [[nodiscard]] QString backupPath(const QString &name) const;
    [[nodiscard]] Result<Backup> backupAndOpen(const QList<BackupSection> &sections);
    [[nodiscard]] QString writeArchive(const QString &name,
                                       const QList<std::pair<QString, QByteArray>> &entries);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<ProfileStore> m_profile;
    std::shared_ptr<store::ContactStore> m_contactStore;
    std::shared_ptr<store::OrganizerStore> m_organizerStore;
    std::shared_ptr<store::CallStore> m_callStore;
    std::shared_ptr<store::MessageStore> m_messageStore;
    std::unique_ptr<ContactsService> m_contacts;
    std::unique_ptr<CalendarService> m_calendar;
    std::unique_ptr<NotesService> m_notes;
    std::unique_ptr<MessagesService> m_messages;
    std::unique_ptr<CallLogService> m_calls;
    std::unique_ptr<BackupService> m_backup;
};

void TestBackup::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_profile = std::make_unique<ProfileStore>(m_dir->filePath(QStringLiteral("profiles")),
                                               QStringLiteral("dev-1"));
    QVERIFY(m_profile->open().hasValue());
    store::DeviceProfile profile;
    profile.deviceId = QStringLiteral("dev-1");
    profile.displayName = QStringLiteral("Testhandy");
    QVERIFY(m_profile->saveProfile(profile).hasValue());
    m_contactStore = std::make_shared<store::ContactStore>(m_profile.get());
    m_organizerStore = std::make_shared<store::OrganizerStore>(m_profile.get());
    m_callStore = std::make_shared<store::CallStore>(m_profile.get());
    m_messageStore = std::make_shared<store::MessageStore>(m_profile.get());

    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("pb"), exampleContacts());
    m_phone->setMetadata(QStringLiteral("pb"), contactMetadata());
    m_phone->setObjects(QStringLiteral("cal"), exampleCalendar());
    m_phone->setMetadata(QStringLiteral("cal"), calendarMetadata());
    m_phone->setObjects(QStringLiteral("notes"),
                        {{noteLuid, encodeNoteAsPhoneSendsIt(shoppingList())}});
    m_phone->setMetadata(QStringLiteral("notes"), noteMetadata());
    m_phone->setObjects(QStringLiteral("msg"), exampleMessages());
    m_phone->setCalls(exampleCalls());

    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_contacts = std::make_unique<ContactsService>(m_connection.get());
    m_contacts->setStore(m_contactStore);
    m_calendar = std::make_unique<CalendarService>(m_connection.get());
    m_calendar->setStore(m_organizerStore);
    m_notes = std::make_unique<NotesService>(m_connection.get());
    m_notes->setStore(m_organizerStore);
    m_messages = std::make_unique<MessagesService>(m_connection.get());
    m_messages->setStore(m_messageStore);
    m_calls = std::make_unique<CallLogService>(m_connection.get());
    m_calls->setStore(m_callStore);
    QVERIFY(syncAll());

    m_backup = std::make_unique<BackupService>(BackupService::Services{
        m_contacts.get(), m_calendar.get(), m_notes.get(), m_messages.get()});
    m_backup->setProfileStore(m_profile.get());
}

void TestBackup::cleanup()
{
    m_backup.reset();
    m_contacts.reset();
    m_calendar.reset();
    m_notes.reset();
    m_messages.reset();
    m_calls.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
    m_contactStore.reset();
    m_organizerStore.reset();
    m_callStore.reset();
    m_messageStore.reset();
    m_profile.reset();
    m_phone.reset();
    m_dir.reset();
}

bool TestBackup::syncAll()
{
    return wait(m_contacts->sync()).hasValue() && wait(m_calendar->sync()).hasValue() &&
           wait(m_notes->sync()).hasValue() && wait(m_messages->sync()).hasValue() &&
           wait(m_calls->sync()).hasValue();
}

QString TestBackup::backupPath(const QString &name) const
{
    return m_dir->filePath(name);
}

Result<Backup> TestBackup::backupAndOpen(const QList<BackupSection> &sections)
{
    const QString path = backupPath(QStringLiteral("phone.qpmbackup"));
    const Result<BackupInfo> created = wait(m_backup->create(path, sections, CsvLanguage::German));
    if (!created.hasValue())
        return created.error();
    return wait(m_backup->open(path));
}

QString TestBackup::writeArchive(const QString &name,
                                 const QList<std::pair<QString, QByteArray>> &entries)
{
    const QString path = backupPath(name);
    ZipWriter writer(path);
    if (!writer.open().hasValue())
        return {};
    for (const auto &[entryName, data] : entries) {
        if (!writer.add(entryName, data).hasValue())
            return {};
    }
    return writer.commit().hasValue() ? path : QString();
}

void TestBackup::backupFollowsTheLayoutOfTheContract()
{
    const QString path = backupPath(QStringLiteral("layout.qpmbackup"));
    const Result<BackupInfo> created =
        wait(m_backup->create(path, everySection(), CsvLanguage::German));
    QVERIFY2(created.hasValue(), qPrintable(created.error().message()));

    ZipReader reader(path);
    QVERIFY(reader.open().hasValue());
    QStringList names;
    for (const ZipEntryInfo &entry : reader.entries())
        names.append(entry.name);
    const QString part = QStringLiteral("messages/attachments/%1/.._.._bild.jpg").arg(photoMessage);
    QCOMPARE(names, (QStringList{
                        QStringLiteral("manifest.json"), QStringLiteral("contacts/contacts.vcf"),
                        QStringLiteral("contacts/phone.vcf"), QStringLiteral("calendar/events.ics"),
                        QStringLiteral("calendar/phone.vcs"), QStringLiteral("tasks/tasks.ics"),
                        QStringLiteral("tasks/phone.vcs"), QStringLiteral("notes/notes.vnt"),
                        QStringLiteral("calls/calls.csv"), QStringLiteral("messages/messages.xml"),
                        part}));
    QCOMPARE(reader.read(part).value(), QByteArray::fromBase64("/9j/2Q=="));

    const QJsonObject manifest =
        QJsonDocument::fromJson(reader.read(QStringLiteral("manifest.json")).value()).object();
    QCOMPARE(manifest.value(QStringLiteral("format")).toString(),
             QStringLiteral("qphonemanager-backup"));
    QCOMPARE(manifest.value(QStringLiteral("formatVersion")).toInt(), 1);
    QCOMPARE(manifest.value(QStringLiteral("app")).toString(),
             QStringLiteral("QPhoneManager 1.0.0"));
    QCOMPARE(manifest.value(QStringLiteral("deviceId")).toString(), QStringLiteral("dev-1"));
    QCOMPARE(manifest.value(QStringLiteral("deviceName")).toString(), QStringLiteral("Testhandy"));
    const QString createdAt = manifest.value(QStringLiteral("createdAt")).toString();
    QVERIFY(createdAt.endsWith(u'Z'));
    QCOMPARE(QDateTime::fromString(createdAt, Qt::ISODate), created.value().createdAt);
    const QJsonObject sections = manifest.value(QStringLiteral("sections")).toObject();
    QCOMPARE(sections.value(QStringLiteral("contacts")).toInt(), 4);
    QCOMPARE(sections.value(QStringLiteral("calendar")).toInt(), 2);
    QCOMPARE(sections.value(QStringLiteral("tasks")).toInt(), 1);
    QCOMPARE(sections.value(QStringLiteral("notes")).toInt(), 1);
    QCOMPARE(sections.value(QStringLiteral("calls")).toInt(), 2);
    QCOMPARE(sections.value(QStringLiteral("messages")).toInt(), 3);

    QVERIFY(reader.read(QStringLiteral("contacts/contacts.vcf")).value().contains("VERSION:3.0"));
    QVERIFY(reader.read(QStringLiteral("calendar/events.ics")).value().contains("BEGIN:VEVENT"));
    QVERIFY(reader.read(QStringLiteral("tasks/tasks.ics")).value().contains("BEGIN:VTODO"));
    QVERIFY(reader.read(QStringLiteral("notes/notes.vnt")).value().contains("BEGIN:VNOTE"));
    QVERIFY(reader.read(QStringLiteral("calls/calls.csv")).value().contains("Ausgehend"));
    const QByteArray xml = reader.read(QStringLiteral("messages/messages.xml")).value();
    QVERIFY(xml.contains("<messages version=\"1\">"));
    QVERIFY(!xml.contains("Frohes Fest"));
}

void TestBackup::backupReadsBackWithEveryEntry()
{
    const Result<Backup> backup = backupAndOpen(everySection());
    QVERIFY2(backup.hasValue(), qPrintable(backup.error().message()));

    QCOMPARE(backup.value().info.deviceName, QStringLiteral("Testhandy"));
    QCOMPARE(backup.value().data.contacts, m_contactStore->allContacts().value());
    QCOMPARE(backup.value().data.events, m_organizerStore->allEvents().value());
    QCOMPARE(backup.value().data.tasks, m_organizerStore->tasks().value());
    QCOMPARE(backup.value().data.notes, m_organizerStore->notes().value());
    QCOMPARE(backup.value().info.counts.value(BackupSection::Calls), 2);
    QVERIFY(backup.value().data.calls.isEmpty());
    QCOMPARE(backup.value().data.messages.size(), 3);
    bool photoFound = false;
    for (const Message &message : backup.value().data.messages) {
        for (const MessageAttachment &part : message.attachments) {
            if (part.mimeType == QLatin1StringView("image/jpeg")) {
                QCOMPARE(part.data, QByteArray::fromBase64("/9j/2Q=="));
                photoFound = true;
            }
        }
    }
    QVERIFY(photoFound);
}

void TestBackup::onlyChosenSectionsAreWrittenAndProgressIsReported()
{
    QSignalSpy progress(m_backup.get(), &BackupService::progressChanged);
    const QString path = backupPath(QStringLiteral("notes.qpmbackup"));
    const Result<BackupInfo> created = wait(m_backup->create(
        path, {BackupSection::Notes, BackupSection::Contacts}, CsvLanguage::English));
    QVERIFY(created.hasValue());
    QCOMPARE(created.value().counts.keys(),
             (QList<BackupSection>{BackupSection::Contacts, BackupSection::Notes}));
    QTRY_VERIFY(!progress.isEmpty());
    QCOMPARE(progress.constLast().at(0).toInt(), progress.constLast().at(1).toInt());

    ZipReader reader(path);
    QVERIFY(reader.open().hasValue());
    QCOMPARE(reader.entries().size(), 4);
    QVERIFY(!reader.contains(QStringLiteral("messages/messages.xml")));
    QVERIFY(!m_backup->isRunning());
    QCOMPARE(wait(m_backup->create(path, {}, CsvLanguage::English)).error().code(),
             ErrorCode::InvalidInput);
}

void TestBackup::previewCountsNewChangedAndUnchangedEntriesOfThePhone()
{
    const Result<Backup> backup = backupAndOpen(everySection());
    QVERIFY(backup.hasValue());
    m_phone->removeObject(QStringLiteral("pb"), 2);
    Contact changed = erika();
    changed.phones[0].value = QStringLiteral("+49 151 7654321");
    m_phone->editObject(QStringLiteral("pb"), 1, encodeAsPhoneSendsIt(changed));
    m_phone->removeObject(QStringLiteral("cal"), meetingLuid);
    CalendarTask renamed = windows();
    renamed.summary = QStringLiteral("Fenster und Türen putzen");
    m_phone->editObject(QStringLiteral("cal"), taskLuid, encodeTaskAsPhoneSendsIt(renamed));

    const Result<BackupPreview> preview = wait(m_backup->preview(backup.value()));

    QVERIFY2(preview.hasValue(), qPrintable(preview.error().message()));
    QVERIFY(preview.value().comparedWithPhone);
    const auto counts = [&preview](BackupSection section) {
        const SectionPreview found = preview.value().sections.value(section);
        return QList<int>{found.added, found.changed, found.unchanged};
    };
    QCOMPARE(counts(BackupSection::Contacts), (QList<int>{1, 1, 2}));
    QCOMPARE(counts(BackupSection::Calendar), (QList<int>{1, 0, 1}));
    QCOMPARE(counts(BackupSection::Tasks), (QList<int>{0, 1, 0}));
    QCOMPARE(counts(BackupSection::Notes), (QList<int>{0, 0, 1}));
    QCOMPARE(counts(BackupSection::Messages), (QList<int>{0, 0, 3}));
    QVERIFY(preview.value().sections.contains(BackupSection::Calls));
    QCOMPARE(m_contactStore->allContacts().value().size(), 3);
}

void TestBackup::restoreBringsBackDeletedAndChangedEntries()
{
    const Result<Backup> backup = backupAndOpen(everySection());
    QVERIFY(backup.hasValue());
    m_phone->removeObject(QStringLiteral("pb"), 2);
    Contact changed = erika();
    changed.phones[0].value = QStringLiteral("+49 151 7654321");
    m_phone->editObject(QStringLiteral("pb"), 1, encodeAsPhoneSendsIt(changed));
    m_phone->removeObject(QStringLiteral("cal"), meetingLuid);
    CalendarTask renamed = windows();
    renamed.summary = QStringLiteral("Fenster und Türen putzen");
    m_phone->editObject(QStringLiteral("cal"), taskLuid, encodeTaskAsPhoneSendsIt(renamed));
    m_phone->removeObject(QStringLiteral("notes"), noteLuid);
    QVERIFY(wait(m_backup->preview(backup.value())).hasValue());

    QSignalSpy progress(m_backup.get(), &BackupService::progressChanged);
    const QList<BackupSection> sections = {BackupSection::Contacts, BackupSection::Calendar,
                                           BackupSection::Tasks, BackupSection::Notes};
    const Result<RestoreReport> report = wait(m_backup->restore(backup.value(), sections));

    QVERIFY2(report.hasValue(), qPrintable(report.error().message()));
    QVERIFY(!report.value().cancelled);
    QCOMPARE(report.value().sections.keys(), sections);
    for (const BackupSection section : sections) {
        const SectionOutcome outcome = report.value().sections.value(section);
        QVERIFY2(outcome.failed == 0, qPrintable(outcome.firstError.message()));
    }
    QCOMPARE(report.value().sections.value(BackupSection::Contacts).restored, 2);
    QCOMPARE(report.value().sections.value(BackupSection::Calendar).restored, 1);
    QCOMPARE(report.value().sections.value(BackupSection::Tasks).restored, 1);
    QCOMPARE(report.value().sections.value(BackupSection::Notes).restored, 1);
    QCOMPARE(progress.constLast().at(0).toInt(), 5);
    QCOMPARE(progress.constLast().at(1).toInt(), 5);

    QVERIFY(syncAll());
    const QList<Contact> contacts = m_contactStore->allContacts().value();
    QCOMPARE(contacts.size(), 4);
    bool maxBack = false;
    for (const Contact &contact : contacts) {
        if (contact.givenName == QLatin1StringView("Max")) {
            maxBack = contact.luid != 2 && contact.familyName == QStringLiteral("Müller") &&
                      contact.phones.value(0).value == QLatin1StringView("030 12-34");
        }
        if (contact.luid == 1) {
            QCOMPARE(contact.phones.value(0).value, QStringLiteral("+49 151 1234567"));
            QCOMPARE(contact.note, QStringLiteral("Grüße 😀"));
        }
    }
    QVERIFY(maxBack);
    bool meetingBack = false;
    const QList<CalendarEvent> events = m_organizerStore->allEvents().value();
    for (const CalendarEvent &event : events) {
        if (event.summary == meeting().summary) {
            meetingBack = event.luid != meetingLuid &&
                          event.calendarTitle == QLatin1StringView("Familie") &&
                          event.location == meeting().location && event.start == meeting().start;
        }
    }
    QVERIFY(meetingBack);
    QCOMPARE(m_organizerStore->task(taskLuid).value().summary, windows().summary);
    QCOMPARE(m_organizerStore->notes().value().size(), 1);
    QCOMPARE(m_organizerStore->notes().value().constFirst().body, shoppingList().body);

    // A second restore finds nothing left to do.
    const Result<BackupPreview> again = wait(m_backup->preview(backup.value()));
    QVERIFY(again.hasValue());
    for (const BackupSection section : sections) {
        const SectionPreview found = again.value().sections.value(section);
        QCOMPARE(found.added, 0);
        QCOMPARE(found.changed, 0);
    }
}

void TestBackup::messagesReturnOnlyToTheArchiveOnThisComputer()
{
    const Result<Backup> backup = backupAndOpen({BackupSection::Messages});
    QVERIFY(backup.hasValue());
    m_phone->removeObject(QStringLiteral("msg"), erikaInbox);
    QVERIFY(wait(m_messages->sync()).hasValue());
    QCOMPARE(m_messageStore->allMessages().value().size(), 2);
    m_phone->clearLog();

    const Result<BackupPreview> preview = wait(m_backup->preview(backup.value()));
    QVERIFY(preview.hasValue());
    QCOMPARE(preview.value().sections.value(BackupSection::Messages).added, 1);
    QCOMPARE(preview.value().sections.value(BackupSection::Messages).unchanged, 2);
    QSignalSpy changed(m_messages.get(), &MessagesService::messagesChanged);
    const Result<RestoreReport> report =
        wait(m_backup->restore(backup.value(), {BackupSection::Messages}));

    QVERIFY(report.hasValue());
    QCOMPARE(report.value().sections.value(BackupSection::Messages).restored, 1);
    QCOMPARE(changed.size(), 1);
    const QList<store::StoredMessage> stored = m_messageStore->allMessages().value();
    QCOMPARE(stored.size(), 3);
    int archivedHere = 0;
    for (const store::StoredMessage &message : stored) {
        if (message.body == QStringLiteral("Grüße aus Köln")) {
            QVERIFY(message.archived);
            QVERIFY(!message.isOnPhone());
            ++archivedHere;
        }
    }
    QCOMPARE(archivedHere, 1);
    const QStringList commands = m_phone->receivedCommands();
    for (const QString &command : commands) {
        QVERIFY2(!command.contains(QLatin1StringView("PUT")), qPrintable(command));
        QVERIFY2(!command.startsWith(QLatin1StringView("AT+CMG")), qPrintable(command));
    }
}

void TestBackup::callLogStaysInTheBackup()
{
    const Result<Backup> backup = backupAndOpen({BackupSection::Calls});
    QVERIFY(backup.hasValue());
    QCOMPARE(backup.value().info.counts.value(BackupSection::Calls), 2);
    QVERIFY(!BackupService::isRestorable(BackupSection::Calls));
    m_phone->clearLog();

    const Result<RestoreReport> report =
        wait(m_backup->restore(backup.value(), {BackupSection::Calls}));

    QVERIFY(!report.hasValue());
    QCOMPARE(report.error().code(), ErrorCode::InvalidInput);
    const QStringList commands = m_phone->receivedCommands();
    for (const QString &command : commands)
        QVERIFY2(!command.startsWith(QLatin1StringView("AT+CPBW")), qPrintable(command));
    QCOMPARE(m_callStore->calls().value().size(), 2);
}

void TestBackup::unreadableArchiveIsRefused_data()
{
    QTest::addColumn<QString>("kind");

    QTest::newRow("no manifest") << QStringLiteral("no manifest");
    QTest::newRow("manifest not JSON") << QStringLiteral("not json");
    QTest::newRow("other format") << QStringLiteral("other format");
    QTest::newRow("section file missing") << QStringLiteral("missing file");
    QTest::newRow("count differs") << QStringLiteral("count differs");
    QTest::newRow("cut short") << QStringLiteral("cut short");
    QTest::newRow("no archive at all") << QStringLiteral("no archive");
}

void TestBackup::unreadableArchiveIsRefused()
{
    QFETCH(QString, kind);
    const QByteArray note = encodeNoteAsPhoneSendsIt(shoppingList());
    QString path;
    if (kind == QLatin1StringView("no manifest")) {
        path = writeArchive(QStringLiteral("a.qpmbackup"),
                            {{QStringLiteral("notes/notes.vnt"), note}});
    } else if (kind == QLatin1StringView("not json")) {
        path =
            writeArchive(QStringLiteral("a.qpmbackup"), {{QStringLiteral("manifest.json"), "{"}});
    } else if (kind == QLatin1StringView("other format")) {
        path = writeArchive(QStringLiteral("a.qpmbackup"),
                            {{QStringLiteral("manifest.json"),
                              manifestWith(1, {}, QStringLiteral("other-backup"))}});
    } else if (kind == QLatin1StringView("missing file")) {
        path = writeArchive(QStringLiteral("a.qpmbackup"),
                            {{QStringLiteral("manifest.json"),
                              manifestWith(1, {{QStringLiteral("contacts"), 0}})}});
    } else if (kind == QLatin1StringView("count differs")) {
        path = writeArchive(
            QStringLiteral("a.qpmbackup"),
            {{QStringLiteral("manifest.json"), manifestWith(1, {{QStringLiteral("notes"), 2}})},
             {QStringLiteral("notes/notes.vnt"), note}});
    } else {
        const QString whole = backupPath(QStringLiteral("whole.qpmbackup"));
        QVERIFY(wait(m_backup->create(whole, everySection(), CsvLanguage::English)).hasValue());
        QFile file(whole);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        path = backupPath(QStringLiteral("a.qpmbackup"));
        QFile damaged(path);
        QVERIFY(damaged.open(QIODevice::WriteOnly));
        damaged.write(kind == QLatin1StringView("cut short") ? bytes.left(bytes.size() * 2 / 3)
                                                             : QByteArray(4096, 'x'));
    }
    QVERIFY(!path.isEmpty());

    const Result<Backup> opened = wait(m_backup->open(path));

    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::InvalidInput);
    QVERIFY(!m_backup->isRunning());
}

void TestBackup::newerFormatVersionIsNotSupported()
{
    const QString path = writeArchive(
        QStringLiteral("new.qpmbackup"),
        {{QStringLiteral("manifest.json"), manifestWith(2, {{QStringLiteral("notes"), 0}})}});
    QVERIFY(!path.isEmpty());

    const Result<Backup> opened = wait(m_backup->open(path));

    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::NotSupported);
}

void TestBackup::manifestAboveTheLimitIsRefused()
{
    QJsonObject sections;
    sections.insert(QStringLiteral("padding"), QString(int(maxManifestBytes), u'x'));
    const QString path =
        writeArchive(QStringLiteral("big.qpmbackup"),
                     {{QStringLiteral("manifest.json"), manifestWith(1, sections)}});
    QVERIFY(!path.isEmpty());

    const Result<Backup> opened = wait(m_backup->open(path));

    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::LimitExceeded);
}

void TestBackup::sectionThePhoneRefusesIsReportedWhileTheOthersAreRestored()
{
    const Result<Backup> backup = backupAndOpen(everySection());
    QVERIFY(backup.hasValue());
    m_phone->removeObject(QStringLiteral("pb"), 2);
    m_phone->removeObject(QStringLiteral("pb"), 3);
    m_phone->removeObject(QStringLiteral("cal"), meetingLuid);
    QVERIFY(wait(m_backup->preview(backup.value())).hasValue());
    m_phone->config().calendarDenied = true;

    const Result<RestoreReport> report =
        wait(m_backup->restore(backup.value(), {BackupSection::Contacts, BackupSection::Calendar}));

    QVERIFY(report.hasValue());
    const SectionOutcome contacts = report.value().sections.value(BackupSection::Contacts);
    const SectionOutcome calendar = report.value().sections.value(BackupSection::Calendar);
    QCOMPARE(contacts.restored, 2);
    QCOMPARE(contacts.failed, 0);
    QCOMPARE(calendar.restored, 0);
    QCOMPARE(calendar.failed, 1);
    QVERIFY(calendar.firstError.isError());
}

void TestBackup::cancelStopsTheRestoreBetweenEntries()
{
    const Result<Backup> backup = backupAndOpen({BackupSection::Contacts});
    QVERIFY(backup.hasValue());
    for (const quint32 luid : {2U, 3U, 4U})
        m_phone->removeObject(QStringLiteral("pb"), luid);
    QVERIFY(wait(m_contacts->sync()).hasValue());

    // Stops right after the first entry, before the next one starts.
    const QMetaObject::Connection stopAfterFirst =
        connect(m_backup.get(), &BackupService::progressChanged, this, [this](int done, int) {
            if (done == 1)
                m_backup->cancel();
        });
    const QFuture<Result<RestoreReport>> running =
        m_backup->restore(backup.value(), {BackupSection::Contacts});
    const Result<RestoreReport> report = wait(running);
    disconnect(stopAfterFirst);

    QVERIFY(report.hasValue());
    QVERIFY(report.value().cancelled);
    QCOMPARE(report.value().sections.value(BackupSection::Contacts).restored, 1);
    QVERIFY(!m_backup->isRunning());
    QVERIFY(wait(m_contacts->sync()).hasValue());
    QCOMPARE(m_contactStore->allContacts().value().size(), 2);
}

void TestBackup::withoutConnectionThePreviewComparesWithThisComputer()
{
    const Result<Backup> backup = backupAndOpen({BackupSection::Contacts, BackupSection::Notes});
    QVERIFY(backup.hasValue());
    m_phone->removeObject(QStringLiteral("pb"), 2);
    m_connection->disconnectFromPhone();
    QTRY_COMPARE(m_connection->state(), PhoneConnection::State::Disconnected);

    const Result<BackupPreview> preview = wait(m_backup->preview(backup.value()));

    QVERIFY(preview.hasValue());
    QVERIFY(!preview.value().comparedWithPhone);
    QCOMPARE(preview.value().sections.value(BackupSection::Contacts).added, 0);
    QCOMPARE(preview.value().sections.value(BackupSection::Contacts).unchanged, 4);

    const Result<RestoreReport> report =
        wait(m_backup->restore(backup.value(), {BackupSection::Contacts}));
    QVERIFY(report.hasValue());
    QCOMPARE(report.value().sections.value(BackupSection::Contacts).restored, 0);
}

void TestBackup::secondOperationWhileOneRunsIsRefused()
{
    const QString path = backupPath(QStringLiteral("busy.qpmbackup"));
    const QFuture<Result<BackupInfo>> first =
        m_backup->create(path, everySection(), CsvLanguage::English);
    QVERIFY(m_backup->isRunning());

    const Result<Backup> second = wait(m_backup->open(path));

    QVERIFY(!second.hasValue());
    QCOMPARE(second.error().code(), ErrorCode::InvalidInput);
    QVERIFY(wait(first).hasValue());
    QVERIFY(!m_backup->isRunning());

    BackupService unchosen(BackupService::Services{m_contacts.get(), m_calendar.get(),
                                                   m_notes.get(), m_messages.get()});
    QCOMPARE(wait(unchosen.create(path, everySection(), CsvLanguage::English)).error().code(),
             ErrorCode::InvalidInput);
}

// The LUIDs of another phone name other entries here: the entry this phone
// keeps under the same LUID stays as it is, and the backup's entry is added.
void TestBackup::backupOfAnotherPhoneOnlyAdds()
{
    const Result<Backup> made = backupAndOpen({BackupSection::Contacts});
    QVERIFY(made.hasValue());
    Backup foreign = made.value();
    foreign.info.deviceId = QStringLiteral("phone-a");
    for (Contact &contact : foreign.data.contacts) {
        if (contact.luid == 2) {
            contact.givenName = QStringLiteral("Anna");
            contact.familyName = QStringLiteral("Fremd");
            contact.displayName = QStringLiteral("Anna Fremd");
        }
    }

    const Result<BackupPreview> preview = wait(m_backup->preview(foreign));
    QVERIFY(preview.hasValue());
    QVERIFY(preview.value().fromOtherPhone);
    const SectionPreview contacts = preview.value().sections.value(BackupSection::Contacts);
    QCOMPARE(contacts.added, 1);
    QCOMPARE(contacts.changed, 0);
    QCOMPARE(contacts.unchanged, 3);

    const Result<RestoreReport> report =
        wait(m_backup->restore(foreign, {BackupSection::Contacts}));
    QVERIFY(report.hasValue());
    QCOMPARE(report.value().sections.value(BackupSection::Contacts).restored, 1);
    QVERIFY(wait(m_contacts->sync()).hasValue());
    QCOMPARE(m_contactStore->contact(2).value().givenName, QStringLiteral("Max"));
    QCOMPARE(m_contactStore->allContacts().value().size(), 5);
}

// A switch of the phone while the parts are prepared must not put the
// messages into the archive of the other phone.
void TestBackup::archiveImportStaysWithTheStoreItStartedWith()
{
    const Result<Backup> backup = backupAndOpen({BackupSection::Messages});
    QVERIFY(backup.hasValue());
    ProfileStore otherProfile(m_dir->filePath(QStringLiteral("profiles")), QStringLiteral("dev-2"));
    QVERIFY(otherProfile.open().hasValue());
    const auto otherStore = std::make_shared<store::MessageStore>(&otherProfile);

    const QFuture<Result<int>> imported = m_messages->importToArchive(backup.value().data.messages);
    m_messages->setStore(otherStore);
    const Result<int> result = wait(imported);

    QVERIFY(!result.hasValue());
    QCOMPARE(result.error().code(), ErrorCode::Cancelled);
    QVERIFY(otherStore->allMessages().value().isEmpty());
    QCOMPARE(m_messageStore->allMessages().value().size(), 3);
    m_messages->setStore(m_messageStore);
}

void TestBackup::partNamesStayUniqueAndPortable()
{
    Message message;
    message.luid = 7;
    message.kind = MessageKind::Mms;
    message.date = utc(2026, 11, 1, 9);
    message.addresses = {QStringLiteral("+49 170 2000003")};
    const QStringList names = {QStringLiteral("3-a"),         QStringLiteral("a"),
                               QStringLiteral("a"),           QString::fromUtf8("b\u202Ec.txt"),
                               QString::fromUtf8("x\u0085y"), QStringLiteral("f<o>o?.txt"),
                               QString(120, QChar(u'\u00e4'))};
    for (const QString &name : names) {
        MessageAttachment part;
        part.name = name;
        part.mimeType = QStringLiteral("application/octet-stream");
        part.data = name.toUtf8();
        part.size = part.data.size();
        message.attachments.append(part);
    }
    BackupData data;
    data.messages = {message};
    BackupInfo info;
    info.createdAt = utc(2026, 11, 12, 10);
    const QString path = backupPath(QStringLiteral("parts.qpmbackup"));

    const Result<BackupInfo> written =
        writeBackupArchive(path, info, data, {BackupSection::Messages}, CsvLanguage::English, {});

    QVERIFY2(written.hasValue(), qPrintable(written.error().message()));
    ZipReader reader(path);
    QVERIFY(reader.open().hasValue());
    QStringList parts;
    for (const ZipEntryInfo &entry : reader.entries()) {
        if (entry.name.startsWith(QLatin1StringView("messages/attachments/7/")))
            parts.append(entry.name.mid(23));
    }
    QCOMPARE(parts, (QStringList{QStringLiteral("3-a"), QStringLiteral("a"), QStringLiteral("4-a"),
                                 QStringLiteral("b_c.txt"), QStringLiteral("x_y"),
                                 QStringLiteral("f_o_o_.txt"), QStringLiteral("part-7")}));
}

QTEST_GUILESS_MAIN(TestBackup)

#include "tst_backup.moc"
