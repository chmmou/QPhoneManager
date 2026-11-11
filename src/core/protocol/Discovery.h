#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

class QTimer;
class QUdpSocket;

namespace qpm::core::protocol {

/// A phone that answered or announced itself on the local network.
struct DiscoveredDevice
{
    QHostAddress address;
    QString deviceId;
    QString model;
};

/**
 * Finds phone clients on the local network. start() binds the receive port,
 * sends three probes half a second apart and keeps listening for answers and
 * for the announcements the client sends every few seconds on its own, until
 * stop().
 *
 * Datagrams are hex text of the plain form XOR-ed with a fixed key, as the
 * client writes them; anything else is ignored. The class talks to no
 * service outside the local network.
 */
class Discovery : public QObject
{
    Q_OBJECT

public:
    struct Ports
    {
        /// Where probes go: the broadcast address in normal use.
        QHostAddress probeAddress = QHostAddress(QHostAddress::Broadcast);
        quint16 probePort = 5211;
        /// Where answers arrive; 0 picks a free port.
        quint16 receivePort = 5212;
    };

    explicit Discovery(QObject *parent = nullptr);
    Discovery(const Ports &ports, QObject *parent);

    /**
     * Binds the receive port and sends the probes. Fails with
     * ErrorCode::IoFailure when the port cannot be bound.
     */
    [[nodiscard]] Result<void> start();

    /// Stops probing and listening; the list of found devices stays.
    void stop();

    /// The bound receive port, or 0 when not started.
    [[nodiscard]] quint16 receivePort() const;

    /// Phones found since start(), one entry per device id.
    [[nodiscard]] QList<DiscoveredDevice> devices() const { return m_devices; }

    /// Returns the wire form of a plain datagram text.
    [[nodiscard]] static QByteArray encodeDatagram(const QString &plain);

    /**
     * Decodes a datagram of the form "MyPhoneExplorer_Client|<ip>|<id>|<model>"
     * or "MyPhoneExplorer_Client_Broadcast|...". Returns nothing for anything
     * else, including an invalid address or overlong fields.
     */
    [[nodiscard]] static std::optional<DiscoveredDevice> decodeDatagram(const QByteArray &wire);

Q_SIGNALS:
    /// Emitted once per device id and again when its address or model changes.
    void deviceFound(const qpm::core::protocol::DiscoveredDevice &device);

private:
    void sendProbe();
    void readDatagrams();

    Ports m_ports;
    QUdpSocket *m_socket = nullptr;
    QTimer *m_probeTimer = nullptr;
    int m_probesSent = 0;
    QList<DiscoveredDevice> m_devices;
};

} // namespace qpm::core::protocol
