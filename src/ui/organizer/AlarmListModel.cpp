#include "ui/organizer/AlarmListModel.h"

#include "ui/organizer/OrganizerText.h"

namespace qpm::ui {

namespace {

constexpr qsizetype daysPerWeek = 7;

} // namespace

AlarmListModel::AlarmListModel(QObject *parent) : QAbstractListModel(parent) {}

int AlarmListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_alarms.size());
}

QVariant AlarmListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::service::PhoneAlarm &alarm = m_alarms.at(index.row());
    switch (role) {
    case AlarmIdRole:
        return alarm.id;
    case Qt::DisplayRole:
    case TimeTextRole:
        return OrganizerText::formatTime(alarm.time);
    case NameRole:
        return alarm.name;
    case DaysTextRole:
        return daysText(alarm.days);
    case EnabledRole:
        return alarm.enabled;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> AlarmListModel::roleNames() const
{
    return {
        {AlarmIdRole, QByteArrayLiteral("alarmId")},
        {TimeTextRole, QByteArrayLiteral("timeText")},
        {NameRole, QByteArrayLiteral("name")},
        {DaysTextRole, QByteArrayLiteral("daysText")},
        {EnabledRole, QByteArrayLiteral("alarmEnabled")},
    };
}

void AlarmListModel::setAlarms(const QList<core::service::PhoneAlarm> &alarms)
{
    beginResetModel();
    m_alarms = alarms;
    endResetModel();
    Q_EMIT countChanged();
}

core::service::PhoneAlarm AlarmListModel::at(int row) const
{
    return row >= 0 && row < m_alarms.size() ? m_alarms.at(row) : core::service::PhoneAlarm();
}

QString AlarmListModel::daysText(const QList<int> &days)
{
    if (days.isEmpty())
        return tr("Once");
    if (days.size() == daysPerWeek)
        return tr("Every day");
    return OrganizerText::weekdayList(days);
}

} // namespace qpm::ui
