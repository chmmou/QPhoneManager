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
 * Lists the Bluetooth devices that are paired with this computer and in
 * reach. Lives in the main thread; start() returns at once and the devices
 * arrive through deviceFound().
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
