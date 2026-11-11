#include "core/protocol/AtSession.h"

#include "core/Logging.h"
#include "core/codec/Limits.h"
#include "core/protocol/Transport.h"

#include <QTimer>

#include <array>

namespace qpm::core::protocol {

namespace {

// Commands whose text can be long. The client keeps reading such a command
// until it ends with a comma, so one that does not end with one would stall.
constexpr std::array longTextPrefixes = {
    QLatin1StringView("AT*CLIPBOARD="),
    QLatin1StringView("AT*KBRD=2,"),
    QLatin1StringView("AT*T="),
    QLatin1StringView("AT*SELTEXT="),
};

constexpr std::array unsolicitedNames = {
    QLatin1StringView("*ALIVE"),  QLatin1StringView("*ECAV"),    QLatin1StringView("*CLIPWATCH"),
    QLatin1StringView("*NOTIFY"), QLatin1StringView("+CMTI"),    QLatin1StringView("+CDS"),
    QLatin1StringView("+CREG"),   QLatin1StringView("*EJAVA"),   QLatin1StringView("*STARTSYNC"),
    QLatin1StringView("*KBRD"),   QLatin1StringView("*BARCODE"), QLatin1StringView("*CMGDEFAPP"),
};

// The name of a command for matching reply lines: "AT*EJAVA=1" -> "*EJAVA",
// "AT+CREG?" -> "+CREG".
QString commandName(const QString &command)
{
    QString name = command.trimmed();
    if (name.startsWith(QLatin1StringView("AT"), Qt::CaseInsensitive))
        name.remove(0, 2);
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (name.at(i) == u'=' || name.at(i) == u'?')
            return name.left(i).toUpper();
    }
    return name.toUpper();
}

// The name in front of the colon of a line such as "*ALIVE: 0,72,0".
QString lineName(const QString &line)
{
    if (line.isEmpty() || (line.front() != u'*' && line.front() != u'+'))
        return {};
    const qsizetype colon = line.indexOf(u':');
    return colon < 0 ? QString() : line.left(colon).trimmed().toUpper();
}

bool isUnsolicitedName(const QString &name)
{
    for (const QLatin1StringView candidate : unsolicitedNames) {
        if (name == candidate)
            return true;
    }
    return false;
}

bool endsWithReason(const QString &line, QLatin1StringView reason)
{
    return line == reason || line.endsWith(QLatin1StringView(": ") + reason);
}

} // namespace

AtSession::AtSession(Transport *transport, QObject *parent)
    : QObject(parent), m_transport(transport), m_timer(new QTimer(this))
{
    Q_ASSERT(transport);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &AtSession::onTimeout);
    connect(m_transport, &Transport::dataReceived, this, &AtSession::onData);
    connect(m_transport, &Transport::closed, this, &AtSession::onTransportClosed);
}

QFuture<AtReply> AtSession::send(const QString &command, std::chrono::milliseconds timeout)
{
    auto pending = std::make_unique<Pending>();
    pending->command = command;
    pending->name = commandName(command);
    pending->timeout = timeout;
    pending->reply.command = command;

    QString text = command.trimmed();
    for (const QLatin1StringView prefix : longTextPrefixes) {
        if (text.startsWith(prefix, Qt::CaseInsensitive) && !text.endsWith(u',')) {
            text += u',';
            break;
        }
    }
    pending->wire = (text + QLatin1StringView("\r\n")).toUtf8();
    pending->echoes = {command.trimmed(), text};
    if (text.startsWith(QLatin1StringView("AT*CLIPBOARD="), Qt::CaseInsensitive))
        pending->echoes.append(QStringLiteral("AT*CLIPBOARD"));
    pending->expectConnect = (text == QLatin1StringView("AT+CPROT=0"));

    pending->promise.start();
    QFuture<AtReply> future = pending->promise.future();
    if (m_ended) {
        pending->reply.status = AtReply::Status::Closed;
        pending->promise.addResult(pending->reply);
        pending->promise.finish();
        return future;
    }
    m_queue.push_back(std::move(pending));
    startNext();
    return future;
}

QFuture<Result<void>> AtSession::enterObex()
{
    return send(QStringLiteral("AT+CPROT=0")).then(this, [](const AtReply &reply) -> Result<void> {
        if (reply.status == AtReply::Status::Ok)
            return {};
        if (reply.status == AtReply::Status::Closed)
            return Error(ErrorCode::ConnectionClosed,
                         QStringLiteral("Connection closed before OBEX mode started"));
        if (reply.status == AtReply::Status::Timeout)
            return Error(ErrorCode::Timeout, QStringLiteral("No answer to AT+CPROT=0"));
        return Error(ErrorCode::ProtocolError,
                     QStringLiteral("The phone refused OBEX mode (status %1)")
                         .arg(static_cast<int>(reply.status)));
    });
}

void AtSession::leaveObex(const QByteArray &leftover)
{
    if (!m_obexMode)
        return;
    m_obexMode = false;
    if (!leftover.isEmpty()) {
        m_buffer += leftover;
        processBuffer();
    }
    startNext();
}

void AtSession::writeRaw(const QByteArray &bytes)
{
    if (m_obexMode && !m_ended)
        m_transport->write(bytes);
}

void AtSession::onData(const QByteArray &chunk)
{
    if (m_ended)
        return;
    if (m_obexMode) {
        Q_EMIT rawData(chunk);
        return;
    }
    if (m_current)
        m_timer->start(m_current->timeout);
    m_buffer += chunk;
    processBuffer();
}

void AtSession::processBuffer()
{
    while (!m_ended && !m_obexMode) {
        const qsizetype end = m_buffer.indexOf('\n');
        if (end < 0) {
            if (m_buffer.size() > limits::maxAtLineBytes) {
                const Error error(
                    ErrorCode::LimitExceeded,
                    QStringLiteral("AT line longer than %1 bytes").arg(limits::maxAtLineBytes));
                qCWarning(lcProtocol) << error.message();
                Q_EMIT fatal(error);
                endSession(AtReply::Status::Closed);
                m_transport->close();
            }
            return;
        }
        QByteArray bytes = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        if (bytes.endsWith('\r'))
            bytes.chop(1);
        if (bytes.trimmed().isEmpty())
            continue;
        processLine(QString::fromUtf8(bytes));
    }
    if (m_obexMode && !m_buffer.isEmpty()) {
        const QByteArray rest = m_buffer;
        m_buffer.clear();
        Q_EMIT rawData(rest);
    }
}

void AtSession::processLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (!m_pendingCdsLine.isEmpty()) {
        const QString report = m_pendingCdsLine + QLatin1StringView("\r\n") + trimmed;
        m_pendingCdsLine.clear();
        Q_EMIT unsolicited(report);
        return;
    }
    if (handleConnectionLevelLine(trimmed))
        return;

    const QString name = lineName(trimmed);
    const bool unsolicitedLine = isUnsolicitedName(name) && !belongsToCurrent(name);
    if (unsolicitedLine) {
        qCDebug(lcProtocol) << "AT unsolicited" << name;
        if (name == QLatin1StringView("+CDS"))
            m_pendingCdsLine = trimmed;
        else
            Q_EMIT unsolicited(trimmed);
        return;
    }

    // Lines without a pending command, or before its echo, are late answers
    // to a command that timed out; they belong to nothing that still waits.
    Pending *current = m_current.get();
    if (!current) {
        qCDebug(lcProtocol) << "AT line without a pending command discarded";
        return;
    }
    if (!current->echoSeen) {
        if (current->echoes.contains(trimmed))
            current->echoSeen = true;
        else
            qCDebug(lcProtocol) << "AT line before the echo of" << current->name << "discarded";
        return;
    }

    if (current->expectConnect && trimmed == QLatin1StringView("CONNECT")) {
        m_obexMode = true;
        complete(AtReply::Status::Ok);
        return;
    }
    if (trimmed == QLatin1StringView("OK")) {
        complete(AtReply::Status::Ok);
        return;
    }
    static constexpr QLatin1StringView cmsErrorPrefix("+CMS ERROR:");
    static constexpr QLatin1StringView dumpPrefix("DUMP:");
    if (trimmed.startsWith(cmsErrorPrefix)) {
        current->reply.detail = trimmed.mid(cmsErrorPrefix.size()).trimmed();
        complete(AtReply::Status::CmsError);
        return;
    }
    if (trimmed == QLatin1StringView("ERROR")) {
        AtReply::Status status = AtReply::Status::Error;
        for (const QString &bodyLine : std::as_const(current->reply.lines)) {
            const QString body = bodyLine.trimmed();
            if (body.startsWith(dumpPrefix)) {
                status = AtReply::Status::Dump;
                current->reply.detail = body.mid(dumpPrefix.size()).trimmed();
            } else if (endsWithReason(body, QLatin1StringView("PERMISSION DENIED"))) {
                status = AtReply::Status::PermissionDenied;
                current->reply.detail = QStringLiteral("PERMISSION DENIED");
            } else if (endsWithReason(body, QLatin1StringView("NOT SUPPORTED"))) {
                status = AtReply::Status::NotSupported;
                current->reply.detail = QStringLiteral("NOT SUPPORTED");
            } else if (endsWithReason(body, QLatin1StringView("NOT BOUND")) ||
                       endsWithReason(body, QLatin1StringView("NOT ENABLED")) ||
                       endsWithReason(body, QLatin1StringView("DISABLED"))) {
                status = AtReply::Status::PermissionDenied;
                current->reply.detail = body.mid(body.lastIndexOf(u':') + 1).trimmed();
            } else if (status == AtReply::Status::Error) {
                current->reply.detail = body;
            }
        }
        complete(status);
        return;
    }

    current->replyBytes += line.size();
    if (current->replyBytes > limits::maxAtReplyBytes) {
        const Error error(ErrorCode::LimitExceeded,
                          QStringLiteral("AT reply to %1 larger than %2 bytes")
                              .arg(current->name)
                              .arg(limits::maxAtReplyBytes));
        qCWarning(lcProtocol) << error.message();
        Q_EMIT fatal(error);
        endSession(AtReply::Status::Closed);
        m_transport->close();
        return;
    }
    current->reply.lines.append(line);
}

bool AtSession::handleConnectionLevelLine(const QString &line)
{
    Error error;
    AtReply::Status status = AtReply::Status::Closed;
    if (line == QLatin1StringView("ACCESS DENIED")) {
        error = Error(ErrorCode::AccessDenied, QStringLiteral("The phone requires the PIN first"));
        status = AtReply::Status::AccessDenied;
    } else if (line == QLatin1StringView("ALREADY CONNECTED")) {
        error = Error(ErrorCode::AlreadyConnected,
                      QStringLiteral("The phone is connected to another computer"));
    } else if (line == QLatin1StringView("WIFI DISABLED")) {
        error = Error(ErrorCode::WifiDisabled, QStringLiteral("The phone does not accept Wi-Fi"));
    } else if (line == QLatin1StringView("CONNECTION CHANGE")) {
        error = Error(ErrorCode::ConnectionClosed,
                      QStringLiteral("Another connection to the phone replaced this one"));
    } else {
        return false;
    }
    qCWarning(lcProtocol) << "AT session ended:" << error.message();
    Q_EMIT fatal(error);
    endSession(status);
    return true;
}

bool AtSession::belongsToCurrent(const QString &lineName) const
{
    return m_current && m_current->echoSeen && !m_current->name.isEmpty() &&
           lineName.startsWith(m_current->name);
}

void AtSession::complete(AtReply::Status status)
{
    m_timer->stop();
    std::unique_ptr<Pending> done = std::move(m_current);
    done->reply.status = status;
    done->promise.addResult(done->reply);
    done->promise.finish();
    startNext();
}

void AtSession::startNext()
{
    if (m_current || m_obexMode || m_ended)
        return;
    while (!m_queue.empty()) {
        std::unique_ptr<Pending> next = std::move(m_queue.front());
        m_queue.pop_front();
        if (next->promise.isCanceled()) {
            next->promise.finish();
            continue;
        }
        m_current = std::move(next);
        qCDebug(lcProtocol) << "AT send" << m_current->name;
        m_timer->start(m_current->timeout);
        m_transport->write(m_current->wire);
        return;
    }
}

void AtSession::endSession(AtReply::Status statusForCurrent)
{
    if (m_ended)
        return;
    m_ended = true;
    m_obexMode = false;
    m_timer->stop();
    m_buffer.clear();

    std::unique_ptr<Pending> current = std::move(m_current);
    std::deque<std::unique_ptr<Pending>> queue;
    queue.swap(m_queue);
    if (current) {
        current->reply.status = statusForCurrent;
        current->promise.addResult(current->reply);
        current->promise.finish();
    }
    for (const std::unique_ptr<Pending> &pending : queue) {
        pending->reply.status = AtReply::Status::Closed;
        pending->promise.addResult(pending->reply);
        pending->promise.finish();
    }
    Q_EMIT ended();
}

void AtSession::onTransportClosed()
{
    if (!m_ended)
        qCInfo(lcProtocol) << "AT session closed by the transport";
    endSession(AtReply::Status::Closed);
}

void AtSession::onTimeout()
{
    if (!m_current)
        return;
    qCWarning(lcProtocol) << "AT command timed out:" << m_current->name;
    complete(AtReply::Status::Timeout);
}

} // namespace qpm::core::protocol
