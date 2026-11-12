#pragma once

#include "core/codec/CalMetadata.h"

#include <QAbstractListModel>
#include <QList>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/// The categories of the tasks or of the notes, by name. Filled by TasksArea or NotesArea.
class CategoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CategoryModel)
    QML_UNCREATABLE("Provided by TasksArea and NotesArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// The names in the order of the rows.
    Q_PROPERTY(QStringList names READ names NOTIFY countChanged FINAL)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
    };

    explicit CategoryModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] QStringList names() const;

    void setCategories(const QList<core::codec::OrganizerCategory> &categories);
    [[nodiscard]] const QList<core::codec::OrganizerCategory> &categories() const
    {
        return m_categories;
    }
    /// The category in `row`; an empty one with id 0 for a row that does not exist.
    [[nodiscard]] core::codec::OrganizerCategory at(int row) const;
    /// The entry of the colour choices of the areas nearest to the colour in `row`.
    [[nodiscard]] Q_INVOKABLE int colorIndexAt(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<core::codec::OrganizerCategory> m_categories;
};

} // namespace qpm::ui
