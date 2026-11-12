#include "core/Holidays.h"

#include <QTest>

#include <algorithm>

using namespace qpm::core;

Q_DECLARE_METATYPE(qpm::core::HolidayRegion)

class TestHolidays : public QObject
{
    Q_OBJECT

public:
    explicit TestHolidays(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void easterSundayFollowsTheCorrectedGaussFormula_data();
    void easterSundayFollowsTheCorrectedGaussFormula();
    void eachCountryHasItsNationwideHolidays_data();
    void eachCountryHasItsNationwideHolidays();
    void spanOverNewYearTakesBothYears();
    void noCountryAndYearsBeforeTheGregorianEasterHaveNone();
};

void TestHolidays::easterSundayFollowsTheCorrectedGaussFormula_data()
{
    QTest::addColumn<QDate>("easter");

    // 1954 and 1981 are the years the uncorrected formula gets wrong.
    QTest::newRow("1954") << QDate(1954, 4, 18);
    QTest::newRow("1981") << QDate(1981, 4, 19);
    QTest::newRow("2000") << QDate(2000, 4, 23);
    QTest::newRow("2008, early") << QDate(2008, 3, 23);
    QTest::newRow("2019") << QDate(2019, 4, 21);
    QTest::newRow("2024") << QDate(2024, 3, 31);
    QTest::newRow("2025") << QDate(2025, 4, 20);
    QTest::newRow("2026") << QDate(2026, 4, 5);
    QTest::newRow("2038, late") << QDate(2038, 4, 25);
    QTest::newRow("2049") << QDate(2049, 4, 18);
    QTest::newRow("2285, earliest possible") << QDate(2285, 3, 22);
}

void TestHolidays::easterSundayFollowsTheCorrectedGaussFormula()
{
    QFETCH(QDate, easter);

    QCOMPARE(easterSunday(easter.year()), easter);
    QCOMPARE(easter.dayOfWeek(), int(Qt::Sunday));
}

void TestHolidays::eachCountryHasItsNationwideHolidays_data()
{
    QTest::addColumn<HolidayRegion>("region");
    QTest::addColumn<int>("count");
    QTest::addColumn<QList<QDate>>("some");

    QTest::newRow("Germany 2026")
        << HolidayRegion::Germany << 9
        << QList<QDate>{QDate(2026, 4, 3), QDate(2026, 4, 6), QDate(2026, 5, 14),
                        QDate(2026, 5, 25), QDate(2026, 10, 3)};
    QTest::newRow("Austria 2026")
        << HolidayRegion::Austria << 13
        << QList<QDate>{QDate(2026, 1, 6), QDate(2026, 6, 4), QDate(2026, 8, 15),
                        QDate(2026, 10, 26), QDate(2026, 12, 8)};
    QTest::newRow("Switzerland 2026")
        << HolidayRegion::Switzerland << 4
        << QList<QDate>{QDate(2026, 1, 1), QDate(2026, 5, 14), QDate(2026, 8, 1),
                        QDate(2026, 12, 25)};
}

void TestHolidays::eachCountryHasItsNationwideHolidays()
{
    QFETCH(HolidayRegion, region);
    QFETCH(int, count);
    QFETCH(QList<QDate>, some);

    const QList<Holiday> list = holidays(region, 2026);

    QCOMPARE(list.size(), count);
    QList<QDate> days;
    for (const Holiday &holiday : list)
        days.append(holiday.date);
    QVERIFY(std::is_sorted(days.cbegin(), days.cend()));
    for (const QDate &day : std::as_const(some))
        QVERIFY2(days.contains(day), qPrintable(day.toString(Qt::ISODate)));
}

void TestHolidays::spanOverNewYearTakesBothYears()
{
    const QList<Holiday> found =
        holidaysBetween(HolidayRegion::Germany, QDate(2026, 12, 20), QDate(2027, 1, 10));

    QCOMPARE(found.size(), 3);
    QCOMPARE(found.at(0), (Holiday{QDate(2026, 12, 25), HolidayKind::ChristmasDay}));
    QCOMPARE(found.at(1), (Holiday{QDate(2026, 12, 26), HolidayKind::StStephensDay}));
    QCOMPARE(found.at(2), (Holiday{QDate(2027, 1, 1), HolidayKind::NewYear}));
}

void TestHolidays::noCountryAndYearsBeforeTheGregorianEasterHaveNone()
{
    QVERIFY(holidays(HolidayRegion::None, 2026).isEmpty());
    QVERIFY(holidays(HolidayRegion::Germany, 1500).isEmpty());
    QVERIFY(!easterSunday(1582).isValid());
    QVERIFY(holidaysBetween(HolidayRegion::Austria, QDate(2026, 5, 1), QDate(2026, 4, 1)).isEmpty());
}

QTEST_GUILESS_MAIN(TestHolidays)

#include "tst_holidays.moc"
