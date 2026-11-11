#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/// A phone the connect dialog offers: found on the network, at USB or over Bluetooth.
struct FoundPhone
{
    /// What the user recognises the phone by, usually the model.
    QString label;
    /// A second line, for example the address or why the phone cannot be used.
    QString detail;
    /// What a connect call takes: an IP address, an adb serial or a Bluetooth address.
    QString address;
    QString deviceId;
    bool available = true;
};

/**
 * The phones of one kind of connection for the connect dialog, one row per
 * address. Filled by AppController.
 */
class FoundPhoneModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FoundPhoneModel)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        DetailRole,
        AddressRole,
        DeviceIdRole,
        AvailableRole,
    };

    explicit FoundPhoneModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return rowCount(); }

    /// Adds the phone, or updates the row with the same address.
    void upsert(const FoundPhone &phone);
    void clear();
    [[nodiscard]] const QList<FoundPhone> &phones() const { return m_phones; }

Q_SIGNALS:
    void countChanged();

private:
    QList<FoundPhone> m_phones;
};

} // namespace qpm::ui
