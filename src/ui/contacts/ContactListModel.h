#pragma once

#include "core/store/ContactStore.h"

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace qpm::ui {

/**
 * The contacts of the current phone, sorted by name, narrowed by the search
 * of the area, a group and an account. Filled by ContactsArea; the search
 * itself runs in the contact store.
 */
class ContactListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ContactListModel)
    QML_UNCREATABLE("Provided by ContactsArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Contacts stored for the phone, before any filter.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged FINAL)
    /// Group title to show only its members; empty shows all.
    Q_PROPERTY(
        QString groupFilter READ groupFilter WRITE setGroupFilter NOTIFY groupFilterChanged FINAL)
    /// "<type>;<name>" of the account to show only its contacts; empty shows all.
    Q_PROPERTY(QString accountFilter READ accountFilter WRITE setAccountFilter NOTIFY
                   accountFilterChanged FINAL)

public:
    enum Role {
        LuidRole = Qt::UserRole + 1,
        DisplayNameRole,
        PrimaryNumberRole,
        PrimaryEmailRole,
        OrganizationRole,
        PhotoUrlRole,
        InitialsRole,
        SectionRole,
        GroupsRole,
    };

    explicit ContactListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] int totalCount() const { return int(m_all.size()); }
    [[nodiscard]] QString groupFilter() const { return m_groupFilter; }
    void setGroupFilter(const QString &group);
    [[nodiscard]] QString accountFilter() const { return m_accountFilter; }
    void setAccountFilter(const QString &account);

    /// Replaces every contact; the filters stay.
    void setContacts(QList<core::store::ContactSummary> contacts);
    /// LUIDs the search found; nothing shows every contact.
    void setSearchMatches(std::optional<QSet<quint32>> matches);

    /// The row of the contact `luid`, or -1 when it is not shown.
    [[nodiscard]] Q_INVOKABLE int rowOf(int luid) const;
    /// The LUID in `row`, or 0.
    [[nodiscard]] Q_INVOKABLE int luidAt(int row) const;
    /// The LUIDs of every row shown.
    [[nodiscard]] QList<quint32> shownLuids() const;
    /// The shown name of the contact `luid`, also when it is filtered out; empty when unknown.
    [[nodiscard]] Q_INVOKABLE QString nameOf(int luid) const;

    /// The image URL of a stored photo; empty without one.
    [[nodiscard]] static QString photoUrl(quint32 luid, const QString &blob);
    /// The letter a contact is listed under: A to Z, or "#".
    [[nodiscard]] static QString sectionOf(const QString &sortKey);

Q_SIGNALS:
    void countChanged();
    void groupFilterChanged();
    void accountFilterChanged();

private:
    void refilter();

    QList<core::store::ContactSummary> m_all;
    QList<qsizetype> m_rows;
    std::optional<QSet<quint32>> m_matches;
    QString m_groupFilter;
    QString m_accountFilter;
};

} // namespace qpm::ui
