#pragma once

#include "core/protocol/Transport.h"

#include <QString>

class QTcpSocket;
class QTimer;

namespace qpm::core::protocol {

/**
 * Transport over TCP. Either connects to a host and port, or adopts a socket
 * that a server accepted when the phone connected to this computer.
 *
 * Connecting gives up after five seconds with ErrorCode::Timeout. The socket
 * runs with TCP_NODELAY and keep-alive.
 *
 * The connection is not encrypted, although network connections are meant to
 * use TLS. The phone app speaks its protocol in plain text only and offers no
 * TLS, and QPhoneManager has to stay compatible with that app.
 */
class TcpTransport : public Transport
{
    Q_OBJECT

public:
    TcpTransport(const QString &host, quint16 port, QObject *parent = nullptr);

    /**
     * Adopts an already connected socket and takes ownership of it. open()
     * then reports opened() without connecting again.
     */
    explicit TcpTransport(QTcpSocket *connectedSocket, QObject *parent = nullptr);

    void open() override;
    void close() override;
    void write(const QByteArray &frame) override;
    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString peerDescription() const override;

private:
    void setUpSocket();
    void onConnected();
    void onConnectTimeout();
    void emitClosedOnce();

    QString m_host;
    quint16 m_port = 0;
    QTcpSocket *m_socket = nullptr;
    QTimer *m_connectTimer = nullptr;
    bool m_adopted = false;
    bool m_wasOpen = false;
    bool m_closedEmitted = false;
};

} // namespace qpm::core::protocol
