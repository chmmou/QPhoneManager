#include "CommandGuard.h"

#include "core/codec/CalMetadata.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/VNote.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/AlarmService.h"
#include "core/service/CalendarService.h"
#include "core/service/NotesService.h"
#include "core/store/OrganizerStore.h"
#include "core/store/ProfileStore.h"

#include "WaitFor.h"

#include <QDir>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>
#include <optional>

using namespace qpm::core;
using namespace qpm::core::codec;
using namespace qpm::core::protocol;
using qpm::core::service::AlarmService;
using qpm::core::service::CalendarService;
using qpm::core::service::EventSaveOutcome;
using qpm::core::service::NoteSaveOutcome;
using qpm::core::service::NotesService;
using qpm::core::service::PhoneAlarm;
using qpm::core::service::SaveMode;
using qpm::core::service::TaskSaveOutcome;
using qpm::core::store::OrganizerChanges;
using qpm::core::store::OrganizerStore;
using qpm::core::store::ProfileStore;
using qpm::test::CommandGuard;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int syncTimeoutMs = 240000;
constexpr int writeTimeoutMs = 60000;
constexpr int connectTries = 10;
constexpr qint64 secondsPerHour = 3600;

// Every entry a test writes carries one of these titles; nothing else is touched.
bool isTestTitle(const QString &title)
{
    static const QRegularExpression pattern(
        QStringLiteral("^QPM Test(termin|serie|tag|aufgabe|notiz) \\d{6}$"));
    return pattern.match(title).hasMatch();
}

QString sixDigits()
{
    return QString::number(QRandomGenerator::global()->bounded(100000, 999999));
}

QString firstLine(const QString &text)
{
    return text.section(u'\n', 0, 0).trimmed();
}

// A store that lives as long as one test function, so that nothing read from
// a phone stays on this computer.
struct ScratchStore
{
    QTemporaryDir dir;
    std::unique_ptr<ProfileStore> profile;
    std::shared_ptr<OrganizerStore> organizer;

    [[nodiscard]] bool open()
    {
        if (!dir.isValid())
            return false;
        profile = std::make_unique<ProfileStore>(dir.path(), QStringLiteral("device"));
        if (!profile->open().hasValue())
            return false;
        organizer = std::make_shared<OrganizerStore>(profile.get());
        return true;
    }
};

QDateTime toTheSecond(const QDateTime &time)
{
    return QDateTime::fromSecsSinceEpoch(time.toSecsSinceEpoch(), QTimeZone::UTC);
}

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling. On the emulator ("emulator-..." serials) all-day, timed and
// repeating appointments, a task and a note are created, read back, changed
// and deleted, and an alarm is switched and switched back. On any other
// phone, which may be someone's own, the calendar, the tasks, the notes and
// the alarms are read and only their count and form are checked; the one
// write there is one appointment, one task and one note with a test title,
// deleted again in the same test. Entries with a test title left over from
// an earlier run are removed first. No content, title, number or name is
// printed or kept beyond the test.
class TestDeviceOrganizer : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceOrganizer(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void leftoversOfEarlierRunsAreRemoved();
    void organizerIsReadAndWellFormed();
    void appointmentsMakeTheRoundTripOnTheEmulator();
    void taskAndNoteMakeTheRoundTripOnTheEmulator();
    void alarmIsSwitchedAndRestoredOnTheEmulator();
    void ownTestEntriesAreWrittenAndDeletedAgain();

private:
    [[nodiscard]] bool connectToClient();
    [[nodiscard]] Result<OrganizerChanges> syncCalendar(CalendarService &service);
    [[nodiscard]] Result<OrganizerChanges> syncNotes(NotesService &service);
    [[nodiscard]] static std::optional<CalendarInfo> writableCalendar(const OrganizerStore &store);

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    QPointer<CommandGuard> m_guard;
};

void TestDeviceOrganizer::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    qInfo() << (m_emulator ? "Target: emulator, full round trips"
                           : "Target: phone, read only and one entry of each kind");

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

void TestDeviceOrganizer::cleanupTestCase()
{
    if (!m_adb || m_localPort == 0)
        return;
    const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, m_localPort);
    QVERIFY(waitFor(removed, readyTimeoutMs));
}

void TestDeviceOrganizer::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    QVERIFY(connectToClient());
}

void TestDeviceOrganizer::cleanup()
{
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
}

bool TestDeviceOrganizer::connectToClient()
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

Result<OrganizerChanges> TestDeviceOrganizer::syncCalendar(CalendarService &service)
{
    const QFuture<Result<OrganizerChanges>> synced = service.sync();
    if (!waitFor(synced, syncTimeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("sync did not finish"));
    return synced.result();
}

Result<OrganizerChanges> TestDeviceOrganizer::syncNotes(NotesService &service)
{
    const QFuture<Result<OrganizerChanges>> synced = service.sync();
    if (!waitFor(synced, syncTimeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("sync did not finish"));
    return synced.result();
}

std::optional<CalendarInfo> TestDeviceOrganizer::writableCalendar(const OrganizerStore &store)
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

void TestDeviceOrganizer::leftoversOfEarlierRunsAreRemoved()
{
    ScratchStore scratch;
    QVERIFY(scratch.open());
    CalendarService calendar(m_connection.get());
    calendar.setStore(scratch.organizer);
    NotesService notes(m_connection.get());
    notes.setStore(scratch.organizer);
    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(syncNotes(notes).hasValue());

    QList<quint32> entries;
    const QList<CalendarEvent> foundEvents = scratch.organizer->allEvents().value();
    for (const CalendarEvent &event : foundEvents) {
        if (isTestTitle(event.summary))
            entries.append(event.luid);
    }
    const QList<CalendarTask> foundTasks = scratch.organizer->tasks().value();
    for (const CalendarTask &task : foundTasks) {
        if (isTestTitle(task.summary))
            entries.append(task.luid);
    }
    QList<quint32> written;
    const QList<Note> foundNotes = scratch.organizer->notes().value();
    for (const Note &note : foundNotes) {
        if (isTestTitle(firstLine(note.body)))
            written.append(note.luid);
    }
    qInfo() << "Leftover test entries:" << entries.size() + written.size();
    if (!entries.isEmpty()) {
        const QFuture<Result<void>> removed = calendar.remove(entries);
        QVERIFY(waitFor(removed, writeTimeoutMs));
        QVERIFY(removed.result().hasValue());
    }
    if (!written.isEmpty()) {
        const QFuture<Result<void>> removed = notes.remove(written);
        QVERIFY(waitFor(removed, writeTimeoutMs));
        QVERIFY(removed.result().hasValue());
    }

    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(syncNotes(notes).hasValue());
    const QList<CalendarEvent> remainingEvents = scratch.organizer->allEvents().value();
    for (const CalendarEvent &event : remainingEvents)
        QVERIFY(!isTestTitle(event.summary));
    const QList<CalendarTask> remainingTasks = scratch.organizer->tasks().value();
    for (const CalendarTask &task : remainingTasks)
        QVERIFY(!isTestTitle(task.summary));
    const QList<Note> remainingNotes = scratch.organizer->notes().value();
    for (const Note &note : remainingNotes)
        QVERIFY(!isTestTitle(firstLine(note.body)));
}

void TestDeviceOrganizer::organizerIsReadAndWellFormed()
{
    ScratchStore scratch;
    QVERIFY(scratch.open());
    CalendarService calendar(m_connection.get());
    calendar.setStore(scratch.organizer);
    NotesService notes(m_connection.get());
    notes.setStore(scratch.organizer);

    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(syncNotes(notes).hasValue());

    const CalMetadata metadata = scratch.organizer->calendarMetadata().value();
    int writable = 0;
    for (const CalendarInfo &info : metadata.calendars) {
        QVERIFY(!info.title.isEmpty());
        if (info.isWritable())
            ++writable;
    }
    const QList<CalendarEvent> events = scratch.organizer->allEvents().value();
    int allDay = 0;
    int repeating = 0;
    for (const CalendarEvent &event : events) {
        QVERIFY2(event.start.isValid(), "an appointment came without a start");
        QVERIFY2(event.end >= event.start, "an appointment ends before it starts");
        QVERIFY(event.luid != 0);
        if (event.allDay) {
            ++allDay;
            QCOMPARE(event.start.time(), QTime(0, 0));
        }
        if (!event.rrule.isEmpty())
            ++repeating;
    }
    const QList<CalendarTask> tasks = scratch.organizer->tasks().value();
    for (const CalendarTask &task : tasks)
        QVERIFY(task.luid != 0);
    const QList<Note> written = scratch.organizer->notes().value();
    for (const Note &note : written)
        QVERIFY(note.luid != 0);
    qInfo() << "Calendars:" << metadata.calendars.size() << "writable:" << writable
            << "appointments:" << events.size() << "all day:" << allDay
            << "repeating:" << repeating << "tasks:" << tasks.size()
            << "notes:" << written.size()
            << "task categories:" << metadata.taskCategories.size();

    AlarmService alarms(m_connection.get());
    const QFuture<Result<QList<PhoneAlarm>>> read = alarms.alarms();
    QVERIFY(waitFor(read, readyTimeoutMs));
    if (read.result().hasValue()) {
        const QList<PhoneAlarm> readAlarms = read.result().value();
        for (const PhoneAlarm &alarm : readAlarms)
            QVERIFY(alarm.time.isValid());
        qInfo() << "Alarms:" << read.result().value().size();
    } else {
        QCOMPARE(read.result().error().code(), ErrorCode::NotSupported);
        qInfo() << "Alarms: not supported by the phone";
    }
}

void TestDeviceOrganizer::appointmentsMakeTheRoundTripOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Appointments are changed and deleted on the emulator only");
    ScratchStore scratch;
    QVERIFY(scratch.open());
    CalendarService service(m_connection.get());
    service.setStore(scratch.organizer);
    QVERIFY(syncCalendar(service).hasValue());
    const std::optional<CalendarInfo> target = writableCalendar(*scratch.organizer);
    QVERIFY2(target, "the emulator has no synced calendar the app may write to");

    const QDate day = QDate::currentDate().addDays(3);
    CalendarEvent timed;
    timed.summary = QStringLiteral("QPM Testtermin ") + sixDigits();
    timed.location = QStringLiteral("Raum 1");
    timed.description = QStringLiteral("Zeile eins\nZeile zwei, mit Komma; und Ä");
    timed.start = toTheSecond(QDateTime(day, QTime(10, 0)).toUTC());
    timed.end = timed.start.addSecs(secondsPerHour);
    timed.alarms = {timed.start.addSecs(-15 * 60)};
    CalendarEvent allDay;
    allDay.summary = QStringLiteral("QPM Testtag ") + sixDigits();
    allDay.allDay = true;
    allDay.start = QDateTime(day, QTime(0, 0), QTimeZone::UTC);
    allDay.end = allDay.start.addDays(2);
    CalendarEvent series;
    series.summary = QStringLiteral("QPM Testserie ") + sixDigits();
    series.start = toTheSecond(QDateTime(day, QTime(18, 30)).toUTC());
    series.end = series.start.addSecs(secondsPerHour / 2);
    series.rrule = QStringLiteral("FREQ=WEEKLY;COUNT=3");

    QList<quint32> created;
    for (const CalendarEvent &planned : {timed, allDay, series}) {
        CalendarEvent event = planned;
        event.calendarUrl = target->url;
        event.calendarTitle = target->title;
        const QFuture<Result<EventSaveOutcome>> saved = service.saveEvent(event, SaveMode::Overwrite);
        QVERIFY(waitFor(saved, writeTimeoutMs));
        QVERIFY2(saved.result().hasValue(), qPrintable(saved.result().hasValue()
                                                           ? QString()
                                                           : saved.result().error().message()));
        const quint32 luid = saved.result().value().luid;
        QVERIFY(luid != 0);
        created.append(luid);

        ScratchStore fresh;
        QVERIFY(fresh.open());
        CalendarService reader(m_connection.get());
        reader.setStore(fresh.organizer);
        QVERIFY(syncCalendar(reader).hasValue());
        const Result<CalendarEvent> back = fresh.organizer->event(luid);
        QVERIFY(back.hasValue());
        QVERIFY2(sameEventContent(back.value(), event),
                 qPrintable(QStringLiteral("appointment %1 came back changed").arg(created.size())));
        QCOMPARE(back.value().calendarTitle, target->title);

        CalendarEvent changed = back.value();
        changed.summary += QStringLiteral(" B");
        changed.location = QStringLiteral("Raum 2");
        const QFuture<Result<EventSaveOutcome>> resaved =
            service.saveEvent(changed, SaveMode::Overwrite);
        QVERIFY(waitFor(resaved, writeTimeoutMs));
        QVERIFY(resaved.result().hasValue());
        QVERIFY(!resaved.result().value().conflict);
        QVERIFY(syncCalendar(reader).hasValue());
        QVERIFY(sameEventContent(fresh.organizer->event(luid).value(), changed));
    }

    const QFuture<Result<void>> removed = service.remove(created);
    QVERIFY(waitFor(removed, writeTimeoutMs));
    QVERIFY(removed.result().hasValue());
    QVERIFY(syncCalendar(service).hasValue());
    for (const quint32 luid : std::as_const(created))
        QVERIFY(!scratch.organizer->event(luid).hasValue());
}

void TestDeviceOrganizer::taskAndNoteMakeTheRoundTripOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Tasks and notes are changed and deleted on the emulator only");
    ScratchStore scratch;
    QVERIFY(scratch.open());
    CalendarService calendar(m_connection.get());
    calendar.setStore(scratch.organizer);
    NotesService notes(m_connection.get());
    notes.setStore(scratch.organizer);
    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(syncNotes(notes).hasValue());

    CalendarTask task;
    task.summary = QStringLiteral("QPM Testaufgabe ") + sixDigits();
    task.description = QStringLiteral("Bitte erledigen");
    task.due = QDate::currentDate().addDays(5);
    task.priority = TaskPriority::High;
    const QFuture<Result<TaskSaveOutcome>> savedTask = calendar.saveTask(task, SaveMode::Overwrite);
    QVERIFY(waitFor(savedTask, writeTimeoutMs));
    QVERIFY(savedTask.result().hasValue());
    const quint32 taskLuid = savedTask.result().value().luid;
    QVERIFY(syncCalendar(calendar).hasValue());
    CalendarTask taskBack = scratch.organizer->task(taskLuid).value();
    QVERIFY2(sameTaskContent(taskBack, task), "the task came back changed");
    taskBack.completed = true;
    const QFuture<Result<TaskSaveOutcome>> done = calendar.saveTask(taskBack, SaveMode::Overwrite);
    QVERIFY(waitFor(done, writeTimeoutMs));
    QVERIFY(done.result().hasValue());
    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(scratch.organizer->task(taskLuid).value().completed);

    Note note;
    note.body = QStringLiteral("QPM Testnotiz ") + sixDigits() + QStringLiteral("\nZweite Zeile ÄÖÜ");
    const QFuture<Result<NoteSaveOutcome>> savedNote = notes.save(note, SaveMode::Overwrite);
    QVERIFY(waitFor(savedNote, writeTimeoutMs));
    QVERIFY(savedNote.result().hasValue());
    const quint32 noteLuid = savedNote.result().value().luid;
    QVERIFY(syncNotes(notes).hasValue());
    Note noteBack = scratch.organizer->note(noteLuid).value();
    QCOMPARE(noteBack.body, note.body);
    noteBack.body += QStringLiteral("\nDritte Zeile");
    const QFuture<Result<NoteSaveOutcome>> changed = notes.save(noteBack, SaveMode::Overwrite);
    QVERIFY(waitFor(changed, writeTimeoutMs));
    QVERIFY(changed.result().hasValue());
    QVERIFY(syncNotes(notes).hasValue());
    QCOMPARE(scratch.organizer->note(noteLuid).value().body, noteBack.body);

    const QFuture<Result<void>> taskRemoved = calendar.remove({taskLuid});
    QVERIFY(waitFor(taskRemoved, writeTimeoutMs));
    QVERIFY(taskRemoved.result().hasValue());
    const QFuture<Result<void>> noteRemoved = notes.remove({noteLuid});
    QVERIFY(waitFor(noteRemoved, writeTimeoutMs));
    QVERIFY(noteRemoved.result().hasValue());
    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(syncNotes(notes).hasValue());
    QVERIFY(!scratch.organizer->task(taskLuid).hasValue());
    QVERIFY(!scratch.organizer->note(noteLuid).hasValue());
}

void TestDeviceOrganizer::alarmIsSwitchedAndRestoredOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Alarms are changed on the emulator only");
    AlarmService service(m_connection.get());
    const QFuture<Result<QList<PhoneAlarm>>> read = service.alarms();
    QVERIFY(waitFor(read, readyTimeoutMs));
    if (!read.result().hasValue() && read.result().error().code() == ErrorCode::NotSupported)
        QSKIP("The clock app of the emulator cannot be reached");
    QVERIFY(read.result().hasValue());
    if (read.result().value().isEmpty())
        QSKIP("The emulator has no alarm to switch");
    const PhoneAlarm original = read.result().value().constFirst();
    PhoneAlarm switched = original;
    switched.enabled = !original.enabled;

    const QFuture<Result<void>> changed = service.save(switched);
    QVERIFY(waitFor(changed, readyTimeoutMs));
    const QFuture<Result<QList<PhoneAlarm>>> again = service.alarms();
    QVERIFY(waitFor(again, readyTimeoutMs));
    const QFuture<Result<void>> restored = service.save(original);
    QVERIFY(waitFor(restored, readyTimeoutMs));

    QVERIFY(changed.result().hasValue());
    QVERIFY(again.result().hasValue());
    bool found = false;
    const QList<PhoneAlarm> switchedAlarms = again.result().value();
    for (const PhoneAlarm &alarm : switchedAlarms) {
        if (alarm.id == original.id) {
            found = true;
            QCOMPARE(alarm.enabled, switched.enabled);
            QCOMPARE(alarm.time, original.time);
        }
    }
    QVERIFY(found);
    QVERIFY(restored.result().hasValue());
    const QFuture<Result<QList<PhoneAlarm>>> last = service.alarms();
    QVERIFY(waitFor(last, readyTimeoutMs));
    QVERIFY(last.result().hasValue());
    const QList<PhoneAlarm> restoredAlarms = last.result().value();
    for (const PhoneAlarm &alarm : restoredAlarms) {
        if (alarm.id == original.id)
            QCOMPARE(alarm.enabled, original.enabled);
    }
}

void TestDeviceOrganizer::ownTestEntriesAreWrittenAndDeletedAgain()
{
    ScratchStore scratch;
    QVERIFY(scratch.open());
    CalendarService calendar(m_connection.get());
    calendar.setStore(scratch.organizer);
    NotesService notes(m_connection.get());
    notes.setStore(scratch.organizer);
    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(syncNotes(notes).hasValue());
    const int eventsBefore = int(scratch.organizer->allEvents().value().size());
    const int tasksBefore = int(scratch.organizer->tasks().value().size());
    const int notesBefore = int(scratch.organizer->notes().value().size());
    const std::optional<CalendarInfo> target = writableCalendar(*scratch.organizer);

    QList<quint32> calendarEntries;
    if (target) {
        CalendarEvent event;
        event.summary = QStringLiteral("QPM Testtermin ") + sixDigits();
        event.start = toTheSecond(QDateTime(QDate::currentDate().addDays(30), QTime(3, 0)).toUTC());
        event.end = event.start.addSecs(secondsPerHour / 4);
        event.calendarUrl = target->url;
        event.calendarTitle = target->title;
        const QFuture<Result<EventSaveOutcome>> saved = calendar.saveEvent(event, SaveMode::Overwrite);
        QVERIFY(waitFor(saved, writeTimeoutMs));
        QVERIFY(saved.result().hasValue());
        calendarEntries.append(saved.result().value().luid);
    } else {
        qInfo() << "No synced calendar the app may write to; no appointment is written";
    }
    CalendarTask task;
    task.summary = QStringLiteral("QPM Testaufgabe ") + sixDigits();
    const QFuture<Result<TaskSaveOutcome>> savedTask = calendar.saveTask(task, SaveMode::Overwrite);
    QVERIFY(waitFor(savedTask, writeTimeoutMs));
    QVERIFY(savedTask.result().hasValue());
    calendarEntries.append(savedTask.result().value().luid);
    Note note;
    note.body = QStringLiteral("QPM Testnotiz ") + sixDigits();
    const QFuture<Result<NoteSaveOutcome>> savedNote = notes.save(note, SaveMode::Overwrite);
    QVERIFY(waitFor(savedNote, writeTimeoutMs));
    QVERIFY(savedNote.result().hasValue());
    const quint32 noteLuid = savedNote.result().value().luid;

    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(syncNotes(notes).hasValue());
    QCOMPARE(int(scratch.organizer->allEvents().value().size()), eventsBefore + (target ? 1 : 0));
    QCOMPARE(int(scratch.organizer->tasks().value().size()), tasksBefore + 1);
    QCOMPARE(int(scratch.organizer->notes().value().size()), notesBefore + 1);

    const QFuture<Result<void>> removed = calendar.remove(calendarEntries);
    QVERIFY(waitFor(removed, writeTimeoutMs));
    QVERIFY(removed.result().hasValue());
    const QFuture<Result<void>> noteRemoved = notes.remove({noteLuid});
    QVERIFY(waitFor(noteRemoved, writeTimeoutMs));
    QVERIFY(noteRemoved.result().hasValue());
    QVERIFY(syncCalendar(calendar).hasValue());
    QVERIFY(syncNotes(notes).hasValue());
    QCOMPARE(int(scratch.organizer->allEvents().value().size()), eventsBefore);
    QCOMPARE(int(scratch.organizer->tasks().value().size()), tasksBefore);
    QCOMPARE(int(scratch.organizer->notes().value().size()), notesBefore);
}

QTEST_GUILESS_MAIN(TestDeviceOrganizer)

#include "tst_device_organizer.moc"
