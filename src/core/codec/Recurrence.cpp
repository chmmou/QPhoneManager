#include "core/codec/Recurrence.h"

#include "core/codec/PhoneTime.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <set>

namespace qpm::core::codec {

namespace {

constexpr int maxInterval = 10000;
constexpr int maxCount = 100000;
constexpr int monthsPerYear = 12;
constexpr int daysPerWeek = 7;
// Periods looked at before giving up on a rule that matches nothing.
constexpr int maxPeriods = 200000;

constexpr std::array<std::pair<const char *, Qt::DayOfWeek>, 7> weekdayNames = {{
    {"MO", Qt::Monday},
    {"TU", Qt::Tuesday},
    {"WE", Qt::Wednesday},
    {"TH", Qt::Thursday},
    {"FR", Qt::Friday},
    {"SA", Qt::Saturday},
    {"SU", Qt::Sunday},
}};

std::optional<Qt::DayOfWeek> weekdayFrom(const QString &text)
{
    for (const auto &[name, day] : weekdayNames) {
        if (text == QLatin1StringView(name))
            return day;
    }
    return std::nullopt;
}

QString weekdayName(Qt::DayOfWeek day)
{
    for (const auto &[name, value] : weekdayNames) {
        if (value == day)
            return QString::fromLatin1(name);
    }
    return QStringLiteral("MO");
}

Error invalid(const QString &what)
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Recurrence rule: %1").arg(what));
}

std::optional<QList<int>> intList(const QString &text, int minimum, int maximum, bool allowNegative)
{
    QList<int> values;
    const QStringList parts = text.split(u',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (!ok || value == 0)
            return std::nullopt;
        const int magnitude = value < 0 ? -value : value;
        if ((value < 0 && !allowNegative) || magnitude < minimum || magnitude > maximum)
            return std::nullopt;
        values.append(value);
    }
    if (values.isEmpty())
        return std::nullopt;
    return values;
}

QDate dateInMonth(int year, int month, int day)
{
    const QDate first(year, month, 1);
    if (!first.isValid())
        return {};
    const int length = first.daysInMonth();
    const int resolved = day > 0 ? day : length + day + 1;
    if (resolved < 1 || resolved > length)
        return {};
    return QDate(year, month, resolved);
}

// Every date in [first, last] on `day`, or only the ordinal-th of them.
QList<QDate> weekdaysIn(const QDate &first, const QDate &last, const WeekdayNumber &wanted)
{
    QList<QDate> matches;
    QDate date = first.addDays((wanted.day - first.dayOfWeek() + daysPerWeek) % daysPerWeek);
    while (date <= last) {
        matches.append(date);
        date = date.addDays(daysPerWeek);
    }
    if (wanted.ordinal == 0)
        return matches;
    const qsizetype index = wanted.ordinal > 0 ? wanted.ordinal - 1 : matches.size() + wanted.ordinal;
    if (index < 0 || index >= matches.size())
        return {};
    return {matches.at(index)};
}

bool weekdayListed(const QList<WeekdayNumber> &days, const QDate &date)
{
    return std::any_of(days.cbegin(), days.cend(),
                       [&date](const WeekdayNumber &day) { return day.day == date.dayOfWeek(); });
}

bool monthDayListed(const QList<int> &days, const QDate &date)
{
    for (const int day : days) {
        if (dateInMonth(date.year(), date.month(), day) == date)
            return true;
    }
    return false;
}

QList<QDate> monthDays(int year, int month, const RecurrenceRule &rule, const QDate &first)
{
    QList<QDate> dates;
    if (!rule.byMonthDay.isEmpty()) {
        for (const int day : rule.byMonthDay) {
            const QDate date = dateInMonth(year, month, day);
            if (date.isValid() && (rule.byDay.isEmpty() || weekdayListed(rule.byDay, date)))
                dates.append(date);
        }
        return dates;
    }
    if (!rule.byDay.isEmpty()) {
        const QDate begin(year, month, 1);
        const QDate end(year, month, begin.daysInMonth());
        for (const WeekdayNumber &day : rule.byDay)
            dates.append(weekdaysIn(begin, end, day));
        return dates;
    }
    const QDate date = dateInMonth(year, month, first.day());
    if (date.isValid())
        dates.append(date);
    return dates;
}

QList<QDate> candidatesOf(const RecurrenceRule &rule, const QDate &first, int period)
{
    const qint64 step = qint64(period) * rule.interval;
    QList<QDate> dates;
    switch (rule.frequency) {
    case RecurrenceFrequency::Daily: {
        const QDate date = first.addDays(step);
        if (rule.byDay.isEmpty() || weekdayListed(rule.byDay, date))
            if (rule.byMonthDay.isEmpty() || monthDayListed(rule.byMonthDay, date))
                dates.append(date);
        break;
    }
    case RecurrenceFrequency::Weekly: {
        const int offset = (first.dayOfWeek() - rule.weekStart + daysPerWeek) % daysPerWeek;
        const QDate weekBegin = first.addDays(-offset + step * daysPerWeek);
        if (rule.byDay.isEmpty()) {
            dates.append(weekBegin.addDays(offset));
        } else {
            for (const WeekdayNumber &day : rule.byDay)
                dates.append(weekBegin.addDays((day.day - rule.weekStart + daysPerWeek) %
                                               daysPerWeek));
        }
        break;
    }
    case RecurrenceFrequency::Monthly: {
        const QDate month = QDate(first.year(), first.month(), 1).addMonths(int(step));
        dates = monthDays(month.year(), month.month(), rule, first);
        break;
    }
    case RecurrenceFrequency::Yearly: {
        const int year = first.year() + int(step);
        if (rule.byMonth.isEmpty() && rule.byMonthDay.isEmpty() && !rule.byDay.isEmpty()) {
            const QDate begin(year, 1, 1);
            const QDate end(year, monthsPerYear, 31);
            for (const WeekdayNumber &day : rule.byDay)
                dates.append(weekdaysIn(begin, end, day));
        } else if (rule.byMonth.isEmpty() && rule.byMonthDay.isEmpty()) {
            const QDate date = dateInMonth(year, first.month(), first.day());
            if (date.isValid() && date.day() == first.day())
                dates.append(date);
        } else {
            QList<int> months = rule.byMonth;
            if (months.isEmpty()) {
                for (int month = 1; month <= monthsPerYear; ++month)
                    months.append(month);
            }
            for (const int month : std::as_const(months)) {
                if (rule.byMonthDay.isEmpty() && rule.byDay.isEmpty()) {
                    const QDate date = dateInMonth(year, month, first.day());
                    if (date.isValid() && date.day() == first.day())
                        dates.append(date);
                } else {
                    dates.append(monthDays(year, month, rule, first));
                }
            }
        }
        break;
    }
    }

    if (!rule.byMonth.isEmpty()) {
        dates.removeIf([&rule](const QDate &date) { return !rule.byMonth.contains(date.month()); });
    }
    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());

    if (!rule.bySetPos.isEmpty() && !dates.isEmpty()) {
        std::set<QDate> picked;
        for (const int position : rule.bySetPos) {
            const qsizetype index = position > 0 ? position - 1 : dates.size() + position;
            if (index >= 0 && index < dates.size())
                picked.insert(dates.at(index));
        }
        dates = QList<QDate>(picked.cbegin(), picked.cend());
    }
    return dates;
}

// The first date the period of `period` can hold, to know when to stop.
QDate periodBegin(const RecurrenceRule &rule, const QDate &first, int period)
{
    const qint64 step = qint64(period) * rule.interval;
    switch (rule.frequency) {
    case RecurrenceFrequency::Daily:
        return first.addDays(step);
    case RecurrenceFrequency::Weekly:
        return first.addDays(step * daysPerWeek - daysPerWeek);
    case RecurrenceFrequency::Monthly:
        return QDate(first.year(), first.month(), 1).addMonths(int(step));
    case RecurrenceFrequency::Yearly:
        break;
    }
    return QDate(first.year() + int(step), 1, 1);
}

// Without COUNT nothing before `from` needs counting, so the periods before
// it are skipped; one period is kept in front for rules that reach back.
int firstUsefulPeriod(const RecurrenceRule &rule, const QDate &first, const QDate &fromDate)
{
    if (rule.count || fromDate <= first)
        return 0;
    qint64 units = 0;
    switch (rule.frequency) {
    case RecurrenceFrequency::Daily:
        units = first.daysTo(fromDate);
        break;
    case RecurrenceFrequency::Weekly:
        units = first.daysTo(fromDate) / daysPerWeek;
        break;
    case RecurrenceFrequency::Monthly:
        units = (fromDate.year() - first.year()) * qint64(monthsPerYear) + fromDate.month() -
                first.month();
        break;
    case RecurrenceFrequency::Yearly:
        units = fromDate.year() - first.year();
        break;
    }
    const qint64 period = units / rule.interval - 1;
    return period > 0 ? int(std::min<qint64>(period, maxPeriods)) : 0;
}

bool excluded(const QList<QDateTime> &exdates, const QDateTime &instant, bool allDay,
              const QDate &localDate)
{
    for (const QDateTime &exdate : exdates) {
        if (allDay) {
            if (exdate.toUTC().date() == instant.toUTC().date())
                return true;
        } else if (exdate == instant) {
            return true;
        } else if (exdate.toUTC().time() == QTime(0, 0) && exdate.toUTC().date() == localDate) {
            // A bare date excludes the whole day of a timed series.
            return true;
        }
    }
    return false;
}

} // namespace

Result<RecurrenceRule> parseRecurrenceRule(const QString &text)
{
    QString value = text.trimmed();
    if (value.startsWith(QLatin1StringView("RRULE:"), Qt::CaseInsensitive))
        value = value.mid(6);
    if (value.isEmpty())
        return invalid(QStringLiteral("empty"));

    RecurrenceRule rule;
    bool frequencySeen = false;
    const QStringList parts = value.split(u';', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const qsizetype equals = part.indexOf(u'=');
        if (equals <= 0)
            return invalid(QStringLiteral("part without a value"));
        const QString key = part.left(equals).trimmed().toUpper();
        const QString argument = part.mid(equals + 1).trimmed().toUpper();
        if (key == QLatin1StringView("FREQ")) {
            frequencySeen = true;
            if (argument == QLatin1StringView("DAILY"))
                rule.frequency = RecurrenceFrequency::Daily;
            else if (argument == QLatin1StringView("WEEKLY"))
                rule.frequency = RecurrenceFrequency::Weekly;
            else if (argument == QLatin1StringView("MONTHLY"))
                rule.frequency = RecurrenceFrequency::Monthly;
            else if (argument == QLatin1StringView("YEARLY"))
                rule.frequency = RecurrenceFrequency::Yearly;
            else
                return Error(ErrorCode::NotSupported,
                             QStringLiteral("Recurrence frequency %1").arg(argument.left(16)));
        } else if (key == QLatin1StringView("INTERVAL")) {
            bool ok = false;
            rule.interval = argument.toInt(&ok);
            if (!ok || rule.interval < 1 || rule.interval > maxInterval)
                return invalid(QStringLiteral("interval"));
        } else if (key == QLatin1StringView("COUNT")) {
            bool ok = false;
            const int count = argument.toInt(&ok);
            if (!ok || count < 1 || count > maxCount)
                return invalid(QStringLiteral("count"));
            rule.count = count;
        } else if (key == QLatin1StringView("UNTIL")) {
            if (const std::optional<QDate> date = parseDate(argument)) {
                rule.until = QDateTime(*date, QTime(0, 0), QTimeZone::UTC);
                rule.untilIsDate = true;
            } else if (const std::optional<QDateTime> time = parseUtcStamp(argument)) {
                rule.until = *time;
            } else if (const std::optional<QDateTime> local = parseLocalStamp(argument)) {
                rule.until = local->toUTC();
            } else {
                return invalid(QStringLiteral("until"));
            }
        } else if (key == QLatin1StringView("BYDAY")) {
            const QStringList days = argument.split(u',', Qt::SkipEmptyParts);
            for (const QString &day : days) {
                const QString trimmed = day.trimmed();
                if (trimmed.size() < 2)
                    return invalid(QStringLiteral("weekday"));
                const std::optional<Qt::DayOfWeek> weekday = weekdayFrom(trimmed.right(2));
                if (!weekday)
                    return invalid(QStringLiteral("weekday"));
                WeekdayNumber entry;
                entry.day = *weekday;
                const QString ordinal = trimmed.chopped(2);
                if (!ordinal.isEmpty()) {
                    bool ok = false;
                    entry.ordinal = ordinal.toInt(&ok);
                    if (!ok || entry.ordinal == 0 || entry.ordinal > 53 || entry.ordinal < -53)
                        return invalid(QStringLiteral("weekday ordinal"));
                }
                rule.byDay.append(entry);
            }
        } else if (key == QLatin1StringView("BYMONTHDAY")) {
            const std::optional<QList<int>> days = intList(argument, 1, 31, true);
            if (!days)
                return invalid(QStringLiteral("month day"));
            rule.byMonthDay = *days;
        } else if (key == QLatin1StringView("BYMONTH")) {
            const std::optional<QList<int>> months = intList(argument, 1, monthsPerYear, false);
            if (!months)
                return invalid(QStringLiteral("month"));
            rule.byMonth = *months;
        } else if (key == QLatin1StringView("BYSETPOS")) {
            const std::optional<QList<int>> positions = intList(argument, 1, 366, true);
            if (!positions)
                return invalid(QStringLiteral("set position"));
            rule.bySetPos = *positions;
        } else if (key == QLatin1StringView("WKST")) {
            const std::optional<Qt::DayOfWeek> weekday = weekdayFrom(argument);
            if (!weekday)
                return invalid(QStringLiteral("week start"));
            rule.weekStart = *weekday;
        } else {
            return Error(ErrorCode::NotSupported,
                         QStringLiteral("Recurrence part %1").arg(key.left(16)));
        }
    }
    if (!frequencySeen)
        return invalid(QStringLiteral("no frequency"));
    return rule;
}

QString formatRecurrenceRule(const RecurrenceRule &rule)
{
    QStringList parts;
    switch (rule.frequency) {
    case RecurrenceFrequency::Daily:
        parts.append(QStringLiteral("FREQ=DAILY"));
        break;
    case RecurrenceFrequency::Weekly:
        parts.append(QStringLiteral("FREQ=WEEKLY"));
        break;
    case RecurrenceFrequency::Monthly:
        parts.append(QStringLiteral("FREQ=MONTHLY"));
        break;
    case RecurrenceFrequency::Yearly:
        parts.append(QStringLiteral("FREQ=YEARLY"));
        break;
    }
    if (rule.interval > 1)
        parts.append(QStringLiteral("INTERVAL=%1").arg(rule.interval));
    if (rule.count)
        parts.append(QStringLiteral("COUNT=%1").arg(*rule.count));
    if (rule.until) {
        parts.append(QStringLiteral("UNTIL=") + (rule.untilIsDate
                                                     ? formatDate(rule.until->toUTC().date())
                                                     : formatUtcStamp(*rule.until)));
    }
    const auto join = [](const QList<int> &values) {
        QStringList texts;
        for (const int value : values)
            texts.append(QString::number(value));
        return texts.join(u',');
    };
    if (!rule.byDay.isEmpty()) {
        QStringList days;
        for (const WeekdayNumber &day : rule.byDay) {
            days.append((day.ordinal != 0 ? QString::number(day.ordinal) : QString()) +
                        weekdayName(day.day));
        }
        parts.append(QStringLiteral("BYDAY=") + days.join(u','));
    }
    if (!rule.byMonthDay.isEmpty())
        parts.append(QStringLiteral("BYMONTHDAY=") + join(rule.byMonthDay));
    if (!rule.byMonth.isEmpty())
        parts.append(QStringLiteral("BYMONTH=") + join(rule.byMonth));
    if (!rule.bySetPos.isEmpty())
        parts.append(QStringLiteral("BYSETPOS=") + join(rule.bySetPos));
    if (rule.weekStart != Qt::Monday)
        parts.append(QStringLiteral("WKST=") + weekdayName(rule.weekStart));
    return parts.join(u';');
}

QList<QDateTime> expandRecurrence(const QDateTime &start, bool allDay, const RecurrenceRule &rule,
                                  const QList<QDateTime> &exdates, const QDateTime &from,
                                  const QDateTime &to, const QTimeZone &zone, qsizetype limit)
{
    QList<QDateTime> starts;
    if (!start.isValid() || !from.isValid() || !to.isValid() || from >= to || limit <= 0)
        return starts;

    const QTimeZone workZone = allDay || !zone.isValid() ? QTimeZone(QTimeZone::UTC) : zone;
    const QDateTime localStart = start.toTimeZone(workZone);
    const QDate firstDate = localStart.date();
    const QTime timeOfDay = localStart.time();
    const QDate lastDate = to.toTimeZone(workZone).date().addDays(1);
    const auto instantOf = [&](const QDate &date) {
        return allDay ? QDateTime(date, QTime(0, 0), QTimeZone::UTC)
                      : QDateTime(date, timeOfDay, workZone).toUTC();
    };

    int counted = 0;
    // Returns false once the series is over or past `to`.
    const auto take = [&](const QDate &date) {
        const QDateTime instant = instantOf(date);
        if (rule.until) {
            const bool past = rule.untilIsDate ? date > rule.until->toUTC().date()
                                               : instant > *rule.until;
            if (past)
                return false;
        }
        ++counted;
        if (rule.count && counted > *rule.count)
            return false;
        if (instant >= to)
            return false;
        if (instant >= from && !excluded(exdates, instant, allDay, date))
            starts.append(instant);
        return starts.size() < limit;
    };

    // The start counts first even where the rule would not hit it.
    if (!take(firstDate))
        return starts;
    for (int period = firstUsefulPeriod(rule, firstDate, from.toTimeZone(workZone).date());
         period < maxPeriods; ++period) {
        if (periodBegin(rule, firstDate, period) > lastDate)
            break;
        const QList<QDate> candidates = candidatesOf(rule, firstDate, period);
        for (const QDate &date : candidates) {
            if (date <= firstDate)
                continue;
            if (!take(date))
                return starts;
        }
    }
    return starts;
}

} // namespace qpm::core::codec
