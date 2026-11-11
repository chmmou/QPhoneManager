#include "ui/contacts/ContactValueModel.h"

#include "ui/contacts/ContactTypes.h"

namespace qpm::ui {

namespace {

namespace part = core::codec::addressPart;

int addressPartOf(int role)
{
    switch (role) {
    case ContactValueModel::PoBoxRole:
        return part::poBox;
    case ContactValueModel::StreetRole:
        return part::street;
    case ContactValueModel::PostcodeRole:
        return part::postcode;
    case ContactValueModel::CityRole:
        return part::city;
    case ContactValueModel::RegionRole:
        return part::region;
    case ContactValueModel::CountryRole:
        return part::country;
    default:
        break;
    }
    return -1;
}

} // namespace

ContactValueModel::ContactValueModel(core::codec::ContactValueKind kind, QObject *parent)
    : QAbstractListModel(parent), m_kind(kind)
{
}

int ContactValueModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_values.size());
}

QVariant ContactValueModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::codec::ContactValue &entry = m_values.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case ValueRole:
        return entry.value;
    case TypeIndexRole:
        return ContactTypes::indexOf(m_kind, entry.type);
    case LabelRole:
        return entry.label;
    case PrimaryRole:
        return entry.primary;
    default:
        break;
    }
    const int part = addressPartOf(role);
    return part < 0 ? QVariant() : QVariant(entry.address.value(part));
}

QHash<int, QByteArray> ContactValueModel::roleNames() const
{
    return {
        {ValueRole, QByteArrayLiteral("value")},
        {TypeIndexRole, QByteArrayLiteral("typeIndex")},
        {LabelRole, QByteArrayLiteral("label")},
        {PrimaryRole, QByteArrayLiteral("primary")},
        {PoBoxRole, QByteArrayLiteral("poBox")},
        {StreetRole, QByteArrayLiteral("street")},
        {PostcodeRole, QByteArrayLiteral("postcode")},
        {CityRole, QByteArrayLiteral("city")},
        {RegionRole, QByteArrayLiteral("region")},
        {CountryRole, QByteArrayLiteral("country")},
    };
}

QStringList ContactValueModel::typeNames() const
{
    return ContactTypes::names(m_kind);
}

int ContactValueModel::customTypeIndex() const
{
    return int(ContactTypes::codes(m_kind).indexOf(ContactTypes::customCode(m_kind)));
}

bool ContactValueModel::isFull() const
{
    return m_values.size() >= core::codec::maxValuesPerWrite;
}

bool ContactValueModel::append()
{
    if (isFull())
        return false;
    core::codec::ContactValue entry;
    entry.type = ContactTypes::codes(m_kind).constFirst();
    if (m_kind == core::codec::ContactValueKind::Address)
        entry.address = QStringList(part::count, QString());
    const int row = int(m_values.size());
    beginInsertRows({}, row, row);
    m_values.append(entry);
    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT edited();
    return true;
}

void ContactValueModel::remove(int row)
{
    if (row < 0 || row >= m_values.size())
        return;
    beginRemoveRows({}, row, row);
    m_values.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    Q_EMIT edited();
}

bool ContactValueModel::setText(int row, const QString &field, const QString &text)
{
    if (row < 0 || row >= m_values.size())
        return false;
    const int role = roleNames().key(field.toUtf8(), -1);
    core::codec::ContactValue &entry = m_values[row];
    if (role == ValueRole) {
        if (entry.value == text)
            return true;
        entry.value = text;
    } else if (role == LabelRole) {
        if (entry.label == text)
            return true;
        entry.label = text;
    } else if (const int part = addressPartOf(role); part >= 0) {
        while (entry.address.size() < part::count)
            entry.address.append(QString());
        if (entry.address.at(part) == text)
            return true;
        entry.address[part] = text;
    } else {
        return false;
    }
    const QModelIndex changed = index(row);
    Q_EMIT dataChanged(changed, changed, {role});
    Q_EMIT edited();
    return true;
}

void ContactValueModel::setTypeIndex(int row, int typeIndex)
{
    const QList<int> codes = ContactTypes::codes(m_kind);
    if (row < 0 || row >= m_values.size() || typeIndex < 0 || typeIndex >= codes.size())
        return;
    if (m_values.at(row).type == codes.at(typeIndex))
        return;
    m_values[row].type = codes.at(typeIndex);
    const QModelIndex changed = index(row);
    Q_EMIT dataChanged(changed, changed, {TypeIndexRole});
    Q_EMIT edited();
}

void ContactValueModel::setValues(QList<core::codec::ContactValue> values)
{
    beginResetModel();
    m_values = std::move(values);
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace qpm::ui
