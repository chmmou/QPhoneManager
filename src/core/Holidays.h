#pragma once

#include <QDate>
#include <QList>

namespace qpm::core {

/// The countries whose public holidays the calendar can show.
enum class HolidayRegion { None, Germany, Austria, Switzerland };

/// Which holiday a date is; the user interface gives each its name.
enum class HolidayKind {
    NewYear,
    Epiphany,
    BerchtoldsDay,
    GoodFriday,
    EasterMonday,
    LabourDay,
    Ascension,
    WhitMonday,
    CorpusChristi,
    AssumptionDay,
    SwissNationalDay,
    GermanUnityDay,
    AustrianNationalDay,
    AllSaints,
    ImmaculateConception,
    ChristmasDay,
    StStephensDay,
};

struct Holiday
{
    QDate date;
    HolidayKind kind = HolidayKind::NewYear;

    bool operator==(const Holiday &other) const = default;
};

/**
 * Easter Sunday of the Gregorian calendar after the Easter formula of Gauss
 * in the corrected form of Lichtenberg, which needs no exceptions. Valid
 * from 1583; returns an invalid date before.
 */
[[nodiscard]] QDate easterSunday(int year);

/**
 * The holidays that hold in the whole of `region` in `year`, in date order:
 * for Germany those of all states, for Austria those of federal law, for
 * Switzerland those of every canton. Holidays of single states or cantons
 * are not listed. Empty for HolidayRegion::None and before 1583.
 */
[[nodiscard]] QList<Holiday> holidays(HolidayRegion region, int year);

/// The holidays of `region` from `first` to `last`, both included, in date order.
[[nodiscard]] QList<Holiday> holidaysBetween(HolidayRegion region, const QDate &first,
                                             const QDate &last);

} // namespace qpm::core
