#include "ui/organizer/CategoryModel.h"

#include "ui/organizer/OrganizerText.h"

#include <QColor>

#include <algorithm>

namespace qpm::ui {

CategoryModel::CategoryModel(QObject *parent) : QAbstractListModel(parent) {}

int CategoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_categories.size());
}

QVariant CategoryModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::codec::OrganizerCategory &category = m_categories.at(index.row());
    switch (role) {
    case IdRole:
        return category.id;
    case Qt::DisplayRole:
    case NameRole:
        return category.name;
    case ColorRole:
        return QColor::fromRgb(category.color | 0xFF000000U);
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> CategoryModel::roleNames() const
{
    return {
        {IdRole, QByteArrayLiteral("categoryId")},
        {NameRole, QByteArrayLiteral("name")},
        {ColorRole, QByteArrayLiteral("categoryColor")},
    };
}

QStringList CategoryModel::names() const
{
    QStringList list;
    for (const core::codec::OrganizerCategory &category : m_categories)
        list.append(category.name);
    return list;
}

void CategoryModel::setCategories(const QList<core::codec::OrganizerCategory> &categories)
{
    beginResetModel();
    m_categories = categories;
    endResetModel();
    Q_EMIT countChanged();
}

int CategoryModel::colorIndexAt(int row) const
{
    return std::max(0, OrganizerText::colorIndex(at(row).color) - 1);
}

core::codec::OrganizerCategory CategoryModel::at(int row) const
{
    return row >= 0 && row < m_categories.size() ? m_categories.at(row)
                                                 : core::codec::OrganizerCategory();
}

} // namespace qpm::ui
