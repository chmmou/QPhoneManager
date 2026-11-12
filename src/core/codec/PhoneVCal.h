#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace qpm::core::codec {

/// Who may see an event, the CLASS of the phone. Default leaves it to the calendar.
enum class EventVisibility { Default, Confidential, Private, Public };

/// How an event shows in a free and busy view, the TRANSP of the phone.
enum class EventAvailability { Busy, Free, Tentative };

/// The three priorities the task app of the phone knows.
enum class TaskPriority { High, Normal, Low };

/// An appointment as the phone client sends and takes it.
struct CalendarEvent
{
    /// Bit 0x10000000 set on the phone; 0 for an event not created yet.
    quint32 luid = 0;
    QString summary;
    QString location;
    QString description;
    bool allDay = false;
    /// UTC. For an all-day event midnight UTC of the first day.
    QDateTime start;
    /// UTC and exclusive. For an all-day event midnight UTC of the day after the last.
    QDateTime end;
    /// RFC 5545 rule without the "RRULE:" prefix; empty for a single event.
    QString rrule;
    /// Starts left out of the rule, in the form of `start`.
    QList<QDateTime> exdates;
    /// Reminders as absolute times in UTC.
    QList<QDateTime> alarms;
    EventVisibility visibility = EventVisibility::Default;
    EventAvailability availability = EventAvailability::Busy;
    /// ARGB; nothing follows the colour of the calendar.
    std::optional<quint32> color;
    /// "<account>@<type>/<sync id>" of the calendar, as metadata.xml names it.
    QString calendarUrl;
    QString calendarTitle;

    bool operator==(const CalendarEvent &other) const = default;
};

/// A task of the task app that comes with the phone client.
struct CalendarTask
{
    /// A small number on the phone; 0 for a task not created yet.
    quint32 luid = 0;
    QString summary;
    QString description;
    QDate start;
    QDate due;
    /// UTC; invalid without a reminder.
    QDateTime reminder;
    QString rrule;
    TaskPriority priority = TaskPriority::Normal;
    bool completed = false;
    /// Category names in their order on the phone.
    QStringList categories;
    /// UTC; set by the phone, never sent.
    QDateTime lastModified;

    bool operator==(const CalendarTask &other) const = default;
};

/// True when `data` holds a task (VTODO) rather than an event.
[[nodiscard]] bool isTaskObject(const QByteArray &data);

/**
 * Reads one VEVENT in the dialect of the phone client: unfolded lines, values
 * quoted-printable where they hold control characters, UTC times or dates,
 * DURATION in place of DTEND for repeating events. The LUID comes from
 * X-IRMC-LUID. Fails with ErrorCode::ProtocolError when there is no VEVENT or
 * no usable start.
 */
[[nodiscard]] Result<CalendarEvent> parsePhoneEvent(const QByteArray &data);

/// Reads one VTODO like parsePhoneEvent(); fails with ErrorCode::ProtocolError without one.
[[nodiscard]] Result<CalendarTask> parsePhoneTask(const QByteArray &data);

/**
 * The text the phone takes to create or replace `event`: every field, since
 * the phone clears what is left out, with X-CALENDAR naming the target
 * calendar. Without a colour, `clearColor` writes "X-COLOR:DEFAULT" so that
 * an earlier colour goes. Lines are never folded.
 */
[[nodiscard]] QByteArray encodePhoneEvent(const CalendarEvent &event, bool clearColor);

/**
 * The text the phone takes to create or replace `task`. `categoryColors`
 * holds an RGB colour per entry of `task.categories`, or nothing; the phone
 * gives a category it does not know yet that colour.
 */
[[nodiscard]] QByteArray encodePhoneTask(const CalendarTask &task,
                                         const QList<std::optional<quint32>> &categoryColors);

/**
 * The event as the phone sends it back, X-IRMC-LUID included;
 * parsePhoneEvent() reads it back unchanged. For backups, tests and the fake
 * phone.
 */
[[nodiscard]] QByteArray encodeEventAsPhoneSendsIt(const CalendarEvent &event);

/**
 * The task as the phone sends it back, X-IRMC-LUID included; parsePhoneTask()
 * reads it back unchanged. For backups, tests and the fake phone.
 */
[[nodiscard]] QByteArray encodeTaskAsPhoneSendsIt(const CalendarTask &task);

/**
 * True when the phone would show both events the same way: every field but
 * the LUID and the calendar, which the phone does not change on update.
 */
[[nodiscard]] bool sameEventContent(const CalendarEvent &a, const CalendarEvent &b);

/// True when both tasks hold the same fields; the LUID and the time of the last change do not count.
[[nodiscard]] bool sameTaskContent(const CalendarTask &a, const CalendarTask &b);

/**
 * Reads an ISO 8601 or RFC 5545 duration such as "PT1H", "P1D" or "P3600S",
 * the latter as the phone writes it. A "M" before any "T" counts as minutes,
 * as the phone reads it. Returns nothing for anything else.
 */
[[nodiscard]] std::optional<qint64> parseDurationSeconds(const QString &text);

} // namespace qpm::core::codec
