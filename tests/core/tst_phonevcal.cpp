#include "core/codec/PhoneVCal.h"

#include <QTest>
#include <QTimeZone>

Q_DECLARE_METATYPE(qpm::core::codec::CalendarEvent)

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

QDateTime utc(int year, int month, int day, int hour = 0, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), QTimeZone::UTC);
}

QByteArray timedEvent()
{
    return "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:Meeting\r\n"
           "LOCATION;CHARSET=UTF-8:Raum 1 Süd\r\nDESCRIPTION:Agenda\r\n"
           "DTSTART:20250505T134500Z\r\nDTEND:20250505T144500Z\r\n"
           "AALARM:20250505T133500Z\r\nAALARM:20250505T131500Z\r\nCLASS:PUBLIC\r\n"
           "TRANSP:OPAQUE\r\nX-COLOR:FFFBD75B\r\n"
           "X-CALENDAR:user@example.invalid@com.google/NULL;Calendar C\r\n"
           "X-IRMC-LUID:0000100003D3\r\nEND:VEVENT\r\nEND:VCALENDAR";
}

QByteArray allDayEvent()
{
    return "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY;CHARSET=UTF-8:Urlaub\r\n"
           "DESCRIPTION;ENCODING=QUOTED-PRINTABLE;CHARSET=UTF-8:Zeile 1=0D=0AZeile 2\r\n"
           "DTSTART:20251130\r\nDTEND:20251201\r\nCLASS:PUBLIC\r\nTRANSP:TRANSPARENT\r\n"
           "X-CALENDAR:user@example.invalid@com.google/NULL;Calendar A\r\n"
           "X-IRMC-LUID:000010000374\r\nEND:VEVENT\r\nEND:VCALENDAR";
}

QByteArray repeatingEvent()
{
    return "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:Jahrestag\r\nDTSTART:20260912\r\n"
           "DURATION:P1D\r\nRRULE:FREQ=WEEKLY;INTERVAL=2;WKST=MO;BYDAY=SA\r\n"
           "EXDATE:20260926;20261010\r\nTRANSP:TRANSPARENT\r\n"
           "X-CALENDAR:user@example.invalid@com.google/NULL;Calendar C\r\n"
           "X-IRMC-LUID:000010000419\r\nEND:VEVENT\r\nEND:VCALENDAR";
}

QByteArray task()
{
    return "BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nSUMMARY:Aufgabe\r\nDESCRIPTION:Details\r\n"
           "RRULE:FREQ=WEEKLY;INTERVAL=1\r\nDTSTART:20250601\r\nDUE:20250608\r\n"
           "DALARM:20250608T070000Z\r\nLAST-MODIFIED:20250520T101500Z\r\n"
           "STATUS:NEEDS ACTION\r\nPRIORITY:2\r\nCATEGORIES:Arbeit;Zu\\;Hause\r\n"
           "X-IRMC-LUID:000000000005\r\nEND:VTODO\r\nEND:VCALENDAR";
}

CalendarEvent sampleEvent()
{
    CalendarEvent event;
    event.luid = 0x10000042;
    event.summary = QStringLiteral("Zahnarzt");
    event.location = QStringLiteral("Praxis; Raum = 2");
    event.description = QStringLiteral("Erste Zeile\nZweite Zeile mit Grüßen");
    event.start = utc(2026, 1, 15, 9);
    event.end = utc(2026, 1, 15, 10);
    event.alarms = {utc(2026, 1, 15, 8, 45)};
    event.visibility = EventVisibility::Private;
    event.availability = EventAvailability::Tentative;
    event.color = 0xFFFF0000U;
    event.calendarUrl = QStringLiteral("anna@example.org@com.google/NULL");
    event.calendarTitle = QStringLiteral("Privat;Familie");
    return event;
}

} // namespace

class TestPhoneVCal : public QObject
{
    Q_OBJECT

public:
    explicit TestPhoneVCal(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void timedEventIsReadAsThePhoneSendsIt();
    void allDayEventKeepsItsDaysAndItsLineBreaks();
    void repeatingEventTakesItsEndFromTheDuration();
    void taskIsReadWithCategoriesAndReminder();
    void eventIsWrittenAsThePhoneReadsIt();
    void allDayEventIsWrittenWithDatesOnly();
    void missingColourIsClearedOnlyWhenAsked();
    void taskIsWrittenWithCategoryColours();
    void eventsRoundTripThroughThePhoneForm_data();
    void eventsRoundTripThroughThePhoneForm();
    void taskRoundTripsThroughThePhoneForm();
    void objectWithoutStartOrComponentIsRejected_data();
    void objectWithoutStartOrComponentIsRejected();
    void durationsAreReadLikeThePhone_data();
    void durationsAreReadLikeThePhone();
    void sameContentIgnoresOrderAndCalendar();
};

void TestPhoneVCal::timedEventIsReadAsThePhoneSendsIt()
{
    const Result<CalendarEvent> event = parsePhoneEvent(timedEvent());

    QVERIFY(event.hasValue());
    QCOMPARE(event.value().luid, 0x100003D3U);
    QCOMPARE(event.value().summary, QStringLiteral("Meeting"));
    QCOMPARE(event.value().location, QStringLiteral("Raum 1 Süd"));
    QCOMPARE(event.value().start, utc(2025, 5, 5, 13, 45));
    QCOMPARE(event.value().end, utc(2025, 5, 5, 14, 45));
    QVERIFY(!event.value().allDay);
    QCOMPARE(event.value().alarms,
             QList<QDateTime>({utc(2025, 5, 5, 13, 35), utc(2025, 5, 5, 13, 15)}));
    QCOMPARE(event.value().visibility, EventVisibility::Public);
    QCOMPARE(event.value().availability, EventAvailability::Busy);
    QCOMPARE(event.value().color, std::optional<quint32>(0xFFFBD75BU));
    QCOMPARE(event.value().calendarUrl, QStringLiteral("user@example.invalid@com.google/NULL"));
    QCOMPARE(event.value().calendarTitle, QStringLiteral("Calendar C"));
}

void TestPhoneVCal::allDayEventKeepsItsDaysAndItsLineBreaks()
{
    const Result<CalendarEvent> event = parsePhoneEvent(allDayEvent());

    QVERIFY(event.hasValue());
    QVERIFY(event.value().allDay);
    QCOMPARE(event.value().start, utc(2025, 11, 30));
    QCOMPARE(event.value().end, utc(2025, 12, 1));
    QCOMPARE(event.value().description, QStringLiteral("Zeile 1\r\nZeile 2"));
    QCOMPARE(event.value().availability, EventAvailability::Free);
}

void TestPhoneVCal::repeatingEventTakesItsEndFromTheDuration()
{
    const Result<CalendarEvent> event = parsePhoneEvent(repeatingEvent());

    QVERIFY(event.hasValue());
    QVERIFY(event.value().allDay);
    QCOMPARE(event.value().start, utc(2026, 9, 12));
    QCOMPARE(event.value().end, utc(2026, 9, 13));
    QCOMPARE(event.value().rrule, QStringLiteral("FREQ=WEEKLY;INTERVAL=2;WKST=MO;BYDAY=SA"));
    QCOMPARE(event.value().exdates, QList<QDateTime>({utc(2026, 9, 26), utc(2026, 10, 10)}));
}

void TestPhoneVCal::taskIsReadWithCategoriesAndReminder()
{
    const Result<CalendarTask> read = parsePhoneTask(task());

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().luid, 5U);
    QCOMPARE(read.value().summary, QStringLiteral("Aufgabe"));
    QCOMPARE(read.value().start, QDate(2025, 6, 1));
    QCOMPARE(read.value().due, QDate(2025, 6, 8));
    QCOMPARE(read.value().reminder, utc(2025, 6, 8, 7));
    QCOMPARE(read.value().lastModified, QDateTime(QDate(2025, 5, 20), QTime(10, 15), QTimeZone::UTC));
    QVERIFY(!read.value().completed);
    QCOMPARE(read.value().priority, TaskPriority::Normal);
    QCOMPARE(read.value().categories, QStringList({QStringLiteral("Arbeit"), QStringLiteral("Zu;Hause")}));
    QVERIFY(isTaskObject(task()));
    QVERIFY(!isTaskObject(timedEvent()));
}

void TestPhoneVCal::eventIsWrittenAsThePhoneReadsIt()
{
    const QByteArray body = encodePhoneEvent(sampleEvent(), false);

    QVERIFY(body.startsWith("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n"));
    QVERIFY(body.endsWith("END:VEVENT\r\nEND:VCALENDAR\r\n"));
    // Line breaks and "=" go quoted-printable, other text stays raw UTF-8.
    QVERIFY(body.contains("DESCRIPTION;ENCODING=QUOTED-PRINTABLE;CHARSET=UTF-8:Erste Zeile=0AZweite "
                          "Zeile mit Grüßen\r\n"));
    QVERIFY(body.contains("LOCATION;ENCODING=QUOTED-PRINTABLE:Praxis; Raum =3D 2\r\n"));
    QVERIFY(body.contains("\r\nDTSTART:20260115T090000Z\r\nDTEND:20260115T100000Z\r\n"));
    QVERIFY(body.contains("AALARM:20260115T084500Z\r\n"));
    QVERIFY(body.contains("CLASS:PRIVATE\r\nTRANSP:TENTATIVE\r\n"));
    QVERIFY(body.contains("X-COLOR:FF0000\r\n"));
    QVERIFY(body.contains("X-CALENDAR:anna@example.org@com.google/NULL;Privat\\;Familie\r\n"));
    // The phone takes the LUID from the name of the object, never from the text.
    QVERIFY(!body.contains("X-IRMC-LUID"));
    const QList<QByteArray> lines = body.split('\n');
    for (const QByteArray &line : lines)
        QVERIFY2(!line.startsWith(' '), "the phone does not unfold lines");
}

void TestPhoneVCal::allDayEventIsWrittenWithDatesOnly()
{
    CalendarEvent event;
    event.summary = QStringLiteral("Frei");
    event.allDay = true;
    event.start = utc(2026, 2, 1);
    event.end = utc(2026, 2, 1);
    event.exdates = {utc(2026, 2, 8)};
    event.rrule = QStringLiteral("FREQ=WEEKLY");

    const QByteArray body = encodePhoneEvent(event, false);

    // Only a bare date after "DTSTART:" makes the phone treat the event as all-day.
    QVERIFY(body.contains("\r\nDTSTART:20260201\r\n"));
    QVERIFY(body.contains("\r\nDTEND:20260202\r\n"));
    QVERIFY(body.contains("\r\nEXDATE:20260208\r\n"));
}

void TestPhoneVCal::missingColourIsClearedOnlyWhenAsked()
{
    CalendarEvent event = sampleEvent();
    event.color.reset();

    QVERIFY(encodePhoneEvent(event, true).contains("X-COLOR:DEFAULT\r\n"));
    QVERIFY(!encodePhoneEvent(event, false).contains("X-COLOR"));
}

void TestPhoneVCal::taskIsWrittenWithCategoryColours()
{
    CalendarTask item;
    item.summary = QStringLiteral("Milch kaufen");
    item.due = QDate(2026, 2, 1);
    item.reminder = utc(2026, 2, 1, 7);
    item.priority = TaskPriority::High;
    item.categories = {QStringLiteral("Einkauf"), QStringLiteral("Haus;Hof")};

    const QByteArray colored = encodePhoneTask(item, {0xFF0000U, 0x00FF00U});
    const QByteArray plain = encodePhoneTask(item, {0xFF0000U, std::nullopt});

    QVERIFY(colored.startsWith("BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\n"));
    QVERIFY(colored.contains("\r\nDUE:20260201\r\n"));
    QVERIFY(colored.contains("\r\nDALARM:20260201T070000Z\r\n"));
    QVERIFY(colored.contains("\r\nSTATUS:NEEDS ACTION\r\nPRIORITY:1\r\n"));
    QVERIFY(colored.contains("\r\nCATEGORIES;COLOR=FF0000,00FF00:Einkauf;Haus\\;Hof\r\n"));
    QVERIFY(plain.contains("\r\nCATEGORIES:Einkauf;Haus\\;Hof\r\n"));
}

void TestPhoneVCal::eventsRoundTripThroughThePhoneForm_data()
{
    QTest::addColumn<CalendarEvent>("event");

    QTest::newRow("timed") << sampleEvent();
    CalendarEvent allDay = sampleEvent();
    allDay.allDay = true;
    allDay.start = utc(2026, 3, 2);
    allDay.end = utc(2026, 3, 5);
    allDay.color.reset();
    allDay.visibility = EventVisibility::Default;
    QTest::newRow("all-day over three days") << allDay;
    CalendarEvent series = sampleEvent();
    series.rrule = QStringLiteral("FREQ=MONTHLY;BYDAY=-1FR");
    series.exdates = {utc(2026, 1, 30, 9)};
    QTest::newRow("repeating with exception") << series;
}

void TestPhoneVCal::eventsRoundTripThroughThePhoneForm()
{
    QFETCH(CalendarEvent, event);

    const Result<CalendarEvent> back = parsePhoneEvent(encodeEventAsPhoneSendsIt(event));

    QVERIFY(back.hasValue());
    QCOMPARE(back.value().luid, event.luid);
    QVERIFY(sameEventContent(back.value(), event));
    QCOMPARE(back.value().calendarTitle, event.calendarTitle);
    const Result<CalendarEvent> written = parsePhoneEvent(encodePhoneEvent(event, true));
    QVERIFY(written.hasValue());
    QVERIFY(sameEventContent(written.value(), event));
}

void TestPhoneVCal::taskRoundTripsThroughThePhoneForm()
{
    CalendarTask item;
    item.luid = 7;
    item.summary = QStringLiteral("Steuer = Pflicht");
    item.description = QStringLiteral("Belege\nsammeln");
    item.start = QDate(2026, 3, 1);
    item.due = QDate(2026, 5, 31);
    item.reminder = utc(2026, 5, 30, 8);
    item.rrule = QStringLiteral("FREQ=YEARLY");
    item.priority = TaskPriority::Low;
    item.completed = true;
    item.categories = {QStringLiteral("Büro")};

    const Result<CalendarTask> back = parsePhoneTask(encodeTaskAsPhoneSendsIt(item));
    const Result<CalendarTask> sent = parsePhoneTask(encodePhoneTask(item, {}));

    QVERIFY(back.hasValue());
    QCOMPARE(back.value().luid, 7U);
    QVERIFY(sameTaskContent(back.value(), item));
    QVERIFY(sent.hasValue());
    QVERIFY(sameTaskContent(sent.value(), item));
}

void TestPhoneVCal::objectWithoutStartOrComponentIsRejected_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("no start") << QByteArray("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:x\r\n"
                                            "END:VEVENT\r\nEND:VCALENDAR");
    QTest::newRow("broken start") << QByteArray("BEGIN:VEVENT\r\nDTSTART:2026-01-01\r\nEND:VEVENT");
    QTest::newRow("no event") << QByteArray("BEGIN:VCALENDAR\r\nEND:VCALENDAR");
}

void TestPhoneVCal::objectWithoutStartOrComponentIsRejected()
{
    QFETCH(QByteArray, data);

    const Result<CalendarEvent> event = parsePhoneEvent(data);

    QVERIFY(!event.hasValue());
    QCOMPARE(event.error().code(), ErrorCode::ProtocolError);
    QCOMPARE(parsePhoneTask(data).error().code(), ErrorCode::ProtocolError);
}

void TestPhoneVCal::durationsAreReadLikeThePhone_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<qint64>("seconds");
    QTest::addColumn<bool>("valid");

    QTest::newRow("hour") << QStringLiteral("PT1H") << qint64(3600) << true;
    QTest::newRow("day") << QStringLiteral("P1D") << qint64(86400) << true;
    QTest::newRow("seconds as the phone writes them") << QStringLiteral("P3600S") << qint64(3600)
                                                       << true;
    QTest::newRow("week") << QStringLiteral("P1W") << qint64(604800) << true;
    QTest::newRow("minutes without T") << QStringLiteral("P15M") << qint64(900) << true;
    QTest::newRow("before") << QStringLiteral("-PT15M") << qint64(-900) << true;
    QTest::newRow("mixed") << QStringLiteral("P1DT2H30M") << qint64(95400) << true;
    QTest::newRow("no unit") << QStringLiteral("P15") << qint64(0) << false;
    QTest::newRow("no P") << QStringLiteral("1H") << qint64(0) << false;
    QTest::newRow("letters") << QStringLiteral("PXD") << qint64(0) << false;
}

void TestPhoneVCal::durationsAreReadLikeThePhone()
{
    QFETCH(QString, text);
    QFETCH(qint64, seconds);
    QFETCH(bool, valid);

    const std::optional<qint64> parsed = parseDurationSeconds(text);

    QCOMPARE(parsed.has_value(), valid);
    if (valid)
        QCOMPARE(*parsed, seconds);
}

void TestPhoneVCal::sameContentIgnoresOrderAndCalendar()
{
    CalendarEvent a = sampleEvent();
    a.alarms = {utc(2026, 1, 15, 8), utc(2026, 1, 15, 8, 45)};
    CalendarEvent b = a;
    b.alarms = {utc(2026, 1, 15, 8, 45), utc(2026, 1, 15, 8)};
    b.calendarTitle = QStringLiteral("Anderer");
    b.color = 0x00FF0000U;

    QVERIFY(sameEventContent(a, b));
    b.summary = QStringLiteral("Zahnarzt verschoben");
    QVERIFY(!sameEventContent(a, b));
}

QTEST_GUILESS_MAIN(TestPhoneVCal)

#include "tst_phonevcal.moc"
