#include "ui/ProfileListModel.h"

#include <QLocale>

#include <algorithm>

namespace qpm::ui {

ProfileListModel::ProfileListModel(QObject *parent) : QAbstractListModel(parent) {}

int ProfileListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_profiles.size());
}

QVariant ProfileListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::store::DeviceProfile &profile = m_profiles.at(index.row());
    switch (role) {
    case DeviceIdRole:
        return profile.deviceId;
    case DisplayNameRole:
    case Qt::DisplayRole:
        return profile.displayName;
    case PhoneModelRole:
        return profile.model;
    case LastConnectionRole:
        return profile.lastConnection.isValid()
                   ? QLocale().toString(profile.lastConnection.toLocalTime(), QLocale::ShortFormat)
                   : QString();
    case CurrentRole:
        return profile.deviceId == m_currentId;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> ProfileListModel::roleNames() const
{
    return {
        {DeviceIdRole, QByteArrayLiteral("deviceId")},
        {DisplayNameRole, QByteArrayLiteral("displayName")},
        // Not "model": a role of that name hides the model object from a
        // delegate, and the delegates of the style read their text through it.
        {PhoneModelRole, QByteArrayLiteral("phoneModel")},
        {LastConnectionRole, QByteArrayLiteral("lastConnection")},
        {CurrentRole, QByteArrayLiteral("current")},
    };
}

void ProfileListModel::setProfiles(QList<core::store::DeviceProfile> profiles,
                                   const QString &currentId)
{
    std::sort(profiles.begin(), profiles.end(),
              [](const core::store::DeviceProfile &a, const core::store::DeviceProfile &b) {
                  return QString::localeAwareCompare(a.displayName, b.displayName) < 0;
              });
    const bool countChanges = profiles.size() != m_profiles.size();
    beginResetModel();
    m_profiles = std::move(profiles);
    m_currentId = currentId;
    endResetModel();
    if (countChanges)
        Q_EMIT countChanged();
}

int ProfileListModel::indexOf(const QString &deviceId) const
{
    for (qsizetype row = 0; row < m_profiles.size(); ++row) {
        if (m_profiles.at(row).deviceId == deviceId)
            return int(row);
    }
    return -1;
}

} // namespace qpm::ui
