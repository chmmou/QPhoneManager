#include "ui/organizer/CalendarDaysModel.h"

#include "ui/organizer/OrganizerText.h"

#include <QLocale>

#include <algorithm>
#include <utility>

namespace qpm::ui {

namespace {

constexpr qsizetype maxDays = 7 * 6;

QString hexColor(quint32 argb)
{
    return QStringLiteral("#%1").arg(argb & 0x00FFFFFFU, 6, 16, QLatin1Char('0'));
}

} // namespace

CalendarDaysModel::CalendarDaysModel(QObject *parent) : QAbstractListModel(parent) {}

int CalendarDaysModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_days.size());
}

QVariant CalendarDaysModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Day &day = m_days.at(index.row());
    switch (role) {
    case DateRole:
        return day.date;
    case Qt::DisplayRole:
    case DayNumberRole:
        return day.date.day();
    case InPeriodRole:
        return day.inPeriod;
    case TodayRole:
        return day.date == QDate::currentDate();
    case SelectedRole:
        return day.date == m_selected;
    case HolidayRole:
        return day.holiday;
    case EntriesRole:
        return day.entries;
    case EntryColorsRole:
        return day.colors;
    case MoreCountRole:
        return std::max(0, day.total - int(day.entries.size()));
    case AccessibleTextRole: {
        QString text = QLocale().toString(day.date, QLocale::LongFormat);
        if (!day.holiday.isEmpty())
            text = tr("%1, %2").arg(text, day.holiday);
        return tr("%1, %n appointment(s)", nullptr, day.total).arg(text);
    }
    case WeekdayRole:
        return QLocale().dayName(day.date.dayOfWeek(), QLocale::ShortFormat);
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> CalendarDaysModel::roleNames() const
{
    return {
        {DateRole, QByteArrayLiteral("date")},
        {DayNumberRole, QByteArrayLiteral("dayNumber")},
        {InPeriodRole, QByteArrayLiteral("inPeriod")},
        {TodayRole, QByteArrayLiteral("today")},
        {SelectedRole, QByteArrayLiteral("selected")},
        {HolidayRole, QByteArrayLiteral("holiday")},
        {EntriesRole, QByteArrayLiteral("entries")},
        {EntryColorsRole, QByteArrayLiteral("entryColors")},
        {MoreCountRole, QByteArrayLiteral("moreCount")},
        {AccessibleTextRole, QByteArrayLiteral("accessibleText")},
        {WeekdayRole, QByteArrayLiteral("weekday")},
    };
}

void CalendarDaysModel::setDays(const QDate &first, const QDate &last, const QDate &periodMonth,
                                const QList<Occurrence> &occurrences)
{
    const int before = int(m_days.size());
    beginResetModel();
    m_days.clear();
    for (QDate date = first; date.isValid() && date <= last && m_days.size() < maxDays;
         date = date.addDays(1)) {
        Day day;
        day.date = date;
        day.inPeriod = !periodMonth.isValid() || (date.month() == periodMonth.month() &&
                                                  date.year() == periodMonth.year());
        m_days.append(day);
    }
    for (const Occurrence &entry : occurrences) {
        const auto [from, to] = CalendarModel::daySpan(entry);
        for (Day &day : m_days) {
            if (day.date < from || day.date > to)
                continue;
            if (entry.holiday) {
                day.holiday = entry.summary;
                continue;
            }
            ++day.total;
            if (day.entries.size() >= entriesPerDay)
                continue;
            const QString title = entry.summary.isEmpty() ? tr("(No title)") : entry.summary;
            day.entries.append(entry.allDay || day.date != entry.start.toLocalTime().date()
                                   ? title
                                   : tr("%1 %2").arg(
                                         OrganizerText::formatTime(entry.start.toLocalTime().time()),
                                         title));
            day.colors.append(hexColor(entry.color));
        }
    }
    endResetModel();
    if (before != m_days.size())
        Q_EMIT countChanged();
}

void CalendarDaysModel::setSelectedDate(const QDate &date)
{
    if (date == m_selected)
        return;
    const int oldRow = rowOf(m_selected);
    m_selected = date;
    const int newRow = rowOf(m_selected);
    for (const int row : {oldRow, newRow}) {
        if (row >= 0)
            Q_EMIT dataChanged(index(row), index(row), {SelectedRole});
    }
    Q_EMIT selectedDateChanged();
}

int CalendarDaysModel::rowOf(const QDate &date) const
{
    for (qsizetype row = 0; row < m_days.size(); ++row) {
        if (m_days.at(row).date == date)
            return int(row);
    }
    return -1;
}

QDate CalendarDaysModel::dateAt(int row) const
{
    return row >= 0 && row < m_days.size() ? m_days.at(row).date : QDate();
}

} // namespace qpm::ui
