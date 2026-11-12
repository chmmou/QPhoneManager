#include "core/codec/CalMetadata.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/VNote.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/CalendarService.h"
#include "core/service/NotesService.h"
#include "core/store/OrganizerStore.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::CalendarService;
using qpm::core::service::EventSaveOutcome;
using qpm::core::service::NoteSaveOutcome;
using qpm::core::service::NotesService;
using qpm::core::service::OrganizerImportOutcome;
using qpm::core::service::SaveMode;
using qpm::core::service::TaskSaveOutcome;
using qpm::core::store::OrganizerChanges;
using qpm::core::store::OrganizerStore;
using qpm::core::store::ProfileStore;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;
constexpr quint32 meetingLuid = 0x10000001;
constexpr quint32 holidayLuid = 0x10000002;
constexpr quint32 seriesLuid = 0x10000003;
constexpr quint32 taskLuid = 4;
constexpr QLatin1StringView accountUrl("anna@example.org@com.google/NULL");

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
           "<calendar selected=\"0\" url=\"anna@example.org@com.google/NULL\" title=\"Arbeit\" "
           "color=\"FF4285F4\" hidden=\"0\" timezone=\"Europe/Berlin\" access_level=\"700\"/>\r\n"
           "</calendars>\r\n<tasksupport>1</tasksupport>\r\n<task_categories>\r\n"
           "<category name=\"Haushalt\" color=\"FF8800\" id=\"1\"/>\r\n"
           "</task_categories>\r\n</metadata>";
}

CalendarEvent makeEvent(quint32 luid, const QString &summary, const QDateTime &start,
                    const QDateTime &end, const QString &calendar)
{
    CalendarEvent result;
    result.luid = luid;
    result.summary = summary;
    result.start = start;
    result.end = end;
    result.calendarUrl = accountUrl;
    result.calendarTitle = calendar;
    return result;
}

QList<FakeSyncObject> exampleCalendar()
{
    CalendarEvent meeting = makeEvent(meetingLuid, QStringLiteral("Elternabend"), utc(2026, 11, 12, 17),
                                  utc(2026, 11, 12, 18, 30), QStringLiteral("Familie"));
    meeting.location = QStringLiteral("Schule");
    meeting.alarms = {utc(2026, 11, 12, 16, 30)};
    CalendarEvent holiday = makeEvent(holidayLuid, QStringLiteral("Tag der Deutschen Einheit"),
                                  utc(2026, 10, 3), utc(2026, 10, 4), QStringLiteral("Feiertage"));
    holiday.allDay = true;
    CalendarEvent series = makeEvent(seriesLuid, QStringLiteral("Sport"), utc(2026, 9, 1, 16),
                                 utc(2026, 9, 1, 17), QStringLiteral("Familie"));
    series.rrule = QStringLiteral("FREQ=WEEKLY;BYDAY=TU");
    CalendarTask task;
    task.luid = taskLuid;
    task.summary = QStringLiteral("Fenster putzen");
    task.due = QDate(2026, 11, 20);
    task.categories = {QStringLiteral("Haushalt")};
    return {{meetingLuid, encodeEventAsPhoneSendsIt(meeting)},
            {holidayLuid, encodeEventAsPhoneSendsIt(holiday)},
            {seriesLuid, encodeEventAsPhoneSendsIt(series)},
            {taskLuid, encodeTaskAsPhoneSendsIt(task)}};
}

QList<FakeSyncObject> exampleNotes()
{
    Note note;
    note.luid = 1;
    note.body = QStringLiteral("Einkaufsliste:\nMilch, Brot");
    note.lastModified = utc(2026, 11, 10, 8);
    note.categories = {QStringLiteral("Ideen")};
    return {{1, encodeNoteAsPhoneSendsIt(note)}};
}

QByteArray noteMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\"><note_categories>\r\n"
           "<category name=\"Ideen\" color=\"00AA00\" id=\"1\"/>\r\n</note_categories>\r\n</metadata>";
}

} // namespace

class TestOrganizerService : public QObject
{
    Q_OBJECT

public:
    explicit TestOrganizerService(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void syncFillsEventsTasksCalendarsAndCategories();
    void nextSyncBringsOnlyWhatChanged();
    void spanFindsOverlappingEventsAndEverySeries();
    void eventIsCreatedChangedAndDeletedOnThePhone();
    void phoneTakesChangesOnlyAfterLoadingTheCalendar();
    void eventGoesOnlyIntoAWritableCalendar();
    void calendarOutsideTheSyncIsChosenBeforeTheEvent();
    void eventChangedOnThePhoneIsReportedAsAConflict();
    void taskWithANewCategoryIsCreatedAndCompleted();
    void taskCategoriesAreAddedRenamedAndDeleted();
    void choiceOfCalendarsGoesToThePhone();
    void importCreatesEventsInTheTargetAndTasks();
    void noteIsCreatedChangedAndDeleted();
    void noteChangedOnThePhoneIsReportedAsAConflict();
    void noteCategoriesAreChanged();
    void missingCalendarPermissionIsReported();
    void callsWithoutStoreOrConnectionFail();

private:
    template <typename T> [[nodiscard]] Result<T> wait(const QFuture<Result<T>> &future)
    {
        if (!waitFor(future, timeoutMs))
            return Error(ErrorCode::Timeout, QStringLiteral("did not finish"));
        return future.result();
    }
    [[nodiscard]] QStringList calendarPuts() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<ProfileStore> m_profile;
    std::shared_ptr<OrganizerStore> m_store;
    std::unique_ptr<CalendarService> m_calendar;
    std::unique_ptr<NotesService> m_notes;
};

void TestOrganizerService::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_profile = std::make_unique<ProfileStore>(m_dir->path(), QStringLiteral("dev-1"));
    QVERIFY(m_profile->open().hasValue());
    m_store = std::make_shared<OrganizerStore>(m_profile.get());

    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("cal"), exampleCalendar());
    m_phone->setMetadata(QStringLiteral("cal"), calendarMetadata());
    m_phone->setObjects(QStringLiteral("notes"), exampleNotes());
    m_phone->setMetadata(QStringLiteral("notes"), noteMetadata());

    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_calendar = std::make_unique<CalendarService>(m_connection.get());
    m_calendar->setStore(m_store);
    m_notes = std::make_unique<NotesService>(m_connection.get());
    m_notes->setStore(m_store);
}

void TestOrganizerService::cleanup()
{
    m_calendar.reset();
    m_notes.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
    m_store.reset();
    m_profile.reset();
    m_phone.reset();
    m_dir.reset();
}

QStringList TestOrganizerService::calendarPuts() const
{
    QStringList names;
    const QList<std::pair<QString, QByteArray>> puts = m_phone->syncPuts();
    for (const auto &put : puts)
        names.append(put.first);
    return names;
}

void TestOrganizerService::syncFillsEventsTasksCalendarsAndCategories()
{
    QSignalSpy changed(m_calendar.get(), &CalendarService::calendarChanged);

    const Result<OrganizerChanges> pulled = wait(m_calendar->sync());

    QVERIFY(pulled.hasValue());
    QCOMPARE(pulled.value().added, 4);
    QCOMPARE(changed.count(), 1);
    const QList<CalendarEvent> events = m_store->allEvents().value();
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.at(0).luid, seriesLuid);
    const CalendarEvent meeting = m_store->event(meetingLuid).value();
    QCOMPARE(meeting.summary, QStringLiteral("Elternabend"));
    QCOMPARE(meeting.location, QStringLiteral("Schule"));
    QCOMPARE(meeting.alarms, QList<QDateTime>({utc(2026, 11, 12, 16, 30)}));
    QCOMPARE(meeting.calendarTitle, QStringLiteral("Familie"));
    const QList<CalendarTask> tasks = m_store->tasks().value();
    QCOMPARE(tasks.size(), 1);
    QCOMPARE(tasks.constFirst().categories, QStringList({QStringLiteral("Haushalt")}));
    const CalMetadata metadata = m_store->calendarMetadata().value();
    QCOMPARE(metadata.calendars.size(), 3);
    QCOMPARE(metadata.taskCategories.size(), 1);
    QCOMPARE(metadata.taskCategories.constFirst().color, 0xFF8800U);
    QCOMPARE(m_store->event(0x10000099).error().code(), ErrorCode::NotFound);
}

void TestOrganizerService::nextSyncBringsOnlyWhatChanged()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    CalendarEvent moved = parsePhoneEvent(m_phone->area(QStringLiteral("cal")).objects.at(0).data).value();
    moved.start = moved.start.addDays(1);
    moved.end = moved.end.addDays(1);
    m_phone->editObject(QStringLiteral("cal"), meetingLuid, encodeEventAsPhoneSendsIt(moved));
    m_phone->removeObject(QStringLiteral("cal"), taskLuid);

    const Result<OrganizerChanges> pulled = wait(m_calendar->sync());

    QVERIFY(pulled.hasValue());
    QCOMPARE(pulled.value().changed, 1);
    QCOMPARE(pulled.value().removed, 1);
    QCOMPARE(pulled.value().added, 0);
    QCOMPARE(m_store->event(meetingLuid).value().start, utc(2026, 11, 13, 17));
    QVERIFY(m_store->tasks().value().isEmpty());
}

void TestOrganizerService::spanFindsOverlappingEventsAndEverySeries()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());

    const QList<CalendarEvent> november =
        m_store->eventsBetween(utc(2026, 11, 1), utc(2026, 12, 1)).value();
    const QList<CalendarEvent> october =
        m_store->eventsBetween(utc(2026, 10, 3, 12), utc(2026, 10, 3, 13)).value();

    QCOMPARE(november.size(), 2);
    QCOMPARE(november.at(0).luid, seriesLuid);
    QCOMPARE(november.at(1).luid, meetingLuid);
    QCOMPARE(october.size(), 2);
    QCOMPARE(october.at(0).luid, seriesLuid);
    QCOMPARE(october.at(1).luid, holidayLuid);
}

void TestOrganizerService::eventIsCreatedChangedAndDeletedOnThePhone()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    CalendarEvent fresh = makeEvent(0, QStringLiteral("Zahnarzt"), utc(2026, 11, 18, 8),
                                utc(2026, 11, 18, 9), QStringLiteral("Familie"));
    fresh.alarms = {utc(2026, 11, 18, 7, 30)};
    fresh.description = QStringLiteral("Karte mitnehmen\nNicht vergessen");

    const Result<EventSaveOutcome> created = wait(m_calendar->saveEvent(fresh, SaveMode::CheckConflict));

    QVERIFY(created.hasValue());
    const quint32 luid = created.value().luid;
    QVERIFY((luid & 0x10000000U) != 0);
    CalendarEvent stored = m_store->event(luid).value();
    QVERIFY(sameEventContent(stored, fresh));
    QCOMPARE(stored.calendarTitle, QStringLiteral("Familie"));

    stored.summary = QStringLiteral("Zahnarzt, verschoben");
    stored.start = stored.start.addSecs(3600);
    stored.end = stored.end.addSecs(3600);
    stored.rrule = QStringLiteral("FREQ=MONTHLY;COUNT=3");
    const Result<EventSaveOutcome> changed = wait(m_calendar->saveEvent(stored, SaveMode::CheckConflict));

    QVERIFY(changed.hasValue());
    QVERIFY(!changed.value().conflict);
    QVERIFY(sameEventContent(m_store->event(luid).value(), stored));
    const CalendarEvent onPhone = parsePhoneEvent(m_phone->area(QStringLiteral("cal")).objects.constLast().data).value();
    QCOMPARE(onPhone.summary, QStringLiteral("Zahnarzt, verschoben"));

    QVERIFY(wait(m_calendar->remove({luid})).hasValue());
    QCOMPARE(m_store->event(luid).error().code(), ErrorCode::NotFound);
    QCOMPARE(m_phone->area(QStringLiteral("cal")).objects.size(), 4);
}

void TestOrganizerService::phoneTakesChangesOnlyAfterLoadingTheCalendar()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    m_phone->clearLog();
    CalendarTask task;
    task.summary = QStringLiteral("Müll rausbringen");

    QVERIFY(wait(m_calendar->saveTask(task, SaveMode::CheckConflict)).hasValue());

    const QStringList commands = m_phone->receivedCommands();
    const qsizetype load = commands.indexOf(QStringLiteral("OBEX GET telecom/cal/initsync"));
    const qsizetype put = commands.indexOf(QStringLiteral("OBEX PUT telecom/cal/luid/.vcs"));
    QVERIFY(load >= 0);
    QVERIFY(put > load);
}

void TestOrganizerService::eventGoesOnlyIntoAWritableCalendar()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    m_phone->clearLog();

    const Result<EventSaveOutcome> readOnly = wait(m_calendar->saveEvent(
        makeEvent(0, QStringLiteral("x"), utc(2026, 12, 1, 9), utc(2026, 12, 1, 10), QStringLiteral("Feiertage")),
        SaveMode::CheckConflict));
    const Result<EventSaveOutcome> unknown = wait(m_calendar->saveEvent(
        makeEvent(0, QStringLiteral("x"), utc(2026, 12, 1, 9), utc(2026, 12, 1, 10), QStringLiteral("Gibt es nicht")),
        SaveMode::CheckConflict));
    const Result<EventSaveOutcome> untitled = wait(m_calendar->saveEvent(
        makeEvent(0, QStringLiteral("  "), utc(2026, 12, 1, 9), utc(2026, 12, 1, 10), QStringLiteral("Familie")),
        SaveMode::CheckConflict));

    QCOMPARE(readOnly.error().code(), ErrorCode::InvalidInput);
    QCOMPARE(unknown.error().code(), ErrorCode::InvalidInput);
    QCOMPARE(untitled.error().code(), ErrorCode::InvalidInput);
    QVERIFY(m_phone->syncPuts().isEmpty());
}

void TestOrganizerService::calendarOutsideTheSyncIsChosenBeforeTheEvent()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    m_phone->clearLog();

    const Result<EventSaveOutcome> created = wait(m_calendar->saveEvent(
        makeEvent(0, QStringLiteral("Projekt"), utc(2026, 12, 2, 9), utc(2026, 12, 2, 10),
              QStringLiteral("Arbeit")),
        SaveMode::CheckConflict));

    QVERIFY(created.hasValue());
    QCOMPARE(calendarPuts(),
             QStringList({QStringLiteral("telecom/cal/metadata.xml"),
                          QStringLiteral("telecom/cal/luid/.vcs")}));
    QCOMPARE(m_store->event(created.value().luid).value().calendarTitle, QStringLiteral("Arbeit"));
    const CalMetadata metadata = parseCalMetadata(m_phone->area(QStringLiteral("cal")).metadata).value();
    for (const CalendarInfo &calendar : metadata.calendars)
        QVERIFY2(calendar.selected, qPrintable(calendar.title));
    // The connection between the two writes is a new session, as the phone needs it.
    const QStringList commands = m_phone->receivedCommands();
    QCOMPARE(commands.count(QStringLiteral("OBEX CONNECT IRMC-SYNC")), 2);
}

void TestOrganizerService::eventChangedOnThePhoneIsReportedAsAConflict()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    CalendarEvent onPhone = m_store->event(meetingLuid).value();
    onPhone.summary = QStringLiteral("Elternabend, verlegt");
    m_phone->editObject(QStringLiteral("cal"), meetingLuid, encodeEventAsPhoneSendsIt(onPhone));
    CalendarEvent mine = m_store->event(meetingLuid).value();
    mine.location = QStringLiteral("Aula");
    m_phone->clearLog();

    const Result<EventSaveOutcome> first = wait(m_calendar->saveEvent(mine, SaveMode::CheckConflict));

    QVERIFY(first.hasValue());
    QVERIFY(first.value().conflict);
    QCOMPARE(first.value().phoneVersion.summary, QStringLiteral("Elternabend, verlegt"));
    QVERIFY(m_phone->syncPuts().isEmpty());

    QVERIFY(m_calendar->keepPhoneVersion(first.value().phoneObject).hasValue());
    QCOMPARE(m_store->event(meetingLuid).value().summary, QStringLiteral("Elternabend, verlegt"));

    const Result<EventSaveOutcome> forced = wait(m_calendar->saveEvent(mine, SaveMode::Overwrite));
    QVERIFY(forced.hasValue());
    QVERIFY(!forced.value().conflict);
    QCOMPARE(m_store->event(meetingLuid).value().location, QStringLiteral("Aula"));
    QCOMPARE(m_store->event(meetingLuid).value().summary, QStringLiteral("Elternabend"));
}

void TestOrganizerService::taskWithANewCategoryIsCreatedAndCompleted()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    CalendarTask task;
    task.summary = QStringLiteral("Steuer");
    task.due = QDate(2026, 12, 31);
    task.reminder = utc(2026, 12, 30, 8);
    task.priority = TaskPriority::High;
    task.categories = {QStringLiteral("Büro"), QStringLiteral("Haushalt")};

    const Result<TaskSaveOutcome> created = wait(m_calendar->saveTask(task, SaveMode::CheckConflict));

    QVERIFY(created.hasValue());
    CalendarTask stored = m_store->task(created.value().luid).value();
    QVERIFY(sameTaskContent(stored, task));
    const QList<OrganizerCategory> categories = m_store->calendarMetadata().value().taskCategories;
    QCOMPARE(categories.size(), 2);
    QCOMPARE(categories.at(0).name, QStringLiteral("Büro"));

    stored.completed = true;
    QVERIFY(wait(m_calendar->saveTask(stored, SaveMode::CheckConflict)).hasValue());
    QVERIFY(m_store->task(created.value().luid).value().completed);
}

void TestOrganizerService::taskCategoriesAreAddedRenamedAndDeleted()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());

    QVERIFY(wait(m_calendar->changeTaskCategories(
                     {{CategoryChange::Kind::Add, {0, QStringLiteral("Garten"), 0x33AA33}},
                      {CategoryChange::Kind::Edit, {1, QStringLiteral("Wohnung"), 0x112233}}}))
                .hasValue());
    const QList<OrganizerCategory> after = m_store->calendarMetadata().value().taskCategories;
    QVERIFY(wait(m_calendar->changeTaskCategories(
                     {{CategoryChange::Kind::Delete, {1, QStringLiteral("Wohnung"), 0}}}))
                .hasValue());

    QCOMPARE(after.size(), 2);
    QCOMPARE(after.at(0), (OrganizerCategory{2, QStringLiteral("Garten"), 0x33AA33}));
    QCOMPARE(after.at(1), (OrganizerCategory{1, QStringLiteral("Wohnung"), 0x112233}));
    QCOMPARE(m_store->calendarMetadata().value().taskCategories.size(), 1);
}

void TestOrganizerService::choiceOfCalendarsGoesToThePhone()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    QList<CalendarInfo> calendars = m_store->calendarMetadata().value().calendars;
    for (CalendarInfo &calendar : calendars) {
        calendar.selected = calendar.title != QLatin1StringView("Feiertage");
        calendar.isDefault = calendar.title == QLatin1StringView("Arbeit");
    }

    QVERIFY(wait(m_calendar->setCalendarSelection(calendars)).hasValue());

    const QList<CalendarInfo> stored = m_store->calendarMetadata().value().calendars;
    QCOMPARE(stored.size(), 3);
    for (const CalendarInfo &calendar : stored) {
        QCOMPARE(calendar.selected, calendar.title != QLatin1StringView("Feiertage"));
        QCOMPARE(calendar.isDefault, calendar.title == QLatin1StringView("Arbeit"));
    }
}

void TestOrganizerService::importCreatesEventsInTheTargetAndTasks()
{
    QVERIFY(wait(m_calendar->sync()).hasValue());
    const CalendarInfo target = m_store->calendarMetadata().value().calendars.at(0);
    QCOMPARE(target.title, QStringLiteral("Arbeit"));
    CalendarTask task;
    task.summary = QStringLiteral("Importierte Aufgabe");

    const Result<OrganizerImportOutcome> imported = wait(m_calendar->importItems(
        {makeEvent(0, QStringLiteral("Messe"), utc(2027, 3, 1, 9), utc(2027, 3, 1, 17), QString()),
         makeEvent(0, QStringLiteral("Ohne Beginn"), QDateTime(), QDateTime(), QString())},
        {task}, target));

    QVERIFY(imported.hasValue());
    QCOMPARE(imported.value().created, 2);
    QCOMPARE(imported.value().skipped, 1);
    const QList<CalendarEvent> events = m_store->allEvents().value();
    QCOMPARE(events.size(), 4);
    QCOMPARE(events.constLast().summary, QStringLiteral("Messe"));
    QCOMPARE(events.constLast().calendarTitle, QStringLiteral("Arbeit"));
    QCOMPARE(m_store->tasks().value().size(), 2);

    CalendarInfo readOnly = target;
    readOnly.accessLevel = 200;
    QCOMPARE(wait(m_calendar->importItems({events.constFirst()}, {}, readOnly)).error().code(),
             ErrorCode::InvalidInput);
}

void TestOrganizerService::noteIsCreatedChangedAndDeleted()
{
    QSignalSpy changed(m_notes.get(), &NotesService::notesChanged);
    QVERIFY(wait(m_notes->sync()).hasValue());
    QCOMPARE(m_store->notes().value().size(), 1);
    Note note;
    note.body = QStringLiteral("Geschenkideen\n- Buch\n- Tee = lecker");
    note.categories = {QStringLiteral("Familie")};

    const Result<NoteSaveOutcome> created = wait(m_notes->save(note, SaveMode::CheckConflict));

    QVERIFY(created.hasValue());
    Note stored = m_store->note(created.value().luid).value();
    QVERIFY(sameNoteContent(stored, note));
    QVERIFY(stored.lastModified.isValid());
    QCOMPARE(m_store->noteCategories().value().size(), 2);

    stored.body += QStringLiteral("\n- Socken");
    QVERIFY(wait(m_notes->save(stored, SaveMode::CheckConflict)).hasValue());
    QCOMPARE(m_store->note(created.value().luid).value().body, stored.body);

    QVERIFY(wait(m_notes->remove({created.value().luid})).hasValue());
    QCOMPARE(m_store->note(created.value().luid).error().code(), ErrorCode::NotFound);
    QCOMPARE(m_phone->area(QStringLiteral("notes")).objects.size(), 1);
    QVERIFY(changed.count() >= 4);
    QCOMPARE(wait(m_notes->save(Note(), SaveMode::CheckConflict)).error().code(),
             ErrorCode::InvalidInput);
}

void TestOrganizerService::noteChangedOnThePhoneIsReportedAsAConflict()
{
    QVERIFY(wait(m_notes->sync()).hasValue());
    Note onPhone = m_store->note(1).value();
    onPhone.body = QStringLiteral("Auf dem Handy geändert");
    m_phone->editObject(QStringLiteral("notes"), 1, encodeNoteAsPhoneSendsIt(onPhone));
    Note mine = m_store->note(1).value();
    mine.body = QStringLiteral("Am Rechner geändert");

    const Result<NoteSaveOutcome> saved = wait(m_notes->save(mine, SaveMode::CheckConflict));

    QVERIFY(saved.hasValue());
    QVERIFY(saved.value().conflict);
    QCOMPARE(saved.value().phoneVersion.body, QStringLiteral("Auf dem Handy geändert"));
    QVERIFY(wait(m_notes->save(mine, SaveMode::Overwrite)).hasValue());
    QCOMPARE(m_store->note(1).value().body, QStringLiteral("Am Rechner geändert"));
}

void TestOrganizerService::noteCategoriesAreChanged()
{
    QVERIFY(wait(m_notes->sync()).hasValue());

    QVERIFY(wait(m_notes->changeCategories(
                     {{CategoryChange::Kind::Edit, {1, QStringLiteral("Einfälle"), 0x0000FF}}}))
                .hasValue());

    QCOMPARE(m_store->noteCategories().value(),
             QList<OrganizerCategory>({{1, QStringLiteral("Einfälle"), 0x0000FF}}));
}

void TestOrganizerService::missingCalendarPermissionIsReported()
{
    m_phone->config().calendarDenied = true;

    const Result<OrganizerChanges> pulled = wait(m_calendar->sync());

    QVERIFY(!pulled.hasValue());
    QCOMPARE(pulled.error().code(), ErrorCode::PermissionDenied);
    QVERIFY(m_store->allEvents().value().isEmpty());
    QVERIFY(wait(m_notes->sync()).hasValue());
}

void TestOrganizerService::callsWithoutStoreOrConnectionFail()
{
    CalendarService orphan(m_connection.get());
    QCOMPARE(wait(orphan.sync()).error().code(), ErrorCode::InvalidInput);
    QCOMPARE(wait(orphan.remove({1})).error().code(), ErrorCode::InvalidInput);

    m_connection->disconnectFromPhone();
    QCOMPARE(wait(m_calendar->sync()).error().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(wait(m_notes->sync()).error().code(), ErrorCode::ConnectionClosed);
    CalendarTask task;
    task.summary = QStringLiteral("x");
    QCOMPARE(wait(m_calendar->saveTask(task, SaveMode::CheckConflict)).error().code(),
             ErrorCode::ConnectionClosed);
}

QTEST_GUILESS_MAIN(TestOrganizerService)

#include "tst_organizerservice.moc"
