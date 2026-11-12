#include "ui/AppController.h"
#include "ui/Settings.h"
#include "ui/organizer/AlarmListModel.h"
#include "ui/organizer/CalendarArea.h"
#include "ui/organizer/CalendarDaysModel.h"
#include "ui/organizer/CalendarListModel.h"
#include "ui/organizer/CalendarModel.h"
#include "ui/organizer/CategoryModel.h"
#include "ui/organizer/EventEditor.h"
#include "ui/organizer/NoteEditor.h"
#include "ui/organizer/NoteListModel.h"
#include "ui/organizer/NotesArea.h"
#include "ui/organizer/OrganizerText.h"
#include "ui/organizer/TaskEditor.h"
#include "ui/organizer/TaskListModel.h"
#include "ui/organizer/TasksArea.h"

#include "core/codec/PhoneVCal.h"
#include "core/codec/VNote.h"
#include "platform/SecretStore.h"

#include "FakePhone.h"

#include <QFile>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QTranslator>
#include <QUrl>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::ui::AppController;
using qpm::ui::CalendarArea;
using qpm::ui::NotesArea;
using qpm::ui::OrganizerText;
using qpm::ui::Settings;
using qpm::ui::TaskListModel;
using qpm::ui::TasksArea;

namespace {

constexpr int timeoutMs = 15000;
constexpr quint32 meetingLuid = 0x10000001;
constexpr quint32 holidayLuid = 0x10000002;
constexpr quint32 seriesLuid = 0x10000003;
constexpr quint32 taskLuid = 4;
constexpr quint32 noteLuid = 1;
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

// Times sit near noon UTC, so their day is the same in every European time zone.
QList<FakeSyncObject> exampleCalendar()
{
    CalendarEvent meeting = makeEvent(meetingLuid, QStringLiteral("Elternabend"),
                                      utc(2026, 11, 12, 11), utc(2026, 11, 12, 12, 30),
                                      QStringLiteral("Familie"));
    meeting.location = QStringLiteral("Schule");
    meeting.alarms = {utc(2026, 11, 12, 10, 30)};
    CalendarEvent holiday = makeEvent(holidayLuid, QStringLiteral("Brückentag"), utc(2026, 10, 2),
                                      utc(2026, 10, 3), QStringLiteral("Feiertage"));
    holiday.allDay = true;
    CalendarEvent series = makeEvent(seriesLuid, QStringLiteral("Sport"), utc(2026, 9, 1, 12),
                                     utc(2026, 9, 1, 13), QStringLiteral("Familie"));
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

Note exampleNote()
{
    Note note;
    note.luid = noteLuid;
    note.body = QStringLiteral("Einkaufsliste:\nMilch, Brot");
    note.lastModified = utc(2026, 11, 10, 8);
    note.categories = {QStringLiteral("Ideen")};
    return note;
}

QByteArray noteMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\"><note_categories>\r\n"
           "<category name=\"Ideen\" color=\"00AA00\" id=\"1\"/>\r\n</note_categories>\r\n</metadata>";
}

QVariant value(const QAbstractItemModel *model, int row, const QByteArray &name)
{
    return model->data(model->index(row, 0), model->roleNames().key(name));
}

QStringList column(const QAbstractItemModel *model, const QByteArray &name)
{
    QStringList result;
    for (int row = 0; row < model->rowCount(); ++row)
        result.append(value(model, row, name).toString());
    return result;
}

std::optional<QByteArray> phoneObject(const FakePhone &phone, const QString &area, quint32 luid)
{
    for (const FakeSyncObject &object : phone.area(area).objects) {
        if (object.luid == luid)
            return object.data;
    }
    return std::nullopt;
}

std::optional<CalendarEvent> phoneEventNamed(const FakePhone &phone, const QString &summary)
{
    for (const FakeSyncObject &object : phone.area(QStringLiteral("cal")).objects) {
        if (isTaskObject(object.data))
            continue;
        const Result<CalendarEvent> event = parsePhoneEvent(object.data);
        if (event.hasValue() && event.value().summary == summary)
            return event.value();
    }
    return std::nullopt;
}

std::optional<CalendarTask> phoneTaskNamed(const FakePhone &phone, const QString &summary)
{
    for (const FakeSyncObject &object : phone.area(QStringLiteral("cal")).objects) {
        if (!isTaskObject(object.data))
            continue;
        const Result<CalendarTask> task = parsePhoneTask(object.data);
        if (task.hasValue() && task.value().summary == summary)
            return task.value();
    }
    return std::nullopt;
}

} // namespace

class TestOrganizer : public QObject
{
    Q_OBJECT

public:
    explicit TestOrganizer(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void monthShowsEventsAndTheHolidaysOfTheChosenCountry();
    void dayAndWeekListTheirAppearances();
    void detailShowsTheChosenAppearance();
    void newEventGoesOnlyIntoAWritableCalendar();
    void formSaysWhatIsMissing();
    void changeOnThePhoneOffersBothVersions();
    void leavingOutOneAppearanceKeepsTheSeries();
    void deletedEventIsGoneOnThePhone();
    void choiceOfCalendarsGoesToThePhone();
    void iCalendarFileIsWrittenAndRead();
    void alarmsAreShownAndChanged();
    void alarmsWithoutAClockAppAreNotSupported();
    void tasksAreFilteredAndTickedOff();
    void newTaskWithANewCategory();
    void taskCategoriesAreAddedOnThePhone();
    void notesAreListedFilteredAndSaved();
    void noteChangedOnThePhoneOffersBothVersions();
    void editingNeedsAConnection();

private:
    [[nodiscard]] CalendarArea *calendar() const { return m_controller->calendar(); }
    [[nodiscard]] TasksArea *tasks() const { return m_controller->tasks(); }
    [[nodiscard]] NotesArea *notes() const { return m_controller->notes(); }
    void syncOrganizer();
    void waitIdle();

    QTranslator m_translator;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<AppController> m_controller;
};

void TestOrganizer::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    // The English catalogue holds the singular and plural forms of the texts.
    QVERIFY(m_translator.load(QStringLiteral(QPM_TRANSLATIONS_DIR "/qphonemanager_en.qm")));
    QCoreApplication::installTranslator(&m_translator);
}

void TestOrganizer::cleanupTestCase()
{
    QCoreApplication::removeTranslator(&m_translator);
}

void TestOrganizer::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setDataDirectory(m_dir->filePath(QStringLiteral("profiles")));
    m_settings->setAdbPath(m_dir->filePath(QStringLiteral("no-adb")));
    m_settings->setHolidayRegion(QString());
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("cal"), exampleCalendar());
    m_phone->setMetadata(QStringLiteral("cal"), calendarMetadata());
    m_phone->setObjects(QStringLiteral("notes"),
                        {{noteLuid, encodeNoteAsPhoneSendsIt(exampleNote())}});
    m_phone->setMetadata(QStringLiteral("notes"), noteMetadata());

    AppController::Environment environment;
    environment.listenPort = 0;
    environment.listenAddress = QHostAddress(QHostAddress::LocalHost);
    environment.searchNetwork = false;
    environment.searchBluetooth = false;
    m_controller = std::make_unique<AppController>(m_settings.get(), nullptr, environment);
    QVERIFY(m_controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port())));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->isConnected(), timeoutMs);
    QVERIFY(calendar()->hasPhone());
    QVERIFY(calendar()->canEdit());
    syncOrganizer();
    calendar()->setViewMode(CalendarArea::ViewMode::Day);
    calendar()->setCurrentDate(QDate(2026, 11, 12));
}

void TestOrganizer::cleanup()
{
    m_controller.reset();
    m_phone.reset();
    m_settings.reset();
    m_dir.reset();
}

void TestOrganizer::syncOrganizer()
{
    const QFuture<Result<store::OrganizerChanges>> events = calendar()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(events.isFinished(), timeoutMs);
    QVERIFY2(events.result().hasValue(), qPrintable(events.result().error().message()));
    const QFuture<Result<store::OrganizerChanges>> written = notes()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(written.isFinished(), timeoutMs);
    QVERIFY2(written.result().hasValue(), qPrintable(written.result().error().message()));
}

void TestOrganizer::waitIdle()
{
    QTRY_VERIFY_WITH_TIMEOUT(!calendar()->isBusy() && !calendar()->editor()->isBusy() &&
                                 !tasks()->isBusy() && !tasks()->editor()->isBusy() &&
                                 !notes()->isBusy() && !notes()->editor()->isBusy(),
                             timeoutMs);
}

void TestOrganizer::monthShowsEventsAndTheHolidaysOfTheChosenCountry()
{
    calendar()->setViewMode(CalendarArea::ViewMode::Month);
    const qpm::ui::CalendarDaysModel *days = calendar()->days();
    QCOMPARE(days->rowCount(), 42);
    QCOMPARE(calendar()->periodTitle(), QStringLiteral("November 2026"));
    const int meetingDay = days->rowOf(QDate(2026, 11, 12));
    QVERIFY(meetingDay >= 0);
    const QString meetingStart =
        OrganizerText::formatTime(utc(2026, 11, 12, 11).toLocalTime().time());
    QCOMPARE(value(days, meetingDay, "entries").toStringList(),
             QStringList{meetingStart + QStringLiteral(" Elternabend")});
    QVERIFY(value(days, meetingDay, "selected").toBool());
    QCOMPARE(value(days, meetingDay, "accessibleText").toString(),
             QLocale().toString(QDate(2026, 11, 12), QLocale::LongFormat) +
                 QStringLiteral(", 1 appointment"));
    const int tuesday = days->rowOf(QDate(2026, 11, 17));
    QVERIFY(value(days, tuesday, "entries").toStringList().constFirst().endsWith(QStringLiteral(" Sport")));

    calendar()->setCurrentDate(QDate(2026, 10, 1));
    const int unityDay = days->rowOf(QDate(2026, 10, 3));
    QVERIFY(value(days, unityDay, "holiday").toString().isEmpty());

    m_settings->setHolidayRegion(QStringLiteral("de"));
    QCOMPARE(value(days, unityDay, "holiday").toString(), QStringLiteral("Day of German Unity"));
    QVERIFY(value(days, days->rowOf(QDate(2026, 10, 2)), "entries")
                .toStringList()
                .contains(QStringLiteral("Brückentag")));

    m_settings->setHolidayRegion(QStringLiteral("at"));
    QVERIFY(value(days, unityDay, "holiday").toString().isEmpty());
    QCOMPARE(value(days, days->rowOf(QDate(2026, 10, 26)), "holiday").toString(),
             QStringLiteral("Austrian National Day"));
}

void TestOrganizer::dayAndWeekListTheirAppearances()
{
    const qpm::ui::CalendarModel *list = calendar()->list();
    QCOMPARE(column(list, "summary"), QStringList{QStringLiteral("Elternabend")});
    QCOMPARE(value(list, 0, "timeText").toString(),
             QStringLiteral("%1 to %2")
                 .arg(QLocale().toString(utc(2026, 11, 12, 11).toLocalTime().time(), QLocale::ShortFormat),
                      QLocale().toString(utc(2026, 11, 12, 12, 30).toLocalTime().time(),
                                         QLocale::ShortFormat)));

    calendar()->previous();
    calendar()->previous();
    QCOMPARE(calendar()->currentDate(), QDate(2026, 11, 10));
    QCOMPARE(column(list, "summary"), QStringList{QStringLiteral("Sport")});
    QVERIFY(value(list, 0, "recurring").toBool());

    calendar()->setViewMode(CalendarArea::ViewMode::Week);
    QCOMPARE(calendar()->days()->rowCount(), 7);
    QVERIFY(calendar()->periodTitle().startsWith(QStringLiteral("Week 46")));
    calendar()->next();
    QCOMPARE(calendar()->currentDate(), QDate(2026, 11, 17));

    calendar()->setViewMode(CalendarArea::ViewMode::List);
    QCOMPARE(column(list, "summary"),
             (QStringList{QStringLiteral("Sport"), QStringLiteral("Sport"),
                          QStringLiteral("Elternabend"), QStringLiteral("Sport"),
                          QStringLiteral("Sport")}));
    calendar()->setSearchText(QStringLiteral("schule"));
    QCOMPARE(column(list, "summary"), QStringList{QStringLiteral("Elternabend")});
}

void TestOrganizer::detailShowsTheChosenAppearance()
{
    calendar()->selectRow(0);
    QCOMPARE(calendar()->currentLuid(), int(meetingLuid));
    QCOMPARE(calendar()->currentLocation(), QStringLiteral("Schule"));
    QCOMPARE(calendar()->currentCalendar(), QStringLiteral("Familie"));
    QCOMPARE(calendar()->currentReminders(), QStringLiteral("30 minutes before"));
    QVERIFY(calendar()->currentWritable());
    QVERIFY(!calendar()->currentRecurring());

    calendar()->setCurrentDate(QDate(2026, 11, 24));
    calendar()->selectRow(0);
    QCOMPARE(calendar()->currentLuid(), int(seriesLuid));
    QVERIFY(calendar()->currentRecurring());
    QVERIFY(!calendar()->currentRepeat().isEmpty());
    QVERIFY(calendar()->currentWhen().startsWith(QLocale().toString(QDate(2026, 11, 24), QLocale::LongFormat)));

    calendar()->setCurrentDate(QDate(2026, 10, 2));
    calendar()->selectRow(0);
    QCOMPARE(calendar()->currentLuid(), int(holidayLuid));
    QVERIFY(!calendar()->currentWritable());

    calendar()->selectEvent(0, 0);
    QCOMPARE(calendar()->currentLuid(), 0);
}

void TestOrganizer::newEventGoesOnlyIntoAWritableCalendar()
{
    qpm::ui::EventEditor *editor = calendar()->editor();
    editor->newEvent(QDate(2026, 11, 20));
    QCOMPARE(editor->calendarNames(), (QStringList{QStringLiteral("Familie"), QStringLiteral("Arbeit")}));
    QVERIFY(editor->isNew());
    editor->setSummary(QStringLiteral("Planung"));
    editor->setStartTime(QStringLiteral("11:00"));
    editor->setEndTime(QStringLiteral("12:00"));
    editor->setCalendarIndex(1);
    editor->addReminder(3);
    QVERIFY2(editor->problem().isEmpty(), qPrintable(editor->problem()));
    QVERIFY(editor->canSave());
    QSignalSpy saved(editor, &qpm::ui::EventEditor::saved);

    QVERIFY(editor->save());

    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, timeoutMs);
    const std::optional<CalendarEvent> onPhone = phoneEventNamed(*m_phone, QStringLiteral("Planung"));
    QVERIFY(onPhone);
    QCOMPARE(onPhone->calendarTitle, QStringLiteral("Arbeit"));
    QCOMPARE(onPhone->start, QDateTime(QDate(2026, 11, 20), QTime(11, 0)).toUTC());
    QCOMPARE(onPhone->alarms, QList<QDateTime>{onPhone->start.addSecs(-15 * 60)});
    QCOMPARE(calendar()->currentLuid(), saved.at(0).at(0).toInt());
    QCOMPARE(calendar()->currentCalendar(), QStringLiteral("Arbeit"));
}

void TestOrganizer::formSaysWhatIsMissing()
{
    qpm::ui::EventEditor *editor = calendar()->editor();
    editor->newEvent(QDate(2026, 11, 20));
    QCOMPARE(editor->problem(), QStringLiteral("Enter a title."));
    QVERIFY(!editor->canSave());
    editor->setSummary(QStringLiteral("Planung"));
    editor->setStartDate(QStringLiteral("31/02/2026"));
    QVERIFY(editor->problem().startsWith(QStringLiteral("Enter the start date like")));
    editor->setStartDate(QStringLiteral("20/11/26"));
    editor->setEndDate(OrganizerText::formatDate(QDate(2026, 11, 19)));
    QCOMPARE(editor->problem(), QStringLiteral("The end lies before the start."));
    QSignalSpy errors(editor, &qpm::ui::EventEditor::errorOccurred);
    QVERIFY(!editor->save());
    QCOMPARE(errors.count(), 1);
}

void TestOrganizer::changeOnThePhoneOffersBothVersions()
{
    qpm::ui::EventEditor *editor = calendar()->editor();
    QVERIFY(editor->load(int(meetingLuid)));
    editor->setLocation(QStringLiteral("Aula"));
    CalendarEvent changed = makeEvent(meetingLuid, QStringLiteral("Elternabend 7b"),
                                      utc(2026, 11, 12, 11), utc(2026, 11, 12, 12, 30),
                                      QStringLiteral("Familie"));
    m_phone->editObject(QStringLiteral("cal"), meetingLuid, encodeEventAsPhoneSendsIt(changed));

    QVERIFY(editor->save());

    QTRY_VERIFY_WITH_TIMEOUT(editor->hasConflict(), timeoutMs);
    QVERIFY(editor->ownVersionText().contains(QStringLiteral("Aula")));
    QVERIFY(editor->phoneVersionText().contains(QStringLiteral("Elternabend 7b")));
    QVERIFY(editor->keepOwnVersion());
    QTRY_VERIFY_WITH_TIMEOUT(!editor->hasConflict() && !editor->isBusy(), timeoutMs);
    const Result<CalendarEvent> onPhone =
        parsePhoneEvent(phoneObject(*m_phone, QStringLiteral("cal"), meetingLuid).value_or(QByteArray()));
    QVERIFY(onPhone.hasValue());
    QCOMPARE(onPhone.value().location, QStringLiteral("Aula"));
    QCOMPARE(onPhone.value().summary, QStringLiteral("Elternabend"));
}

void TestOrganizer::leavingOutOneAppearanceKeepsTheSeries()
{
    calendar()->setCurrentDate(QDate(2026, 11, 17));
    calendar()->selectRow(0);
    QCOMPARE(calendar()->currentLuid(), int(seriesLuid));
    QSignalSpy info(calendar(), &CalendarArea::infoMessage);

    QVERIFY(calendar()->skipCurrentOccurrence());

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    const Result<CalendarEvent> onPhone =
        parsePhoneEvent(phoneObject(*m_phone, QStringLiteral("cal"), seriesLuid).value_or(QByteArray()));
    QVERIFY(onPhone.hasValue());
    // The series keeps its wall-clock time in the calendar's zone across the change to winter time.
    QCOMPARE(onPhone.value().exdates,
             QList<QDateTime>{QDateTime(QDate(2026, 11, 17), QTime(14, 0),
                                        QTimeZone(QByteArrayLiteral("Europe/Berlin")))
                                  .toUTC()});
    QCOMPARE(calendar()->list()->rowCount(), 0);
    calendar()->setCurrentDate(QDate(2026, 11, 24));
    QCOMPARE(column(calendar()->list(), "summary"), QStringList{QStringLiteral("Sport")});
}

void TestOrganizer::deletedEventIsGoneOnThePhone()
{
    QVERIFY(calendar()->removeEvent(int(meetingLuid)));
    waitIdle();
    QVERIFY(!phoneObject(*m_phone, QStringLiteral("cal"), meetingLuid));
    QCOMPARE(calendar()->list()->rowCount(), 0);
    QVERIFY(calendar()->summaryOf(int(meetingLuid)).isEmpty());
}

void TestOrganizer::choiceOfCalendarsGoesToThePhone()
{
    qpm::ui::CalendarListModel *calendars = calendar()->calendars();
    const QStringList titles = column(calendars, "title");
    QCOMPARE(titles.size(), 3);
    const int holidays = int(titles.indexOf(QStringLiteral("Feiertage")));
    const int work = int(titles.indexOf(QStringLiteral("Arbeit")));
    QVERIFY(!value(calendars, holidays, "writable").toBool());
    QVERIFY(value(calendars, work, "writable").toBool());
    QVERIFY(!calendars->isSelected(work));
    calendars->setSelected(work, true);
    calendars->setSelected(holidays, false);
    QSignalSpy info(calendar(), &CalendarArea::infoMessage);

    QVERIFY(calendar()->applyCalendarChoice());

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    waitIdle();
    const QByteArray metadata = m_phone->area(QStringLiteral("cal")).metadata;
    const CalMetadata parsed = parseCalMetadata(metadata).value();
    QStringList selected;
    for (const CalendarInfo &info : parsed.calendars) {
        if (info.selected)
            selected.append(info.title);
    }
    selected.sort();
    QCOMPARE(selected, (QStringList{QStringLiteral("Arbeit"), QStringLiteral("Familie")}));
}

void TestOrganizer::iCalendarFileIsWrittenAndRead()
{
    const QString exported = m_dir->filePath(QStringLiteral("export.ics"));
    QSignalSpy info(calendar(), &CalendarArea::infoMessage);
    QVERIFY(calendar()->exportTo(QUrl::fromLocalFile(exported)));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QCOMPARE(info.at(0).at(0).toString(), QStringLiteral("4 entries exported."));
    QFile file(exported);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QVERIFY(data.startsWith("BEGIN:VCALENDAR\r\n"));
    QVERIFY(data.contains("SUMMARY:Elternabend\r\n"));
    QVERIFY(data.contains("BEGIN:VTODO\r\n"));

    const QString imported = m_dir->filePath(QStringLiteral("import.ics"));
    QFile out(imported);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
              "BEGIN:VEVENT\r\nUID:a@test\r\nSUMMARY:Zahnarzt\r\n"
              "DTSTART:20261203T090000Z\r\nDTEND:20261203T093000Z\r\nEND:VEVENT\r\n"
              "BEGIN:VEVENT\r\nUID:b@test\r\nDTSTART:20261204T090000Z\r\nEND:VEVENT\r\n"
              "BEGIN:VTODO\r\nUID:c@test\r\nSUMMARY:Rezept holen\r\nDUE;VALUE=DATE:20261202\r\n"
              "END:VTODO\r\nEND:VCALENDAR\r\n");
    out.close();
    info.clear();

    QVERIFY(calendar()->importFrom(QUrl::fromLocalFile(imported)));

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QCOMPARE(info.at(0).at(0).toString(),
             QStringLiteral("2 entries imported. 1 entry was left out, because it lacks a title "
                            "or a start."));
    const std::optional<CalendarEvent> dentist = phoneEventNamed(*m_phone, QStringLiteral("Zahnarzt"));
    QVERIFY(dentist);
    QCOMPARE(dentist->calendarTitle, QStringLiteral("Familie"));
    QVERIFY(phoneTaskNamed(*m_phone, QStringLiteral("Rezept holen")));

    QSignalSpy errors(calendar(), &CalendarArea::errorOccurred);
    QVERIFY(calendar()->importFrom(QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("missing.ics")))));
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, timeoutMs);
    QCOMPARE(errors.at(0).at(0).toString(),
             QStringLiteral("The file cannot be opened. Check that it exists."));
    QVERIFY(!calendar()->importFrom(QUrl(QStringLiteral("https://example.org/a.ics"))));
}

void TestOrganizer::alarmsAreShownAndChanged()
{
    m_phone->config().alarms =
        QStringList{QStringLiteral("\"07:30\",1,0,\"Wecker\",\"1,2,3,4,5\",1")};

    QVERIFY(calendar()->loadAlarms());

    QTRY_COMPARE_WITH_TIMEOUT(calendar()->alarmState(), CalendarArea::AlarmState::Ready, timeoutMs);
    const qpm::ui::AlarmListModel *alarms = calendar()->alarms();
    QCOMPARE(alarms->rowCount(), 1);
    QCOMPARE(value(alarms, 0, "name").toString(), QStringLiteral("Wecker"));
    QCOMPARE(calendar()->alarmTimeText(0), QLocale().toString(QTime(7, 30), QLocale::ShortFormat));
    QCOMPARE(calendar()->alarmDays(0), (QList<int>{1, 2, 3, 4, 5}));
    QVERIFY(calendar()->alarmEnabled(0));

    QVERIFY(calendar()->saveAlarm(0, QStringLiteral("06:45"), QStringLiteral("Wecker"), {2, 1}, false));

    QTRY_VERIFY_WITH_TIMEOUT(!calendar()->isBusy() &&
                                 calendar()->alarmState() == CalendarArea::AlarmState::Ready &&
                                 !calendar()->alarmEnabled(0),
                             timeoutMs);
    QCOMPARE(*m_phone->config().alarms,
             QStringList{QStringLiteral("\"06:45\",1,0,\"Wecker\",\"1,2\",0")});
    QCOMPARE(calendar()->alarmDays(0), (QList<int>{1, 2}));

    QSignalSpy errors(calendar(), &CalendarArea::errorOccurred);
    QVERIFY(!calendar()->saveAlarm(-1, QStringLiteral("25:00"), QString(), {}, true));
    QCOMPARE(errors.count(), 1);
}

void TestOrganizer::alarmsWithoutAClockAppAreNotSupported()
{
    QSignalSpy errors(calendar(), &CalendarArea::errorOccurred);

    QVERIFY(calendar()->loadAlarms());

    QTRY_COMPARE_WITH_TIMEOUT(calendar()->alarmState(), CalendarArea::AlarmState::NotSupported,
                              timeoutMs);
    QCOMPARE(calendar()->alarms()->rowCount(), 0);
    QCOMPARE(errors.count(), 0);
}

void TestOrganizer::tasksAreFilteredAndTickedOff()
{
    TaskListModel *list = tasks()->list();
    QCOMPARE(column(list, "summary"), QStringList{QStringLiteral("Fenster putzen")});
    QCOMPARE(value(list, 0, "categoriesText").toString(), QStringLiteral("Haushalt"));
    QVERIFY(!value(list, 0, "dueText").toString().isEmpty());
    list->setFilter(TaskListModel::Filter::Done);
    QCOMPARE(list->rowCount(), 0);

    QVERIFY(tasks()->setCompleted(int(taskLuid), true));

    waitIdle();
    QTRY_COMPARE_WITH_TIMEOUT(list->rowCount(), 1, timeoutMs);
    const Result<CalendarTask> onPhone = parsePhoneTask(
        phoneObject(*m_phone, QStringLiteral("cal"), taskLuid).value_or(QByteArray()));
    QVERIFY(onPhone.hasValue());
    QVERIFY(onPhone.value().completed);
    list->setFilterIndex(0);
    QCOMPARE(list->rowCount(), 0);
    list->setFilterIndex(2);
    QCOMPARE(list->rowCount(), 1);
    tasks()->setSearchText(QStringLiteral("nothing"));
    QCOMPARE(list->rowCount(), 0);
}

void TestOrganizer::newTaskWithANewCategory()
{
    tasks()->startNew();
    qpm::ui::TaskEditor *editor = tasks()->editor();
    QVERIFY(editor->isNew());
    editor->setSummary(QStringLiteral("Steuer"));
    editor->setDueDate(OrganizerText::formatDate(QDate(2026, 12, 31)));
    QVERIFY(editor->addCategory(QStringLiteral("Büro")));
    QVERIFY(editor->categoryNames().contains(QStringLiteral("Haushalt")));
    QSignalSpy saved(editor, &qpm::ui::TaskEditor::saved);

    QVERIFY(editor->save());

    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, timeoutMs);
    const std::optional<CalendarTask> onPhone = phoneTaskNamed(*m_phone, QStringLiteral("Steuer"));
    QVERIFY(onPhone);
    QCOMPARE(onPhone->due, QDate(2026, 12, 31));
    QCOMPARE(onPhone->categories, QStringList{QStringLiteral("Büro")});
    QTRY_VERIFY_WITH_TIMEOUT(tasks()->categories()->names().contains(QStringLiteral("Büro")), timeoutMs);
}

void TestOrganizer::taskCategoriesAreAddedOnThePhone()
{
    QSignalSpy errors(tasks(), &TasksArea::errorOccurred);
    QVERIFY(!tasks()->addCategory(QStringLiteral("haushalt"), 0));
    QCOMPARE(errors.count(), 1);

    QVERIFY(tasks()->addCategory(QStringLiteral("Garten"), 2));

    QTRY_VERIFY_WITH_TIMEOUT(tasks()->categories()->names().contains(QStringLiteral("Garten")), timeoutMs);
    QVERIFY(m_phone->area(QStringLiteral("cal")).metadata.contains("name=\"Garten\""));
    const int row = int(tasks()->categories()->names().indexOf(QStringLiteral("Garten")));
    QVERIFY(tasks()->removeCategory(row));
    QTRY_VERIFY_WITH_TIMEOUT(!tasks()->categories()->names().contains(QStringLiteral("Garten")), timeoutMs);
}

void TestOrganizer::notesAreListedFilteredAndSaved()
{
    qpm::ui::NoteListModel *list = notes()->list();
    QCOMPARE(column(list, "title"), QStringList{QStringLiteral("Einkaufsliste:")});
    QCOMPARE(value(list, 0, "preview").toString(), QStringLiteral("Milch, Brot"));
    QCOMPARE(notes()->categories()->names(), QStringList{QStringLiteral("Ideen")});
    list->setCategoryFilter(QStringLiteral("Ideen"));
    QCOMPARE(list->rowCount(), 1);

    notes()->startNew();
    qpm::ui::NoteEditor *editor = notes()->editor();
    QVERIFY(!editor->canSave());
    editor->setBody(QStringLiteral("Anruf Schule\nMontag"));
    QSignalSpy saved(editor, &qpm::ui::NoteEditor::saved);
    QVERIFY(editor->save());
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, timeoutMs);
    const quint32 newLuid = quint32(saved.at(0).at(0).toInt());
    const Result<Note> onPhone = parsePhoneNote(
        phoneObject(*m_phone, QStringLiteral("notes"), newLuid).value_or(QByteArray()));
    QVERIFY(onPhone.hasValue());
    QCOMPARE(onPhone.value().body, QStringLiteral("Anruf Schule\nMontag"));
    QCOMPARE(list->rowCount(), 1);
    list->setCategoryFilter(QString());
    QCOMPARE(list->rowCount(), 2);

    QVERIFY(notes()->removeNote(int(newLuid)));
    waitIdle();
    QVERIFY(!phoneObject(*m_phone, QStringLiteral("notes"), newLuid));
    QCOMPARE(list->rowCount(), 1);
}

void TestOrganizer::noteChangedOnThePhoneOffersBothVersions()
{
    qpm::ui::NoteEditor *editor = notes()->editor();
    QVERIFY(editor->load(int(noteLuid)));
    editor->setBody(QStringLiteral("Einkaufsliste:\nMilch, Brot, Käse"));
    Note changed = exampleNote();
    changed.body = QStringLiteral("Einkaufsliste:\nÄpfel");
    m_phone->editObject(QStringLiteral("notes"), noteLuid, encodeNoteAsPhoneSendsIt(changed));

    QVERIFY(editor->save());

    QTRY_VERIFY_WITH_TIMEOUT(editor->hasConflict(), timeoutMs);
    QVERIFY(editor->phoneVersionText().contains(QStringLiteral("Äpfel")));
    QVERIFY(editor->takePhoneVersion());
    QTRY_VERIFY_WITH_TIMEOUT(!editor->hasConflict() && !editor->isBusy(), timeoutMs);
    QCOMPARE(editor->body(), QStringLiteral("Einkaufsliste:\nÄpfel"));
}

void TestOrganizer::editingNeedsAConnection()
{
    m_controller->disconnectPhone();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->isConnected(), timeoutMs);
    QVERIFY(calendar()->hasPhone());
    QVERIFY(!calendar()->canEdit());
    QVERIFY(!tasks()->canEdit());
    QVERIFY(!notes()->canEdit());
    QCOMPARE(column(calendar()->list(), "summary"), QStringList{QStringLiteral("Elternabend")});
    QVERIFY(calendar()->editor()->load(int(meetingLuid)));
    QVERIFY(!calendar()->editor()->canSave());
    QSignalSpy errors(calendar(), &CalendarArea::errorOccurred);
    QVERIFY(!calendar()->removeEvent(int(meetingLuid)));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!calendar()->loadAlarms());
}

QTEST_MAIN(TestOrganizer)

#include "tst_organizer.moc"
