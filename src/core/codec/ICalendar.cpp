#include "core/codec/ICalendar.h"

#include "core/Version.h"
#include "core/codec/ContentLines.h"
#include "core/codec/PhoneTime.h"
#include "core/codec/Recurrence.h"

#include <QHash>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>

namespace qpm::core::codec {

namespace {

constexpr int maxDepth = 8;
constexpr qsizetype foldOctets = 75;
constexpr int highPriorityLimit = 4;
constexpr int normalPriority = 5;
constexpr int lowPriority = 9;
constexpr int onsetLookBackDays = 400;

struct Component
{
    QString name;
    QList<ContentLine> properties;
    QList<Component> children;

    [[nodiscard]] const ContentLine *first(QLatin1StringView property) const
    {
        for (const ContentLine &line : properties) {
            if (line.name == property)
                return &line;
        }
        return nullptr;
    }

    [[nodiscard]] QList<const ContentLine *> all(QLatin1StringView property) const
    {
        QList<const ContentLine *> lines;
        for (const ContentLine &line : properties) {
            if (line.name == property)
                lines.append(&line);
        }
        return lines;
    }
};

struct TimeValue
{
    QDateTime utc;
    bool dateOnly = false;
};

QString unescaped(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\\' && i + 1 < text.size()) {
            const QChar next = text.at(++i);
            if (next == u'n' || next == u'N')
                out.append(u'\n');
            else
                out.append(next);
            continue;
        }
        out.append(c);
    }
    return out;
}

QString escaped(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'\\':
            out += QLatin1StringView("\\\\");
            break;
        case u';':
            out += QLatin1StringView("\\;");
            break;
        case u',':
            out += QLatin1StringView("\\,");
            break;
        case u'\n':
            out += QLatin1StringView("\\n");
            break;
        case u'\r':
            break;
        default:
            out += c;
        }
    }
    return out;
}

// Splits at commas that are not escaped and unescapes the parts.
QStringList splitTextList(const QString &text)
{
    QStringList parts;
    QString current;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\\' && i + 1 < text.size()) {
            current.append(c);
            current.append(text.at(++i));
        } else if (c == u',') {
            parts.append(unescaped(current));
            current.clear();
        } else {
            current.append(c);
        }
    }
    parts.append(unescaped(current));
    return parts;
}

Result<Component> buildTree(const QList<ContentLine> &lines)
{
    QList<Component> stack;
    std::optional<Component> root;
    qsizetype objects = 0;
    for (const ContentLine &line : lines) {
        if (line.name == QLatin1StringView("BEGIN")) {
            if (stack.size() >= maxDepth) {
                return Error(ErrorCode::LimitExceeded,
                             QStringLiteral("iCalendar nested deeper than %1").arg(maxDepth));
            }
            if (++objects > maxICalendarObjects * 4) {
                return Error(ErrorCode::LimitExceeded,
                             QStringLiteral("iCalendar with more than %1 objects")
                                 .arg(maxICalendarObjects));
            }
            stack.append(Component{line.value.trimmed().toUpper(), {}, {}});
        } else if (line.name == QLatin1StringView("END")) {
            if (stack.isEmpty())
                continue;
            Component done = stack.takeLast();
            if (stack.isEmpty()) {
                if (!root && done.name == QLatin1StringView("VCALENDAR"))
                    root = std::move(done);
            } else {
                stack.last().children.append(std::move(done));
            }
        } else if (!stack.isEmpty()) {
            stack.last().properties.append(line);
        }
    }
    // A file cut short still gives what was complete.
    while (!root && !stack.isEmpty()) {
        Component done = stack.takeLast();
        if (stack.isEmpty() && done.name == QLatin1StringView("VCALENDAR"))
            root = std::move(done);
        else if (!stack.isEmpty())
            stack.last().children.append(std::move(done));
    }
    if (!root)
        return Error(ErrorCode::InvalidInput, QStringLiteral("No VCALENDAR in the file"));
    return *root;
}

// "+0100", "-0530" or "+013000" as seconds east of UTC.
std::optional<int> parseOffset(const QString &text)
{
    const QString value = text.trimmed();
    if (value.size() != 5 && value.size() != 7)
        return std::nullopt;
    const int sign = value.at(0) == u'-' ? -1 : 1;
    if (value.at(0) != u'-' && value.at(0) != u'+')
        return std::nullopt;
    bool ok = false;
    const int hours = value.mid(1, 2).toInt(&ok);
    if (!ok)
        return std::nullopt;
    const int minutes = value.mid(3, 2).toInt(&ok);
    if (!ok)
        return std::nullopt;
    const int seconds = value.size() == 7 ? value.mid(5, 2).toInt(&ok) : 0;
    if (!ok)
        return std::nullopt;
    return sign * (hours * 3600 + minutes * 60 + seconds);
}

// A local time without zone, held as UTC so that no zone of this computer
// interferes.
std::optional<QDateTime> naiveTime(const QString &text)
{
    const QString value = text.trimmed();
    if (value.size() != 15 || value.at(8) != u'T')
        return std::nullopt;
    const std::optional<QDate> date = parseDate(value.left(8));
    if (!date)
        return std::nullopt;
    const QTime time = QTime::fromString(value.mid(9), QStringLiteral("HHmmss"));
    if (!time.isValid())
        return std::nullopt;
    return QDateTime(*date, time, QTimeZone::UTC);
}

class TimeResolver
{
public:
    explicit TimeResolver(QHash<QString, Component> zones) : m_zones(std::move(zones)) {}

    [[nodiscard]] std::optional<TimeValue> resolve(const ContentLine &line) const
    {
        return resolveText(line.value, line.parameter(QStringLiteral("TZID")),
                           line.parameter(QStringLiteral("VALUE")).toUpper() ==
                               QLatin1StringView("DATE"));
    }

    [[nodiscard]] std::optional<TimeValue> resolveText(const QString &text, const QString &tzid,
                                                       bool valueDate) const
    {
        const QString value = text.trimmed().toUpper();
        if (valueDate || value.size() == 8) {
            const std::optional<QDate> date = parseDate(value.left(8));
            if (!date)
                return std::nullopt;
            return TimeValue{QDateTime(*date, QTime(0, 0), QTimeZone::UTC), true};
        }
        if (value.endsWith(u'Z')) {
            const std::optional<QDateTime> time = parseUtcStamp(value);
            if (!time)
                return std::nullopt;
            return TimeValue{*time, false};
        }
        const std::optional<QDateTime> naive = naiveTime(value);
        if (!naive)
            return std::nullopt;
        if (tzid.isEmpty())
            return TimeValue{QDateTime(naive->date(), naive->time()).toUTC(), false};
        const QTimeZone zone = zoneFor(tzid);
        if (zone.isValid())
            return TimeValue{QDateTime(naive->date(), naive->time(), zone).toUTC(), false};
        if (const auto definition = m_zones.constFind(tzid); definition != m_zones.cend()) {
            const std::optional<int> offset = offsetIn(definition.value(), *naive);
            if (offset)
                return TimeValue{naive->addSecs(-*offset), false};
        }
        return TimeValue{QDateTime(naive->date(), naive->time()).toUTC(), false};
    }

private:
    [[nodiscard]] static QTimeZone zoneFor(const QString &tzid)
    {
        QString id = tzid.trimmed();
        // Some programs put a path in front of the IANA name.
        if (id.startsWith(u'/'))
            id = id.section(u'/', -2);
        const QByteArray bytes = id.toUtf8();
        if (QTimeZone::isTimeZoneIdAvailable(bytes))
            return QTimeZone(bytes);
        const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(bytes);
        if (!iana.isEmpty() && QTimeZone::isTimeZoneIdAvailable(iana))
            return QTimeZone(iana);
        return {};
    }

    // The offset of the observance that started last before `local`.
    [[nodiscard]] static std::optional<int> offsetIn(const Component &zone, const QDateTime &local)
    {
        QDateTime bestOnset;
        std::optional<int> bestOffset;
        std::optional<int> earliestFrom;
        QDateTime earliestOnset;
        for (const Component &observance : zone.children) {
            if (observance.name != QLatin1StringView("STANDARD") &&
                observance.name != QLatin1StringView("DAYLIGHT")) {
                continue;
            }
            const ContentLine *start = observance.first(QLatin1StringView("DTSTART"));
            const ContentLine *to = observance.first(QLatin1StringView("TZOFFSETTO"));
            const ContentLine *from = observance.first(QLatin1StringView("TZOFFSETFROM"));
            const std::optional<QDateTime> first = start ? naiveTime(start->value) : std::nullopt;
            const std::optional<int> offset = to ? parseOffset(to->value) : std::nullopt;
            if (!first || !offset)
                continue;
            if (!earliestOnset.isValid() || *first < earliestOnset) {
                earliestOnset = *first;
                earliestFrom = from ? parseOffset(from->value) : offset;
            }
            QDateTime onset;
            if (*first <= local)
                onset = *first;
            if (const ContentLine *rule = observance.first(QLatin1StringView("RRULE"))) {
                const Result<RecurrenceRule> parsed = parseRecurrenceRule(rule->value);
                if (parsed.hasValue()) {
                    const QList<QDateTime> onsets = expandRecurrence(
                        *first, false, parsed.value(), {}, local.addDays(-onsetLookBackDays),
                        local.addSecs(1), QTimeZone(QTimeZone::UTC));
                    if (!onsets.isEmpty())
                        onset = std::max(onset, onsets.constLast());
                }
            }
            const QList<const ContentLine *> rdates = observance.all(QLatin1StringView("RDATE"));
            for (const ContentLine *rdate : rdates) {
                const QStringList values = rdate->value.split(u',', Qt::SkipEmptyParts);
                for (const QString &value : values) {
                    const std::optional<QDateTime> when = naiveTime(value);
                    if (when && *when <= local && (!onset.isValid() || *when > onset))
                        onset = *when;
                }
            }
            if (onset.isValid() && (!bestOnset.isValid() || onset > bestOnset)) {
                bestOnset = onset;
                bestOffset = offset;
            }
        }
        return bestOffset ? bestOffset : earliestFrom;
    }

    QHash<QString, Component> m_zones;
};

std::optional<qint64> triggerOffset(const ContentLine &line)
{
    if (line.parameter(QStringLiteral("VALUE")).toUpper() == QLatin1StringView("DATE-TIME"))
        return std::nullopt;
    return parseDurationSeconds(line.value);
}

QList<QDateTime> alarmsOf(const Component &component, const TimeResolver &times,
                          const QDateTime &start, const QDateTime &end)
{
    QList<QDateTime> alarms;
    for (const Component &child : component.children) {
        if (child.name != QLatin1StringView("VALARM"))
            continue;
        const ContentLine *trigger = child.first(QLatin1StringView("TRIGGER"));
        if (!trigger)
            continue;
        if (const std::optional<qint64> offset = triggerOffset(*trigger)) {
            const bool fromEnd =
                trigger->parameter(QStringLiteral("RELATED")).toUpper() == QLatin1StringView("END");
            const QDateTime base = fromEnd && end.isValid() ? end : start;
            if (base.isValid())
                alarms.append(base.addSecs(*offset));
        } else if (const std::optional<TimeValue> absolute = times.resolve(*trigger)) {
            alarms.append(absolute->utc);
        }
    }
    return alarms;
}

std::optional<CalendarEvent> readEvent(const Component &component, const TimeResolver &times)
{
    const ContentLine *startLine = component.first(QLatin1StringView("DTSTART"));
    const std::optional<TimeValue> start = startLine ? times.resolve(*startLine) : std::nullopt;
    if (!start)
        return std::nullopt;
    CalendarEvent event;
    event.allDay = start->dateOnly;
    event.start = start->utc;
    if (const ContentLine *line = component.first(QLatin1StringView("SUMMARY")))
        event.summary = unescaped(line->value);
    if (const ContentLine *line = component.first(QLatin1StringView("LOCATION")))
        event.location = unescaped(line->value);
    if (const ContentLine *line = component.first(QLatin1StringView("DESCRIPTION")))
        event.description = unescaped(line->value);
    const ContentLine *endLine = component.first(QLatin1StringView("DTEND"));
    const ContentLine *durationLine = component.first(QLatin1StringView("DURATION"));
    if (const std::optional<TimeValue> end = endLine ? times.resolve(*endLine) : std::nullopt) {
        event.end = end->utc;
    } else if (const std::optional<qint64> seconds =
                   durationLine ? parseDurationSeconds(durationLine->value) : std::nullopt) {
        event.end = event.start.addSecs(*seconds);
    } else {
        event.end = event.allDay ? event.start.addDays(1) : event.start;
    }
    if (event.end < event.start)
        event.end = event.start;
    if (const ContentLine *line = component.first(QLatin1StringView("RRULE")))
        event.rrule = line->value.trimmed();
    const QList<const ContentLine *> exdates = component.all(QLatin1StringView("EXDATE"));
    for (const ContentLine *line : exdates) {
        const QString tzid = line->parameter(QStringLiteral("TZID"));
        const bool valueDate =
            line->parameter(QStringLiteral("VALUE")).toUpper() == QLatin1StringView("DATE");
        const QStringList values = line->value.split(u',', Qt::SkipEmptyParts);
        for (const QString &value : values) {
            if (const std::optional<TimeValue> time = times.resolveText(value, tzid, valueDate))
                event.exdates.append(time->utc);
        }
    }
    if (const ContentLine *line = component.first(QLatin1StringView("CLASS"))) {
        const QString value = line->value.trimmed().toUpper();
        if (value == QLatin1StringView("PRIVATE"))
            event.visibility = EventVisibility::Private;
        else if (value == QLatin1StringView("CONFIDENTIAL"))
            event.visibility = EventVisibility::Confidential;
        else if (value == QLatin1StringView("PUBLIC"))
            event.visibility = EventVisibility::Public;
    }
    if (const ContentLine *line = component.first(QLatin1StringView("TRANSP"))) {
        if (line->value.trimmed().toUpper() == QLatin1StringView("TRANSPARENT"))
            event.availability = EventAvailability::Free;
    }
    if (const ContentLine *line = component.first(QLatin1StringView("STATUS"))) {
        if (line->value.trimmed().toUpper() == QLatin1StringView("TENTATIVE"))
            event.availability = EventAvailability::Tentative;
    }
    event.alarms = alarmsOf(component, times, event.start, event.end);
    return event;
}

CalendarTask readTask(const Component &component, const TimeResolver &times)
{
    CalendarTask task;
    if (const ContentLine *line = component.first(QLatin1StringView("SUMMARY")))
        task.summary = unescaped(line->value);
    if (const ContentLine *line = component.first(QLatin1StringView("DESCRIPTION")))
        task.description = unescaped(line->value);
    if (const ContentLine *line = component.first(QLatin1StringView("RRULE")))
        task.rrule = line->value.trimmed();
    QDateTime startTime;
    QDateTime dueTime;
    if (const ContentLine *line = component.first(QLatin1StringView("DTSTART"))) {
        if (const std::optional<TimeValue> value = times.resolve(*line)) {
            startTime = value->utc;
            task.start = value->dateOnly ? value->utc.date() : value->utc.toLocalTime().date();
        }
    }
    if (const ContentLine *line = component.first(QLatin1StringView("DUE"))) {
        if (const std::optional<TimeValue> value = times.resolve(*line)) {
            dueTime = value->utc;
            task.due = value->dateOnly ? value->utc.date() : value->utc.toLocalTime().date();
        }
    }
    if (const ContentLine *line = component.first(QLatin1StringView("PRIORITY"))) {
        const int value = line->value.trimmed().toInt();
        if (value >= 1 && value <= highPriorityLimit)
            task.priority = TaskPriority::High;
        else if (value > normalPriority)
            task.priority = TaskPriority::Low;
    }
    if (const ContentLine *line = component.first(QLatin1StringView("STATUS")))
        task.completed = line->value.trimmed().toUpper() == QLatin1StringView("COMPLETED");
    if (component.first(QLatin1StringView("COMPLETED")))
        task.completed = true;
    const QList<const ContentLine *> categoryLines = component.all(QLatin1StringView("CATEGORIES"));
    for (const ContentLine *line : categoryLines) {
        const QStringList names = splitTextList(line->value);
        for (const QString &name : names) {
            if (!name.trimmed().isEmpty() && !task.categories.contains(name.trimmed()))
                task.categories.append(name.trimmed());
        }
    }
    const QList<QDateTime> alarms =
        alarmsOf(component, times, startTime.isValid() ? startTime : dueTime, dueTime);
    if (!alarms.isEmpty())
        task.reminder = *std::min_element(alarms.cbegin(), alarms.cend());
    return task;
}

// Folds at 75 octets without cutting a UTF-8 sequence.
QByteArray folded(const QString &line)
{
    const QByteArray bytes = line.toUtf8();
    QByteArray out;
    qsizetype taken = 0;
    qsizetype limit = foldOctets;
    while (bytes.size() - taken > limit) {
        qsizetype cut = taken + limit;
        while (cut > taken && (static_cast<unsigned char>(bytes.at(cut)) & 0xC0) == 0x80)
            --cut;
        out += bytes.mid(taken, cut - taken) + "\r\n ";
        taken = cut;
        // The space that starts a continuation counts towards its 75 octets.
        limit = foldOctets - 1;
    }
    out += bytes.mid(taken) + "\r\n";
    return out;
}

QString utc(const QDateTime &time)
{
    return formatUtcStamp(time);
}

QString dateValue(const QDateTime &time)
{
    return formatDate(time.toUTC().date());
}

QString uidFor(const QString &kind, quint32 luid, const QString &summary, const QDateTime &start)
{
    static const QUuid space = QUuid::fromString(QStringLiteral("{6f0c4d2a-6b7e-4d5b-9a51-3a3c1f9e2b10}"));
    const QByteArray name = kind.toUtf8() + ':' + QByteArray::number(luid) + ':' +
                            summary.toUtf8() + ':' + formatUtcStamp(start).toLatin1();
    return QUuid::createUuidV5(space, name).toString(QUuid::WithoutBraces) +
           QStringLiteral("@qphonemanager");
}

void appendAlarm(QByteArray &out, const QDateTime &alarm)
{
    out += folded(QStringLiteral("BEGIN:VALARM"));
    out += folded(QStringLiteral("ACTION:DISPLAY"));
    out += folded(QStringLiteral("DESCRIPTION:Reminder"));
    out += folded(QStringLiteral("TRIGGER;VALUE=DATE-TIME:") + utc(alarm));
    out += folded(QStringLiteral("END:VALARM"));
}

} // namespace

Result<ICalendarContent> readICalendar(const QByteArray &data)
{
    if (data.size() > maxICalendarBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("iCalendar file larger than %1 bytes").arg(maxICalendarBytes));
    }
    const Result<Component> tree = buildTree(readContentLines(data));
    if (!tree.hasValue())
        return tree.error();

    QHash<QString, Component> zones;
    for (const Component &child : tree.value().children) {
        if (child.name != QLatin1StringView("VTIMEZONE"))
            continue;
        if (const ContentLine *id = child.first(QLatin1StringView("TZID")))
            zones.insert(id->value.trimmed(), child);
    }
    const TimeResolver times(zones);

    ICalendarContent content;
    for (const Component &child : tree.value().children) {
        if (content.events.size() + content.tasks.size() >= maxICalendarObjects) {
            return Error(ErrorCode::LimitExceeded,
                         QStringLiteral("iCalendar file with more than %1 entries")
                             .arg(maxICalendarObjects));
        }
        if (child.name == QLatin1StringView("VEVENT")) {
            if (std::optional<CalendarEvent> event = readEvent(child, times))
                content.events.append(*event);
            else
                ++content.skipped;
        } else if (child.name == QLatin1StringView("VTODO")) {
            content.tasks.append(readTask(child, times));
        }
    }
    return content;
}

QByteArray writeICalendar(const QList<CalendarEvent> &events, const QList<CalendarTask> &tasks)
{
    const QString stamp = utc(QDateTime::currentDateTimeUtc());
    QByteArray out;
    out += folded(QStringLiteral("BEGIN:VCALENDAR"));
    out += folded(QStringLiteral("VERSION:2.0"));
    out += folded(QStringLiteral("PRODID:-//QPhoneManager//QPhoneManager %1//EN")
                      .arg(qpm::applicationVersion()));
    out += folded(QStringLiteral("CALSCALE:GREGORIAN"));
    for (const CalendarEvent &event : events) {
        out += folded(QStringLiteral("BEGIN:VEVENT"));
        out += folded(QStringLiteral("UID:") +
                      uidFor(QStringLiteral("event"), event.luid, event.summary, event.start));
        out += folded(QStringLiteral("DTSTAMP:") + stamp);
        if (!event.summary.isEmpty())
            out += folded(QStringLiteral("SUMMARY:") + escaped(event.summary));
        if (!event.location.isEmpty())
            out += folded(QStringLiteral("LOCATION:") + escaped(event.location));
        if (!event.description.isEmpty())
            out += folded(QStringLiteral("DESCRIPTION:") + escaped(event.description));
        QDateTime end = event.end;
        if (event.allDay) {
            if (!end.isValid() || end <= event.start)
                end = event.start.addDays(1);
            out += folded(QStringLiteral("DTSTART;VALUE=DATE:") + dateValue(event.start));
            out += folded(QStringLiteral("DTEND;VALUE=DATE:") + dateValue(end));
        } else {
            if (!end.isValid() || end < event.start)
                end = event.start;
            out += folded(QStringLiteral("DTSTART:") + utc(event.start));
            out += folded(QStringLiteral("DTEND:") + utc(end));
        }
        if (!event.rrule.isEmpty())
            out += folded(QStringLiteral("RRULE:") + event.rrule);
        if (!event.exdates.isEmpty()) {
            QStringList values;
            for (const QDateTime &exdate : event.exdates)
                values.append(event.allDay ? dateValue(exdate) : utc(exdate));
            out += folded((event.allDay ? QStringLiteral("EXDATE;VALUE=DATE:")
                                        : QStringLiteral("EXDATE:")) +
                          values.join(u','));
        }
        switch (event.visibility) {
        case EventVisibility::Public:
            out += folded(QStringLiteral("CLASS:PUBLIC"));
            break;
        case EventVisibility::Private:
            out += folded(QStringLiteral("CLASS:PRIVATE"));
            break;
        case EventVisibility::Confidential:
            out += folded(QStringLiteral("CLASS:CONFIDENTIAL"));
            break;
        case EventVisibility::Default:
            break;
        }
        out += folded(event.availability == EventAvailability::Free
                          ? QStringLiteral("TRANSP:TRANSPARENT")
                          : QStringLiteral("TRANSP:OPAQUE"));
        if (event.availability == EventAvailability::Tentative)
            out += folded(QStringLiteral("STATUS:TENTATIVE"));
        for (const QDateTime &alarm : event.alarms)
            appendAlarm(out, alarm);
        out += folded(QStringLiteral("END:VEVENT"));
    }
    for (const CalendarTask &task : tasks) {
        out += folded(QStringLiteral("BEGIN:VTODO"));
        const QDateTime anchor =
            task.due.isValid() ? QDateTime(task.due, QTime(0, 0), QTimeZone::UTC) : QDateTime();
        out += folded(QStringLiteral("UID:") +
                      uidFor(QStringLiteral("task"), task.luid, task.summary, anchor));
        out += folded(QStringLiteral("DTSTAMP:") + stamp);
        if (!task.summary.isEmpty())
            out += folded(QStringLiteral("SUMMARY:") + escaped(task.summary));
        if (!task.description.isEmpty())
            out += folded(QStringLiteral("DESCRIPTION:") + escaped(task.description));
        if (task.start.isValid())
            out += folded(QStringLiteral("DTSTART;VALUE=DATE:") + formatDate(task.start));
        if (task.due.isValid())
            out += folded(QStringLiteral("DUE;VALUE=DATE:") + formatDate(task.due));
        if (!task.rrule.isEmpty())
            out += folded(QStringLiteral("RRULE:") + task.rrule);
        const int priority = task.priority == TaskPriority::High  ? 1
                             : task.priority == TaskPriority::Low ? lowPriority
                                                                  : normalPriority;
        out += folded(QStringLiteral("PRIORITY:%1").arg(priority));
        out += folded(task.completed ? QStringLiteral("STATUS:COMPLETED")
                                     : QStringLiteral("STATUS:NEEDS-ACTION"));
        if (!task.categories.isEmpty()) {
            QStringList names;
            for (const QString &name : task.categories)
                names.append(escaped(name));
            out += folded(QStringLiteral("CATEGORIES:") + names.join(u','));
        }
        if (task.lastModified.isValid())
            out += folded(QStringLiteral("LAST-MODIFIED:") + utc(task.lastModified));
        if (task.reminder.isValid())
            appendAlarm(out, task.reminder);
        out += folded(QStringLiteral("END:VTODO"));
    }
    out += folded(QStringLiteral("END:VCALENDAR"));
    return out;
}

} // namespace qpm::core::codec
