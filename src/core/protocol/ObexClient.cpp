#include "core/protocol/ObexClient.h"

#include "core/Logging.h"
#include "core/codec/Limits.h"
#include "core/codec/PhoneTime.h"
#include "core/protocol/AtSession.h"

#include <QPromise>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace qpm::core::protocol {

namespace {

constexpr std::chrono::seconds smallTimeout{20};
constexpr std::chrono::seconds controlTimeout{10};
constexpr quint16 minPacketSize = 255;
constexpr int luidHexDigits = 12;

QByteArray targetBytes(ObexClient::Target target)
{
    if (target == ObexClient::Target::IrmcSync)
        return QByteArrayLiteral("IRMC-SYNC");
    return QByteArray::fromHex("F9EC7BC4953C11D2984E525400DC9E09");
}

QByteArray continueBytes()
{
    return QByteArray(obex::continueRequest, obex::continueRequestSize);
}

Error errorForResponse(const ObexPacket &packet)
{
    const QString code = QStringLiteral("0x%1").arg(uint(packet.code), 2, 16, QLatin1Char('0'));
    switch (packet.code) {
    case obex::rspUnauthorized:
        return Error(ErrorCode::PermissionDenied,
                     QStringLiteral("The phone lacks a permission (OBEX %1)").arg(code));
    case obex::rspForbidden:
        return Error(ErrorCode::AccessDenied,
                     QStringLiteral("The phone refused access (OBEX %1)").arg(code));
    case obex::rspNotFound:
    case obex::rspDatabaseLocked:
        return Error(ErrorCode::NotFound, QStringLiteral("Object not found (OBEX %1)").arg(code));
    case obex::rspRequestTimeout:
        return Error(ErrorCode::Timeout,
                     QStringLiteral("The phone timed out a transfer (OBEX %1)").arg(code));
    case obex::rspConflict:
        return Error(ErrorCode::Rejected, QStringLiteral("Target exists (OBEX %1)").arg(code));
    case obex::rspNotImplemented:
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("Not supported by the phone (OBEX %1)").arg(code));
    case obex::rspDatabaseFull:
        return Error(ErrorCode::IoFailure,
                     QStringLiteral("Phone storage full (OBEX %1)").arg(code));
    case obex::rspInternalServerError: {
        // The phone puts the Java exception into App-Parameters; only its first
        // line is kept, cut short, so that file names deep in a trace stay out.
        static constexpr qsizetype maxDetail = 120;
        const QByteArray trace = packet.appParameters.value_or(QByteArray());
        const QString firstLine =
            QString::fromUtf8(
                trace.left(trace.indexOf('\r') < 0 ? trace.size() : trace.indexOf('\r')))
                .left(maxDetail);
        return Error(ErrorCode::ProtocolError,
                     QStringLiteral("Phone error (OBEX %1): %2").arg(code, firstLine));
    }
    default:
        break;
    }
    return Error(ErrorCode::ProtocolError, QStringLiteral("OBEX response %1").arg(code));
}

} // namespace

ObexClient::ObexClient(AtSession *session, QObject *parent)
    : QObject(parent), m_session(session), m_timer(new QTimer(this))
{
    Q_ASSERT(session);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ObexClient::onTimeout);
    connect(m_session, &AtSession::rawData, this, &ObexClient::onRawData);
    connect(m_session, &AtSession::ended, this, [this]() {
        failAll(Error(ErrorCode::ConnectionClosed, QStringLiteral("The connection was closed")));
    });
}

QFuture<Result<void>> ObexClient::connectTarget(Target target)
{
    auto promise = std::make_shared<QPromise<Result<void>>>();
    promise->start();
    QFuture<Result<void>> future = promise->future();
    if (m_connected) {
        promise->addResult(
            Error(ErrorCode::ProtocolError, QStringLiteral("An OBEX target is already connected")));
        promise->finish();
        return future;
    }

    m_session->enterObex().then(this, [this, promise, target](const Result<void> &entered) {
        if (!entered.hasValue()) {
            promise->addResult(entered);
            promise->finish();
            return;
        }
        ObexPacket packet;
        packet.code = obex::opConnect;
        packet.layout = ObexPacket::Layout::Connect;
        packet.target = targetBytes(target);
        std::unique_ptr<Request> request = makeRequest(packet, smallTimeout);
        request->responseLayout = ObexPacket::Layout::Connect;
        request->isCanceled = [promise]() { return promise->isCanceled(); };
        request->done = [this, promise, target](const Result<ObexPacket> &result) {
            if (!result.hasValue()) {
                promise->addResult(result.error());
            } else if (result.value().code != obex::rspOk) {
                promise->addResult(errorForResponse(result.value()));
            } else {
                m_connected = true;
                m_target = target;
                m_connectionId = result.value().connectionId.value_or(1);
                m_maxPacket = std::max(minPacketSize, result.value().maxPacketLength);
                promise->addResult(Result<void>());
            }
            promise->finish();
        };
        enqueue(std::move(request));
    });
    return future;
}

QFuture<Result<ObexResponse>> ObexClient::get(const QString &name, const QByteArray &type,
                                              std::chrono::milliseconds timeout)
{
    ObexPacket packet;
    packet.code = obex::opGetFinal;
    if (!name.isEmpty())
        packet.name = name;
    packet.type = type;
    std::unique_ptr<Request> request = makeRequest(packet, timeout);
    request->maxBodyBytes =
        m_target == Target::IrmcSync ? limits::maxObexSyncBytes : limits::maxObexFileBytes;
    request->progressBodies = name.endsWith(QLatin1StringView("/initsync"));
    return enqueueForResponse(std::move(request));
}

QFuture<Result<ObexResponse>> ObexClient::put(const QString &name, const QByteArray &body,
                                              const PutOptions &options)
{
    ObexPacket packet;
    packet.code = obex::opPutFinal;
    packet.name = name;
    packet.length = quint32(body.size());
    if (options.modified)
        packet.time = codec::formatUtcStamp(*options.modified).toLatin1();
    packet.bodyKind = ObexPacket::BodyKind::EndOfBody;
    packet.body = body;
    return enqueueForResponse(makeRequest(packet, smallTimeout));
}

QFuture<Result<ObexResponse>> ObexClient::remove(const QString &name)
{
    ObexPacket packet;
    packet.code = obex::opPutFinal;
    packet.name = name;
    return enqueueForResponse(makeRequest(packet, smallTimeout));
}

QFuture<Result<void>> ObexClient::setPath(const QString &name, SetPathFlags flags)
{
    ObexPacket packet;
    packet.code = obex::opSetPath;
    packet.layout = ObexPacket::Layout::SetPath;
    packet.setPathFlags = quint8(flags.toInt());
    if (!(name.isEmpty() && flags.testFlag(SetPathFlag::Backup)))
        packet.name = name;
    return enqueueForSuccess(makeRequest(packet, controlTimeout));
}

QFuture<Result<void>> ObexClient::move(const QString &from, const QString &to)
{
    ObexPacket packet;
    packet.code = obex::opMove;
    packet.name = from;
    packet.targetName = to;
    return enqueueForSuccess(makeRequest(packet, smallTimeout));
}

QFuture<Result<ObexResponse>> ObexClient::socketGet(const QString &name)
{
    ObexPacket packet;
    packet.code = obex::opSocketGet;
    packet.name = name;
    return enqueueForResponse(makeRequest(packet, smallTimeout));
}

QFuture<Result<ObexResponse>> ObexClient::socketPut(const QString &name, quint64 length,
                                                    const QDateTime &modified)
{
    ObexPacket packet;
    packet.code = obex::opSocketPut;
    packet.name = name;
    // The phone takes Length-Big when it is above zero and Length otherwise;
    // a request with neither would never end its transfer.
    if (length <= std::numeric_limits<quint32>::max())
        packet.length = quint32(length);
    packet.lengthBig = length;
    if (modified.isValid())
        packet.time = codec::formatUtcStamp(modified).toLatin1();
    return enqueueForResponse(makeRequest(packet, smallTimeout));
}

QFuture<Result<void>> ObexClient::socketStatus()
{
    ObexPacket packet;
    packet.code = obex::opSocketStatus;
    return enqueueForSuccess(makeRequest(packet, smallTimeout));
}

QFuture<Result<void>> ObexClient::abort()
{
    if (m_current && m_current->opcode != obex::opAbort) {
        if (m_current->aborting || m_current->abortDone)
            return QtFuture::makeReadyValueFuture(Result<void>());
        auto promise = std::make_shared<QPromise<Result<void>>>();
        promise->start();
        m_current->abortDone = [promise](const Result<void> &result) {
            promise->addResult(result);
            promise->finish();
        };
        QFuture<Result<void>> future = promise->future();
        startAbort(Error(ErrorCode::Cancelled, QStringLiteral("The request was aborted")));
        return future;
    }
    ObexPacket packet;
    packet.code = obex::opAbort;
    return enqueueForSuccess(makeRequest(packet, controlTimeout));
}

QFuture<Result<void>> ObexClient::disconnectTarget()
{
    if (!m_session->isInObexMode())
        return QtFuture::makeReadyValueFuture(Result<void>());
    ObexPacket packet;
    packet.code = obex::opDisconnect;
    std::unique_ptr<Request> request = makeRequest(packet, controlTimeout);
    request->endsObex = true;
    return enqueueForSuccess(std::move(request));
}

std::optional<quint32> ObexClient::createdLuid(const QByteArray &appParams)
{
    const QByteArray digits = appParams.trimmed();
    if (digits.isEmpty() || digits.size() > luidHexDigits)
        return std::nullopt;
    bool ok = false;
    const qulonglong value = digits.toULongLong(&ok, 16);
    if (!ok || value > std::numeric_limits<quint32>::max())
        return std::nullopt;
    return quint32(value);
}

std::optional<quint16> ObexClient::sideChannelPort(const QByteArray &appParams)
{
    static constexpr QByteArrayView prefix("TCP:");
    if (!appParams.startsWith(prefix))
        return std::nullopt;
    bool ok = false;
    const uint port = appParams.mid(prefix.size()).toUInt(&ok);
    if (!ok || port == 0 || port > std::numeric_limits<quint16>::max())
        return std::nullopt;
    return quint16(port);
}

std::unique_ptr<ObexClient::Request> ObexClient::makeRequest(ObexPacket packet,
                                                             std::chrono::milliseconds timeout)
{
    auto request = std::make_unique<Request>();
    request->opcode = packet.code;
    request->timeout = timeout;
    if (packet.code != obex::opConnect && packet.code != obex::opAbort)
        packet.connectionId = m_connectionId;
    const Result<QByteArray> encoded = packet.encode();
    if (!encoded.hasValue()) {
        request->abortError = encoded.error();
    } else if (encoded.value().size() > m_maxPacket) {
        request->abortError =
            Error(ErrorCode::LimitExceeded, QStringLiteral("OBEX packet of %1 bytes exceeds the "
                                                           "negotiated %2 bytes")
                                                .arg(encoded.value().size())
                                                .arg(m_maxPacket));
    } else {
        request->packet = encoded.value();
    }
    return request;
}

QFuture<Result<ObexResponse>> ObexClient::enqueueForResponse(std::unique_ptr<Request> request)
{
    auto promise = std::make_shared<QPromise<Result<ObexResponse>>>();
    promise->start();
    QFuture<Result<ObexResponse>> future = promise->future();
    request->isCanceled = [promise]() { return promise->isCanceled(); };
    request->done = [promise](const Result<ObexPacket> &result) {
        if (!result.hasValue()) {
            promise->addResult(result.error());
        } else {
            const ObexPacket &packet = result.value();
            if (packet.code != obex::rspOk && packet.code != obex::rspCreated) {
                promise->addResult(errorForResponse(packet));
            } else {
                ObexResponse response;
                response.code = packet.code;
                response.body = packet.body;
                response.appParams = packet.appParameters.value_or(QByteArray());
                response.name = packet.name.value_or(QString());
                if (packet.lengthBig && *packet.lengthBig > 0)
                    response.length = packet.lengthBig;
                else if (packet.length)
                    response.length = *packet.length;
                promise->addResult(Result<ObexResponse>(response));
            }
        }
        promise->finish();
    };
    enqueue(std::move(request));
    return future;
}

QFuture<Result<void>> ObexClient::enqueueForSuccess(std::unique_ptr<Request> request)
{
    auto promise = std::make_shared<QPromise<Result<void>>>();
    promise->start();
    QFuture<Result<void>> future = promise->future();
    request->isCanceled = [promise]() { return promise->isCanceled(); };
    request->done = [promise](const Result<ObexPacket> &result) {
        if (!result.hasValue())
            promise->addResult(result.error());
        else if (result.value().code != obex::rspOk && result.value().code != obex::rspCreated)
            promise->addResult(errorForResponse(result.value()));
        else
            promise->addResult(Result<void>());
        promise->finish();
    };
    enqueue(std::move(request));
    return future;
}

void ObexClient::enqueue(std::unique_ptr<Request> request)
{
    if (request->abortError.isError()) {
        request->done(request->abortError);
        return;
    }
    // A failed CONNECT leaves the session in OBEX mode, and only DISCONNECT
    // brings it back, so both are allowed without a connected target.
    if (request->opcode != obex::opConnect && request->opcode != obex::opDisconnect &&
        !m_connected) {
        request->done(Error(ErrorCode::ProtocolError, QStringLiteral("No OBEX target connected")));
        return;
    }
    m_queue.push_back(std::move(request));
    startNext();
}

void ObexClient::startNext()
{
    if (m_current || !m_session->isInObexMode())
        return;
    while (!m_queue.empty()) {
        std::unique_ptr<Request> next = std::move(m_queue.front());
        m_queue.pop_front();
        if (next->isCanceled && next->isCanceled()) {
            next->done(Error(ErrorCode::Cancelled, QStringLiteral("The request was cancelled")));
            continue;
        }
        m_current = std::move(next);
        m_current->outstanding = 1;
        qCDebug(lcProtocol) << "OBEX request" << Qt::hex << uint(m_current->opcode);
        m_timer->start(m_current->timeout);
        writePacket(m_current->packet);
        return;
    }
}

void ObexClient::writePacket(const QByteArray &bytes)
{
    m_session->writeRaw(bytes);
}

void ObexClient::onRawData(const QByteArray &chunk)
{
    m_buffer += chunk;
    while (true) {
        const std::optional<quint16> length = ObexPacket::declaredLength(m_buffer);
        if (!length || m_buffer.size() < *length)
            return;
        if (*length < obex::continueRequestSize) {
            failAll(Error(ErrorCode::ProtocolError,
                          QStringLiteral("OBEX packet with an invalid length")));
            return;
        }
        const QByteArray bytes = m_buffer.left(*length);
        m_buffer.remove(0, *length);
        handlePacket(bytes);
    }
}

void ObexClient::handlePacket(const QByteArray &bytes)
{
    if (!m_current) {
        qCDebug(lcProtocol) << "OBEX packet without a request discarded";
        return;
    }
    Request &request = *m_current;
    m_timer->start(request.timeout);
    const quint8 code = quint8(bytes.at(0));

    // The phone sends 90 00 03 while it works, and during a sync start also
    // 90 with a "done/total" body. It then expects exactly three bytes back
    // for each of them and reads nothing else from them.
    const bool keepAlive = code == obex::rspContinue &&
                           (bytes.size() == obex::continueRequestSize || request.progressBodies);
    if (keepAlive) {
        if (request.progressBodies && bytes.size() > obex::continueRequestSize) {
            const Result<ObexPacket> packet = ObexPacket::decode(bytes, ObexPacket::Layout::Plain);
            if (packet.hasValue()) {
                const QList<QByteArray> counts = packet.value().body.trimmed().split('/');
                if (counts.size() == 2)
                    Q_EMIT progress(counts.at(0).toULongLong(), counts.at(1).toULongLong());
            }
        }
        writePacket(continueBytes());
        if (!request.aborting && request.isCanceled && request.isCanceled())
            startAbort(Error(ErrorCode::Cancelled, QStringLiteral("The request was cancelled")));
        return;
    }

    --request.outstanding;
    if (request.aborting) {
        if (request.outstanding <= 0)
            finishCurrent(request.abortError);
        return;
    }

    const ObexPacket::Layout layout =
        code == obex::rspContinue ? ObexPacket::Layout::Plain : request.responseLayout;
    Result<ObexPacket> decoded = ObexPacket::decode(bytes, layout);
    if (!decoded.hasValue()) {
        finishCurrent(decoded.error());
        return;
    }
    ObexPacket packet = decoded.value();
    request.body += packet.body;
    if (request.maxBodyBytes > 0 && quint64(request.body.size()) > request.maxBodyBytes) {
        startAbort(
            Error(ErrorCode::LimitExceeded,
                  QStringLiteral("OBEX body larger than %1 bytes").arg(request.maxBodyBytes)));
        return;
    }

    if (code == obex::rspContinue) {
        Q_EMIT progress(quint64(request.body.size()), std::nullopt);
        if (request.isCanceled && request.isCanceled()) {
            startAbort(Error(ErrorCode::Cancelled, QStringLiteral("The request was cancelled")));
            return;
        }
        writePacket(continueBytes());
        ++request.outstanding;
        return;
    }

    packet.body = request.body;
    if (request.endsObex) {
        m_connected = false;
        const QByteArray leftover = m_buffer;
        m_buffer.clear();
        m_session->leaveObex(leftover);
    }
    finishCurrent(packet);
}

void ObexClient::startAbort(const Error &reason)
{
    Request &request = *m_current;
    request.aborting = true;
    request.abortError = reason;
    qCInfo(lcProtocol) << "OBEX request aborted:" << reason.message();
    writePacket(QByteArray(1, char(obex::opAbort)) + QByteArray::fromHex("0003"));
    ++request.outstanding;
}

void ObexClient::finishCurrent(const Result<ObexPacket> &result)
{
    m_timer->stop();
    std::unique_ptr<Request> request = std::move(m_current);
    if (request->abortDone)
        request->abortDone(Result<void>());
    request->done(result);
    if (!m_connected) {
        std::deque<std::unique_ptr<Request>> queue;
        queue.swap(m_queue);
        for (const std::unique_ptr<Request> &waiting : queue) {
            waiting->done(
                Error(ErrorCode::ProtocolError, QStringLiteral("No OBEX target connected")));
        }
    }
    startNext();
}

void ObexClient::failAll(const Error &error)
{
    m_timer->stop();
    m_buffer.clear();
    m_connected = false;
    std::unique_ptr<Request> current = std::move(m_current);
    std::deque<std::unique_ptr<Request>> queue;
    queue.swap(m_queue);
    if (current) {
        if (current->abortDone)
            current->abortDone(error);
        current->done(error);
    }
    for (const std::unique_ptr<Request> &waiting : queue)
        waiting->done(error);
}

void ObexClient::onTimeout()
{
    if (!m_current)
        return;
    qCWarning(lcProtocol) << "OBEX request timed out:" << Qt::hex << uint(m_current->opcode);
    m_buffer.clear();
    if (m_current->aborting) {
        finishCurrent(m_current->abortError);
        return;
    }
    if (m_current->endsObex) {
        m_connected = false;
        m_session->leaveObex();
    }
    finishCurrent(Error(ErrorCode::Timeout, QStringLiteral("No OBEX answer in time")));
}

} // namespace qpm::core::protocol
