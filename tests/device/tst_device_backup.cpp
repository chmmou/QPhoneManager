#include "CommandGuard.h"

#include "core/backup/BackupService.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/PhoneVCard.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/CalendarService.h"
#include "core/service/CallLogService.h"
#include "core/service/ContactsService.h"
#include "core/service/MessagesService.h"
#include "core/service/NotesService.h"
#include "core/store/ProfileStore.h"

#include "WaitFor.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>
#include <optional>

using namespace qpm::core;
using namespace qpm::core::backup;
using namespace qpm::core::codec;
using namespace qpm::core::protocol;
using namespace qpm::core::service;
using qpm::core::store::ProfileStore;
using qpm::test::CommandGuard;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int syncTimeoutMs = 300000;
constexpr int writeTimeoutMs = 60000;
constexpr int connectTries = 10;

// The same names as the contacts and the organizer tests use, so that each
// of them also removes what a broken run of this one left behind.
const QRegularExpression &testContactPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^QPM Testkontakt \\d{6}$"));
    return pattern;
}

const QRegularExpression &testEventPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^QPM Testtermin \\d{6}$"));
    return pattern;
}

QString sixDigits()
{
    return QString::number(QRandomGenerator::global()->bounded(100000, 999999));
}

ContactValue value(int type, const QString &text)
{
    ContactValue entry;
    entry.type = type;
    entry.value = text;
    return entry;
}

// The stores and services of one test function, in a folder that is gone
// afterwards, so that nothing read from a phone stays on this computer.
struct Scratch
{
    QTemporaryDir dir;
    std::unique_ptr<ProfileStore> profile;
    std::shared_ptr<store::ContactStore> contacts;
    std::shared_ptr<store::OrganizerStore> organizer;
    std::shared_ptr<store::CallStore> calls;
    std::shared_ptr<store::MessageStore> messages;
    std::unique_ptr<ContactsService> contactsService;
    std::unique_ptr<CalendarService> calendarService;
    std::unique_ptr<NotesService> notesService;
    std::unique_ptr<MessagesService> messagesService;
    std::unique_ptr<CallLogService> callsService;
    std::unique_ptr<BackupService> backup;

    [[nodiscard]] bool open(PhoneConnection *connection)
    {
        if (!dir.isValid())
            return false;
        profile = std::make_unique<ProfileStore>(dir.filePath(QStringLiteral("profiles")),
                                                 QStringLiteral("device"));
        if (!profile->open().hasValue())
            return false;
        contacts = std::make_shared<store::ContactStore>(profile.get());
        organizer = std::make_shared<store::OrganizerStore>(profile.get());
        calls = std::make_shared<store::CallStore>(profile.get());
        messages = std::make_shared<store::MessageStore>(profile.get());
        contactsService = std::make_unique<ContactsService>(connection);
        contactsService->setStore(contacts);
        calendarService = std::make_unique<CalendarService>(connection);
        calendarService->setStore(organizer);
        notesService = std::make_unique<NotesService>(connection);
        notesService->setStore(organizer);
        messagesService = std::make_unique<MessagesService>(connection);
        messagesService->setStore(messages);
        callsService = std::make_unique<CallLogService>(connection);
        callsService->setStore(calls);
        backup = std::make_unique<BackupService>(
            BackupService::Services{contactsService.get(), calendarService.get(),
                                    notesService.get(), messagesService.get()});
        backup->setProfileStore(profile.get());
        return true;
    }
};

template <typename T> Result<T> waitResult(const QFuture<Result<T>> &future, int timeoutMs)
{
    if (!waitFor(future, timeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("did not finish"));
    return future.result();
}

std::optional<CalendarInfo> writableCalendar(const store::OrganizerStore &store)
{
    const Result<CalMetadata> metadata = store.calendarMetadata();
    if (!metadata.hasValue())
        return std::nullopt;
    std::optional<CalendarInfo> chosen;
    for (const CalendarInfo &calendar : metadata.value().calendars) {
        if (calendar.isWritable() && calendar.selected && (!chosen || calendar.isDefault))
            chosen = calendar;
    }
    return chosen;
}

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling. On every phone the stored data of all sections is backed up into a
// temporary folder, read back and counted; only the counts are printed, and
// the file is deleted. On the emulator ("emulator-..." serials) a contact and,
// where a calendar may be written, an appointment of the test's own are also
// backed up, deleted on the phone, shown as new in the preview, restored and
// compared field by field, then deleted again. Nothing is ever restored to or
// written on any other phone.
class TestDeviceBackup : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceBackup(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void backupOfThePhoneIsReadBackWithTheSameCounts();
    void deletedTestEntriesComeBackFromTheBackup();

private:
    [[nodiscard]] bool connectToClient();
    /// Deletes contacts and appointments with a test name on the emulator.
    void removeTestEntries(Scratch &scratch);

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    QPointer<CommandGuard> m_guard;
};

void TestDeviceBackup::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    qInfo() << (m_emulator ? "Target: emulator, backup and restore of own test entries"
                           : "Target: phone, backup into a temporary folder only");

    m_adb = std::make_unique<AdbBridge>(m_adbPath);
    const QFuture<Result<void>> server = m_adb->startServer();
    QVERIFY(waitFor(server, readyTimeoutMs));
    QVERIFY2(server.result().hasValue(), "adb start-server failed");
    const QFuture<Result<quint16>> forward = m_adb->forward(m_serial, clientPort);
    QVERIFY(waitFor(forward, readyTimeoutMs));
    // The error text of adb can name the device, so only the code is shown.
    QVERIFY2(forward.result().hasValue(),
             qPrintable(QStringLiteral("adb forward failed with code %1")
                            .arg(int(forward.result().error().code()))));
    m_localPort = forward.result().value();
    const QFuture<Result<void>> client = m_adb->startClient(m_serial);
    QVERIFY(waitFor(client, readyTimeoutMs));
    QVERIFY2(client.result().hasValue(),
             qPrintable(QStringLiteral("Starting the client failed with code %1")
                            .arg(int(client.result().error().code()))));
}

void TestDeviceBackup::cleanupTestCase()
{
    if (!m_adb || m_localPort == 0)
        return;
    const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, m_localPort);
    QVERIFY(waitFor(removed, readyTimeoutMs));
}

void TestDeviceBackup::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    QVERIFY(connectToClient());
}

void TestDeviceBackup::cleanup()
{
    // Whatever failed above, no test entry stays on the emulator.
    if (m_emulator && m_connection && m_connection->state() == State::Ready) {
        Scratch scratch;
        QVERIFY(scratch.open(m_connection.get()));
        removeTestEntries(scratch);
    }
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
}

bool TestDeviceBackup::connectToClient()
{
    for (int attempt = 0; attempt < connectTries; ++attempt) {
        auto guard = std::make_unique<CommandGuard>(
            std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_localPort));
        m_guard = guard.get();
        m_connection->connectTransport(std::move(guard));
        const bool settled = QTest::qWaitFor(
            [this]() {
                return m_connection->state() == State::Ready ||
                       m_connection->state() == State::Lost;
            },
            readyTimeoutMs);
        if (settled && m_connection->state() == State::Ready)
            return true;
        // Only spaces out the tries; no result depends on how long it is.
        QTest::qWait(1000);
    }
    return false;
}

void TestDeviceBackup::removeTestEntries(Scratch &scratch)
{
    // Changing data is for the emulator only.
    if (!m_emulator)
        return;
    QVERIFY(waitResult(scratch.contactsService->sync(), syncTimeoutMs).hasValue());
    QList<quint32> contacts;
    const QList<Contact> stored = scratch.contacts->allContacts().value();
    for (const Contact &contact : stored) {
        if (testContactPattern().match(contact.givenName).hasMatch())
            contacts.append(contact.luid);
    }
    if (!contacts.isEmpty())
        QVERIFY(waitResult(scratch.contactsService->remove(contacts), writeTimeoutMs).hasValue());

    QVERIFY(waitResult(scratch.calendarService->sync(), syncTimeoutMs).hasValue());
    QList<quint32> events;
    const QList<CalendarEvent> storedEvents = scratch.organizer->allEvents().value();
    for (const CalendarEvent &event : storedEvents) {
        if (testEventPattern().match(event.summary).hasMatch())
            events.append(event.luid);
    }
    if (!events.isEmpty())
        QVERIFY(waitResult(scratch.calendarService->remove(events), writeTimeoutMs).hasValue());
    qInfo() << "Test entries removed:" << contacts.size() + events.size();
}

// Reads only: every area is pulled into a temporary store and backed up into a
// temporary folder; the archive must give back every entry the store holds.
void TestDeviceBackup::backupOfThePhoneIsReadBackWithTheSameCounts()
{
    Scratch scratch;
    QVERIFY(scratch.open(m_connection.get()));
    QList<BackupSection> sections;
    const auto pulled = [&sections](BackupSection section, const Error &error) {
        if (!error.isError()) {
            sections.append(section);
            return true;
        }
        // A permission the phone app lacks leaves the section out, not the test.
        qInfo() << "Section left out:" << sectionKey(section) << "error code" << int(error.code());
        return error.code() == ErrorCode::PermissionDenied ||
               error.code() == ErrorCode::AccessDenied;
    };
    QVERIFY(pulled(BackupSection::Contacts,
                   waitResult(scratch.contactsService->sync(), syncTimeoutMs).error()));
    const Error calendar = waitResult(scratch.calendarService->sync(), syncTimeoutMs).error();
    QVERIFY(pulled(BackupSection::Calendar, calendar));
    if (!calendar.isError())
        sections.append(BackupSection::Tasks);
    QVERIFY(pulled(BackupSection::Notes,
                   waitResult(scratch.notesService->sync(), syncTimeoutMs).error()));
    QVERIFY(pulled(BackupSection::Calls,
                   waitResult(scratch.callsService->sync(), syncTimeoutMs).error()));
    QVERIFY(pulled(BackupSection::Messages,
                   waitResult(scratch.messagesService->sync(), syncTimeoutMs).error()));
    QVERIFY(!sections.isEmpty());

    QTemporaryDir target;
    QVERIFY(target.isValid());
    const QString path = target.filePath(QStringLiteral("phone.qpmbackup"));
    const Result<BackupInfo> created =
        waitResult(scratch.backup->create(path, sections, CsvLanguage::English), syncTimeoutMs);
    QVERIFY2(
        created.hasValue(),
        qPrintable(QStringLiteral("backup failed with code %1").arg(int(created.error().code()))));
    const Result<Backup> opened = waitResult(scratch.backup->open(path), syncTimeoutMs);
    QVERIFY2(
        opened.hasValue(),
        qPrintable(QStringLiteral("reading failed with code %1").arg(int(opened.error().code()))));
    const Backup &backup = opened.value();

    QCOMPARE(backup.info.counts, created.value().counts);
    QMap<BackupSection, int> stored;
    if (sections.contains(BackupSection::Contacts))
        stored.insert(BackupSection::Contacts, int(scratch.contacts->allContacts().value().size()));
    if (sections.contains(BackupSection::Calendar))
        stored.insert(BackupSection::Calendar, int(scratch.organizer->allEvents().value().size()));
    if (sections.contains(BackupSection::Tasks))
        stored.insert(BackupSection::Tasks, int(scratch.organizer->tasks().value().size()));
    if (sections.contains(BackupSection::Notes))
        stored.insert(BackupSection::Notes, int(scratch.organizer->notes().value().size()));
    if (sections.contains(BackupSection::Calls))
        stored.insert(BackupSection::Calls, int(scratch.calls->calls().value().size()));
    if (sections.contains(BackupSection::Messages))
        stored.insert(BackupSection::Messages, int(scratch.messages->allMessages().value().size()));
    QCOMPARE(backup.info.counts, stored);
    // Compared without printing: a difference would show content of the phone.
    if (sections.contains(BackupSection::Contacts))
        QVERIFY(backup.data.contacts == scratch.contacts->allContacts().value());
    if (sections.contains(BackupSection::Calendar))
        QVERIFY(backup.data.events == scratch.organizer->allEvents().value());
    if (sections.contains(BackupSection::Tasks))
        QVERIFY(backup.data.tasks == scratch.organizer->tasks().value());
    if (sections.contains(BackupSection::Notes))
        QVERIFY(backup.data.notes == scratch.organizer->notes().value());

    for (auto it = backup.info.counts.cbegin(); it != backup.info.counts.cend(); ++it)
        qInfo() << "Backed up" << sectionKey(it.key()) << it.value();
    QVERIFY(QFile::remove(path));
}

void TestDeviceBackup::deletedTestEntriesComeBackFromTheBackup()
{
    if (!m_emulator)
        QSKIP("Deleting and restoring run on the emulator only");
    Scratch scratch;
    QVERIFY(scratch.open(m_connection.get()));
    removeTestEntries(scratch);

    Contact contact;
    contact.givenName = QStringLiteral("QPM Testkontakt ") + sixDigits();
    contact.familyName = QStringLiteral("Größe");
    contact.phones = {value(phoneType::mobile, QStringLiteral("+49 30 00000007"))};
    contact.emails = {value(emailType::work, QStringLiteral("qpm-backup@example.org"))};
    contact.note = QStringLiteral("Sicherung Äö 😀");
    const Result<SaveOutcome> saved = waitResult(
        scratch.contactsService->save(contact, false, SaveMode::CheckConflict), writeTimeoutMs);
    QVERIFY(saved.hasValue());
    QList<BackupSection> sections = {BackupSection::Contacts};

    const std::optional<CalendarInfo> target = writableCalendar(*scratch.organizer);
    CalendarEvent event;
    quint32 eventLuid = 0;
    if (target) {
        const QDate tomorrow = QDate::currentDate().addDays(1);
        event.summary = QStringLiteral("QPM Testtermin ") + sixDigits();
        event.location = QStringLiteral("Prüfraum");
        event.start = QDateTime(tomorrow, QTime(10, 0), QTimeZone::UTC);
        event.end = QDateTime(tomorrow, QTime(11, 0), QTimeZone::UTC);
        event.calendarUrl = target->url;
        event.calendarTitle = target->title;
        const Result<EventSaveOutcome> created = waitResult(
            scratch.calendarService->saveEvent(event, SaveMode::CheckConflict), writeTimeoutMs);
        QVERIFY(created.hasValue());
        eventLuid = created.value().luid;
        sections.append(BackupSection::Calendar);
    } else {
        qInfo() << "No writable calendar; the appointment is left out";
    }

    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString path = folder.filePath(QStringLiteral("emulator.qpmbackup"));
    QVERIFY(waitResult(scratch.backup->create(path, sections, CsvLanguage::English), syncTimeoutMs)
                .hasValue());

    QVERIFY(waitResult(scratch.contactsService->remove({saved.value().luid}), writeTimeoutMs)
                .hasValue());
    if (eventLuid != 0)
        QVERIFY(
            waitResult(scratch.calendarService->remove({eventLuid}), writeTimeoutMs).hasValue());

    const Result<Backup> backup = waitResult(scratch.backup->open(path), syncTimeoutMs);
    QVERIFY(backup.hasValue());
    const Result<BackupPreview> preview =
        waitResult(scratch.backup->preview(backup.value()), syncTimeoutMs);
    QVERIFY(preview.hasValue());
    QVERIFY(preview.value().comparedWithPhone);
    QCOMPARE(preview.value().sections.value(BackupSection::Contacts).added, 1);
    QCOMPARE(preview.value().sections.value(BackupSection::Contacts).changed, 0);
    if (eventLuid != 0) {
        QCOMPARE(preview.value().sections.value(BackupSection::Calendar).added, 1);
        QCOMPARE(preview.value().sections.value(BackupSection::Calendar).changed, 0);
    }

    const Result<RestoreReport> report =
        waitResult(scratch.backup->restore(backup.value(), sections), syncTimeoutMs);
    QVERIFY(report.hasValue());
    QVERIFY(!report.value().cancelled);
    for (const BackupSection section : std::as_const(sections)) {
        QCOMPARE(report.value().sections.value(section).restored, 1);
        QCOMPARE(report.value().sections.value(section).failed, 0);
    }

    QVERIFY(waitResult(scratch.contactsService->sync(), syncTimeoutMs).hasValue());
    std::optional<Contact> back;
    const QList<Contact> contacts = scratch.contacts->allContacts().value();
    for (const Contact &stored : contacts) {
        if (stored.givenName == contact.givenName)
            back = stored;
    }
    QVERIFY(back);
    QVERIFY(back->luid != saved.value().luid);
    QCOMPARE(back->familyName, contact.familyName);
    QCOMPARE(back->note, contact.note);
    QCOMPARE(back->phones.size(), 1);
    QCOMPARE(back->phones.at(0).value, contact.phones.at(0).value);
    QCOMPARE(back->phones.at(0).type, contact.phones.at(0).type);
    QCOMPARE(back->emails.size(), 1);
    QCOMPARE(back->emails.at(0).value, contact.emails.at(0).value);
    if (eventLuid != 0) {
        QVERIFY(waitResult(scratch.calendarService->sync(), syncTimeoutMs).hasValue());
        std::optional<CalendarEvent> restored;
        const QList<CalendarEvent> events = scratch.organizer->allEvents().value();
        for (const CalendarEvent &stored : events) {
            if (stored.summary == event.summary)
                restored = stored;
        }
        QVERIFY(restored);
        QCOMPARE(restored->location, event.location);
        QCOMPARE(restored->start, event.start);
        QCOMPARE(restored->end, event.end);
        QCOMPARE(restored->calendarTitle, event.calendarTitle);
    }

    // A second look finds nothing left to restore.
    const Result<BackupPreview> again =
        waitResult(scratch.backup->preview(backup.value()), syncTimeoutMs);
    QVERIFY(again.hasValue());
    for (const BackupSection section : std::as_const(sections)) {
        QCOMPARE(again.value().sections.value(section).added, 0);
        QCOMPARE(again.value().sections.value(section).changed, 0);
    }
    removeTestEntries(scratch);
}

QTEST_GUILESS_MAIN(TestDeviceBackup)

#include "tst_device_backup.moc"
