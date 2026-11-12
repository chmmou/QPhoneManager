#include "core/service/ClipboardService.h"

#include "core/Logging.h"
#include "core/codec/AtText.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"

#include <chrono>

namespace qpm::core::service {

namespace {

using protocol::AtReply;
using protocol::PhoneConnection;

// The phone reads a clip in the background only through a window of its
// app, which polls the clipboard for a while before it answers.
constexpr std::chrono::seconds readTimeout{25};

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

Error notConnected()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected"));
}

Error replyError(const AtReply &reply, const QString &what)
{
    switch (reply.status) {
    case AtReply::Status::Ok:
        return {};
    case AtReply::Status::Timeout:
        return Error(ErrorCode::Timeout, QStringLiteral("The phone did not answer %1").arg(what));
    case AtReply::Status::Closed:
    case AtReply::Status::AccessDenied:
        return notConnected();
    case AtReply::Status::PermissionDenied:
        return Error(ErrorCode::PermissionDenied, QStringLiteral("The phone refused %1").arg(what));
    case AtReply::Status::NotSupported:
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("The phone does not support %1").arg(what));
    case AtReply::Status::CmsError:
    case AtReply::Status::Dump:
    case AtReply::Status::Error:
        break;
    }
    return Error(ErrorCode::Rejected, QStringLiteral("The phone refused %1").arg(what));
}

} // namespace

ClipboardService::ClipboardService(PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
    connect(connection, &PhoneConnection::unsolicited, this, &ClipboardService::onUnsolicited);
    connect(connection, &PhoneConnection::stateChanged, this, &ClipboardService::onStateChanged);
}

void ClipboardService::setSyncing(bool syncing)
{
    if (m_syncing == syncing)
        return;
    m_syncing = syncing;
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return;
    session->send(syncing ? QStringLiteral("AT*CLIPWATCH=1") : QStringLiteral("AT*CLIPWATCH=0"))
        .then(this, [](const AtReply &reply) {
            // Without the report the clips of the phone arrive only on request.
            // Syncing towards the phone goes on regardless.
            const Error error = replyError(reply, QStringLiteral("to watch the clipboard"));
            if (error.isError())
                qCInfo(lcProtocol) << error.message();
        });
}

QFuture<Result<void>> ClipboardService::sendToPhone(const QString &text)
{
    if (text.isEmpty())
        return failed<void>(Error(ErrorCode::InvalidInput, QStringLiteral("Empty clip")));
    if (text.size() > maxTextLength) {
        return failed<void>(
            Error(ErrorCode::LimitExceeded,
                  QStringLiteral("Clip longer than %1 characters").arg(maxTextLength)));
    }
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    m_lastExchanged = text;
    return session->send(QStringLiteral("AT*CLIPBOARD=") + codec::encodeQp(text))
        .then(this,
              [](const AtReply &reply) -> Result<void> {
                  return Result<void>(replyError(reply, QStringLiteral("the clip")));
              })
        .onCanceled(this, []() { return Result<void>(notConnected()); });
}

QFuture<Result<QString>> ClipboardService::readFromPhone()
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<QString>(notConnected());
    return session->send(QStringLiteral("AT*CLIPBOARD?"), readTimeout)
        .then(this,
              [this](const AtReply &reply) -> Result<QString> {
                  const Error error = replyError(reply, QStringLiteral("to read the clipboard"));
                  if (error.isError())
                      return error;
                  static constexpr QLatin1StringView prefix("*CLIPBOARD:");
                  for (const QString &line : reply.lines) {
                      const QString trimmed = line.trimmed();
                      if (!trimmed.startsWith(prefix))
                          continue;
                      const QString text = codec::decodeQp(trimmed.mid(prefix.size()));
                      if (text.size() > maxTextLength) {
                          return Error(ErrorCode::LimitExceeded,
                                       QStringLiteral("Clip of the phone longer than %1 characters")
                                           .arg(maxTextLength));
                      }
                      m_lastExchanged = text;
                      return text;
                  }
                  return Error(ErrorCode::ProtocolError,
                               QStringLiteral("The phone sent no clip in its answer"));
              })
        .onCanceled(this, []() { return Result<QString>(notConnected()); });
}

void ClipboardService::offerDesktopText(const QString &text)
{
    if (!m_syncing || text.isEmpty() || text == m_lastExchanged || !m_connection->session())
        return;
    sendToPhone(text).then(this, [](const Result<void> &sent) {
        // A clip that does not reach the phone is not repeated. The next copy
        // on this computer goes out as usual.
        if (!sent.hasValue())
            qCInfo(lcProtocol) << "Clip not sent:" << sent.error().message();
    });
}

void ClipboardService::onUnsolicited(const QString &line)
{
    static constexpr QLatin1StringView prefix("*CLIPWATCH:");
    if (!m_syncing || !line.startsWith(prefix))
        return;
    const QString text = codec::decodeQp(line.mid(prefix.size()));
    if (text.size() > maxTextLength) {
        qCInfo(lcProtocol) << "Clip of the phone ignored, characters:" << text.size();
        return;
    }
    if (text.isEmpty() || text == m_lastExchanged)
        return;
    m_lastExchanged = text;
    Q_EMIT phoneTextChanged(text);
}

void ClipboardService::onStateChanged()
{
    const PhoneConnection::State state = m_connection->state();
    if (state != PhoneConnection::State::Ready && state != PhoneConnection::State::Busy)
        m_lastExchanged.clear();
}

} // namespace qpm::core::service
