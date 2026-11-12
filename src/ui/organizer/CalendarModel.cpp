#include "ui/organizer/CalendarModel.h"

#include "ui/organizer/OrganizerText.h"

#include <QColor>
#include <QLocale>

#include <algorithm>

namespace qpm::ui {

CalendarModel::CalendarModel(QObject *parent) : QAbstractListModel(parent) {}

int CalendarModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_shown.size());
}

QVariant CalendarModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Occurrence &entry = m_shown.at(index.row());
    switch (role) {
    case LuidRole:
        return int(entry.luid);
    case Qt::DisplayRole:
    case SummaryRole:
        return entry.summary;
    case LocationRole:
        return entry.location;
    case StartRole:
        return entry.allDay ? QDateTime(entry.start.toUTC().date(), QTime(0, 0))
                            : entry.start.toLocalTime();
    case EndRole:
        return entry.allDay ? QDateTime(entry.end.toUTC().date(), QTime(0, 0))
                            : entry.end.toLocalTime();
    case AllDayRole:
        return entry.allDay;
    case ColorRole:
        return QColor::fromRgb(entry.color | 0xFF000000U);
    case CalendarRole:
        return entry.calendarTitle;
    case TimeTextRole:
        return timeText(entry);
    case DayTextRole: {
        const QDate day = entry.allDay ? entry.start.toUTC().date() : entry.start.toLocalTime().date();
        return QLocale().toString(day, QLocale::LongFormat);
    }
    case RecurringRole:
        return entry.recurring;
    case HolidayRole:
        return entry.holiday;
    case OccurrenceStartRole:
        return entry.start.toMSecsSinceEpoch();
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> CalendarModel::roleNames() const
{
    return {
        {LuidRole, QByteArrayLiteral("luid")},
        {SummaryRole, QByteArrayLiteral("summary")},
        {LocationRole, QByteArrayLiteral("location")},
        {StartRole, QByteArrayLiteral("start")},
        {EndRole, QByteArrayLiteral("end")},
        {AllDayRole, QByteArrayLiteral("allDay")},
        {ColorRole, QByteArrayLiteral("entryColor")},
        {CalendarRole, QByteArrayLiteral("calendar")},
        {TimeTextRole, QByteArrayLiteral("timeText")},
        {DayTextRole, QByteArrayLiteral("dayText")},
        {RecurringRole, QByteArrayLiteral("recurring")},
        {HolidayRole, QByteArrayLiteral("holiday")},
        {OccurrenceStartRole, QByteArrayLiteral("occurrenceStart")},
    };
}

void CalendarModel::setOccurrences(QList<Occurrence> occurrences)
{
    const auto dayOf = [](const Occurrence &entry) {
        return entry.allDay ? entry.start.toUTC().date() : entry.start.toLocalTime().date();
    };
    std::stable_sort(occurrences.begin(), occurrences.end(),
                     [&dayOf](const Occurrence &a, const Occurrence &b) {
                         const QDate dayA = dayOf(a);
                         const QDate dayB = dayOf(b);
                         if (dayA != dayB)
                             return dayA < dayB;
                         if (a.allDay != b.allDay)
                             return a.allDay;
                         return a.start < b.start;
                     });
    m_all = std::move(occurrences);
    refilter();
}

void CalendarModel::setSearchText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_search)
        return;
    m_search = trimmed;
    refilter();
}

QString CalendarModel::timeText(const Occurrence &occurrence)
{
    if (occurrence.allDay) {
        const QDate first = occurrence.start.toUTC().date();
        const QDate last = occurrence.end.toUTC().date().addDays(-1);
        if (last <= first)
            return tr("All day");
        return tr("All day until %1").arg(OrganizerText::formatDate(last));
    }
    const QDateTime start = occurrence.start.toLocalTime();
    const QDateTime end = occurrence.end.toLocalTime();
    if (end <= start)
        return OrganizerText::formatTime(start.time());
    if (end.date() != start.date()) {
        return tr("%1 to %2, %3")
            .arg(OrganizerText::formatTime(start.time()), OrganizerText::formatDate(end.date()),
                 OrganizerText::formatTime(end.time()));
    }
    return tr("%1 to %2").arg(OrganizerText::formatTime(start.time()),
                              OrganizerText::formatTime(end.time()));
}

std::pair<QDate, QDate> CalendarModel::daySpan(const Occurrence &occurrence)
{
    if (occurrence.allDay) {
        const QDate first = occurrence.start.toUTC().date();
        const QDate last = occurrence.end.toUTC().date().addDays(-1);
        return {first, last < first ? first : last};
    }
    const QDateTime start = occurrence.start.toLocalTime();
    const QDateTime end = occurrence.end.toLocalTime();
    QDate last = end.date();
    if (end > start && end.time() == QTime(0, 0))
        last = last.addDays(-1);
    return {start.date(), last < start.date() ? start.date() : last};
}

int CalendarModel::rowOf(int luid, qint64 startMs) const
{
    for (qsizetype row = 0; row < m_shown.size(); ++row) {
        const Occurrence &entry = m_shown.at(row);
        if (int(entry.luid) == luid && entry.start.toMSecsSinceEpoch() == startMs)
            return int(row);
    }
    return -1;
}

int CalendarModel::luidAt(int row) const
{
    return row >= 0 && row < m_shown.size() ? int(m_shown.at(row).luid) : 0;
}

void CalendarModel::refilter()
{
    const int before = int(m_shown.size());
    beginResetModel();
    m_shown.clear();
    for (const Occurrence &entry : std::as_const(m_all)) {
        if (m_search.isEmpty() || entry.summary.contains(m_search, Qt::CaseInsensitive) ||
            entry.location.contains(m_search, Qt::CaseInsensitive)) {
            m_shown.append(entry);
        }
    }
    endResetModel();
    if (before != m_shown.size())
        Q_EMIT countChanged();
}

} // namespace qpm::ui
