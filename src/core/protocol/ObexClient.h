#pragma once

#include "core/Error.h"
#include "core/protocol/ObexPacket.h"

#include <QByteArray>
#include <QDateTime>
#include <QFlags>
#include <QFuture>
#include <QObject>
#include <QString>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

class QTimer;

namespace qpm::core::protocol {

class AtSession;

/// The final answer of the phone to an OBEX request.
struct ObexResponse
{
    quint8 code = 0;
    /// The whole body, assembled from all chunks.
    QByteArray body;
    QByteArray appParams;
    QString name;
    /// Length-Big when the phone sent it, otherwise Length.
    std::optional<quint64> length;
};

/**
 * OBEX client over an AtSession, for the targets IRMC-SYNC and Filebrowser of
 * the phone client. Lives in the main thread and never blocks.
 *
 * Requests run one after another in the order they are made. Each packet goes
 * out with one write. A GET body that arrives in several chunks is assembled;
 * a keep-alive the phone sends while it works is answered with exactly the
 * three bytes 83 00 03. A GET body larger than 64 MiB on IRMC-SYNC or 512 MiB
 * on Filebrowser is aborted with ErrorCode::LimitExceeded. Cancelling a
 * returned future aborts the request on the phone.
 *
 * Response codes other than OK and Created map to ErrorCode values:
 * C1 PermissionDenied, C3 AccessDenied, C4 and E1 NotFound, C8 Timeout, C9
 * Rejected, D1 NotSupported, E0 IoFailure (phone storage full), everything else
 * ProtocolError; of an internal error (D0) only the class name of the phone's
 * exception is kept. When the session ends, open requests fail with
 * ErrorCode::ConnectionClosed.
 *
 * A request without an answer in time fails with ErrorCode::Timeout and ends
 * the AT session through AtSession::terminate(), which closes the transport;
 * the requests waiting behind it fail with ErrorCode::ConnectionClosed.
 *
 * The client does not own the session; the session must outlive it.
 */
class ObexClient : public QObject
{
    Q_OBJECT

public:
    enum class Target { IrmcSync, FileBrowser };
    Q_ENUM(Target)

    enum class SetPathFlag {
        None = 0x00,
        /// Go to the parent folder.
        Backup = 0x01,
        /// Fail with NotFound instead of creating a missing folder.
        DontCreate = 0x02,
    };
    Q_DECLARE_FLAGS(SetPathFlags, SetPathFlag)

    /// How far a folder listing reaches.
    enum class ListingDepth {
        Folder,
        /// The folder and every folder below it, one listing block each.
        Tree,
    };

    struct PutOptions
    {
        /// Sent as the Time header in UTC when set.
        std::optional<QDateTime> modified;
    };

    explicit ObexClient(AtSession *session, QObject *parent = nullptr);

    /// Switches the session to OBEX mode and connects to `target`.
    QFuture<Result<void>> connectTarget(Target target);

    /**
     * Reads the object `name`, or the current folder listing when `name` is
     * empty and `type` is "x-obex/folder-listing". Every received chunk
     * restarts `timeout`. With `maxBodyBytes`, a body above it is aborted
     * with ErrorCode::LimitExceeded instead of the limit of the target.
     */
    QFuture<Result<ObexResponse>> get(const QString &name, const QByteArray &type = {},
                                      std::chrono::milliseconds timeout = std::chrono::seconds(90),
                                      std::optional<quint64> maxBodyBytes = std::nullopt);

    /**
     * Reads the listing of the current Filebrowser folder as the phone sends
     * it, see codec::parseFolderListing(). Every received chunk and every
     * keep-alive restarts `timeout`.
     */
    QFuture<Result<ObexResponse>>
    listFolder(ListingDepth depth, std::chrono::milliseconds timeout = std::chrono::seconds(30));

    /**
     * Writes `body` to `name` in a single packet. Fails with
     * ErrorCode::LimitExceeded without sending when the packet would exceed
     * the negotiated packet size.
     */
    QFuture<Result<ObexResponse>> put(const QString &name, const QByteArray &body,
                                      const PutOptions &options = {});

    /// Deletes `name`: a PUT without a body.
    QFuture<Result<ObexResponse>> remove(const QString &name);

    /// Changes the folder; an empty `name` without Backup goes to the root.
    QFuture<Result<void>> setPath(const QString &name, SetPathFlags flags);

    /// Renames or moves `from` to `to` on Filebrowser.
    QFuture<Result<void>> move(const QString &from, const QString &to);

    /**
     * Asks the phone to send the file `name` over a separate TCP connection.
     * The response carries the length and, in appParams, "TCP:<port>".
     */
    QFuture<Result<ObexResponse>> socketGet(const QString &name);

    /**
     * Announces an upload of `length` bytes to `name` over a separate TCP
     * connection. The response carries the final file name and, in
     * appParams, "TCP:<port>".
     */
    QFuture<Result<ObexResponse>> socketPut(const QString &name, quint64 length,
                                            const QDateTime &modified);

    /// Waits until the phone has finished a separate TCP transfer.
    QFuture<Result<void>> socketStatus();

    /// Aborts the running request, or sends a plain ABORT when none runs.
    QFuture<Result<void>> abort();

    /**
     * Disconnects the target and returns the session to AT mode. Succeeds at
     * once when the session is not in OBEX mode.
     */
    QFuture<Result<void>> disconnectTarget();

    [[nodiscard]] bool isConnected() const { return m_connected; }

    /**
     * Returns the LUID from the App-Parameters of a create answer, which the
     * phone sends as two spaces, twelve hex digits and one space.
     */
    [[nodiscard]] static std::optional<quint32> createdLuid(const QByteArray &appParams);

    /// Returns the port from App-Parameters of the form "TCP:<port>".
    [[nodiscard]] static std::optional<quint16> sideChannelPort(const QByteArray &appParams);

Q_SIGNALS:
    /// Body bytes received so far; during a sync start the phone's own count.
    void progress(quint64 bytesDone, std::optional<quint64> bytesTotal);

private:
    struct Request
    {
        quint8 opcode = 0;
        QByteArray packet;
        ObexPacket::Layout responseLayout = ObexPacket::Layout::Plain;
        std::chrono::milliseconds timeout{0};
        quint64 maxBodyBytes = 0;
        bool progressBodies = false;
        bool endsObex = false;
        std::function<void(const Result<ObexPacket> &)> done;
        std::function<bool()> isCanceled;
        QByteArray body;
        int outstanding = 0;
        bool aborting = false;
        Error abortError;
        std::function<void(const Result<void> &)> abortDone;
    };

    [[nodiscard]] std::unique_ptr<Request> makeRequest(ObexPacket packet,
                                                       std::chrono::milliseconds timeout);
    QFuture<Result<ObexResponse>> enqueueForResponse(std::unique_ptr<Request> request);
    QFuture<Result<void>> enqueueForSuccess(std::unique_ptr<Request> request);
    void enqueue(std::unique_ptr<Request> request);
    void startNext();
    void writePacket(const QByteArray &bytes);
    void onRawData(const QByteArray &chunk);
    void handlePacket(const QByteArray &bytes);
    void startAbort(const Error &reason);
    void finishCurrent(const Result<ObexPacket> &result);
    void complete(std::unique_ptr<Request> request, const Result<ObexPacket> &result);
    void failAll(const Error &error);
    void onTimeout();

    AtSession *m_session = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_buffer;
    std::unique_ptr<Request> m_current;
    std::deque<std::unique_ptr<Request>> m_queue;
    quint32 m_connectionId = 1;
    quint16 m_maxPacket = 0xFFFF;
    Target m_target = Target::IrmcSync;
    bool m_connected = false;
};

} // namespace qpm::core::protocol

Q_DECLARE_OPERATORS_FOR_FLAGS(qpm::core::protocol::ObexClient::SetPathFlags)
