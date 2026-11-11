#pragma once

#include "core/service/ContactsService.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * Pairs of contacts that look like the same person, with the reason as a
 * translated text. Filled by ContactsArea after a search for duplicates.
 */
class DuplicateListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DuplicateListModel)
    QML_UNCREATABLE("Provided by ContactsArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        FirstLuidRole = Qt::UserRole + 1,
        SecondLuidRole,
        FirstNameRole,
        SecondNameRole,
        ReasonRole,
    };

    /// One pair with the names to show.
    struct Entry
    {
        core::service::DuplicatePair pair;
        QString firstName;
        QString secondName;
    };

    explicit DuplicateListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return rowCount(); }

    void setEntries(QList<Entry> entries);
    /// The pair in `row`; a pair of zeros for an invalid row.
    [[nodiscard]] core::service::DuplicatePair pairAt(int row) const;
    /// Removes the row and every other pair that names one of its contacts in `luids`.
    void removeContacts(int row, const QList<quint32> &luids);

Q_SIGNALS:
    void countChanged();

private:
    QList<Entry> m_entries;
};

} // namespace qpm::ui
