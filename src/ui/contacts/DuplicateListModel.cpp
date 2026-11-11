#include "ui/contacts/DuplicateListModel.h"

namespace qpm::ui {

using core::service::DuplicatePair;

DuplicateListModel::DuplicateListModel(QObject *parent) : QAbstractListModel(parent) {}

int DuplicateListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant DuplicateListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case FirstLuidRole:
        return int(entry.pair.first);
    case SecondLuidRole:
        return int(entry.pair.second);
    case Qt::DisplayRole:
    case FirstNameRole:
        return entry.firstName;
    case SecondNameRole:
        return entry.secondName;
    case ReasonRole:
        switch (entry.pair.reason) {
        case DuplicatePair::Reason::Number:
            return tr("Same phone number");
        case DuplicatePair::Reason::Email:
            return tr("Same mail address");
        case DuplicatePair::Reason::Name:
            return tr("Similar name");
        }
        break;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> DuplicateListModel::roleNames() const
{
    return {
        {FirstLuidRole, QByteArrayLiteral("firstLuid")},
        {SecondLuidRole, QByteArrayLiteral("secondLuid")},
        {FirstNameRole, QByteArrayLiteral("firstName")},
        {SecondNameRole, QByteArrayLiteral("secondName")},
        {ReasonRole, QByteArrayLiteral("reason")},
    };
}

void DuplicateListModel::setEntries(QList<Entry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    Q_EMIT countChanged();
}

DuplicatePair DuplicateListModel::pairAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return m_entries.at(row).pair;
}

void DuplicateListModel::removeContacts(int row, const QList<quint32> &luids)
{
    for (qsizetype i = m_entries.size() - 1; i >= 0; --i) {
        const DuplicatePair &pair = m_entries.at(i).pair;
        const bool affected = i == row || luids.contains(pair.first) || luids.contains(pair.second);
        if (!affected)
            continue;
        beginRemoveRows({}, int(i), int(i));
        m_entries.removeAt(i);
        endRemoveRows();
    }
    Q_EMIT countChanged();
}

} // namespace qpm::ui
