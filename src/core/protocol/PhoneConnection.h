#pragma once

#include "core/Error.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/Transport.h"

#include <QFuture>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QPromise>
#include <QString>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

class QTcpServer;
class QTimer;

namespace qpm::core::protocol {

class AdbBridge;

/// What the phone tells about itself during the handshake.
struct DeviceIdentity
{
    /// Answer to AT+CGSN; the key of the device profile.
    QString deviceId;
    QString model;
    QString manufacturer;
    int androidApi = 0;
    QString clientVersion;
    QString androidVersion;
    QString androidCodename;
    /// Answer to AT+CMGS=?, for example "TXTv2,PING".
    QString smsCapabilities;
};

/// Values that change while connected. Unset fields are not known yet.
struct DeviceStatus
{
    std::optional<int> batteryPercent;
    std::optional<bool> charging;
    std::optional<int> temperatureCelsius;
    std::optional<int> voltageMillivolts;
    /// 0 to 31, 99 for unknown, as the phone reports it.
    std::optional<int> signal;
    std::optional<int> cpuPercent;
};

/// Host and port to reach one of the phone's side channels.
struct Endpoint
{
    QString host;
    quint16 port = 0;
};

/**
 * The connection to one phone: transport, AT session and OBEX client, the
 * handshake with PIN, the status updates and the hand-over between AT and
 * OBEX. Lives in the main thread and never blocks.
 *
 * States move forward from Disconnected over Connecting, Authenticating and
 * Handshaking to Ready, and Busy while an OBEX session runs. Every state can
 * fall back to Disconnected (on request) or Lost (on a failure, reported
 * through failed()). Discovering means waiting for the phone to connect to
 * this computer after listenForPhone().
 *
 * The connection owns its transport, session and OBEX client. Pointers handed
 * out by session() stay valid until the state leaves Ready or Busy.
 */
class PhoneConnection : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Discovering,
        Connecting,
        Authenticating,
        Handshaking,
        Ready,
        Busy,
        Lost,
    };
    Q_ENUM(State)

    struct Options
    {
        /// Sends AT*NOTIFY=2 in the handshake and AT*NOTIR after each OBEX session.
        bool notifications = false;
        /// Sends AT*CLIPWATCH=1 in the handshake.
        bool clipboardWatch = false;
        /// How often battery, signal and processor load are asked for.
        std::chrono::milliseconds statusInterval = std::chrono::seconds(30);
        /// Delay before connecting again after Lost; zero turns reconnecting off.
        std::chrono::milliseconds reconnectDelay{0};
        /**
         * How long the phone may leave a command of this connection
         * unanswered. A command without an answer in this time means the
         * link is lost.
         */
        std::chrono::milliseconds commandTimeout = std::chrono::seconds(15);
    };

    explicit PhoneConnection(QObject *parent = nullptr);
    ~PhoneConnection() override;

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] const DeviceIdentity &identity() const { return m_identity; }
    [[nodiscard]] const DeviceStatus &status() const { return m_status; }

    /// Takes effect with the next connection, except the status interval.
    void setOptions(const Options &options);

    /// Connects over TCP, normally to port 5210 of the phone.
    void connectTcp(const QString &host, quint16 port = 5210);

    /**
     * Connects over USB: starts the adb server, forwards free local ports to
     * the phone ports 5210, 5213, 5216 and 5217, starts the client and
     * connects to the forwarded 5210. `adbPath` must be absolute.
     */
    void connectAdb(const QString &adbPath, const QString &serial);

    /// Connects over any other transport, for example Bluetooth.
    void connectTransport(std::unique_ptr<qpm::core::protocol::Transport> transport);

    /**
     * Waits on `port` of `address` for the phone to connect to this computer,
     * as it does after scanning the QR code. The first connection is taken
     * and the handshake runs as usual; listening goes on until
     * stopListening(). Fails with ErrorCode::IoFailure when the port cannot
     * be bound. Port 0 picks a free port, see listeningPort().
     */
    [[nodiscard]] Result<void> listenForPhone(quint16 port, const QHostAddress &address);
    void stopListening();
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] quint16 listeningPort() const;

    /**
     * Sends the PIN after pinRequired(); ignored at any other time and while
     * an earlier PIN still waits for its answer.
     */
    void submitPin(const QString &pin);

    /// Ends the connection on request; no reconnect follows.
    void disconnectFromPhone();

    /**
     * Asks for battery, signal and processor load now; statusUpdated()
     * follows. While an earlier round still waits for its answers, no new
     * one starts and the earlier one's statusUpdated() is the one that
     * follows. A command of the round that gets no answer within
     * Options::commandTimeout counts as a lost link.
     */
    void refreshStatus();

    /// The AT session while Ready or Busy, otherwise null.
    [[nodiscard]] AtSession *session() const;

    /**
     * Where to reach the phone's TCP side channel `phonePort` (5213, 5216 or
     * 5217): the phone's address over Wi-Fi, a forwarded local port over USB,
     * nothing over other transports.
     */
    [[nodiscard]] std::optional<Endpoint> sideChannelEndpoint(quint16 phonePort) const;

    /**
     * Runs `work` inside an OBEX session on `target`. Sessions run one after
     * another; each one is connected before `work` starts and disconnected
     * after its future ends, and the status is asked for again afterwards,
     * since the phone drops its own messages during OBEX. The returned future
     * ends after the disconnect. Fails with ErrorCode::ConnectionClosed when
     * the connection ends or this object is destroyed first, also while `work`
     * still runs; what `work` delivers later is then dropped.
     *
     * `work` gets a client that stays valid until its future ends or the
     * connection ends, whichever comes first. Work that can outlive the
     * connection holds the client in a QPointer.
     */
    template <typename T>
    QFuture<Result<T>> withObex(ObexClient::Target target,
                                std::function<QFuture<Result<T>>(ObexClient &)> work);

Q_SIGNALS:
    void stateChanged(qpm::core::protocol::PhoneConnection::State state);
    /// The phone needs the PIN; `remainingTries` 0 means it locks on the next try.
    void pinRequired(int remainingTries);
    void deviceIdentified(const qpm::core::protocol::DeviceIdentity &identity);
    void statusUpdated(const qpm::core::protocol::DeviceStatus &status);
    /// Lines the phone sent on its own, and the AT*NOTIR lines after OBEX.
    void unsolicited(const QString &line);
    void failed(const qpm::core::Error &error);

private:
    enum class Method { None, Tcp, Adb, Transport, Accepted };

    struct ObexJob
    {
        ObexClient::Target target = ObexClient::Target::IrmcSync;
        /// Starts the work; the callback gets what to deliver after the disconnect.
        std::function<void(ObexClient &, std::function<void(std::function<void()>)>)> run;
        std::function<void(const Error &)> fail;
    };

    void start(std::unique_ptr<Transport> transport, Method method);
    void beginHandshake();
    void continueHandshake();
    void finishHandshake();
    void handleUnsolicited(const QString &line);
    void handleReply(const AtReply &reply);
    /**
     * A command without an answer means that the phone or the link is gone:
     * on status Timeout the connection counts as lost, so that reconnecting
     * can start, and true is returned.
     */
    bool lostOnTimeout(const AtReply &reply);
    void onLost(const Error &error);
    void teardown();
    /**
     * Ends the running and the queued OBEX jobs. The running job gets the
     * result of its work when only the disconnect was still open, and fails
     * with ErrorCode::ConnectionClosed otherwise.
     */
    void failObexJobs();
    void setState(State state);
    void forwardNextPort(QList<quint16> phonePorts);
    void enqueueObexJob(ObexJob job);
    void runNextObexJob();
    void afterObex();
    void reconnect();

    Options m_options;
    State m_state = State::Disconnected;
    Method m_method = Method::None;
    QString m_host;
    quint16 m_port = 0;
    QString m_adbPath;
    QString m_serial;
    QHash<quint16, quint16> m_forwards;
    DeviceIdentity m_identity;
    DeviceStatus m_status;
    quint64 m_generation = 0;
    bool m_pinPending = false;
    /// An AT*PIN= is on its way and its answer has not arrived.
    bool m_pinInFlight = false;
    bool m_statusPending = false;
    bool m_obexRunning = false;
    std::deque<ObexJob> m_obexJobs;
    /// Fails the running OBEX job while its work runs.
    std::function<void(const Error &)> m_runningObexFail;
    /// Hands over the result of the running OBEX job while its session is disconnected.
    std::function<void()> m_pendingObexDelivery;
    QTcpServer *m_server = nullptr;
    QTimer *m_statusTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    std::unique_ptr<AdbBridge> m_adb;
    std::unique_ptr<Transport> m_transport;
    std::unique_ptr<AtSession> m_session;
    std::unique_ptr<ObexClient> m_obex;
};

template <typename T>
QFuture<Result<T>> PhoneConnection::withObex(ObexClient::Target target,
                                             std::function<QFuture<Result<T>>(ObexClient &)> work)
{
    auto promise = std::make_shared<QPromise<Result<T>>>();
    promise->start();
    QFuture<Result<T>> future = promise->future();
    // The connection can fail the job while the work still runs, and the
    // work ends later; the first result is kept.
    const auto settle = [promise](const Result<T> &result) {
        if (promise->future().isFinished())
            return;
        promise->addResult(result);
        promise->finish();
    };

    ObexJob job;
    job.target = target;
    job.fail = [settle](const Error &error) { settle(Result<T>(error)); };
    job.run = [this, settle, work = std::move(work)](
                  ObexClient &obex, std::function<void(std::function<void()>)> done) {
        work(obex)
            .then(this,
                  [settle, done](const Result<T> &result) {
                      done([settle, result]() { settle(result); });
                  })
            .onCanceled(this, [settle, done]() {
                done([settle]() {
                    settle(Result<T>(Error(ErrorCode::Cancelled,
                                           QStringLiteral("The OBEX work was cancelled"))));
                });
            });
    };
    enqueueObexJob(std::move(job));
    return future;
}

} // namespace qpm::core::protocol
