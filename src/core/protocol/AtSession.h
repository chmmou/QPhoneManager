#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QFuture>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QStringList>

#include <chrono>
#include <deque>
#include <memory>

class QTimer;

namespace qpm::core::protocol {

class Transport;

/// The answer of the phone client to one AT command.
struct AtReply
{
    enum class Status {
        Ok,
        Error,
        /// "+CMS ERROR: <n>"; detail holds n.
        CmsError,
        /// "<X>: PERMISSION DENIED", or a service the user has to enable
        /// ("NOT BOUND", "NOT ENABLED", "DISABLED"); detail holds the reason.
        PermissionDenied,
        NotSupported,
        /// "ACCESS DENIED": a PIN is required. The phone closes the connection.
        AccessDenied,
        /// "DUMP: <exception>" before ERROR; detail holds the exception text.
        Dump,
        Timeout,
        Closed,
    };

    Status status = Status::Ok;
    /// The command as passed to AtSession::send().
    QString command;
    /// Reply lines without the echo, the final line and unsolicited lines.
    QStringList lines;
    QString detail;
};

/**
 * Line-based AT session with the phone client on top of a Transport. Lives in
 * the main thread and never blocks.
 *
 * One command is on the wire at a time; further commands wait in a queue and
 * go out in the order of send(). Each command is written with one write. Lines
 * the client sends on its own (alive, notifications, new messages and so on)
 * are reported through unsolicited() at any time, also in the middle of a
 * reply. The four connection-level answers "ACCESS DENIED", "ALREADY
 * CONNECTED", "WIFI DISABLED" and "CONNECTION CHANGE" end the session: fatal()
 * is emitted and every open and later command completes with an error status.
 *
 * In OBEX mode, entered with enterObex(), every received byte goes to rawData()
 * and queued commands wait until leaveObex().
 *
 * The session does not own the transport; the transport must outlive it.
 */
class AtSession : public QObject
{
    Q_OBJECT

public:
    explicit AtSession(Transport *transport, QObject *parent = nullptr);

    /**
     * Queues a command without the line end. The future gets the reply, or a
     * reply with status Timeout when no line of the reply arrives for
     * `timeout`; every line of the reply restarts that time, so long lists do
     * not time out. Lines the client sends on its own do not restart it.
     * Cancelling the future before the command is written drops it from the
     * queue. The command arguments never reach the log.
     */
    QFuture<AtReply> send(const QString &command,
                          std::chrono::milliseconds timeout = std::chrono::seconds(15));

    /**
     * Sends "AT+CPROT=0" and switches to OBEX mode when the client answers
     * "CONNECT". Fails with ErrorCode::ProtocolError on any other answer.
     */
    QFuture<Result<void>> enterObex();

    /**
     * Returns to line mode after the OBEX DISCONNECT has been answered, and
     * sends the commands that waited meanwhile. `leftover` holds bytes that
     * arrived after the last OBEX packet; they are read as AT lines.
     */
    void leaveObex(const QByteArray &leftover = {});

    [[nodiscard]] bool isInObexMode() const { return m_obexMode; }

    /// Returns true once the session has ended through fatal() or a closed transport.
    [[nodiscard]] bool isEnded() const { return m_ended; }

    /// Writes raw bytes in OBEX mode; ignored in line mode.
    void writeRaw(const QByteArray &bytes);

    /**
     * Reads `line`, a line the client sent on its own that arrived inside the
     * OBEX stream, the way a line in line mode is read: unsolicited() and the
     * connection-level answers take effect, anything else is dropped. A
     * trailing CR is ignored. Does nothing once the session has ended.
     */
    void takeLateLine(const QByteArray &line);

    /**
     * Ends the session because the link can no longer be trusted: fatal()
     * carries `error`, every open command completes with status Closed and
     * the transport is closed. Does nothing once the session has ended.
     */
    void terminate(const qpm::core::Error &error);

Q_SIGNALS:
    void unsolicited(const QString &line);
    /// Bytes received in OBEX mode.
    void rawData(const QByteArray &chunk);
    /// The client ended the session; the transport is about to close.
    void fatal(const qpm::core::Error &error);
    /// The session ended, through fatal() or because the transport closed.
    void ended();

private:
    struct Pending
    {
        QString command;
        QByteArray wire;
        QStringList echoes;
        QString name;
        std::chrono::milliseconds timeout{0};
        QPromise<AtReply> promise;
        AtReply reply;
        qsizetype replyBytes = 0;
        bool echoSeen = false;
        bool expectConnect = false;
    };

    void onData(const QByteArray &chunk);
    void onTransportClosed();
    void processBuffer();
    void processLine(const QString &line);
    bool handleConnectionLevelLine(const QString &line);
    bool belongsToCurrent(const QString &lineName) const;
    /// Judged by the name in front of the colon, like a whole line. A partial
    /// line too short to show its name does not count yet.
    bool partialLineBelongsToCurrent() const;
    void complete(AtReply::Status status);
    void startNext();
    void endSession(AtReply::Status statusForCurrent);
    void onTimeout();

    Transport *m_transport = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_buffer;
    std::unique_ptr<Pending> m_current;
    std::deque<std::unique_ptr<Pending>> m_queue;
    QString m_pendingCdsLine;
    bool m_obexMode = false;
    bool m_ended = false;
};

} // namespace qpm::core::protocol
