#include "ui/backup/BackupSectionModel.h"

namespace qpm::ui {

using core::backup::BackupSection;

BackupSectionModel::BackupSectionModel(QObject *parent) : QAbstractListModel(parent)
{
    for (const BackupSection section : core::backup::allBackupSections)
        m_rows.append({section, true, true, QString()});
}

int BackupSectionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant BackupSectionModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case KeyRole:
        return core::backup::sectionKey(row.section);
    case TitleRole:
        return titleOf(row.section);
    case ChosenRole:
        return row.chosen;
    case SelectableRole:
        return row.selectable;
    case DetailRole:
        return row.detail;
    default:
        return {};
    }
}

QHash<int, QByteArray> BackupSectionModel::roleNames() const
{
    return {
        {KeyRole, QByteArrayLiteral("key")},
        {TitleRole, QByteArrayLiteral("title")},
        {ChosenRole, QByteArrayLiteral("chosen")},
        {SelectableRole, QByteArrayLiteral("selectable")},
        {DetailRole, QByteArrayLiteral("detail")},
    };
}

QStringList BackupSectionModel::chosenKeys() const
{
    QStringList keys;
    for (const Row &row : m_rows) {
        if (row.chosen && row.selectable)
            keys.append(core::backup::sectionKey(row.section));
    }
    return keys;
}

void BackupSectionModel::setChosen(int row, bool chosen)
{
    if (row < 0 || row >= m_rows.size())
        return;
    Row &entry = m_rows[row];
    const bool wanted = chosen && entry.selectable;
    if (entry.chosen == wanted)
        return;
    entry.chosen = wanted;
    Q_EMIT dataChanged(index(row), index(row), {ChosenRole});
    Q_EMIT chosenChanged();
}

BackupSection BackupSectionModel::sectionAt(int row) const
{
    return m_rows.value(row).section;
}

int BackupSectionModel::rowOf(BackupSection section) const
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).section == section)
            return int(row);
    }
    return -1;
}

void BackupSectionModel::setState(BackupSection section, bool selectable, bool chosen)
{
    const int row = rowOf(section);
    if (row < 0)
        return;
    Row &entry = m_rows[row];
    const bool wanted = chosen && selectable;
    if (entry.selectable == selectable && entry.chosen == wanted)
        return;
    const bool chosenBefore = entry.chosen;
    entry.selectable = selectable;
    entry.chosen = wanted;
    Q_EMIT dataChanged(index(row), index(row), {ChosenRole, SelectableRole});
    if (chosenBefore != wanted)
        Q_EMIT chosenChanged();
}

void BackupSectionModel::setDetail(BackupSection section, const QString &detail)
{
    const int row = rowOf(section);
    if (row < 0 || m_rows.at(row).detail == detail)
        return;
    m_rows[row].detail = detail;
    Q_EMIT dataChanged(index(row), index(row), {DetailRole});
}

void BackupSectionModel::clearDetails()
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).detail.isEmpty())
            continue;
        m_rows[row].detail.clear();
        Q_EMIT dataChanged(index(int(row)), index(int(row)), {DetailRole});
    }
}

QString BackupSectionModel::titleOf(BackupSection section)
{
    switch (section) {
    case BackupSection::Contacts:
        return tr("Contacts");
    case BackupSection::Calendar:
        return tr("Calendar");
    case BackupSection::Tasks:
        return tr("Tasks");
    case BackupSection::Notes:
        return tr("Notes");
    case BackupSection::Calls:
        return tr("Call log");
    case BackupSection::Messages:
        break;
    }
    return tr("Messages");
}

} // namespace qpm::ui
