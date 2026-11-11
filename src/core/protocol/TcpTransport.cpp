#include "core/protocol/TcpTransport.h"

#include "core/Logging.h"

#include <QTcpSocket>
#include <QTimer>

#include <chrono>

namespace qpm::core::protocol {

namespace {
constexpr std::chrono::seconds connectTimeout{5};
}

TcpTransport::TcpTransport(const QString &host, quint16 port, QObject *parent)
    : Transport(parent), m_host(host), m_port(port), m_socket(new QTcpSocket(this))
{
    setUpSocket();
}

TcpTransport::TcpTransport(QTcpSocket *connectedSocket, QObject *parent)
    : Transport(parent), m_socket(connectedSocket), m_adopted(true)
{
    Q_ASSERT(connectedSocket);
    m_socket->setParent(this);
    m_host = m_socket->peerAddress().toString();
    m_port = m_socket->peerPort();
    setUpSocket();
}

void TcpTransport::setUpSocket()
{
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    m_connectTimer->setInterval(connectTimeout);
    connect(m_connectTimer, &QTimer::timeout, this, &TcpTransport::onConnectTimeout);

    connect(m_socket, &QTcpSocket::connected, this, &TcpTransport::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this,
            [this]() { Q_EMIT dataReceived(m_socket->readAll()); });
    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_connectTimer->stop();
        emitClosedOnce();
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError socketError) {
                if (socketError == QAbstractSocket::RemoteHostClosedError)
                    return;
                m_connectTimer->stop();
                qCWarning(lcProtocol) << "TCP transport error" << socketError;
                Q_EMIT failed(Error(
                    ErrorCode::ConnectionClosed,
                    QStringLiteral("TCP connection failed: %1").arg(m_socket->errorString())));
                if (m_socket->state() == QAbstractSocket::UnconnectedState)
                    emitClosedOnce();
            });
}

void TcpTransport::open()
{
    if (m_adopted) {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            onConnected();
        } else {
            Q_EMIT failed(Error(ErrorCode::ConnectionClosed,
                                QStringLiteral("The accepted connection is already closed")));
        }
        return;
    }
    m_connectTimer->start();
    m_socket->connectToHost(m_host, m_port);
}

void TcpTransport::close()
{
    m_connectTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();
}

void TcpTransport::write(const QByteArray &frame)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    m_socket->write(frame);
    m_socket->flush();
}

bool TcpTransport::isOpen() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QString TcpTransport::peerDescription() const
{
    return m_host;
}

void TcpTransport::emitClosedOnce()
{
    if (!m_wasOpen || m_closedEmitted)
        return;
    m_closedEmitted = true;
    Q_EMIT closed();
}

void TcpTransport::onConnected()
{
    m_connectTimer->stop();
    m_wasOpen = true;
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    Q_EMIT opened();
}

void TcpTransport::onConnectTimeout()
{
    qCWarning(lcProtocol) << "TCP connect timed out after" << connectTimeout.count() << "s";
    m_socket->abort();
    Q_EMIT failed(
        Error(ErrorCode::Timeout, QStringLiteral("No TCP connection to port %1 within %2 s")
                                      .arg(m_port)
                                      .arg(connectTimeout.count())));
}

} // namespace qpm::core::protocol
