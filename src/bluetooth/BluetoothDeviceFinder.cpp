#include "bluetooth/BluetoothDeviceFinder.h"

#include "core/Logging.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothLocalDevice>
#include <QTimer>

namespace qpm {

using core::Error;
using core::ErrorCode;

BluetoothDeviceFinder::BluetoothDeviceFinder(QObject *parent) : QObject(parent) {}

BluetoothDeviceFinder::~BluetoothDeviceFinder() = default;

void BluetoothDeviceFinder::start()
{
    stop();
    if (!m_local)
        m_local = new QBluetoothLocalDevice(this);
    if (!m_local->isValid() || m_local->hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        // Reported after start() has returned, like the result of a search.
        QTimer::singleShot(0, this, [this]() {
            Q_EMIT failed(Error(ErrorCode::NotSupported,
                                QStringLiteral("No Bluetooth adapter is available or it is off")));
            Q_EMIT finished();
        });
        return;
    }
    if (!m_agent) {
        m_agent = new QBluetoothDeviceDiscoveryAgent(this);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
                &BluetoothDeviceFinder::onDiscovered);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this,
                &BluetoothDeviceFinder::finished);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this,
                &BluetoothDeviceFinder::finished);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
                [this](QBluetoothDeviceDiscoveryAgent::Error error) {
                    qCWarning(core::lcPlatform) << "Bluetooth search failed" << error;
                    Q_EMIT failed(Error(ErrorCode::IoFailure,
                                        QStringLiteral("Bluetooth search failed: %1")
                                            .arg(m_agent->errorString())));
                    Q_EMIT finished();
                });
    }
    m_agent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
}

void BluetoothDeviceFinder::stop()
{
    if (m_agent && m_agent->isActive())
        m_agent->stop();
}

bool BluetoothDeviceFinder::isRunning() const
{
    return m_agent && m_agent->isActive();
}

void BluetoothDeviceFinder::onDiscovered(const QBluetoothDeviceInfo &info)
{
    if (m_local->pairingStatus(info.address()) == QBluetoothLocalDevice::Unpaired)
        return;
    Q_EMIT deviceFound(PairedDevice{info.address().toString(), info.name()});
}

} // namespace qpm
