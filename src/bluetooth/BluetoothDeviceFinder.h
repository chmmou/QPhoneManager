#pragma once

#include "core/Error.h"

#include <QObject>
#include <QString>

class QBluetoothDeviceDiscoveryAgent;
class QBluetoothDeviceInfo;
class QBluetoothLocalDevice;

namespace qpm {

/// A phone paired with this computer over Bluetooth.
struct PairedDevice
{
    /// "AA:BB:CC:DD:EE:FF".
    QString address;
    QString name;
};

/**
 * Lists the phones that are paired with this computer over Bluetooth and in
 * reach; other paired devices, such as headsets, are left out. Lives in the
 * main thread; start() returns at once and the phones arrive through
 * deviceFound().
 *
 * Without a Bluetooth adapter, or when the adapter is off, failed() reports
 * ErrorCode::NotSupported. Every run ends with finished().
 */
class BluetoothDeviceFinder : public QObject
{
    Q_OBJECT

public:
    explicit BluetoothDeviceFinder(QObject *parent = nullptr);
    ~BluetoothDeviceFinder() override;

    /// Starts a search; a search that still runs is restarted.
    void start();
    void stop();
    [[nodiscard]] bool isRunning() const;

    /// Whether `info` announces itself as a phone through its major device class.
    [[nodiscard]] static bool isPhone(const QBluetoothDeviceInfo &info);

Q_SIGNALS:
    void deviceFound(const qpm::PairedDevice &device);
    void failed(const qpm::core::Error &error);
    void finished();

private:
    void onDiscovered(const QBluetoothDeviceInfo &info);

    QBluetoothLocalDevice *m_local = nullptr;
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
};

} // namespace qpm
