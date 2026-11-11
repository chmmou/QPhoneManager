#pragma once

#include "core/codec/PhoneVCard.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The entries of one kind in the contact form: numbers, mail addresses and
 * so on. Rows are added and removed by the form; a removed entry that the
 * phone knows is deleted there on saving. Owned by ContactEditor.
 */
class ContactValueModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ContactValueModel)
    QML_UNCREATABLE("Provided by ContactEditor")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// The translated type names to choose from, the custom type last.
    Q_PROPERTY(QStringList typeNames READ typeNames CONSTANT FINAL)
    /// The position of the custom type in typeNames.
    Q_PROPERTY(int customTypeIndex READ customTypeIndex CONSTANT FINAL)
    /// True when the phone takes no more entries of this kind in one save.
    Q_PROPERTY(bool full READ isFull NOTIFY countChanged FINAL)

public:
    enum Role {
        ValueRole = Qt::UserRole + 1,
        TypeIndexRole,
        LabelRole,
        PrimaryRole,
        PoBoxRole,
        StreetRole,
        PostcodeRole,
        CityRole,
        RegionRole,
        CountryRole,
    };

    explicit ContactValueModel(core::codec::ContactValueKind kind, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] QStringList typeNames() const;
    [[nodiscard]] int customTypeIndex() const;
    [[nodiscard]] bool isFull() const;
    [[nodiscard]] core::codec::ContactValueKind kind() const { return m_kind; }

    /// Adds an empty entry of the first type; false when the kind is full.
    Q_INVOKABLE bool append();
    /// Removes the entry in `row`.
    Q_INVOKABLE void remove(int row);
    /**
     * Sets a text of the entry in `row`; `field` is one of the role names
     * "value", "label", "poBox", "street", "postcode", "city", "region" and
     * "country". Returns false for an unknown row or field.
     */
    Q_INVOKABLE bool setText(int row, const QString &field, const QString &text);
    /// Sets the type of the entry in `row` to the one at `typeIndex` of typeNames.
    Q_INVOKABLE void setTypeIndex(int row, int typeIndex);

    void setValues(QList<core::codec::ContactValue> values);
    [[nodiscard]] const QList<core::codec::ContactValue> &values() const { return m_values; }

Q_SIGNALS:
    void countChanged();
    /// Any entry was added, removed or changed.
    void edited();

private:
    core::codec::ContactValueKind m_kind;
    QList<core::codec::ContactValue> m_values;
};

} // namespace qpm::ui
