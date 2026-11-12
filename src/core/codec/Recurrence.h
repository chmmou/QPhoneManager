#pragma once

#include "core/Error.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimeZone>

#include <optional>

namespace qpm::core::codec {

enum class RecurrenceFrequency { Daily, Weekly, Monthly, Yearly };

/// A weekday of BYDAY with its optional ordinal: 2MO is the second Monday, -1FR the last Friday.
struct WeekdayNumber
{
    /// 0 for every such weekday of the period.
    int ordinal = 0;
    Qt::DayOfWeek day = Qt::Monday;

    bool operator==(const WeekdayNumber &other) const = default;
};

/**
 * The parts of an RFC 5545 RRULE the phone writes: FREQ DAILY to YEARLY,
 * INTERVAL, COUNT, UNTIL, BYDAY with ordinals, BYMONTHDAY, BYMONTH, BYSETPOS
 * and WKST.
 */
struct RecurrenceRule
{
    RecurrenceFrequency frequency = RecurrenceFrequency::Daily;
    /// At least 1.
    int interval = 1;
    std::optional<int> count;
    /// UTC. With `untilIsDate` the rule runs up to and including that day.
    std::optional<QDateTime> until;
    bool untilIsDate = false;
    QList<WeekdayNumber> byDay;
    /// 1 to 31, or -1 to -31 counted from the end of the month.
    QList<int> byMonthDay;
    /// 1 to 12.
    QList<int> byMonth;
    QList<int> bySetPos;
    Qt::DayOfWeek weekStart = Qt::Monday;

    bool operator==(const RecurrenceRule &other) const = default;
};

/**
 * Reads a rule such as "FREQ=WEEKLY;INTERVAL=2;BYDAY=SA", with or without
 * the "RRULE:" prefix. Fails with ErrorCode::InvalidInput for a malformed
 * rule and ErrorCode::NotSupported for parts outside RecurrenceRule, such as
 * FREQ=HOURLY or BYHOUR.
 */
[[nodiscard]] Result<RecurrenceRule> parseRecurrenceRule(const QString &text);

/// Writes the rule without the "RRULE:" prefix, in the order FREQ, INTERVAL, COUNT, UNTIL, BY..., WKST.
[[nodiscard]] QString formatRecurrenceRule(const RecurrenceRule &rule);

/**
 * The starts of a series in [from, to), in UTC and in order. `start` is the
 * first start in UTC; it always counts as the first occurrence, as RFC 5545
 * says. The rule is worked out in `zone`, so that a weekly appointment keeps
 * its local time across a change to or from summer time; an all-day series
 * is worked out on dates alone. Starts in `exdates` are left out but still
 * count for COUNT. At most `limit` starts are returned.
 */
[[nodiscard]] QList<QDateTime> expandRecurrence(const QDateTime &start, bool allDay,
                                                const RecurrenceRule &rule,
                                                const QList<QDateTime> &exdates,
                                                const QDateTime &from, const QDateTime &to,
                                                const QTimeZone &zone, qsizetype limit = 5000);

} // namespace qpm::core::codec
