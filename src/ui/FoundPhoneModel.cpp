#include "ui/FoundPhoneModel.h"

namespace qpm::ui {

FoundPhoneModel::FoundPhoneModel(QObject *parent) : QAbstractListModel(parent) {}

int FoundPhoneModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_phones.size());
}

QVariant FoundPhoneModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const FoundPhone &phone = m_phones.at(index.row());
    switch (role) {
    case LabelRole:
    case Qt::DisplayRole:
        return phone.label;
    case DetailRole:
        return phone.detail;
    case AddressRole:
        return phone.address;
    case DeviceIdRole:
        return phone.deviceId;
    case AvailableRole:
        return phone.available;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> FoundPhoneModel::roleNames() const
{
    return {
        {LabelRole, QByteArrayLiteral("label")},
        {DetailRole, QByteArrayLiteral("detail")},
        {AddressRole, QByteArrayLiteral("address")},
        {DeviceIdRole, QByteArrayLiteral("deviceId")},
        {AvailableRole, QByteArrayLiteral("available")},
    };
}

void FoundPhoneModel::upsert(const FoundPhone &phone)
{
    for (qsizetype row = 0; row < m_phones.size(); ++row) {
        if (m_phones.at(row).address == phone.address) {
            m_phones[row] = phone;
            const QModelIndex changed = index(int(row));
            Q_EMIT dataChanged(changed, changed);
            return;
        }
    }
    const int row = int(m_phones.size());
    beginInsertRows({}, row, row);
    m_phones.append(phone);
    endInsertRows();
    Q_EMIT countChanged();
}

void FoundPhoneModel::clear()
{
    if (m_phones.isEmpty())
        return;
    beginResetModel();
    m_phones.clear();
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace qpm::ui
