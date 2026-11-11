#include "ui/contacts/ContactListModel.h"

namespace qpm::ui {

ContactListModel::ContactListModel(QObject *parent) : QAbstractListModel(parent) {}

int ContactListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ContactListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::store::ContactSummary &contact = m_all.at(m_rows.at(index.row()));
    switch (role) {
    case LuidRole:
        return int(contact.luid);
    case Qt::DisplayRole:
    case DisplayNameRole:
        return contact.displayName;
    case PrimaryNumberRole:
        return contact.primaryNumber;
    case PrimaryEmailRole:
        return contact.primaryEmail;
    case OrganizationRole:
        return contact.organization;
    case PhotoUrlRole:
        return photoUrl(contact.luid, contact.photoBlob);
    case InitialsRole: {
        QString initials;
        const QStringList words = contact.displayName.split(u' ', Qt::SkipEmptyParts);
        for (const QString &word : words) {
            if (word.at(0).isLetterOrNumber())
                initials.append(word.at(0).toUpper());
            if (initials.size() == 2)
                break;
        }
        return initials.isEmpty() ? QStringLiteral("#") : initials;
    }
    case SectionRole:
        return sectionOf(contact.sortKey);
    case GroupsRole:
        return contact.groups.join(QLatin1StringView(", "));
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> ContactListModel::roleNames() const
{
    return {
        {LuidRole, QByteArrayLiteral("luid")},
        {DisplayNameRole, QByteArrayLiteral("displayName")},
        {PrimaryNumberRole, QByteArrayLiteral("primaryNumber")},
        {PrimaryEmailRole, QByteArrayLiteral("primaryEmail")},
        {OrganizationRole, QByteArrayLiteral("organization")},
        {PhotoUrlRole, QByteArrayLiteral("photoUrl")},
        {InitialsRole, QByteArrayLiteral("initials")},
        {SectionRole, QByteArrayLiteral("section")},
        {GroupsRole, QByteArrayLiteral("groups")},
    };
}

void ContactListModel::setGroupFilter(const QString &group)
{
    if (m_groupFilter == group)
        return;
    m_groupFilter = group;
    Q_EMIT groupFilterChanged();
    refilter();
}

void ContactListModel::setAccountFilter(const QString &account)
{
    if (m_accountFilter == account)
        return;
    m_accountFilter = account;
    Q_EMIT accountFilterChanged();
    refilter();
}

void ContactListModel::setContacts(QList<core::store::ContactSummary> contacts)
{
    m_all = std::move(contacts);
    refilter();
}

void ContactListModel::setSearchMatches(std::optional<QSet<quint32>> matches)
{
    m_matches = std::move(matches);
    refilter();
}

int ContactListModel::rowOf(int luid) const
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (int(m_all.at(m_rows.at(row)).luid) == luid)
            return int(row);
    }
    return -1;
}

int ContactListModel::luidAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return 0;
    return int(m_all.at(m_rows.at(row)).luid);
}

QList<quint32> ContactListModel::shownLuids() const
{
    QList<quint32> luids;
    luids.reserve(m_rows.size());
    for (const qsizetype row : m_rows)
        luids.append(m_all.at(row).luid);
    return luids;
}

QString ContactListModel::nameOf(int luid) const
{
    for (const core::store::ContactSummary &contact : m_all) {
        if (int(contact.luid) == luid)
            return contact.displayName;
    }
    return {};
}

QString ContactListModel::photoUrl(quint32 luid, const QString &blob)
{
    if (blob.isEmpty())
        return {};
    return QStringLiteral("image://qpm/photo/%1/%2").arg(luid).arg(blob);
}

QString ContactListModel::sectionOf(const QString &sortKey)
{
    if (sortKey.isEmpty())
        return QStringLiteral("#");
    const QChar first = sortKey.at(0).toUpper();
    return first >= u'A' && first <= u'Z' ? QString(first) : QStringLiteral("#");
}

void ContactListModel::refilter()
{
    beginResetModel();
    m_rows.clear();
    for (qsizetype i = 0; i < m_all.size(); ++i) {
        const core::store::ContactSummary &contact = m_all.at(i);
        if (m_matches && !m_matches->contains(contact.luid))
            continue;
        if (!m_groupFilter.isEmpty() && !contact.groups.contains(m_groupFilter))
            continue;
        // The phone leaves out the account of its own contacts; the
        // metadata calls that account "null".
        const bool ownAccount = contact.accountType.isEmpty() && contact.accountName.isEmpty();
        const QString account = ownAccount ? QStringLiteral("null;null")
                                           : contact.accountType + u';' + contact.accountName;
        if (!m_accountFilter.isEmpty() && account != m_accountFilter)
            continue;
        m_rows.append(i);
    }
    endResetModel();
    // The total may have changed too, so the signal goes out every time.
    Q_EMIT countChanged();
}

} // namespace qpm::ui
