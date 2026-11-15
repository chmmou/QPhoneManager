#pragma once

#include "core/protocol/Transport.h"

#include <QBluetoothAddress>
#include <QBluetoothSocket>
#include <QString>

class QIODevice;
class QTimer;

namespace qpm {

/**
 * Transport over a Bluetooth RFCOMM link to the Serial Port Profile of a
 * paired phone, the only service MyPhoneExplorer Client offers over
 * Bluetooth. Lives in the main thread.
 *
 * Connecting gives up after ten seconds with ErrorCode::Timeout. The link is
 * not encrypted beyond what the Bluetooth pairing provides; the phone app
 * offers nothing else.
 */
class RfcommTransport : public core::protocol::Transport
{
    Q_OBJECT

public:
    /// Connects to the Serial Port Profile of the phone at `address` on open().
    explicit RfcommTransport(const QBluetoothAddress &address, QObject *parent = nullptr);

    /**
     * Uses `stream`, which must already be open, instead of a Bluetooth
     * socket, and takes ownership of it. open() then reports opened() at
     * once. The stream ends when its read channel finishes or it closes.
     */
    explicit RfcommTransport(QIODevice *stream, QObject *parent = nullptr);

    void open() override;
    void close() override;
    void write(const QByteArray &frame) override;
    [[nodiscard]] bool isOpen() const override;
    /// Always "Bluetooth".
    [[nodiscard]] QString peerDescription() const override;

    /**
     * The failure reported for `socketError`, with `detail` in the message.
     * A phone that cannot be found, or that does not offer the serial port
     * service, gives ErrorCode::BluetoothServiceNotFound; any other error
     * gives ErrorCode::ConnectionClosed.
     */
    [[nodiscard]] static core::Error errorFor(QBluetoothSocket::SocketError socketError,
                                              const QString &detail);

private:
    void watchStream();
    void onConnected();
    void emitClosedOnce();

    QBluetoothAddress m_address;
    QIODevice *m_stream = nullptr;
    QTimer *m_connectTimer = nullptr;
    bool m_adopted = false;
    bool m_open = false;
    bool m_closedEmitted = false;
};

} // namespace qpm
