#include "core/codec/PhoneVCal.h"

#include "core/codec/ContentLines.h"
#include "core/codec/PhoneTime.h"

#include <QTimeZone>

#include <algorithm>

namespace qpm::core::codec {

namespace {

constexpr int luidDigits = 12;
constexpr qint64 secondsPerDay = 24 * 60 * 60;
constexpr quint32 opaqueAlpha = 0xFF000000U;
constexpr quint32 rgbMask = 0x00FFFFFFU;

struct TimeValue
{
    QDateTime utc;
    bool dateOnly = false;
};

// A date "yyyyMMdd", a UTC time "yyyyMMddTHHmmssZ" or a local time without
// the "Z". A vCalendar 1.0 suffix such as ";;;" after the value is ignored.
std::optional<TimeValue> parseTimeValue(const QString &text)
{
    const QString value = text.section(u';', 0, 0).trimmed();
    if (value.size() == 8) {
        if (const std::optional<QDate> date = parseDate(value))
            return TimeValue{QDateTime(*date, QTime(0, 0), QTimeZone::UTC), true};
        return std::nullopt;
    }
    if (value.endsWith(u'Z') || value.endsWith(u'z')) {
        const QString upper = value.toUpper();
        if (const std::optional<QDateTime> time = parseUtcStamp(upper))
            return TimeValue{*time, false};
        return std::nullopt;
    }
    if (const std::optional<QDateTime> time = parseLocalStamp(value))
        return TimeValue{time->toUTC(), false};
    return std::nullopt;
}

QList<QDateTime> parseTimeList(const QString &text)
{
    QList<QDateTime> times;
    QString normalized = text;
    normalized.replace(u',', u';');
    const QStringList parts = normalized.split(u';', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (const std::optional<TimeValue> value = parseTimeValue(part))
            times.append(value->utc);
    }
    return times;
}

std::optional<quint32> parseLuid(const QString &text)
{
    const QString digits = text.trimmed();
    if (digits.isEmpty() || digits.size() > luidDigits)
        return std::nullopt;
    bool ok = false;
    const qulonglong value = digits.toULongLong(&ok, 16);
    if (!ok || value > 0xFFFFFFFFULL)
        return std::nullopt;
    return quint32(value);
}

std::optional<quint32> parseColor(const QString &text)
{
    const QString hex = text.trimmed();
    if (hex.isEmpty() || hex.size() > 8)
        return std::nullopt;
    bool ok = false;
    const quint32 value = hex.toUInt(&ok, 16);
    if (!ok)
        return std::nullopt;
    return hex.size() <= 6 ? (value | opaqueAlpha) : value;
}

QString formatTime(const QDateTime &time, bool dateOnly)
{
    return dateOnly ? formatDate(time.toUTC().date()) : formatUtcStamp(time);
}

QString formatTimeList(const QList<QDateTime> &times, bool dateOnly)
{
    QStringList parts;
    for (const QDateTime &time : times)
        parts.append(formatTime(time, dateOnly));
    return parts.join(u';');
}

QString visibilityText(EventVisibility visibility)
{
    switch (visibility) {
    case EventVisibility::Confidential:
        return QStringLiteral("CONFIDENTIAL");
    case EventVisibility::Private:
        return QStringLiteral("PRIVATE");
    case EventVisibility::Public:
        return QStringLiteral("PUBLIC");
    case EventVisibility::Default:
        break;
    }
    return {};
}

QString availabilityText(EventAvailability availability)
{
    switch (availability) {
    case EventAvailability::Free:
        return QStringLiteral("TRANSPARENT");
    case EventAvailability::Tentative:
        return QStringLiteral("TENTATIVE");
    case EventAvailability::Busy:
        break;
    }
    return QStringLiteral("OPAQUE");
}

QString hex(quint32 value, int digits)
{
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

// The end the phone works with: an all-day event lasts at least one day, a
// timed one at least no time at all.
QDateTime effectiveEnd(const CalendarEvent &event)
{
    if (event.allDay) {
        const QDateTime nextDay = event.start.addDays(1);
        return event.end.isValid() && event.end >= nextDay ? event.end : nextDay;
    }
    return event.end.isValid() && event.end >= event.start ? event.end : event.start;
}

QList<QDateTime> sorted(QList<QDateTime> times)
{
    std::sort(times.begin(), times.end());
    return times;
}

QByteArray wrap(const char *component, const QByteArray &body)
{
    return QByteArrayLiteral("BEGIN:VCALENDAR\r\nBEGIN:") + component + "\r\n" + body + "END:" +
           component + "\r\nEND:VCALENDAR\r\n";
}

// Lines inside the first component `component`, BEGIN and END left out.
std::optional<QList<ContentLine>> componentLines(const QByteArray &data, const QString &component)
{
    const QList<ContentLine> lines = readContentLines(data);
    QList<ContentLine> inside;
    bool found = false;
    for (const ContentLine &line : lines) {
        const QString value = line.value.trimmed().toUpper();
        if (line.name == QLatin1StringView("BEGIN") && value == component) {
            found = true;
            continue;
        }
        if (line.name == QLatin1StringView("END") && value == component && found)
            break;
        if (found)
            inside.append(line);
    }
    if (!found)
        return std::nullopt;
    return inside;
}

QByteArray eventBody(const CalendarEvent &event)
{
    QByteArray body;
    if (!event.summary.isEmpty())
        body += encodePhoneLine(QStringLiteral("SUMMARY"), event.summary);
    if (!event.location.isEmpty())
        body += encodePhoneLine(QStringLiteral("LOCATION"), event.location);
    if (!event.description.isEmpty())
        body += encodePhoneLine(QStringLiteral("DESCRIPTION"), event.description);
    body += "DTSTART:" + formatTime(event.start, event.allDay).toLatin1() + "\r\n";
    return body;
}

QByteArray eventTail(const CalendarEvent &event)
{
    QByteArray body;
    if (!event.rrule.isEmpty())
        body += encodePhoneLine(QStringLiteral("RRULE"), event.rrule);
    if (!event.exdates.isEmpty())
        body += "EXDATE:" + formatTimeList(event.exdates, event.allDay).toLatin1() + "\r\n";
    for (const QDateTime &alarm : event.alarms)
        body += "AALARM:" + formatUtcStamp(alarm).toLatin1() + "\r\n";
    const QString visibility = visibilityText(event.visibility);
    if (!visibility.isEmpty())
        body += "CLASS:" + visibility.toLatin1() + "\r\n";
    body += "TRANSP:" + availabilityText(event.availability).toLatin1() + "\r\n";
    return body;
}

QByteArray calendarLine(const CalendarEvent &event)
{
    if (event.calendarUrl.isEmpty())
        return {};
    return encodePhoneLine(QStringLiteral("X-CALENDAR"),
                           joinEscapedList({event.calendarUrl, event.calendarTitle}));
}

} // namespace

bool isTaskObject(const QByteArray &data)
{
    return data.toUpper().contains("BEGIN:VTODO");
}

std::optional<qint64> parseDurationSeconds(const QString &text)
{
    QString value = text.trimmed().toUpper();
    bool negative = false;
    if (value.startsWith(u'-') || value.startsWith(u'+')) {
        negative = value.startsWith(u'-');
        value.remove(0, 1);
    }
    if (!value.startsWith(u'P') || value.size() < 3)
        return std::nullopt;
    qint64 seconds = 0;
    QString number;
    for (qsizetype i = 1; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c.isDigit()) {
            number.append(c);
            continue;
        }
        if (c == u'T') {
            if (!number.isEmpty())
                return std::nullopt;
            continue;
        }
        bool ok = false;
        const qint64 amount = number.toLongLong(&ok);
        if (!ok)
            return std::nullopt;
        number.clear();
        switch (c.unicode()) {
        case u'W':
            seconds += amount * 7 * secondsPerDay;
            break;
        case u'D':
            seconds += amount * secondsPerDay;
            break;
        case u'H':
            seconds += amount * 3600;
            break;
        case u'M':
            // Minutes before "T" too: the phone writes and reads it so.
            seconds += amount * 60;
            break;
        case u'S':
            seconds += amount;
            break;
        default:
            return std::nullopt;
        }
    }
    if (!number.isEmpty())
        return std::nullopt;
    return negative ? -seconds : seconds;
}

Result<CalendarEvent> parsePhoneEvent(const QByteArray &data)
{
    const std::optional<QList<ContentLine>> lines = componentLines(data, QStringLiteral("VEVENT"));
    if (!lines)
        return Error(ErrorCode::ProtocolError, QStringLiteral("No VEVENT in the object"));

    CalendarEvent event;
    std::optional<TimeValue> start;
    std::optional<TimeValue> end;
    std::optional<qint64> duration;
    bool summarySeen = false;
    bool locationSeen = false;
    bool descriptionSeen = false;
    for (const ContentLine &line : *lines) {
        const QString &name = line.name;
        if (name == QLatin1StringView("SUMMARY") && !summarySeen) {
            event.summary = line.value;
            summarySeen = true;
        } else if (name == QLatin1StringView("LOCATION") && !locationSeen) {
            event.location = line.value;
            locationSeen = true;
        } else if (name == QLatin1StringView("DESCRIPTION") && !descriptionSeen) {
            event.description = line.value;
            descriptionSeen = true;
        } else if (name == QLatin1StringView("DTSTART") && !start) {
            start = parseTimeValue(line.value);
        } else if (name == QLatin1StringView("DTEND") && !end) {
            end = parseTimeValue(line.value);
        } else if (name == QLatin1StringView("DURATION") && !duration) {
            duration = parseDurationSeconds(line.value);
        } else if (name == QLatin1StringView("RRULE") && event.rrule.isEmpty()) {
            event.rrule = line.value.trimmed();
        } else if (name == QLatin1StringView("EXDATE")) {
            event.exdates.append(parseTimeList(line.value));
        } else if (name == QLatin1StringView("AALARM") || name == QLatin1StringView("DALARM")) {
            if (const std::optional<TimeValue> alarm = parseTimeValue(line.value))
                event.alarms.append(alarm->utc);
        } else if (name == QLatin1StringView("CLASS")) {
            const QString value = line.value.trimmed().toUpper();
            if (value == QLatin1StringView("CONFIDENTIAL"))
                event.visibility = EventVisibility::Confidential;
            else if (value == QLatin1StringView("PRIVATE"))
                event.visibility = EventVisibility::Private;
            else if (value == QLatin1StringView("PUBLIC"))
                event.visibility = EventVisibility::Public;
        } else if (name == QLatin1StringView("TRANSP")) {
            const QString value = line.value.trimmed().toUpper();
            if (value == QLatin1StringView("TRANSPARENT") || value == QLatin1StringView("1"))
                event.availability = EventAvailability::Free;
            else if (value == QLatin1StringView("TENTATIVE") || value == QLatin1StringView("2"))
                event.availability = EventAvailability::Tentative;
            else
                event.availability = EventAvailability::Busy;
        } else if (name == QLatin1StringView("X-COLOR")) {
            event.color = parseColor(line.value);
        } else if (name == QLatin1StringView("X-CALENDAR")) {
            const QStringList parts = splitEscapedList(line.value);
            event.calendarUrl = parts.value(0);
            event.calendarTitle = parts.value(1);
        } else if (name == QLatin1StringView("X-IRMC-LUID")) {
            event.luid = parseLuid(line.value).value_or(0);
        }
    }
    if (!start) {
        return Error(ErrorCode::ProtocolError,
                     QStringLiteral("Event %1 without a start").arg(hex(event.luid, luidDigits)));
    }
    event.allDay = start->dateOnly;
    event.start = start->utc;
    if (end)
        event.end = end->utc;
    else if (duration)
        event.end = event.start.addSecs(*duration);
    else
        event.end = event.allDay ? event.start.addDays(1) : event.start;
    if (event.end < event.start)
        event.end = event.start;
    return event;
}

Result<CalendarTask> parsePhoneTask(const QByteArray &data)
{
    const std::optional<QList<ContentLine>> lines = componentLines(data, QStringLiteral("VTODO"));
    if (!lines)
        return Error(ErrorCode::ProtocolError, QStringLiteral("No VTODO in the object"));

    CalendarTask task;
    bool summarySeen = false;
    bool descriptionSeen = false;
    bool reminderSeen = false;
    for (const ContentLine &line : *lines) {
        const QString &name = line.name;
        if (name == QLatin1StringView("SUMMARY") && !summarySeen) {
            task.summary = line.value;
            summarySeen = true;
        } else if (name == QLatin1StringView("DESCRIPTION") && !descriptionSeen) {
            task.description = line.value;
            descriptionSeen = true;
        } else if (name == QLatin1StringView("RRULE") && task.rrule.isEmpty()) {
            task.rrule = line.value.trimmed();
        } else if (name == QLatin1StringView("DTSTART")) {
            if (const std::optional<TimeValue> value = parseTimeValue(line.value))
                task.start = value->utc.date();
        } else if (name == QLatin1StringView("DUE")) {
            if (const std::optional<TimeValue> value = parseTimeValue(line.value))
                task.due = value->utc.date();
        } else if ((name == QLatin1StringView("DALARM") || name == QLatin1StringView("AALARM")) &&
                   !reminderSeen) {
            if (const std::optional<TimeValue> value = parseTimeValue(line.value)) {
                task.reminder = value->utc;
                reminderSeen = true;
            }
        } else if (name == QLatin1StringView("LAST-MODIFIED")) {
            if (const std::optional<TimeValue> value = parseTimeValue(line.value))
                task.lastModified = value->utc;
        } else if (name == QLatin1StringView("STATUS")) {
            task.completed = line.value.trimmed().toUpper() == QLatin1StringView("COMPLETED");
        } else if (name == QLatin1StringView("PRIORITY")) {
            const QString value = line.value.trimmed();
            if (value == QLatin1StringView("1"))
                task.priority = TaskPriority::High;
            else if (value == QLatin1StringView("3"))
                task.priority = TaskPriority::Low;
            else
                task.priority = TaskPriority::Normal;
        } else if (name == QLatin1StringView("CATEGORIES")) {
            const QStringList names = splitEscapedList(line.value);
            for (const QString &category : names) {
                if (!category.trimmed().isEmpty())
                    task.categories.append(category.trimmed());
            }
        } else if (name == QLatin1StringView("X-IRMC-LUID")) {
            task.luid = parseLuid(line.value).value_or(0);
        }
    }
    return task;
}

QByteArray encodePhoneEvent(const CalendarEvent &event, bool clearColor)
{
    CalendarEvent normalized = event;
    normalized.end = effectiveEnd(event);
    QByteArray body = eventBody(normalized);
    body += "DTEND:" + formatTime(normalized.end, normalized.allDay).toLatin1() + "\r\n";
    body += eventTail(normalized);
    if (normalized.color)
        body += "X-COLOR:" + hex(*normalized.color & rgbMask, 6).toLatin1() + "\r\n";
    else if (clearColor)
        body += "X-COLOR:DEFAULT\r\n";
    body += calendarLine(normalized);
    return wrap("VEVENT", body);
}

QByteArray encodePhoneTask(const CalendarTask &task,
                           const QList<std::optional<quint32>> &categoryColors)
{
    QByteArray body;
    if (!task.summary.isEmpty())
        body += encodePhoneLine(QStringLiteral("SUMMARY"), task.summary);
    if (!task.description.isEmpty())
        body += encodePhoneLine(QStringLiteral("DESCRIPTION"), task.description);
    if (!task.rrule.isEmpty())
        body += encodePhoneLine(QStringLiteral("RRULE"), task.rrule);
    if (task.start.isValid())
        body += "DTSTART:" + formatDate(task.start).toLatin1() + "\r\n";
    if (task.due.isValid())
        body += "DUE:" + formatDate(task.due).toLatin1() + "\r\n";
    if (task.reminder.isValid())
        body += "DALARM:" + formatUtcStamp(task.reminder).toLatin1() + "\r\n";
    body += task.completed ? QByteArrayLiteral("STATUS:COMPLETED\r\n")
                           : QByteArrayLiteral("STATUS:NEEDS ACTION\r\n");
    switch (task.priority) {
    case TaskPriority::High:
        body += "PRIORITY:1\r\n";
        break;
    case TaskPriority::Low:
        body += "PRIORITY:3\r\n";
        break;
    case TaskPriority::Normal:
        body += "PRIORITY:2\r\n";
        break;
    }
    if (!task.categories.isEmpty()) {
        QString header = QStringLiteral("CATEGORIES");
        const bool allColored =
            categoryColors.size() == task.categories.size() &&
            std::all_of(categoryColors.cbegin(), categoryColors.cend(),
                        [](const std::optional<quint32> &color) { return color.has_value(); });
        if (allColored) {
            QStringList colors;
            for (const std::optional<quint32> &color : categoryColors)
                colors.append(hex(*color & rgbMask, 6));
            // The phone reads this parameter only as the first one.
            header += QStringLiteral(";COLOR=") + colors.join(u',');
        }
        body += encodePhoneLine(header, joinEscapedList(task.categories));
    }
    return wrap("VTODO", body);
}

QByteArray encodeEventAsPhoneSendsIt(const CalendarEvent &event)
{
    CalendarEvent normalized = event;
    normalized.end = effectiveEnd(event);
    QByteArray body = eventBody(normalized);
    if (normalized.rrule.isEmpty()) {
        body += "DTEND:" + formatTime(normalized.end, normalized.allDay).toLatin1() + "\r\n";
    } else {
        const qint64 seconds = normalized.start.secsTo(normalized.end);
        body += normalized.allDay ? "DURATION:P" + QByteArray::number(seconds / secondsPerDay) + "D\r\n"
                                  : "DURATION:P" + QByteArray::number(seconds) + "S\r\n";
    }
    body += eventTail(normalized);
    if (normalized.color)
        body += "X-COLOR:" + hex(*normalized.color, 8).toLatin1() + "\r\n";
    body += calendarLine(normalized);
    body += "X-IRMC-LUID:" + hex(normalized.luid, luidDigits).toLatin1() + "\r\n";
    return wrap("VEVENT", body);
}

QByteArray encodeTaskAsPhoneSendsIt(const CalendarTask &task)
{
    QByteArray body;
    if (!task.summary.isEmpty())
        body += encodePhoneLine(QStringLiteral("SUMMARY"), task.summary);
    if (!task.description.isEmpty())
        body += encodePhoneLine(QStringLiteral("DESCRIPTION"), task.description);
    if (!task.rrule.isEmpty())
        body += encodePhoneLine(QStringLiteral("RRULE"), task.rrule);
    if (task.start.isValid())
        body += "DTSTART:" + formatDate(task.start).toLatin1() + "\r\n";
    if (task.due.isValid())
        body += "DUE:" + formatDate(task.due).toLatin1() + "\r\n";
    if (task.reminder.isValid())
        body += "DALARM:" + formatUtcStamp(task.reminder).toLatin1() + "\r\n";
    if (task.lastModified.isValid())
        body += "LAST-MODIFIED:" + formatUtcStamp(task.lastModified).toLatin1() + "\r\n";
    body += task.completed ? QByteArrayLiteral("STATUS:COMPLETED\r\n")
                           : QByteArrayLiteral("STATUS:NEEDS ACTION\r\n");
    body += task.priority == TaskPriority::High  ? QByteArrayLiteral("PRIORITY:1\r\n")
            : task.priority == TaskPriority::Low ? QByteArrayLiteral("PRIORITY:3\r\n")
                                                 : QByteArrayLiteral("PRIORITY:2\r\n");
    if (!task.categories.isEmpty())
        body += encodePhoneLine(QStringLiteral("CATEGORIES"), joinEscapedList(task.categories));
    body += "X-IRMC-LUID:" + hex(task.luid, luidDigits).toLatin1() + "\r\n";
    return wrap("VTODO", body);
}

bool sameEventContent(const CalendarEvent &a, const CalendarEvent &b)
{
    return a.summary == b.summary && a.location == b.location && a.description == b.description &&
           a.allDay == b.allDay && a.start == b.start && effectiveEnd(a) == effectiveEnd(b) &&
           a.rrule == b.rrule && sorted(a.exdates) == sorted(b.exdates) &&
           sorted(a.alarms) == sorted(b.alarms) && a.visibility == b.visibility &&
           a.availability == b.availability && a.color.has_value() == b.color.has_value() &&
           (a.color.value_or(0) & rgbMask) == (b.color.value_or(0) & rgbMask);
}

bool sameTaskContent(const CalendarTask &a, const CalendarTask &b)
{
    return a.summary == b.summary && a.description == b.description && a.start == b.start &&
           a.due == b.due && a.reminder == b.reminder && a.rrule == b.rrule &&
           a.priority == b.priority && a.completed == b.completed && a.categories == b.categories;
}

} // namespace qpm::core::codec
