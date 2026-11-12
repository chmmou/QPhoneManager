#include "core/codec/ICalendar.h"

#include <QTest>
#include <QTimeZone>

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

QDateTime utc(int year, int month, int day, int hour = 0, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), QTimeZone::UTC);
}

// A zone under a name no time zone database knows, so that only its own
// definition can give the offsets.
QByteArray ownZoneFile()
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Example//Test//EN\r\n"
           "BEGIN:VTIMEZONE\r\nTZID:Mitteleuropa Beispiel\r\n"
           "BEGIN:DAYLIGHT\r\nTZOFFSETFROM:+0100\r\nTZOFFSETTO:+0200\r\n"
           "DTSTART:19700329T020000\r\nRRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU\r\nEND:DAYLIGHT\r\n"
           "BEGIN:STANDARD\r\nTZOFFSETFROM:+0200\r\nTZOFFSETTO:+0100\r\n"
           "DTSTART:19701025T030000\r\nRRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\nEND:STANDARD\r\n"
           "END:VTIMEZONE\r\n"
           "BEGIN:VEVENT\r\nUID:a@example.org\r\nSUMMARY:Sommer\r\n"
           "DTSTART;TZID=Mitteleuropa Beispiel:20260715T100000\r\n"
           "DTEND;TZID=Mitteleuropa Beispiel:20260715T113000\r\nEND:VEVENT\r\n"
           "BEGIN:VEVENT\r\nUID:b@example.org\r\nSUMMARY:Winter\r\n"
           "DTSTART;TZID=\"Mitteleuropa Beispiel\":20260115T100000\r\nDURATION:PT45M\r\n"
           "END:VEVENT\r\nEND:VCALENDAR\r\n";
}

} // namespace

class TestICalendar : public QObject
{
    Q_OBJECT

public:
    explicit TestICalendar(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void zoneOfTheFileGivesSummerAndWinterOffsets();
    void ianaAndWindowsZoneNamesAreKnown();
    void allDayEventsTasksAndRemindersAreRead();
    void foldedAndEscapedTextIsRestored();
    void floatingTimeIsLocalTime();
    void writtenFileReadsBackAndKeepsItsLinesShort();
    void allDayEventIsWrittenAsDates();
    void fileWithoutCalendarOrTooLargeIsRejected();
    void eventWithoutStartIsCountedAsSkipped();
};

void TestICalendar::zoneOfTheFileGivesSummerAndWinterOffsets()
{
    const Result<ICalendarContent> read = readICalendar(ownZoneFile());

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().events.size(), 2);
    QCOMPARE(read.value().events.at(0).start, utc(2026, 7, 15, 8));
    QCOMPARE(read.value().events.at(0).end, utc(2026, 7, 15, 9, 30));
    QCOMPARE(read.value().events.at(1).start, utc(2026, 1, 15, 9));
    QCOMPARE(read.value().events.at(1).end, utc(2026, 1, 15, 9, 45));
    QVERIFY(read.value().events.at(0).calendarUrl.isEmpty());
}

void TestICalendar::ianaAndWindowsZoneNamesAreKnown()
{
    const QByteArray file =
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:A\r\nDTSTART;TZID=Europe/Berlin:20260715T100000\r\n"
        "END:VEVENT\r\nBEGIN:VEVENT\r\nSUMMARY:B\r\n"
        "DTSTART;TZID=W. Europe Standard Time:20260115T100000\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    const Result<ICalendarContent> read = readICalendar(file);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().events.size(), 2);
    QCOMPARE(read.value().events.at(0).start, utc(2026, 7, 15, 8));
    QCOMPARE(read.value().events.at(1).start, utc(2026, 1, 15, 9));
}

void TestICalendar::allDayEventsTasksAndRemindersAreRead()
{
    const QByteArray file =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nSUMMARY:Feiertag\r\nDTSTART;VALUE=DATE:20261003\r\n"
        "DTEND;VALUE=DATE:20261004\r\nRRULE:FREQ=YEARLY\r\nEXDATE;VALUE=DATE:20271003\r\n"
        "CLASS:PRIVATE\r\nTRANSP:TRANSPARENT\r\n"
        "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nSUMMARY:Termin\r\nDTSTART:20261110T120000Z\r\nDTEND:20261110T130000Z\r\n"
        "STATUS:TENTATIVE\r\n"
        "BEGIN:VALARM\r\nTRIGGER;RELATED=END:PT5M\r\nEND:VALARM\r\n"
        "BEGIN:VALARM\r\nTRIGGER;VALUE=DATE-TIME:20261110T100000Z\r\nEND:VALARM\r\nEND:VEVENT\r\n"
        "BEGIN:VTODO\r\nSUMMARY:Rechnung\r\nDUE;VALUE=DATE:20261120\r\nPRIORITY:1\r\n"
        "STATUS:COMPLETED\r\nCATEGORIES:Büro,Finanzen\\, Steuern\r\n"
        "BEGIN:VALARM\r\nTRIGGER;VALUE=DATE-TIME:20261119T080000Z\r\nEND:VALARM\r\nEND:VTODO\r\n"
        "END:VCALENDAR\r\n";

    const Result<ICalendarContent> read = readICalendar(file);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().events.size(), 2);
    const CalendarEvent &holiday = read.value().events.at(0);
    QVERIFY(holiday.allDay);
    QCOMPARE(holiday.start, utc(2026, 10, 3));
    QCOMPARE(holiday.end, utc(2026, 10, 4));
    QCOMPARE(holiday.rrule, QStringLiteral("FREQ=YEARLY"));
    QCOMPARE(holiday.exdates, QList<QDateTime>({utc(2027, 10, 3)}));
    QCOMPARE(holiday.visibility, EventVisibility::Private);
    QCOMPARE(holiday.availability, EventAvailability::Free);
    QCOMPARE(holiday.alarms, QList<QDateTime>({utc(2026, 10, 2, 23, 45)}));
    const CalendarEvent &meeting = read.value().events.at(1);
    QCOMPARE(meeting.availability, EventAvailability::Tentative);
    QCOMPARE(meeting.alarms, QList<QDateTime>({utc(2026, 11, 10, 13, 5), utc(2026, 11, 10, 10)}));
    QCOMPARE(read.value().tasks.size(), 1);
    const CalendarTask &task = read.value().tasks.constFirst();
    QCOMPARE(task.summary, QStringLiteral("Rechnung"));
    QCOMPARE(task.due, QDate(2026, 11, 20));
    QCOMPARE(task.priority, TaskPriority::High);
    QVERIFY(task.completed);
    QCOMPARE(task.categories,
             QStringList({QStringLiteral("Büro"), QStringLiteral("Finanzen, Steuern")}));
    QCOMPARE(task.reminder, utc(2026, 11, 19, 8));
}

void TestICalendar::foldedAndEscapedTextIsRestored()
{
    const QByteArray file = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20261110T120000Z\r\n"
                            "SUMMARY:Ein sehr langer Titel, der über zwei Zeilen\r\n  gefaltet ist\r\n"
                            "DESCRIPTION:Zeile 1\\nZeile 2\\; mit Semikolon\\, Komma und \\\\\r\n"
                            "END:VEVENT\r\nEND:VCALENDAR\r\n";

    const Result<ICalendarContent> read = readICalendar(file);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().events.constFirst().summary,
             QStringLiteral("Ein sehr langer Titel, der über zwei Zeilen gefaltet ist"));
    QCOMPARE(read.value().events.constFirst().description,
             QStringLiteral("Zeile 1\nZeile 2; mit Semikolon, Komma und \\"));
}

void TestICalendar::floatingTimeIsLocalTime()
{
    const QByteArray file = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:x\r\n"
                            "DTSTART:20261110T120000\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    const Result<ICalendarContent> read = readICalendar(file);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().events.constFirst().start,
             QDateTime(QDate(2026, 11, 10), QTime(12, 0)).toUTC());
}

void TestICalendar::writtenFileReadsBackAndKeepsItsLinesShort()
{
    CalendarEvent event;
    event.luid = 0x10000005;
    event.summary = QStringLiteral("Planung; Teil 2, mit Umlauten äöü und einem Titel, der so lang "
                                   "ist, dass er gefaltet werden muss");
    event.location = QStringLiteral("Köln");
    event.description = QStringLiteral("Zeile 1\nZeile 2");
    event.start = utc(2026, 11, 20, 9);
    event.end = utc(2026, 11, 20, 10);
    event.rrule = QStringLiteral("FREQ=WEEKLY;COUNT=3");
    event.exdates = {utc(2026, 11, 27, 9)};
    event.alarms = {utc(2026, 11, 20, 8, 30)};
    event.visibility = EventVisibility::Confidential;
    event.availability = EventAvailability::Free;
    CalendarTask task;
    task.summary = QStringLiteral("Bericht");
    task.description = QStringLiteral("Bis Freitag");
    task.start = QDate(2026, 11, 16);
    task.due = QDate(2026, 11, 20);
    task.reminder = utc(2026, 11, 19, 7);
    task.priority = TaskPriority::Low;
    task.categories = {QStringLiteral("Arbeit"), QStringLiteral("A, B")};

    const QByteArray file = writeICalendar({event}, {task});
    const Result<ICalendarContent> read = readICalendar(file);

    QVERIFY(file.startsWith("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:"));
    QVERIFY(file.contains("\r\nDTSTART:20261120T090000Z\r\n"));
    const QList<QByteArray> lines = file.split('\n');
    for (const QByteArray &line : lines)
        QVERIFY2(line.chopped(line.endsWith('\r') ? 1 : 0).size() <= 75, line.constData());
    QVERIFY(read.hasValue());
    QCOMPARE(read.value().events.size(), 1);
    const CalendarEvent &back = read.value().events.constFirst();
    QCOMPARE(back.summary, event.summary);
    QCOMPARE(back.location, event.location);
    QCOMPARE(back.description, event.description);
    QCOMPARE(back.start, event.start);
    QCOMPARE(back.end, event.end);
    QCOMPARE(back.rrule, event.rrule);
    QCOMPARE(back.exdates, event.exdates);
    QCOMPARE(back.alarms, event.alarms);
    QCOMPARE(back.visibility, event.visibility);
    QCOMPARE(back.availability, event.availability);
    QCOMPARE(read.value().tasks.size(), 1);
    const CalendarTask &taskBack = read.value().tasks.constFirst();
    QVERIFY(sameTaskContent(taskBack, task));
}

void TestICalendar::allDayEventIsWrittenAsDates()
{
    CalendarEvent event;
    event.summary = QStringLiteral("Urlaub");
    event.allDay = true;
    event.start = utc(2026, 12, 23);
    event.end = utc(2026, 12, 28);

    const QByteArray file = writeICalendar({event}, {});

    QVERIFY(file.contains("\r\nDTSTART;VALUE=DATE:20261223\r\n"));
    QVERIFY(file.contains("\r\nDTEND;VALUE=DATE:20261228\r\n"));
    const Result<ICalendarContent> read = readICalendar(file);
    QVERIFY(read.hasValue());
    QVERIFY(read.value().events.constFirst().allDay);
    QCOMPARE(read.value().events.constFirst().end, utc(2026, 12, 28));
}

void TestICalendar::fileWithoutCalendarOrTooLargeIsRejected()
{
    QCOMPARE(readICalendar("BEGIN:VCARD\r\nFN:x\r\nEND:VCARD\r\n").error().code(),
             ErrorCode::InvalidInput);
    QCOMPARE(readICalendar(QByteArray()).error().code(), ErrorCode::InvalidInput);
    QCOMPARE(readICalendar(QByteArray(maxICalendarBytes + 1, 'x')).error().code(),
             ErrorCode::LimitExceeded);
    QByteArray deep = "BEGIN:VCALENDAR\r\n";
    for (int i = 0; i < 12; ++i)
        deep += "BEGIN:X\r\n";
    QCOMPARE(readICalendar(deep).error().code(), ErrorCode::LimitExceeded);
}

void TestICalendar::eventWithoutStartIsCountedAsSkipped()
{
    const QByteArray file = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:ohne Beginn\r\n"
                            "END:VEVENT\r\nBEGIN:VEVENT\r\nSUMMARY:gut\r\nDTSTART:20261110\r\n"
                            "END:VEVENT\r\nEND:VCALENDAR\r\n";

    const Result<ICalendarContent> read = readICalendar(file);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().events.size(), 1);
    QCOMPARE(read.value().skipped, 1);
}

QTEST_GUILESS_MAIN(TestICalendar)

#include "tst_icalendar.moc"
