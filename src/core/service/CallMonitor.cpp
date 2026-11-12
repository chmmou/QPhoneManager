#include "core/service/CallMonitor.h"

#include "core/Logging.h"
#include "core/codec/AtText.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"
#include "core/store/ContactStore.h"

namespace qpm::core::service {

namespace {

using protocol::AtReply;
using protocol::PhoneConnection;

constexpr int phoneIdle = 0;
constexpr int phoneDialling = 1;
constexpr int phoneActive = 3;
constexpr int phoneRinging = 6;

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
        return Error(ErrorCode::PermissionDenied,
                     QStringLiteral("The phone app may not do %1").arg(what));
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

// The number in the form the phone may send; nothing for anything else.
std::optional<QString> checkedNumber(const QString &field)
{
    QString number = field.trimmed();
    if (number.size() >= 2 && number.startsWith(u'"') && number.endsWith(u'"'))
        number = number.mid(1, number.size() - 2).trimmed();
    if (number.size() > CallMonitor::maxNumberLength)
        return std::nullopt;
    for (qsizetype i = 0; i < number.size(); ++i) {
        const char16_t c = number.at(i).unicode();
        const bool allowed =
            (c >= u'0' && c <= u'9') || c == u'*' || c == u'#' || (c == u'+' && i == 0);
        if (!allowed)
            return std::nullopt;
    }
    return number;
}

} // namespace

CallMonitor::CallMonitor(PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
    connect(connection, &PhoneConnection::unsolicited, this, &CallMonitor::onUnsolicited);
    connect(connection, &PhoneConnection::stateChanged, this, &CallMonitor::onStateChanged);
}

void CallMonitor::setContactStore(const std::shared_ptr<store::ContactStore> &store)
{
    m_contacts = store;
    if (m_state == State::Idle)
        return;
    const QString name = lookUpName(m_number);
    if (name == m_name)
        return;
    m_name = name;
    Q_EMIT callChanged();
}

QFuture<Result<void>> CallMonitor::answer()
{
    if (m_state != State::Ringing)
        return failed<void>(Error(ErrorCode::InvalidInput, QStringLiteral("No call is ringing")));
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    return session->send(QStringLiteral("ATA"))
        .then(this,
              [](const AtReply &reply) -> Result<void> {
                  return Result<void>(replyError(reply, QStringLiteral("to answer")));
              })
        .onCanceled(this, []() { return Result<void>(notConnected()); });
}

QFuture<Result<void>> CallMonitor::hangUp()
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    return session->send(QStringLiteral("ATH"))
        .then(this,
              [this](const AtReply &reply) -> QFuture<Result<void>> {
                  const Error error = replyError(reply, QStringLiteral("to hang up"));
                  if (error.code() != ErrorCode::Rejected)
                      return QtFuture::makeReadyValueFuture(Result<void>(error));
                  // The client takes both forms. The second is tried where it
                  // refuses the first.
                  protocol::AtSession *again = m_connection->session();
                  if (!again)
                      return failed<void>(notConnected());
                  return again->send(QStringLiteral("AT+CHUP"))
                      .then(this, [](const AtReply &second) -> Result<void> {
                          return Result<void>(replyError(second, QStringLiteral("to hang up")));
                      });
              })
        .unwrap()
        .onCanceled(this, []() { return Result<void>(notConnected()); });
}

QFuture<Result<void>> CallMonitor::setMuted(bool muted)
{
    return sendSwitch(QStringLiteral("AT+CMUT=%1").arg(muted ? 1 : 0), muted, &CallMonitor::m_muted,
                      &CallMonitor::mutedChanged);
}

QFuture<Result<void>> CallMonitor::setSpeaker(bool on)
{
    return sendSwitch(QStringLiteral("AT*SESP=%1").arg(on ? 1 : 0), on, &CallMonitor::m_speaker,
                      &CallMonitor::speakerChanged);
}

QFuture<Result<void>> CallMonitor::sendSwitch(const QString &command, bool on,
                                              bool CallMonitor::*target,
                                              void (CallMonitor::*changed)())
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    return session->send(command)
        .then(this,
              [this, on, target, changed](const AtReply &reply) -> Result<void> {
                  const Error error = replyError(reply, QStringLiteral("to switch"));
                  if (error.isError())
                      return error;
                  if (this->*target != on) {
                      this->*target = on;
                      Q_EMIT(this->*changed)();
                  }
                  return {};
              })
        .onCanceled(this, []() { return Result<void>(notConnected()); });
}

void CallMonitor::onUnsolicited(const QString &line)
{
    static constexpr QLatin1StringView prefix("*ECAV:");
    if (!line.startsWith(prefix))
        return;
    const QStringList fields = codec::splitFields(line.mid(prefix.size()).trimmed());
    bool ok = false;
    const int phoneState = fields.value(1).trimmed().toInt(&ok);
    const std::optional<QString> number = checkedNumber(fields.value(5));
    if (fields.size() < 6 || !ok || !number) {
        qCInfo(lcProtocol) << "Call report not readable, fields:" << fields.size();
        return;
    }
    switch (phoneState) {
    case phoneIdle:
        setCall(State::Idle, QString());
        return;
    case phoneDialling:
        setCall(State::Dialling, *number);
        return;
    case phoneActive:
        setCall(State::Active, *number);
        return;
    case phoneRinging:
        setCall(State::Ringing, *number);
        return;
    default:
        break;
    }
    qCInfo(lcProtocol) << "Call report with the unknown state" << phoneState;
}

void CallMonitor::onStateChanged()
{
    const PhoneConnection::State state = m_connection->state();
    if (state != PhoneConnection::State::Ready && state != PhoneConnection::State::Busy)
        setCall(State::Idle, QString());
}

void CallMonitor::setCall(State state, const QString &number)
{
    // The phone sends the number of a call that goes on only within a few
    // seconds of its ringing. The number known from then stays.
    const QString kept = state == State::Idle ? QString() : (number.isEmpty() ? m_number : number);
    if (state == m_state && kept == m_number)
        return;
    const bool ended = state == State::Idle;
    m_state = state;
    m_number = kept;
    m_name = lookUpName(kept);
    Q_EMIT callChanged();
    if (ended && m_muted) {
        m_muted = false;
        Q_EMIT mutedChanged();
    }
    if (ended && m_speaker) {
        m_speaker = false;
        Q_EMIT speakerChanged();
    }
}

QString CallMonitor::lookUpName(const QString &number) const
{
    const std::shared_ptr<store::ContactStore> contacts = m_contacts.lock();
    const QString key = store::ContactStore::numberKey(number);
    if (!contacts || key.isEmpty())
        return {};
    const Result<QHash<QString, QString>> names = contacts->namesByNumber();
    if (!names.hasValue()) {
        qCInfo(lcStore) << "Caller name not looked up:" << names.error().message();
        return {};
    }
    return names.value().value(key);
}

} // namespace qpm::core::service
