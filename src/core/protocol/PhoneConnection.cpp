#include "core/protocol/PhoneConnection.h"

#include "core/Logging.h"
#include "core/codec/AtText.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/PinAuth.h"
#include "core/protocol/TcpTransport.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace qpm::core::protocol {

namespace {

constexpr quint16 atPort = 5210;
constexpr quint16 screenPort = 5213;
constexpr quint16 fileTransferPort = 5216;
constexpr quint16 thumbnailPort = 5217;
constexpr qsizetype maxDeviceIdLength = 128;
constexpr int maxPercent = 100;
constexpr int maxSignal = 99;
constexpr int maxApiLevel = 1000;
constexpr int minTemperature = -100;
constexpr int maxTemperature = 200;
constexpr int maxVoltage = 100000;

// The desktop version announced to the client. 2.3.0 enables every branch
// of client 4.15, among them categories and external storage.
QString appVersionCommand()
{
    return QStringLiteral("AT*APPVERSION=2.3.0");
}

// The fields after "<prefix>" in the first reply line that starts with it.
QStringList replyFields(const QStringList &lines, QLatin1StringView prefix)
{
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(prefix))
            return codec::splitFields(trimmed.mid(prefix.size()).trimmed());
    }
    return {};
}

std::optional<int> boundedInt(const QStringList &fields, qsizetype index, int minimum, int maximum)
{
    bool ok = false;
    const int value = fields.value(index).trimmed().toInt(&ok);
    if (!ok || value < minimum || value > maximum)
        return std::nullopt;
    return value;
}

Error closedError()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The connection to the phone ended"));
}

} // namespace

PhoneConnection::PhoneConnection(QObject *parent)
    : QObject(parent), m_statusTimer(new QTimer(this)), m_reconnectTimer(new QTimer(this))
{
    m_statusTimer->setInterval(m_options.statusInterval);
    connect(m_statusTimer, &QTimer::timeout, this, &PhoneConnection::refreshStatus);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &PhoneConnection::reconnect);
}

PhoneConnection::~PhoneConnection()
{
    // The members go in reverse order of declaration: OBEX client, session,
    // transport. None of their signals may reach this object meanwhile.
    ++m_generation;
    if (m_obex)
        m_obex->disconnect(this);
    if (m_session)
        m_session->disconnect(this);
    if (m_transport)
        m_transport->disconnect(this);
}

void PhoneConnection::setOptions(const Options &options)
{
    m_options = options;
    m_statusTimer->setInterval(m_options.statusInterval);
}

void PhoneConnection::connectTcp(const QString &host, quint16 port)
{
    m_reconnectTimer->stop();
    teardown();
    m_host = host;
    m_port = port;
    start(std::make_unique<TcpTransport>(host, port), Method::Tcp);
}

void PhoneConnection::connectAdb(const QString &adbPath, const QString &serial)
{
    m_reconnectTimer->stop();
    teardown();
    m_method = Method::Adb;
    m_adbPath = adbPath;
    m_serial = serial;
    m_forwards.clear();
    m_adb = std::make_unique<AdbBridge>(adbPath);
    setState(State::Connecting);

    const quint64 generation = m_generation;
    m_adb->startServer().then(this, [this, generation](const Result<void> &started) {
        if (generation != m_generation)
            return;
        if (!started.hasValue()) {
            onLost(started.error());
            return;
        }
        forwardNextPort({atPort, screenPort, fileTransferPort, thumbnailPort});
    });
}

void PhoneConnection::forwardNextPort(QList<quint16> phonePorts)
{
    const quint64 generation = m_generation;
    if (phonePorts.isEmpty()) {
        m_adb->startClient(m_serial).then(this, [this, generation](const Result<void> &started) {
            if (generation != m_generation)
                return;
            if (!started.hasValue()) {
                onLost(started.error());
                return;
            }
            m_host = QStringLiteral("127.0.0.1");
            m_port = m_forwards.value(atPort);
            start(std::make_unique<TcpTransport>(m_host, m_port), Method::Adb);
        });
        return;
    }
    const quint16 phonePort = phonePorts.takeFirst();
    m_adb->forward(m_serial, phonePort)
        .then(this, [this, generation, phonePort, phonePorts](const Result<quint16> &local) {
            if (generation != m_generation)
                return;
            if (!local.hasValue()) {
                onLost(local.error());
                return;
            }
            m_forwards.insert(phonePort, local.value());
            forwardNextPort(phonePorts);
        });
}

void PhoneConnection::connectTransport(std::unique_ptr<Transport> transport)
{
    m_reconnectTimer->stop();
    teardown();
    start(std::move(transport), Method::Transport);
}

Result<void> PhoneConnection::listenForPhone(quint16 port, const QHostAddress &address)
{
    if (!m_server) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server->hasPendingConnections()) {
                QTcpSocket *socket = m_server->nextPendingConnection();
                const bool busy = m_state != State::Disconnected && m_state != State::Lost &&
                                  m_state != State::Discovering;
                if (busy) {
                    qCInfo(lcProtocol) << "Second incoming phone connection refused";
                    socket->abort();
                    socket->deleteLater();
                    continue;
                }
                m_reconnectTimer->stop();
                teardown();
                m_host = socket->peerAddress().toString();
                m_port = socket->peerPort();
                start(std::make_unique<TcpTransport>(socket), Method::Accepted);
            }
        });
    }
    if (m_server->isListening())
        m_server->close();
    if (!m_server->listen(address, port)) {
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot listen on TCP port %1: %2")
                                               .arg(port)
                                               .arg(m_server->errorString()));
    }
    if (m_state == State::Disconnected || m_state == State::Lost)
        setState(State::Discovering);
    return {};
}

void PhoneConnection::stopListening()
{
    if (m_server)
        m_server->close();
    if (m_state == State::Discovering)
        setState(State::Disconnected);
}

bool PhoneConnection::isListening() const
{
    return m_server && m_server->isListening();
}

quint16 PhoneConnection::listeningPort() const
{
    return isListening() ? m_server->serverPort() : 0;
}

void PhoneConnection::submitPin(const QString &pin)
{
    if (!m_pinPending || !m_session)
        return;
    const quint64 generation = m_generation;
    m_session->send(QStringLiteral("AT*PIN=") + pinHash(pin))
        .then(this, [this, generation](const AtReply &reply) {
            if (generation != m_generation || reply.status == AtReply::Status::Closed)
                return;
            std::optional<PinStatus> pinStatus;
            for (const QString &line : reply.lines) {
                pinStatus = parsePinLine(line);
                if (pinStatus)
                    break;
            }
            if (!pinStatus) {
                onLost(
                    Error(ErrorCode::ProtocolError, QStringLiteral("No PIN state in the answer")));
                return;
            }
            if (pinStatus->state == PinStatus::State::AccessOk) {
                m_pinPending = false;
                continueHandshake();
                return;
            }
            Q_EMIT pinRequired(pinStatus->remainingTries);
        });
}

void PhoneConnection::disconnectFromPhone()
{
    m_reconnectTimer->stop();
    teardown();
    if (m_method == Method::Adb && m_adb) {
        for (auto it = m_forwards.cbegin(); it != m_forwards.cend(); ++it) {
            // A forward that stays behind is harmless: it leads nowhere once
            // the client is gone and ends with the adb server.
            m_adb->removeForward(m_serial, it.value()).then(this, [](const Result<void> &removed) {
                if (!removed.hasValue())
                    qCInfo(lcProtocol) << "adb forward not removed:" << removed.error().message();
            });
        }
    }
    m_forwards.clear();
    m_method = Method::None;
    setState(isListening() ? State::Discovering : State::Disconnected);
}

void PhoneConnection::refreshStatus()
{
    if (m_state != State::Ready || !m_session)
        return;
    const quint64 generation = m_generation;
    const QStringList commands = {QStringLiteral("AT+CBCEX"), QStringLiteral("AT+CSQ"),
                                  QStringLiteral("AT*ECPU")};
    for (qsizetype i = 0; i < commands.size(); ++i) {
        const bool last = i == commands.size() - 1;
        m_session->send(commands.at(i)).then(this, [this, generation, last](const AtReply &reply) {
            if (generation != m_generation)
                return;
            handleReply(reply);
            if (last)
                Q_EMIT statusUpdated(m_status);
        });
    }
}

AtSession *PhoneConnection::session() const
{
    return m_state == State::Ready || m_state == State::Busy ? m_session.get() : nullptr;
}

std::optional<Endpoint> PhoneConnection::sideChannelEndpoint(quint16 phonePort) const
{
    switch (m_method) {
    case Method::Tcp:
    case Method::Accepted:
        return Endpoint{m_host, phonePort};
    case Method::Adb:
        if (m_forwards.contains(phonePort))
            return Endpoint{QStringLiteral("127.0.0.1"), m_forwards.value(phonePort)};
        return std::nullopt;
    case Method::None:
    case Method::Transport:
        break;
    }
    return std::nullopt;
}

void PhoneConnection::start(std::unique_ptr<Transport> transport, Method method)
{
    m_method = method;
    m_identity = {};
    m_status = {};
    m_pinPending = false;
    m_transport = std::move(transport);
    m_session = std::make_unique<AtSession>(m_transport.get());
    m_obex = std::make_unique<ObexClient>(m_session.get());

    connect(m_transport.get(), &Transport::opened, this, &PhoneConnection::beginHandshake);
    connect(m_transport.get(), &Transport::failed, this, &PhoneConnection::onLost);
    connect(m_transport.get(), &Transport::closed, this, [this]() { onLost(closedError()); });
    connect(m_session.get(), &AtSession::fatal, this, &PhoneConnection::onLost);
    connect(m_session.get(), &AtSession::unsolicited, this, &PhoneConnection::handleUnsolicited);

    setState(State::Connecting);
    m_transport->open();
}

void PhoneConnection::beginHandshake()
{
    setState(State::Authenticating);
    const quint64 generation = m_generation;
    m_session->send(QStringLiteral("AT+CGSN")).then(this, [this, generation](const AtReply &reply) {
        if (generation != m_generation || reply.status == AtReply::Status::Closed)
            return;
        const QString deviceId = reply.lines.value(0).trimmed();
        if (reply.status != AtReply::Status::Ok || deviceId.isEmpty() ||
            deviceId.size() > maxDeviceIdLength) {
            onLost(Error(ErrorCode::ProtocolError,
                         QStringLiteral("The phone sent no usable device id (status %1)")
                             .arg(static_cast<int>(reply.status))));
            return;
        }
        m_identity.deviceId = deviceId;
    });
    m_session->send(QStringLiteral("AT*PIN?")).then(this, [this, generation](const AtReply &reply) {
        if (generation != m_generation || reply.status == AtReply::Status::Closed)
            return;
        std::optional<PinStatus> pinStatus;
        for (const QString &line : reply.lines) {
            pinStatus = parsePinLine(line);
            if (pinStatus)
                break;
        }
        if (reply.status != AtReply::Status::Ok || !pinStatus) {
            onLost(Error(ErrorCode::ProtocolError,
                         QStringLiteral("The phone sent no PIN state (status %1)")
                             .arg(static_cast<int>(reply.status))));
            return;
        }
        if (pinStatus->state == PinStatus::State::AccessProtected) {
            m_pinPending = true;
            Q_EMIT pinRequired(pinStatus->remainingTries);
            return;
        }
        continueHandshake();
    });
}

void PhoneConnection::continueHandshake()
{
    setState(State::Handshaking);
    QStringList commands = {
        QStringLiteral("ATI"),       QStringLiteral("AT+CGMI"),
        appVersionCommand(),         QStringLiteral("AT*ECAM=1"),
        QStringLiteral("AT+CBC"),    QStringLiteral("AT+CSQ"),
        QStringLiteral("AT*SOPS"),   QStringLiteral("AT*SIM"),
        QStringLiteral("AT+CMGS=?"), QStringLiteral("AT*ANDROIDVERSION"),
        QStringLiteral("AT*STATFS"),
    };
    if (m_options.notifications)
        commands.append(QStringLiteral("AT*NOTIFY=2"));
    if (m_options.clipboardWatch)
        commands.append(QStringLiteral("AT*CLIPWATCH=1"));

    const quint64 generation = m_generation;
    for (qsizetype i = 0; i < commands.size(); ++i) {
        const bool last = i == commands.size() - 1;
        m_session->send(commands.at(i)).then(this, [this, generation, last](const AtReply &reply) {
            if (generation != m_generation || reply.status == AtReply::Status::Closed)
                return;
            if (reply.status == AtReply::Status::Timeout) {
                onLost(Error(ErrorCode::Timeout,
                             QStringLiteral("The phone did not answer %1").arg(reply.command)));
                return;
            }
            handleReply(reply);
            if (last)
                finishHandshake();
        });
    }
}

void PhoneConnection::finishHandshake()
{
    setState(State::Ready);
    qCInfo(lcProtocol) << "Phone ready, Android API" << m_identity.androidApi << "client"
                       << m_identity.clientVersion;
    Q_EMIT deviceIdentified(m_identity);
    Q_EMIT statusUpdated(m_status);
    m_statusTimer->start();
    runNextObexJob();
}

void PhoneConnection::handleReply(const AtReply &reply)
{
    // These details are optional: a phone without the permission or the
    // feature answers with an error, and the field stays unknown.
    if (reply.status != AtReply::Status::Ok)
        return;
    const QString command = reply.command.trimmed();
    if (command == QLatin1StringView("ATI")) {
        m_identity.model = reply.lines.value(0).trimmed();
    } else if (command == QLatin1StringView("AT+CGMI")) {
        m_identity.manufacturer = reply.lines.value(0).trimmed();
    } else if (command == appVersionCommand()) {
        const QStringList fields = codec::splitFields(reply.lines.value(0).trimmed());
        m_identity.androidApi = boundedInt(fields, 0, 0, maxApiLevel).value_or(0);
        m_identity.clientVersion = fields.value(1).trimmed();
    } else if (command == QLatin1StringView("AT+CMGS=?")) {
        const QStringList fields = replyFields(reply.lines, QLatin1StringView("+CMGS:"));
        m_identity.smsCapabilities = fields.join(u',');
    } else if (command == QLatin1StringView("AT*ANDROIDVERSION")) {
        const QStringList fields = replyFields(reply.lines, QLatin1StringView("*ANDROIDVERSION:"));
        m_identity.androidVersion = codec::decodeQp(fields.value(0));
        m_identity.androidCodename = codec::decodeQp(fields.value(1));
    } else if (command == QLatin1StringView("AT+CBC")) {
        const QStringList fields = replyFields(reply.lines, QLatin1StringView("+CBC:"));
        if (const std::optional<int> plugged = boundedInt(fields, 0, 0, 1))
            m_status.charging = *plugged == 1;
        if (const std::optional<int> percent = boundedInt(fields, 1, 0, maxPercent))
            m_status.batteryPercent = percent;
    } else if (command == QLatin1StringView("AT+CBCEX")) {
        const QStringList fields = replyFields(reply.lines, QLatin1StringView("+CBCEX:"));
        if (const std::optional<int> percent = boundedInt(fields, 1, 0, maxPercent))
            m_status.batteryPercent = percent;
        if (const std::optional<int> temperature =
                boundedInt(fields, 2, minTemperature, maxTemperature))
            m_status.temperatureCelsius = temperature;
        if (const std::optional<int> voltage = boundedInt(fields, 3, 0, maxVoltage))
            m_status.voltageMillivolts = voltage;
    } else if (command == QLatin1StringView("AT+CSQ")) {
        const QStringList fields = replyFields(reply.lines, QLatin1StringView("+CSQ:"));
        if (const std::optional<int> signal = boundedInt(fields, 0, 0, maxSignal))
            m_status.signal = signal;
    } else if (command == QLatin1StringView("AT*ECPU")) {
        const QStringList fields = replyFields(reply.lines, QLatin1StringView("*ECPU:"));
        if (const std::optional<int> load = boundedInt(fields, 0, 0, maxPercent))
            m_status.cpuPercent = load;
    } else if (command == QLatin1StringView("AT*NOTIR")) {
        for (const QString &line : reply.lines) {
            if (line.trimmed().startsWith(QLatin1StringView("*NOTIR:")))
                Q_EMIT unsolicited(line.trimmed());
        }
    }
}

void PhoneConnection::handleUnsolicited(const QString &line)
{
    static constexpr QLatin1StringView alivePrefix("*ALIVE:");
    if (line.startsWith(alivePrefix)) {
        const QStringList fields = codec::splitFields(line.mid(alivePrefix.size()).trimmed());
        if (const std::optional<int> plugged = boundedInt(fields, 0, 0, 1))
            m_status.charging = *plugged == 1;
        if (const std::optional<int> percent = boundedInt(fields, 1, 0, maxPercent))
            m_status.batteryPercent = percent;
        Q_EMIT statusUpdated(m_status);
    }
    Q_EMIT unsolicited(line);
}

void PhoneConnection::onLost(const Error &error)
{
    if (m_state == State::Disconnected || m_state == State::Lost)
        return;
    Error reported = error;
    if (m_pinPending && error.code() == ErrorCode::ConnectionClosed) {
        reported = Error(ErrorCode::WrongPin,
                         QStringLiteral("The phone stopped after too many wrong PINs"));
    }
    qCWarning(lcProtocol) << "Phone connection lost:" << reported.message();
    teardown();
    setState(State::Lost);
    Q_EMIT failed(reported);
    if (m_state == State::Lost && m_options.reconnectDelay.count() > 0 &&
        (m_method == Method::Tcp || m_method == Method::Adb)) {
        m_reconnectTimer->start(m_options.reconnectDelay);
    }
}

void PhoneConnection::teardown()
{
    ++m_generation;
    m_statusTimer->stop();
    m_pinPending = false;
    m_obexRunning = false;

    std::deque<ObexJob> jobs;
    jobs.swap(m_obexJobs);
    for (const ObexJob &job : jobs)
        job.fail(closedError());

    // The objects may be in the middle of emitting the signal that led here,
    // so they are handed to the event loop for deletion instead of being
    // destroyed at once.
    if (m_obex) {
        m_obex->disconnect(this);
        m_obex.release()->deleteLater();
    }
    if (m_session) {
        m_session->disconnect(this);
        m_session.release()->deleteLater();
    }
    if (m_transport) {
        m_transport->disconnect(this);
        m_transport->close();
        m_transport.release()->deleteLater();
    }
}

void PhoneConnection::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

void PhoneConnection::enqueueObexJob(ObexJob job)
{
    if (m_state == State::Disconnected || m_state == State::Lost || m_state == State::Discovering) {
        job.fail(closedError());
        return;
    }
    m_obexJobs.push_back(std::move(job));
    runNextObexJob();
}

void PhoneConnection::runNextObexJob()
{
    if (m_obexRunning || m_state != State::Ready || m_obexJobs.empty())
        return;
    ObexJob job = std::move(m_obexJobs.front());
    m_obexJobs.pop_front();
    m_obexRunning = true;
    setState(State::Busy);

    const quint64 generation = m_generation;
    // Ends the OBEX session and hands the result over once the phone is back
    // in AT mode.
    const auto finish = [this, generation](std::function<void()> deliver) {
        if (generation != m_generation || m_session->isEnded()) {
            deliver();
            return;
        }
        m_obex->disconnectTarget().then(this, [this, generation, deliver](const Result<void> &) {
            deliver();
            if (generation == m_generation && !m_session->isEnded())
                afterObex();
        });
    };
    m_obex->connectTarget(job.target)
        .then(this, [this, generation, job, finish](const Result<void> &connected) {
            if (generation != m_generation) {
                job.fail(closedError());
                return;
            }
            if (!connected.hasValue()) {
                const Error error = connected.error();
                finish([job, error]() { job.fail(error); });
                return;
            }
            job.run(*m_obex, finish);
        });
}

void PhoneConnection::afterObex()
{
    m_obexRunning = false;
    setState(State::Ready);
    // The client drops its unsolicited lines while in OBEX mode, so whatever
    // changed meanwhile is asked for again.
    QStringList commands = {QStringLiteral("AT+CBC"), QStringLiteral("AT+CSQ")};
    if (m_options.notifications)
        commands.append(QStringLiteral("AT*NOTIR"));
    const quint64 generation = m_generation;
    for (qsizetype i = 0; i < commands.size(); ++i) {
        const bool last = i == commands.size() - 1;
        m_session->send(commands.at(i)).then(this, [this, generation, last](const AtReply &reply) {
            if (generation != m_generation)
                return;
            handleReply(reply);
            if (last)
                Q_EMIT statusUpdated(m_status);
        });
    }
    runNextObexJob();
}

void PhoneConnection::reconnect()
{
    if (m_state != State::Lost)
        return;
    if (m_method == Method::Tcp)
        connectTcp(m_host, m_port);
    else if (m_method == Method::Adb)
        connectAdb(m_adbPath, m_serial);
}

} // namespace qpm::core::protocol
