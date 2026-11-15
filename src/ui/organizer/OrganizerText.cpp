#include "ui/organizer/OrganizerText.h"

#include "core/codec/Recurrence.h"

#include <QLocale>

#include <array>
#include <limits>

namespace qpm::ui {

namespace {

using core::ErrorCode;

constexpr int twoDigitBaseYear = 2000;
constexpr int centuryYears = 100;
constexpr int minutesPerHour = 60;
constexpr int minutesPerDay = 24 * 60;
constexpr int exampleHour = 9;
constexpr int exampleMinute = 30;

struct NamedColor
{
    const char *name;
    quint32 rgb;
};

// Marked for the catalogues here, translated where they are shown.
constexpr std::array<NamedColor, 8> eventColors = {{
    {QT_TRANSLATE_NOOP("OrganizerText", "Red"), 0xD50000},
    {QT_TRANSLATE_NOOP("OrganizerText", "Orange"), 0xF4511E},
    {QT_TRANSLATE_NOOP("OrganizerText", "Yellow"), 0xF6BF26},
    {QT_TRANSLATE_NOOP("OrganizerText", "Green"), 0x0B8043},
    {QT_TRANSLATE_NOOP("OrganizerText", "Teal"), 0x039BE5},
    {QT_TRANSLATE_NOOP("OrganizerText", "Blue"), 0x3F51B5},
    {QT_TRANSLATE_NOOP("OrganizerText", "Purple"), 0x8E24AA},
    {QT_TRANSLATE_NOOP("OrganizerText", "Grey"), 0x616161},
}};

QString longYearDateFormat()
{
    QString format = QLocale().dateFormat(QLocale::ShortFormat);
    if (!format.contains(QLatin1StringView("yyyy")))
        format.replace(QLatin1StringView("yy"), QLatin1StringView("yyyy"));
    return format;
}

int channelDistance(quint32 a, quint32 b)
{
    int sum = 0;
    for (const int shift : {16, 8, 0}) {
        const int delta = int((a >> shift) & 0xFF) - int((b >> shift) & 0xFF);
        sum += delta * delta;
    }
    return sum;
}

} // namespace

QString OrganizerText::formatDate(const QDate &date)
{
    return date.isValid() ? QLocale().toString(date, longYearDateFormat()) : QString();
}

QDate OrganizerText::parseDate(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QLocale locale;
    QDate date = locale.toDate(trimmed, longYearDateFormat(), twoDigitBaseYear);
    if (!date.isValid())
        date = locale.toDate(trimmed, QLocale::ShortFormat, twoDigitBaseYear);
    if (!date.isValid()) {
        QString shortYear = longYearDateFormat();
        shortYear.replace(QLatin1StringView("yyyy"), QLatin1StringView("yy"));
        date = locale.toDate(trimmed, shortYear, twoDigitBaseYear);
    }
    if (!date.isValid())
        date = locale.toDate(trimmed, QLocale::LongFormat, twoDigitBaseYear);
    if (!date.isValid())
        date = QDate::fromString(trimmed, Qt::ISODate);
    // A four-digit pattern also takes "26" and reads it as the year 26.
    if (date.isValid() && date.year() < centuryYears)
        date = date.addYears(twoDigitBaseYear);
    return date;
}

QString OrganizerText::formatTime(const QTime &time)
{
    return time.isValid() ? QLocale().toString(time, QLocale::ShortFormat) : QString();
}

QTime OrganizerText::parseTime(const QString &text)
{
    const QString trimmed = text.trimmed();
    QTime time = QLocale().toTime(trimmed, QLocale::ShortFormat);
    if (!time.isValid())
        time = QTime::fromString(trimmed, QStringLiteral("H:mm"));
    if (!time.isValid())
        time = QTime::fromString(trimmed, QStringLiteral("H.mm"));
    return time;
}

QString OrganizerText::dateExample()
{
    return formatDate(QDate::currentDate());
}

QString OrganizerText::timeExample()
{
    return formatTime(QTime(exampleHour, exampleMinute));
}

QString OrganizerText::dateHint()
{
    const QString format = longYearDateFormat();
    QString hint;
    qsizetype i = 0;
    while (i < format.size()) {
        const QChar c = format.at(i);
        if (c == u'\'') {
            const qsizetype close = format.indexOf(u'\'', i + 1);
            const qsizetype end = close < 0 ? format.size() : close;
            hint += format.mid(i + 1, end - i - 1);
            i = end + 1;
            continue;
        }
        qsizetype run = 1;
        while (i + run < format.size() && format.at(i + run) == c)
            ++run;
        if (c == u'd')
            hint += tr("DD", "letters for the day in the form of a date");
        else if (c == u'M')
            hint += tr("MM", "letters for the month in the form of a date");
        else if (c == u'y')
            hint += tr("YYYY", "letters for the year in the form of a date");
        else
            hint += format.mid(i, run);
        i += run;
    }
    return hint;
}

QString OrganizerText::timeHint()
{
    return tr("hh:mm", "letters for hours and minutes in the form of a time");
}

QString OrganizerText::errorText(const core::Error &error, Area area)
{
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::ConnectionClosed:
    case ErrorCode::Timeout:
        return tr("The phone is not connected. Connect it, then try again.");
    case ErrorCode::PermissionDenied:
    case ErrorCode::AccessDenied:
        if (area == Area::Alarms)
            return tr("The app on the phone may not reach the alarms. Allow it in the Android "
                      "settings.");
        return tr("The app on the phone may not access the calendar. Allow it in the Android "
                  "settings.");
    case ErrorCode::NotFound:
        switch (area) {
        case Area::Calendar:
            return tr("This appointment is no longer on the phone. Sync to update the calendar.");
        case Area::Tasks:
            return tr("This task is no longer on the phone. Sync to update the list.");
        case Area::Notes:
            return tr("This note is no longer on the phone. Sync to update the list.");
        case Area::Alarms:
            break;
        }
        return tr("This alarm is no longer on the phone. Load the alarms again.");
    case ErrorCode::InvalidInput:
        switch (area) {
        case Area::Calendar:
            return tr("Enter a title and a start, and choose a calendar the phone can write to.");
        case Area::Tasks:
            return tr("Enter a title for the task.");
        case Area::Notes:
            return tr("Write some text for the note.");
        case Area::Alarms:
            break;
        }
        return tr("Enter a time like %1.").arg(timeExample());
    case ErrorCode::IoFailure:
        return tr("The data cannot be saved on this computer. Check the storage folder in the "
                  "settings.");
    case ErrorCode::NotSupported:
        if (area == Area::Alarms)
            return tr("This phone does not let the app reach its alarms.");
        return tr("The phone does not support this. Update MyPhoneExplorer Client on the phone.");
    case ErrorCode::LimitExceeded:
        return tr("The entry is too large for the phone. Shorten the text.");
    case ErrorCode::WrongPin:
    case ErrorCode::AlreadyConnected:
    case ErrorCode::BluetoothServiceNotFound:
    case ErrorCode::WifiDisabled:
    case ErrorCode::Rejected:
    case ErrorCode::ProtocolError:
        break;
    }
    return tr("The phone did not take the change. Sync, then try again.");
}

QString OrganizerText::holidayName(core::HolidayKind kind)
{
    using core::HolidayKind;
    switch (kind) {
    case HolidayKind::NewYear:
        return tr("New Year's Day");
    case HolidayKind::Epiphany:
        return tr("Epiphany");
    case HolidayKind::BerchtoldsDay:
        return tr("Berchtold's Day");
    case HolidayKind::GoodFriday:
        return tr("Good Friday");
    case HolidayKind::EasterMonday:
        return tr("Easter Monday");
    case HolidayKind::LabourDay:
        return tr("Labour Day");
    case HolidayKind::Ascension:
        return tr("Ascension Day");
    case HolidayKind::WhitMonday:
        return tr("Whit Monday");
    case HolidayKind::CorpusChristi:
        return tr("Corpus Christi");
    case HolidayKind::AssumptionDay:
        return tr("Assumption Day");
    case HolidayKind::SwissNationalDay:
        return tr("Swiss National Day");
    case HolidayKind::GermanUnityDay:
        return tr("Day of German Unity");
    case HolidayKind::AustrianNationalDay:
        return tr("Austrian National Day");
    case HolidayKind::AllSaints:
        return tr("All Saints' Day");
    case HolidayKind::ImmaculateConception:
        return tr("Immaculate Conception");
    case HolidayKind::ChristmasDay:
        return tr("Christmas Day");
    case HolidayKind::StStephensDay:
        break;
    }
    return tr("St Stephen's Day");
}

QString OrganizerText::repeatText(const QString &rrule)
{
    if (rrule.trimmed().isEmpty())
        return {};
    const core::Result<core::codec::RecurrenceRule> parsed = core::codec::parseRecurrenceRule(rrule);
    if (!parsed.hasValue())
        return tr("Repeats by a rule of its own");
    const core::codec::RecurrenceRule &rule = parsed.value();
    QString base;
    switch (rule.frequency) {
    case core::codec::RecurrenceFrequency::Daily:
        base = tr("Every %n day(s)", nullptr, rule.interval);
        break;
    case core::codec::RecurrenceFrequency::Weekly:
        if (rule.byDay.isEmpty()) {
            base = tr("Every %n week(s)", nullptr, rule.interval);
        } else {
            QList<int> days;
            for (const core::codec::WeekdayNumber &day : rule.byDay)
                days.append(int(day.day));
            base = tr("Every %n week(s) on %1", nullptr, rule.interval).arg(weekdayList(days));
        }
        break;
    case core::codec::RecurrenceFrequency::Monthly:
        base = tr("Every %n month(s)", nullptr, rule.interval);
        break;
    case core::codec::RecurrenceFrequency::Yearly:
        base = tr("Every %n year(s)", nullptr, rule.interval);
        break;
    }
    if (rule.count)
        return tr("%1, %n time(s) in all", nullptr, *rule.count).arg(base);
    if (rule.until)
        return tr("%1, until %2").arg(base, formatDate(rule.until->toLocalTime().date()));
    return base;
}

QStringList OrganizerText::repeatChoices()
{
    return {tr("Does not repeat"), tr("Every day"), tr("Every week"), tr("Every month"),
            tr("Every year")};
}

QString OrganizerText::repeatRule(int index)
{
    switch (index) {
    case 1:
        return QStringLiteral("FREQ=DAILY");
    case 2:
        return QStringLiteral("FREQ=WEEKLY");
    case 3:
        return QStringLiteral("FREQ=MONTHLY");
    case 4:
        return QStringLiteral("FREQ=YEARLY");
    default:
        break;
    }
    return {};
}

std::optional<int> OrganizerText::repeatIndex(const QString &rrule)
{
    if (rrule.trimmed().isEmpty())
        return 0;
    const core::Result<core::codec::RecurrenceRule> parsed = core::codec::parseRecurrenceRule(rrule);
    if (!parsed.hasValue())
        return std::nullopt;
    const core::codec::RecurrenceRule &rule = parsed.value();
    const bool simple = rule.interval == 1 && !rule.count && !rule.until && rule.byDay.isEmpty() &&
                        rule.byMonthDay.isEmpty() && rule.byMonth.isEmpty() &&
                        rule.bySetPos.isEmpty();
    if (!simple)
        return std::nullopt;
    switch (rule.frequency) {
    case core::codec::RecurrenceFrequency::Daily:
        return 1;
    case core::codec::RecurrenceFrequency::Weekly:
        return 2;
    case core::codec::RecurrenceFrequency::Monthly:
        return 3;
    case core::codec::RecurrenceFrequency::Yearly:
        break;
    }
    return 4;
}

QList<int> OrganizerText::reminderOffsets()
{
    return {0, 5, 10, 15, 30, 60, 120, minutesPerDay, 2 * minutesPerDay, 7 * minutesPerDay};
}

QStringList OrganizerText::reminderChoices()
{
    QStringList choices;
    const QList<int> offsets = reminderOffsets();
    for (const int minutes : offsets)
        choices.append(reminderText(minutes));
    return choices;
}

QString OrganizerText::reminderText(qint64 minutes)
{
    if (minutes == 0)
        return tr("At the start");
    if (minutes < 0) {
        const int after = int(std::min<qint64>(-minutes, std::numeric_limits<int>::max()));
        return tr("%n minute(s) after the start", nullptr, after);
    }
    const int value = int(std::min<qint64>(minutes, std::numeric_limits<int>::max()));
    if (value % minutesPerDay == 0)
        return tr("%n day(s) before", nullptr, value / minutesPerDay);
    if (value % minutesPerHour == 0)
        return tr("%n hour(s) before", nullptr, value / minutesPerHour);
    return tr("%n minute(s) before", nullptr, value);
}

QStringList OrganizerText::visibilityChoices()
{
    return {tr("Like the calendar"), tr("Confidential"), tr("Private"), tr("Public")};
}

QStringList OrganizerText::availabilityChoices()
{
    return {tr("Busy"), tr("Free"), tr("Tentative")};
}

QStringList OrganizerText::priorityChoices()
{
    return {tr("High"), tr("Normal"), tr("Low")};
}

QStringList OrganizerText::colorChoices()
{
    QStringList choices{tr("Colour of the calendar")};
    for (const NamedColor &color : eventColors)
        choices.append(QCoreApplication::translate("OrganizerText", color.name));
    return choices;
}

std::optional<quint32> OrganizerText::colorValue(int index)
{
    if (index < 1 || index > int(eventColors.size()))
        return std::nullopt;
    return eventColors.at(index - 1).rgb;
}

int OrganizerText::colorIndex(const std::optional<quint32> &argb)
{
    if (!argb)
        return 0;
    const quint32 rgb = *argb & 0x00FFFFFFU;
    int best = 1;
    int bestDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < int(eventColors.size()); ++i) {
        const int distance = channelDistance(rgb, eventColors.at(i).rgb);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i + 1;
        }
    }
    return best;
}

QString OrganizerText::weekdayList(const QList<int> &days)
{
    const QLocale locale;
    QStringList names;
    for (int offset = 0; offset < 7; ++offset) {
        const int day = (int(locale.firstDayOfWeek()) - 1 + offset) % 7 + 1;
        if (days.contains(day))
            names.append(locale.dayName(day, QLocale::LongFormat));
    }
    return names.join(QLatin1StringView(", "));
}

} // namespace qpm::ui
