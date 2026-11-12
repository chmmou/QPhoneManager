#include "ui/organizer/CalendarListModel.h"

#include <QColor>

namespace qpm::ui {

CalendarListModel::CalendarListModel(QObject *parent) : QAbstractListModel(parent) {}

int CalendarListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_edited.size());
}

QVariant CalendarListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::codec::CalendarInfo &calendar = m_edited.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return calendar.title;
    case ColorRole:
        return QColor::fromRgb(calendar.color | 0xFF000000U);
    case WritableRole:
        return calendar.isWritable();
    case SelectedRole:
        return calendar.selected;
    case DefaultRole:
        return calendar.isDefault;
    case AccountRole: {
        // "<account>@<type>/<sync id>"; the account itself may hold an "@".
        const qsizetype typeStart = calendar.url.lastIndexOf(u'@');
        return typeStart > 0 ? calendar.url.left(typeStart) : calendar.url;
    }
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> CalendarListModel::roleNames() const
{
    return {
        {TitleRole, QByteArrayLiteral("title")},
        {ColorRole, QByteArrayLiteral("calendarColor")},
        {WritableRole, QByteArrayLiteral("writable")},
        {SelectedRole, QByteArrayLiteral("chosen")},
        {DefaultRole, QByteArrayLiteral("isDefault")},
        {AccountRole, QByteArrayLiteral("account")},
    };
}

void CalendarListModel::setCalendars(const QList<core::codec::CalendarInfo> &calendars)
{
    const bool wasModified = isModified();
    const int before = int(m_edited.size());
    beginResetModel();
    m_stored = calendars;
    m_edited = calendars;
    endResetModel();
    if (before != m_edited.size())
        Q_EMIT countChanged();
    if (wasModified)
        Q_EMIT modifiedChanged();
}

bool CalendarListModel::isSelected(int row) const
{
    return row >= 0 && row < m_edited.size() && m_edited.at(row).selected;
}

void CalendarListModel::setSelected(int row, bool selected)
{
    if (row < 0 || row >= m_edited.size() || m_edited.at(row).selected == selected)
        return;
    const bool wasModified = isModified();
    m_edited[row].selected = selected;
    Q_EMIT dataChanged(index(row), index(row), {SelectedRole});
    if (wasModified != isModified())
        Q_EMIT modifiedChanged();
}

bool CalendarListModel::setDefault(int row)
{
    if (row < 0 || row >= m_edited.size() || !m_edited.at(row).isWritable())
        return false;
    const bool wasModified = isModified();
    for (qsizetype i = 0; i < m_edited.size(); ++i) {
        const bool isDefault = i == row;
        if (m_edited.at(i).isDefault != isDefault) {
            m_edited[i].isDefault = isDefault;
            Q_EMIT dataChanged(index(int(i)), index(int(i)), {DefaultRole});
        }
    }
    // The calendar for new events is always one the phone syncs.
    setSelected(row, true);
    if (wasModified != isModified())
        Q_EMIT modifiedChanged();
    return true;
}

void CalendarListModel::revertChoice()
{
    setCalendars(m_stored);
}

} // namespace qpm::ui
