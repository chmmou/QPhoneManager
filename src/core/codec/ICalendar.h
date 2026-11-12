#pragma once

#include "core/Error.h"
#include "core/codec/PhoneVCal.h"

#include <QByteArray>
#include <QList>

namespace qpm::core::codec {

/// Largest iCalendar file read.
inline constexpr qsizetype maxICalendarBytes = 32 * 1024 * 1024;
/// Most events and tasks read from one file.
inline constexpr qsizetype maxICalendarObjects = 20000;

/// What an iCalendar file holds that the phone can take.
struct ICalendarContent
{
    QList<CalendarEvent> events;
    QList<CalendarTask> tasks;
    /// Components left out because they lack a start or a readable time.
    int skipped = 0;
};

/**
 * Reads an iCalendar 2.0 file: folded lines, escaped text, times in UTC, as
 * floating local time or with a TZID. A TZID is looked up as an IANA zone,
 * then as a Windows zone name, then in the VTIMEZONE of the file. Reminders
 * of VALARM become absolute times. The calendar of an event is left empty.
 * Fails with ErrorCode::LimitExceeded above maxICalendarBytes or
 * maxICalendarObjects and with ErrorCode::InvalidInput when there is no
 * VCALENDAR.
 */
[[nodiscard]] Result<ICalendarContent> readICalendar(const QByteArray &data);

/**
 * Writes an iCalendar 2.0 file with every time in UTC and all-day values as
 * dates, a reminder as an absolute VALARM trigger, lines folded at 75
 * octets.
 */
[[nodiscard]] QByteArray writeICalendar(const QList<CalendarEvent> &events,
                                        const QList<CalendarTask> &tasks);

} // namespace qpm::core::codec
