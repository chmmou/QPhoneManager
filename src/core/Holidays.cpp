#include "core/Holidays.h"

#include <algorithm>

namespace qpm::core {

namespace {

constexpr int firstGregorianEasterYear = 1583;
// Days from Easter Sunday.
constexpr int goodFriday = -2;
constexpr int easterMonday = 1;
constexpr int ascension = 39;
constexpr int whitMonday = 50;
constexpr int corpusChristi = 60;

} // namespace

QDate easterSunday(int year)
{
    if (year < firstGregorianEasterYear)
        return {};
    const int century = year / 100;
    const int moon = 15 + (3 * century + 3) / 4 - (8 * century + 13) / 25;
    const int sun = 2 - (3 * century + 3) / 4;
    const int cycle = year % 19;
    const int seed = (19 * cycle + moon) % 30;
    const int correction = (seed + cycle / 11) / 29;
    const int fullMoon = 21 + seed - correction;
    const int firstSunday = 7 - (year + year / 4 + sun) % 7;
    const int distance = 7 - (fullMoon - firstSunday) % 7;
    // Days after the last of February.
    const int marchDay = fullMoon + distance;
    return QDate(year, 3, 1).addDays(marchDay - 1);
}

QList<Holiday> holidays(HolidayRegion region, int year)
{
    const QDate easter = easterSunday(year);
    if (!easter.isValid() || region == HolidayRegion::None)
        return {};
    const auto fixed = [year](int month, int day, HolidayKind kind) {
        return Holiday{QDate(year, month, day), kind};
    };
    const auto moving = [&easter](int days, HolidayKind kind) {
        return Holiday{easter.addDays(days), kind};
    };

    QList<Holiday> list;
    switch (region) {
    case HolidayRegion::Germany:
        list = {
            fixed(1, 1, HolidayKind::NewYear),
            moving(goodFriday, HolidayKind::GoodFriday),
            moving(easterMonday, HolidayKind::EasterMonday),
            fixed(5, 1, HolidayKind::LabourDay),
            moving(ascension, HolidayKind::Ascension),
            moving(whitMonday, HolidayKind::WhitMonday),
            fixed(10, 3, HolidayKind::GermanUnityDay),
            fixed(12, 25, HolidayKind::ChristmasDay),
            fixed(12, 26, HolidayKind::StStephensDay),
        };
        break;
    case HolidayRegion::Austria:
        list = {
            fixed(1, 1, HolidayKind::NewYear),
            fixed(1, 6, HolidayKind::Epiphany),
            moving(easterMonday, HolidayKind::EasterMonday),
            fixed(5, 1, HolidayKind::LabourDay),
            moving(ascension, HolidayKind::Ascension),
            moving(whitMonday, HolidayKind::WhitMonday),
            moving(corpusChristi, HolidayKind::CorpusChristi),
            fixed(8, 15, HolidayKind::AssumptionDay),
            fixed(10, 26, HolidayKind::AustrianNationalDay),
            fixed(11, 1, HolidayKind::AllSaints),
            fixed(12, 8, HolidayKind::ImmaculateConception),
            fixed(12, 25, HolidayKind::ChristmasDay),
            fixed(12, 26, HolidayKind::StStephensDay),
        };
        break;
    case HolidayRegion::Switzerland:
        list = {
            fixed(1, 1, HolidayKind::NewYear),
            moving(ascension, HolidayKind::Ascension),
            fixed(8, 1, HolidayKind::SwissNationalDay),
            fixed(12, 25, HolidayKind::ChristmasDay),
        };
        break;
    case HolidayRegion::None:
        break;
    }
    std::sort(list.begin(), list.end(),
              [](const Holiday &a, const Holiday &b) { return a.date < b.date; });
    return list;
}

QList<Holiday> holidaysBetween(HolidayRegion region, const QDate &first, const QDate &last)
{
    QList<Holiday> found;
    if (!first.isValid() || !last.isValid() || last < first)
        return found;
    for (int year = first.year(); year <= last.year(); ++year) {
        const QList<Holiday> ofYear = holidays(region, year);
        for (const Holiday &holiday : ofYear) {
            if (holiday.date >= first && holiday.date <= last)
                found.append(holiday);
        }
    }
    return found;
}

} // namespace qpm::core
