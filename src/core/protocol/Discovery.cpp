#include "core/protocol/Discovery.h"

#include "core/Logging.h"
#include "core/codec/Limits.h"

#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include <cctype>
#include <chrono>

namespace qpm::core::protocol {

namespace {

constexpr int probeCount = 3;
constexpr std::chrono::milliseconds probeInterval{500};
constexpr qsizetype maxFieldLength = 128;
constexpr char key[] = "MPE_Syskey2006";
constexpr qsizetype keyLength = sizeof(key) - 1;

QByteArray applyKey(const QByteArray &bytes)
{
    QByteArray result = bytes;
    for (qsizetype i = 0; i < result.size(); ++i)
        result[i] = char(quint8(result.at(i)) ^ quint8(key[i % keyLength]));
    return result;
}

} // namespace

Discovery::Discovery(QObject *parent) : Discovery(Ports{}, parent) {}

Discovery::Discovery(const Ports &ports, QObject *parent)
    : QObject(parent), m_ports(ports), m_socket(new QUdpSocket(this)),
      m_probeTimer(new QTimer(this))
{
    m_probeTimer->setInterval(probeInterval);
    connect(m_probeTimer, &QTimer::timeout, this, &Discovery::sendProbe);
    connect(m_socket, &QUdpSocket::readyRead, this, &Discovery::readDatagrams);
}

Result<void> Discovery::start()
{
    stop();
    m_devices.clear();
    const bool bound = m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), m_ports.receivePort,
                                      QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot listen on UDP port %1: %2")
                                               .arg(m_ports.receivePort)
                                               .arg(m_socket->errorString()));
    }
    m_probesSent = 0;
    sendProbe();
    m_probeTimer->start();
    return {};
}

void Discovery::stop()
{
    m_probeTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();
}

quint16 Discovery::receivePort() const
{
    return m_socket->state() == QAbstractSocket::BoundState ? m_socket->localPort() : 0;
}

QByteArray Discovery::encodeDatagram(const QString &plain)
{
    return applyKey(plain.toLatin1()).toHex().toUpper();
}

std::optional<DiscoveredDevice> Discovery::decodeDatagram(const QByteArray &wire)
{
    if (wire.isEmpty() || wire.size() > limits::maxDatagramBytes || wire.size() % 2 != 0)
        return std::nullopt;
    for (const char c : wire) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return std::nullopt;
    }
    // The client XORs the UTF-16 code of each character with the key and keeps
    // the low byte, so the plain text is Latin-1 by construction.
    const QString plain = QString::fromLatin1(applyKey(QByteArray::fromHex(wire)));
    const QStringList fields = plain.split(u'|');
    if (fields.size() != 4)
        return std::nullopt;
    if (fields.at(0) != QLatin1StringView("MyPhoneExplorer_Client") &&
        fields.at(0) != QLatin1StringView("MyPhoneExplorer_Client_Broadcast")) {
        return std::nullopt;
    }
    DiscoveredDevice device;
    if (!device.address.setAddress(fields.at(1)) || device.address.isNull())
        return std::nullopt;
    device.deviceId = fields.at(2).trimmed();
    device.model = fields.at(3).trimmed();
    if (device.deviceId.isEmpty() || device.deviceId.size() > maxFieldLength ||
        device.model.size() > maxFieldLength) {
        return std::nullopt;
    }
    return device;
}

void Discovery::sendProbe()
{
    if (m_probesSent >= probeCount) {
        m_probeTimer->stop();
        return;
    }
    ++m_probesSent;
    // The client answers any datagram on its port; the content is not read.
    const qint64 written = m_socket->writeDatagram(QByteArrayLiteral("QPhoneManager"),
                                                   m_ports.probeAddress, m_ports.probePort);
    // A lost probe needs no handling: the client also announces itself every
    // few seconds on its own.
    if (written < 0)
        qCWarning(lcProtocol) << "Discovery probe not sent:" << m_socket->errorString();
}

void Discovery::readDatagrams()
{
    while (m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram(limits::maxDatagramBytes + 1);
        const std::optional<DiscoveredDevice> device = decodeDatagram(datagram.data());
        if (!device) {
            qCDebug(lcProtocol) << "Discovery datagram ignored";
            continue;
        }
        bool changed = true;
        bool known = false;
        for (DiscoveredDevice &existing : m_devices) {
            if (existing.deviceId != device->deviceId)
                continue;
            known = true;
            changed = existing.address != device->address || existing.model != device->model;
            existing = *device;
            break;
        }
        if (!known)
            m_devices.append(*device);
        if (changed)
            Q_EMIT deviceFound(*device);
    }
}

} // namespace qpm::core::protocol
