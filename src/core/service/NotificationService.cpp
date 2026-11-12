#include "core/service/NotificationService.h"

#include "core/Logging.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/PhoneConnection.h"

#include <algorithm>
#include <chrono>

namespace qpm::core::service {

namespace {

using codec::PhoneNotification;
using protocol::AtReply;
using protocol::ObexClient;
using protocol::ObexResponse;
using protocol::PhoneConnection;

constexpr std::chrono::seconds listTimeout{30};
constexpr std::chrono::seconds imageTimeout{20};

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

Error notConnected()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected"));
}

Error unknownNotification()
{
    return Error(ErrorCode::NotFound, QStringLiteral("The phone shows no such notification"));
}

bool newerFirst(const PhoneNotification &a, const PhoneNotification &b)
{
    return a.postTime > b.postTime;
}

} // namespace

NotificationService::NotificationService(PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
    connect(connection, &PhoneConnection::stateChanged, this, &NotificationService::onStateChanged);
    connect(connection, &PhoneConnection::unsolicited, this, &NotificationService::onUnsolicited);
    connect(connection, &PhoneConnection::notificationListRead, this,
            &NotificationService::onListRead);
}

std::optional<PhoneNotification> NotificationService::notification(const QString &key) const
{
    for (const PhoneNotification &candidate : m_notifications) {
        if (candidate.key() == key)
            return candidate;
    }
    return std::nullopt;
}

QFuture<Result<void>> NotificationService::refresh()
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    QFuture<AtReply> reply = session->send(QStringLiteral("AT*NOTIR"), listTimeout);
    return reply
        .then(this,
              [this](const AtReply &answer) -> Result<void> {
                  const Error error = accessError(answer, QStringLiteral("the notifications"));
                  if (error.isError())
                      return error;
                  applyList(answer.lines);
                  return {};
              })
        .onCanceled(this, []() { return Result<void>(notConnected()); });
}

QFuture<Result<void>> NotificationService::dismiss(const QString &key)
{
    const std::optional<PhoneNotification> found = notification(key);
    if (!found)
        return failed<void>(unknownNotification());
    if (found->ongoing) {
        return failed<void>(
            Error(ErrorCode::Rejected, QStringLiteral("The phone keeps an ongoing notification")));
    }
    const Result<QString> command = codec::dismissNotificationCommand(*found);
    if (!command.hasValue())
        return failed<void>(command.error());
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    return session->send(command.value())
        .then(this,
              [this, key](const AtReply &answer) -> Result<void> {
                  const Error error = accessError(answer, QStringLiteral("to dismiss"));
                  if (error.isError())
                      return error;
                  const qsizetype removed = m_notifications.removeIf(
                      [&key](const PhoneNotification &held) { return held.key() == key; });
                  if (removed > 0)
                      Q_EMIT notificationsChanged();
                  return {};
              })
        .onCanceled(this, []() { return Result<void>(notConnected()); });
}

QFuture<Result<void>> NotificationService::trigger(const QString &key, const QString &action)
{
    const std::optional<PhoneNotification> found = notification(key);
    if (!found)
        return failed<void>(unknownNotification());
    const Result<QString> command = codec::notificationActionCommand(*found, action);
    if (!command.hasValue())
        return failed<void>(command.error());
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    return session->send(command.value())
        .then(this,
              [this](const AtReply &answer) -> Result<void> {
                  return Result<void>(accessError(answer, QStringLiteral("the action")));
              })
        .onCanceled(this, []() { return Result<void>(notConnected()); });
}

QFuture<Result<QByteArray>> NotificationService::fetchImage(const QString &key)
{
    const std::optional<PhoneNotification> found = notification(key);
    if (!found)
        return failed<QByteArray>(unknownNotification());
    if (!found->hasPicture) {
        return failed<QByteArray>(
            Error(ErrorCode::NotFound, QStringLiteral("The notification carries no picture")));
    }
    const Result<QString> name = codec::notificationImageName(*found);
    if (!name.hasValue())
        return failed<QByteArray>(name.error());
    if (!m_connection->session())
        return failed<QByteArray>(notConnected());
    return m_connection->withObex<QByteArray>(
        ObexClient::Target::FileBrowser, [name = name.value()](ObexClient &obex) {
            return obex.get(name, {}, imageTimeout, codec::maxNotificationImageBytes)
                .then([](const Result<ObexResponse> &answer) -> Result<QByteArray> {
                    if (!answer.hasValue())
                        return answer.error();
                    if (answer.value().body.isEmpty()) {
                        return Error(ErrorCode::NotFound,
                                     QStringLiteral("The phone sent no picture"));
                    }
                    return answer.value().body;
                });
        });
}

QFuture<Result<void>> NotificationService::showAccessSettings()
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    return session->send(QStringLiteral("AT*NOTISHOWSETTING"))
        .then(this,
              [this](const AtReply &answer) -> Result<void> {
                  return Result<void>(accessError(answer, QStringLiteral("the settings")));
              })
        .onCanceled(this, []() { return Result<void>(notConnected()); });
}

void NotificationService::onStateChanged()
{
    const PhoneConnection::State state = m_connection->state();
    if (state == PhoneConnection::State::Ready && !m_listRequested) {
        m_listRequested = true;
        refresh().then(this, [](const Result<void> &listed) {
            // The access state tells the user why the list stays empty.
            if (!listed.hasValue())
                qCInfo(lcProtocol) << "Notifications not read:" << listed.error().message();
        });
        return;
    }
    if (state == PhoneConnection::State::Ready || state == PhoneConnection::State::Busy)
        return;
    m_listRequested = false;
    m_listKnown = false;
    setAccess(Access::Unknown);
    if (!m_notifications.isEmpty()) {
        m_notifications.clear();
        Q_EMIT notificationsChanged();
    }
}

void NotificationService::onUnsolicited(const QString &line)
{
    if (!line.startsWith(QLatin1StringView("*NOTIFY:")))
        return;
    const Result<codec::NotificationEvent> event = codec::parseNotificationEvent(line);
    if (!event.hasValue()) {
        qCInfo(lcProtocol) << "Notification report not readable:" << event.error().message();
        return;
    }
    setAccess(Access::Granted);
    const PhoneNotification &notification = event.value().notification;
    if (event.value().kind == codec::NotificationEvent::Kind::Removed) {
        const QString key = notification.key();
        const qsizetype removed = m_notifications.removeIf(
            [&key](const PhoneNotification &held) { return held.key() == key; });
        if (removed > 0)
            Q_EMIT notificationsChanged();
        return;
    }
    upsert(notification, true);
}

void NotificationService::onListRead(const AtReply &reply)
{
    if (reply.status == AtReply::Status::Closed)
        return;
    const Error error = accessError(reply, QStringLiteral("the notifications"));
    if (error.isError()) {
        qCInfo(lcProtocol) << "Notifications not read:" << error.message();
        return;
    }
    applyList(reply.lines);
}

void NotificationService::applyList(const QStringList &lines)
{
    static constexpr QLatin1StringView prefix("*NOTIR:");
    const bool announce = m_listKnown;
    m_listKnown = true;
    setAccess(Access::Granted);
    QList<PhoneNotification> fresh;
    int unreadable = 0;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(prefix))
            continue;
        if (fresh.size() == codec::maxNotifications) {
            ++unreadable;
            continue;
        }
        const Result<PhoneNotification> parsed =
            codec::parseNotificationRecord(trimmed.mid(prefix.size()));
        if (!parsed.hasValue()) {
            ++unreadable;
            continue;
        }
        const QString key = parsed.value().key();
        fresh.removeIf([&key](const PhoneNotification &held) { return held.key() == key; });
        fresh.append(parsed.value());
    }
    if (unreadable > 0)
        qCInfo(lcProtocol) << "Notification records left out:" << unreadable;
    std::stable_sort(fresh.begin(), fresh.end(), newerFirst);
    QList<PhoneNotification> posted;
    if (announce) {
        for (const PhoneNotification &candidate : std::as_const(fresh)) {
            if (!notification(candidate.key()))
                posted.append(candidate);
        }
    }
    const bool changed = fresh != m_notifications;
    m_notifications = fresh;
    if (changed)
        Q_EMIT notificationsChanged();
    for (const PhoneNotification &one : std::as_const(posted))
        Q_EMIT notificationPosted(one);
}

void NotificationService::upsert(const PhoneNotification &notification, bool announce)
{
    const QString key = notification.key();
    const auto held =
        std::find_if(m_notifications.begin(), m_notifications.end(),
                     [&key](const PhoneNotification &one) { return one.key() == key; });
    const bool seen = held != m_notifications.end();
    if (seen) {
        if (*held == notification)
            return;
        *held = notification;
    } else {
        if (m_notifications.size() == codec::maxNotifications)
            m_notifications.removeLast();
        m_notifications.prepend(notification);
    }
    std::stable_sort(m_notifications.begin(), m_notifications.end(), newerFirst);
    Q_EMIT notificationsChanged();
    if (announce && !seen)
        Q_EMIT notificationPosted(notification);
}

void NotificationService::setAccess(Access access)
{
    if (m_access == access)
        return;
    m_access = access;
    if (access != Access::Granted && access != Access::Unknown) {
        // What was read before the access ended is no longer what the phone
        // shows, and the list that follows a new access is no news.
        m_listKnown = false;
        if (!m_notifications.isEmpty()) {
            m_notifications.clear();
            Q_EMIT notificationsChanged();
        }
    }
    Q_EMIT accessChanged();
}

Error NotificationService::accessError(const AtReply &reply, const QString &what)
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
        if (reply.detail == QLatin1StringView("NOT BOUND")) {
            setAccess(Access::NotBound);
            return Error(ErrorCode::PermissionDenied,
                         QStringLiteral("The notification access of the phone app is not running"));
        }
        setAccess(Access::Disabled);
        return Error(ErrorCode::PermissionDenied,
                     QStringLiteral("The phone app may not read notifications"));
    case AtReply::Status::NotSupported:
        setAccess(Access::NotSupported);
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("The phone cannot pass on notifications"));
    case AtReply::Status::CmsError:
    case AtReply::Status::Dump:
    case AtReply::Status::Error:
        break;
    }
    return Error(ErrorCode::Rejected, QStringLiteral("The phone refused %1").arg(what));
}

} // namespace qpm::core::service
