#pragma once

#include "core/Error.h"
#include "core/Holidays.h"
#include "core/codec/PhoneVCal.h"

#include <QCoreApplication>
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTime>

#include <optional>

namespace qpm::ui {

/**
 * Texts, choices and the reading of dates and times that the calendar, the
 * tasks and the notes share, in the language of the program.
 */
class OrganizerText
{
    Q_DECLARE_TR_FUNCTIONS(OrganizerText)

public:
    /// Which part of the organizer a failure belongs to, for its message.
    enum class Area { Calendar, Tasks, Notes, Alarms };

    /// The short date of the locale with a four-digit year.
    [[nodiscard]] static QString formatDate(const QDate &date);
    /**
     * Reads a date in the short or long form of the locale or as ISO 8601.
     * A year written with two digits is one of 2000 to 2099. Invalid for
     * anything else.
     */
    [[nodiscard]] static QDate parseDate(const QString &text);
    [[nodiscard]] static QString formatTime(const QTime &time);
    /// Reads "9:30", "09:30" or the short time of the locale; invalid for anything else.
    [[nodiscard]] static QTime parseTime(const QString &text);
    /// A date written the way parseDate() takes it, for hints.
    [[nodiscard]] static QString dateExample();
    /// A time written the way parseTime() takes it, for hints.
    [[nodiscard]] static QString timeExample();
    /// The form of a date in the locale with letters, such as "DD/MM/YYYY", for an empty field.
    [[nodiscard]] static QString dateHint();
    /// The form of a time with letters, such as "hh:mm", for an empty field.
    [[nodiscard]] static QString timeHint();

    /// What went wrong and what to do; empty for a cancelled operation.
    [[nodiscard]] static QString errorText(const core::Error &error, Area area);

    [[nodiscard]] static QString holidayName(core::HolidayKind kind);

    /// "Every week on Tuesday" and the like; empty for no rule.
    [[nodiscard]] static QString repeatText(const QString &rrule);

    /// The repetitions the forms offer, in the order of repeatRule().
    [[nodiscard]] static QStringList repeatChoices();
    /// The rule for entry `index` of repeatChoices(); empty for none.
    [[nodiscard]] static QString repeatRule(int index);
    /// The entry of repeatChoices() a rule matches, or nothing for a rule of its own.
    [[nodiscard]] static std::optional<int> repeatIndex(const QString &rrule);

    /// Minutes before the start that the forms offer for a reminder.
    [[nodiscard]] static QList<int> reminderOffsets();
    /// "15 minutes before" and the like, for each of reminderOffsets().
    [[nodiscard]] static QStringList reminderChoices();
    /// The text for a reminder `minutes` before the start; a negative value lies after it.
    [[nodiscard]] static QString reminderText(qint64 minutes);

    [[nodiscard]] static QStringList visibilityChoices();
    [[nodiscard]] static QStringList availabilityChoices();
    [[nodiscard]] static QStringList priorityChoices();

    /// The colours an event can take, "colour of the calendar" first.
    [[nodiscard]] static QStringList colorChoices();
    /// RGB for entry `index` of colorChoices(); nothing for the colour of the calendar.
    [[nodiscard]] static std::optional<quint32> colorValue(int index);
    /// The entry of colorChoices() for an event colour; the nearest one for an unknown colour.
    [[nodiscard]] static int colorIndex(const std::optional<quint32> &argb);

    /// "Monday" to "Sunday" in the order of the locale's week, as written in a list.
    [[nodiscard]] static QString weekdayList(const QList<int> &days);
};

} // namespace qpm::ui
