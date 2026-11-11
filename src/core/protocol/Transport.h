#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace qpm::core::protocol {

/**
 * A byte stream to the phone client: TCP, a TCP connection through adb or an
 * RFCOMM link. All calls and signals happen in the thread the object lives
 * in, which is the main thread.
 *
 * After open() exactly one of opened() or failed() follows. After opened(),
 * the stream ends with closed(), possibly preceded by failed().
 */
class Transport : public QObject
{
    Q_OBJECT

public:
    explicit Transport(QObject *parent = nullptr) : QObject(parent) {}

    /// Starts opening the stream; the result arrives as opened() or failed().
    virtual void open() = 0;

    /// Closes the stream. closed() follows unless the stream was never open.
    virtual void close() = 0;

    /**
     * Sends one frame with one write and a flush, so that the phone reads it
     * as one unit. Has no effect while the stream is not open.
     */
    virtual void write(const QByteArray &frame) = 0;

    [[nodiscard]] virtual bool isOpen() const = 0;

    /**
     * Describes the far end for the status line, for example "192.0.2.10",
     * "USB" or "Bluetooth".
     */
    [[nodiscard]] virtual QString peerDescription() const = 0;

Q_SIGNALS:
    void opened();
    void dataReceived(const QByteArray &chunk);
    void failed(const qpm::core::Error &error);
    void closed();
};

} // namespace qpm::core::protocol
