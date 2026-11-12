#include "core/codec/Recurrence.h"

#include <QTest>
#include <QTimeZone>

Q_DECLARE_METATYPE(qpm::core::ErrorCode)

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

QDateTime utc(int year, int month, int day, int hour = 0, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), QTimeZone::UTC);
}

QList<QDateTime> dates(const QList<QDate> &days)
{
    QList<QDateTime> out;
    for (const QDate &day : days)
        out.append(QDateTime(day, QTime(0, 0), QTimeZone::UTC));
    return out;
}

} // namespace

class TestRecurrence : public QObject
{
    Q_OBJECT

public:
    explicit TestRecurrence(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void rulesSurviveWritingAndReading_data();
    void rulesSurviveWritingAndReading();
    void allDaySeriesAreExpanded_data();
    void allDaySeriesAreExpanded();
    void startCountsFirstEvenOutsideTheRule();
    void exceptionsAreLeftOutButCounted();
    void weeklySeriesKeepsItsLocalTimeAcrossSummerTime();
    void timedExceptionMatchesItsOccurrence();
    void untilWithTimeEndsTheSeries();
    void farRangeIsReachedWithoutCountingEverything();
    void unknownOrBrokenRulesAreReported_data();
    void unknownOrBrokenRulesAreReported();
};

void TestRecurrence::rulesSurviveWritingAndReading_data()
{
    QTest::addColumn<QString>("rule");

    QTest::newRow("phone weekly") << QStringLiteral("FREQ=WEEKLY;INTERVAL=2;WKST=MO;BYDAY=SA");
    QTest::newRow("yearly until") << QStringLiteral("FREQ=YEARLY;UNTIL=20220829;WKST=TU");
    QTest::newRow("last weekday")
        << QStringLiteral("FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1");
    QTest::newRow("count and month days") << QStringLiteral("FREQ=MONTHLY;COUNT=5;BYMONTHDAY=1,-1");
    QTest::newRow("until time") << QStringLiteral("RRULE:FREQ=DAILY;UNTIL=20261231T235959Z");
}

void TestRecurrence::rulesSurviveWritingAndReading()
{
    QFETCH(QString, rule);

    const Result<RecurrenceRule> parsed = parseRecurrenceRule(rule);
    QVERIFY(parsed.hasValue());
    const Result<RecurrenceRule> again = parseRecurrenceRule(formatRecurrenceRule(parsed.value()));

    QVERIFY(again.hasValue());
    QCOMPARE(again.value(), parsed.value());
}

void TestRecurrence::allDaySeriesAreExpanded_data()
{
    QTest::addColumn<QDate>("start");
    QTest::addColumn<QString>("rule");
    QTest::addColumn<QDate>("from");
    QTest::addColumn<QDate>("to");
    QTest::addColumn<QList<QDate>>("expected");

    QTest::newRow("daily, three times")
        << QDate(2026, 11, 1) << QStringLiteral("FREQ=DAILY;COUNT=3") << QDate(2026, 10, 1)
        << QDate(2026, 12, 1)
        << QList<QDate>{QDate(2026, 11, 1), QDate(2026, 11, 2), QDate(2026, 11, 3)};
    QTest::newRow("every second Saturday")
        << QDate(2026, 9, 12) << QStringLiteral("FREQ=WEEKLY;INTERVAL=2;WKST=MO;BYDAY=SA")
        << QDate(2026, 9, 1) << QDate(2026, 10, 25)
        << QList<QDate>{QDate(2026, 9, 12), QDate(2026, 9, 26), QDate(2026, 10, 10),
                        QDate(2026, 10, 24)};
    QTest::newRow("Tuesday and Thursday")
        << QDate(2026, 11, 3) << QStringLiteral("FREQ=WEEKLY;BYDAY=TU,TH;COUNT=4")
        << QDate(2026, 11, 1) << QDate(2026, 12, 31)
        << QList<QDate>{QDate(2026, 11, 3), QDate(2026, 11, 5), QDate(2026, 11, 10),
                        QDate(2026, 11, 12)};
    QTest::newRow("last Friday of the month")
        << QDate(2026, 1, 30) << QStringLiteral("FREQ=MONTHLY;BYDAY=-1FR") << QDate(2026, 1, 1)
        << QDate(2026, 5, 1)
        << QList<QDate>{QDate(2026, 1, 30), QDate(2026, 2, 27), QDate(2026, 3, 27),
                        QDate(2026, 4, 24)};
    QTest::newRow("31st skips short months")
        << QDate(2026, 1, 31) << QStringLiteral("FREQ=MONTHLY") << QDate(2026, 1, 1)
        << QDate(2026, 6, 1)
        << QList<QDate>{QDate(2026, 1, 31), QDate(2026, 3, 31), QDate(2026, 5, 31)};
    QTest::newRow("29 February only in leap years")
        << QDate(2024, 2, 29) << QStringLiteral("FREQ=YEARLY") << QDate(2024, 1, 1)
        << QDate(2033, 1, 1) << QList<QDate>{QDate(2024, 2, 29), QDate(2028, 2, 29), QDate(2032, 2, 29)};
    QTest::newRow("last weekday of the month")
        << QDate(2026, 10, 30) << QStringLiteral("FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1")
        << QDate(2026, 10, 1) << QDate(2027, 2, 1)
        << QList<QDate>{QDate(2026, 10, 30), QDate(2026, 11, 30), QDate(2026, 12, 31),
                        QDate(2027, 1, 29)};
    QTest::newRow("last Sunday of March")
        << QDate(2026, 3, 29) << QStringLiteral("FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU")
        << QDate(2026, 1, 1) << QDate(2029, 1, 1)
        << QList<QDate>{QDate(2026, 3, 29), QDate(2027, 3, 28), QDate(2028, 3, 26)};
    QTest::newRow("second Monday every other month")
        << QDate(2026, 1, 12) << QStringLiteral("FREQ=MONTHLY;INTERVAL=2;BYDAY=2MO")
        << QDate(2026, 1, 1) << QDate(2026, 8, 1)
        << QList<QDate>{QDate(2026, 1, 12), QDate(2026, 3, 9), QDate(2026, 5, 11),
                        QDate(2026, 7, 13)};
    QTest::newRow("until a date, that day included")
        << QDate(2022, 8, 29) << QStringLiteral("FREQ=YEARLY;UNTIL=20240829;WKST=TU")
        << QDate(2020, 1, 1) << QDate(2030, 1, 1)
        << QList<QDate>{QDate(2022, 8, 29), QDate(2023, 8, 29), QDate(2024, 8, 29)};
    QTest::newRow("only the range")
        << QDate(2026, 1, 1) << QStringLiteral("FREQ=DAILY") << QDate(2026, 6, 10)
        << QDate(2026, 6, 13)
        << QList<QDate>{QDate(2026, 6, 10), QDate(2026, 6, 11), QDate(2026, 6, 12)};
}

void TestRecurrence::allDaySeriesAreExpanded()
{
    QFETCH(QDate, start);
    QFETCH(QString, rule);
    QFETCH(QDate, from);
    QFETCH(QDate, to);
    QFETCH(QList<QDate>, expected);
    const Result<RecurrenceRule> parsed = parseRecurrenceRule(rule);
    QVERIFY(parsed.hasValue());

    const QList<QDateTime> starts =
        expandRecurrence(QDateTime(start, QTime(0, 0), QTimeZone::UTC), true, parsed.value(), {},
                         QDateTime(from, QTime(0, 0), QTimeZone::UTC),
                         QDateTime(to, QTime(0, 0), QTimeZone::UTC), QTimeZone(QTimeZone::UTC));

    QCOMPARE(starts, dates(expected));
}

void TestRecurrence::startCountsFirstEvenOutsideTheRule()
{
    // A Wednesday start with a rule for Mondays.
    const RecurrenceRule rule = parseRecurrenceRule(QStringLiteral("FREQ=WEEKLY;BYDAY=MO;COUNT=3")).value();

    const QList<QDateTime> starts = expandRecurrence(utc(2026, 11, 4), true, rule, {},
                                                     utc(2026, 11, 1), utc(2026, 12, 31),
                                                     QTimeZone(QTimeZone::UTC));

    QCOMPARE(starts, dates({QDate(2026, 11, 4), QDate(2026, 11, 9), QDate(2026, 11, 16)}));
}

void TestRecurrence::exceptionsAreLeftOutButCounted()
{
    const RecurrenceRule rule = parseRecurrenceRule(QStringLiteral("FREQ=DAILY;COUNT=4")).value();

    const QList<QDateTime> starts =
        expandRecurrence(utc(2026, 11, 1), true, rule, {utc(2026, 11, 2), utc(2026, 11, 3)},
                         utc(2026, 10, 1), utc(2026, 12, 1), QTimeZone(QTimeZone::UTC));

    QCOMPARE(starts, dates({QDate(2026, 11, 1), QDate(2026, 11, 4)}));
}

void TestRecurrence::weeklySeriesKeepsItsLocalTimeAcrossSummerTime()
{
    const QTimeZone berlin("Europe/Berlin");
    QVERIFY(berlin.isValid());
    // Tuesdays at 09:00 in Berlin; summer time starts on 29 March 2026.
    const QDateTime start = QDateTime(QDate(2026, 3, 24), QTime(9, 0), berlin).toUTC();
    const RecurrenceRule rule = parseRecurrenceRule(QStringLiteral("FREQ=WEEKLY;COUNT=2")).value();

    const QList<QDateTime> starts =
        expandRecurrence(start, false, rule, {}, utc(2026, 3, 1), utc(2026, 5, 1), berlin);

    QCOMPARE(starts, QList<QDateTime>({utc(2026, 3, 24, 8), utc(2026, 3, 31, 7)}));
}

void TestRecurrence::timedExceptionMatchesItsOccurrence()
{
    const RecurrenceRule rule = parseRecurrenceRule(QStringLiteral("FREQ=DAILY;COUNT=3")).value();

    const QList<QDateTime> starts =
        expandRecurrence(utc(2026, 11, 1, 9), false, rule, {utc(2026, 11, 2, 9)}, utc(2026, 11, 1),
                         utc(2026, 12, 1), QTimeZone(QTimeZone::UTC));

    QCOMPARE(starts, QList<QDateTime>({utc(2026, 11, 1, 9), utc(2026, 11, 3, 9)}));
}

void TestRecurrence::untilWithTimeEndsTheSeries()
{
    const RecurrenceRule rule =
        parseRecurrenceRule(QStringLiteral("FREQ=DAILY;UNTIL=20261103T090000Z")).value();

    const QList<QDateTime> starts =
        expandRecurrence(utc(2026, 11, 1, 9), false, rule, {}, utc(2026, 10, 1), utc(2026, 12, 1),
                         QTimeZone(QTimeZone::UTC));

    QCOMPARE(starts.size(), 3);
    QCOMPARE(starts.constLast(), utc(2026, 11, 3, 9));
}

void TestRecurrence::farRangeIsReachedWithoutCountingEverything()
{
    const RecurrenceRule rule = parseRecurrenceRule(QStringLiteral("FREQ=DAILY")).value();

    const QList<QDateTime> starts =
        expandRecurrence(utc(1990, 1, 1, 7), false, rule, {}, utc(2026, 11, 11), utc(2026, 11, 13),
                         QTimeZone(QTimeZone::UTC));

    QCOMPARE(starts, QList<QDateTime>({utc(2026, 11, 11, 7), utc(2026, 11, 12, 7)}));
}

void TestRecurrence::unknownOrBrokenRulesAreReported_data()
{
    QTest::addColumn<QString>("rule");
    QTest::addColumn<ErrorCode>("code");

    QTest::newRow("hourly") << QStringLiteral("FREQ=HOURLY") << ErrorCode::NotSupported;
    QTest::newRow("by hour") << QStringLiteral("FREQ=DAILY;BYHOUR=9") << ErrorCode::NotSupported;
    QTest::newRow("no frequency") << QStringLiteral("INTERVAL=2") << ErrorCode::InvalidInput;
    QTest::newRow("zero interval") << QStringLiteral("FREQ=DAILY;INTERVAL=0")
                                   << ErrorCode::InvalidInput;
    QTest::newRow("bad weekday") << QStringLiteral("FREQ=WEEKLY;BYDAY=XX") << ErrorCode::InvalidInput;
    QTest::newRow("month 13") << QStringLiteral("FREQ=YEARLY;BYMONTH=13") << ErrorCode::InvalidInput;
    QTest::newRow("empty") << QString() << ErrorCode::InvalidInput;
    QTest::newRow("vCalendar 1.0") << QStringLiteral("W1 MO #0") << ErrorCode::InvalidInput;
}

void TestRecurrence::unknownOrBrokenRulesAreReported()
{
    QFETCH(QString, rule);
    QFETCH(ErrorCode, code);

    const Result<RecurrenceRule> parsed = parseRecurrenceRule(rule);

    QVERIFY(!parsed.hasValue());
    QCOMPARE(parsed.error().code(), code);
}

QTEST_GUILESS_MAIN(TestRecurrence)

#include "tst_recurrence.moc"
