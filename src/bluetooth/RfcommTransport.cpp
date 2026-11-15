#include "bluetooth/RfcommTransport.h"

#include "bluetooth/SerialPortProfile.h"
#include "core/Logging.h"

#include <QBluetoothSocket>
#include <QTimer>

#include <chrono>

namespace qpm {

namespace {
constexpr std::chrono::seconds connectTimeout{10};
}

using core::Error;
using core::ErrorCode;

RfcommTransport::RfcommTransport(const QBluetoothAddress &address, QObject *parent)
    : Transport(parent), m_address(address), m_connectTimer(new QTimer(this))
{
    auto *socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    m_stream = socket;
    m_connectTimer->setSingleShot(true);
    m_connectTimer->setInterval(connectTimeout);
    connect(m_connectTimer, &QTimer::timeout, this, [this, socket]() {
        qCWarning(core::lcProtocol) << "Bluetooth connect timed out";
        socket->abort();
        Q_EMIT failed(Error(ErrorCode::Timeout,
                            QStringLiteral("No Bluetooth connection within %1 s")
                                .arg(connectTimeout.count())));
    });
    connect(socket, &QBluetoothSocket::connected, this, &RfcommTransport::onConnected);
    connect(socket, &QBluetoothSocket::disconnected, this, [this]() {
        m_connectTimer->stop();
        emitClosedOnce();
    });
    connect(socket, &QBluetoothSocket::errorOccurred, this,
            [this, socket](QBluetoothSocket::SocketError socketError) {
                if (socketError == QBluetoothSocket::SocketError::RemoteHostClosedError)
                    return;
                m_connectTimer->stop();
                qCWarning(core::lcProtocol) << "Bluetooth transport error" << socketError;
                Q_EMIT failed(errorFor(socketError, socket->errorString()));
                if (socket->state() == QBluetoothSocket::SocketState::UnconnectedState)
                    emitClosedOnce();
            });
    watchStream();
}

RfcommTransport::RfcommTransport(QIODevice *stream, QObject *parent)
    : Transport(parent), m_stream(stream), m_connectTimer(new QTimer(this)), m_adopted(true)
{
    Q_ASSERT(stream);
    m_stream->setParent(this);
    connect(m_stream, &QIODevice::readChannelFinished, this, &RfcommTransport::emitClosedOnce);
    connect(m_stream, &QIODevice::aboutToClose, this, &RfcommTransport::emitClosedOnce);
    watchStream();
}

void RfcommTransport::watchStream()
{
    connect(m_stream, &QIODevice::readyRead, this, [this]() {
        if (m_open)
            Q_EMIT dataReceived(m_stream->readAll());
    });
}

void RfcommTransport::open()
{
    if (m_adopted) {
        if (m_stream->isOpen()) {
            onConnected();
        } else {
            Q_EMIT failed(
                Error(ErrorCode::ConnectionClosed, QStringLiteral("The stream is not open")));
        }
        return;
    }
    m_connectTimer->start();
    qobject_cast<QBluetoothSocket *>(m_stream)->connectToService(m_address,
                                                                 serialPortProfileUuid());
}

void RfcommTransport::close()
{
    m_connectTimer->stop();
    if (auto *socket = qobject_cast<QBluetoothSocket *>(m_stream)) {
        if (socket->state() != QBluetoothSocket::SocketState::UnconnectedState)
            socket->disconnectFromService();
        return;
    }
    if (m_stream->isOpen())
        m_stream->close();
}

void RfcommTransport::write(const QByteArray &frame)
{
    if (!isOpen())
        return;
    m_stream->write(frame);
}

bool RfcommTransport::isOpen() const
{
    return m_open && !m_closedEmitted && m_stream->isOpen();
}

QString RfcommTransport::peerDescription() const
{
    return QStringLiteral("Bluetooth");
}

Error RfcommTransport::errorFor(QBluetoothSocket::SocketError socketError, const QString &detail)
{
    const QString message = QStringLiteral("Bluetooth connection failed: %1").arg(detail);
    // BlueZ reports a phone app without Bluetooth access the same way as a
    // service list read before the app offered its service: the host or the
    // service is not found.
    if (socketError == QBluetoothSocket::SocketError::HostNotFoundError ||
        socketError == QBluetoothSocket::SocketError::ServiceNotFoundError) {
        return Error(ErrorCode::BluetoothServiceNotFound, message);
    }
    return Error(ErrorCode::ConnectionClosed, message);
}

void RfcommTransport::onConnected()
{
    m_connectTimer->stop();
    m_open = true;
    Q_EMIT opened();
    // Bytes that arrived before the stream was handed over are not lost.
    if (m_stream->bytesAvailable() > 0)
        Q_EMIT dataReceived(m_stream->readAll());
}

void RfcommTransport::emitClosedOnce()
{
    if (!m_open || m_closedEmitted)
        return;
    m_closedEmitted = true;
    Q_EMIT closed();
}

} // namespace qpm
