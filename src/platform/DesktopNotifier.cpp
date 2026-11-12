#include "platform/DesktopNotifier.h"

#include "core/Logging.h"

#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPromise>
#include <QVariantMap>

#include <memory>

namespace qpm {

using core::Error;
using core::ErrorCode;
using core::Result;

namespace {

const char serviceName[] = "org.freedesktop.Notifications";
const char servicePath[] = "/org/freedesktop/Notifications";
const char serviceInterface[] = "org.freedesktop.Notifications";
// Lets the service choose how long the notification stays.
constexpr int defaultExpiry = -1;

Error callError(const QDBusError &error, const QString &method)
{
    const QString name = error.name();
    if (name == QLatin1StringView("org.freedesktop.DBus.Error.ServiceUnknown") ||
        name == QLatin1StringView("org.freedesktop.DBus.Error.NameHasNoOwner") ||
        name == QLatin1StringView("org.freedesktop.DBus.Error.Disconnected")) {
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("No notification service on the desktop"));
    }
    // Only the error name: the message text comes from another process.
    return Error(ErrorCode::IoFailure,
                 QStringLiteral("Notification call %1 failed: %2").arg(method, name));
}

template <typename T> void finish(const std::shared_ptr<QPromise<T>> &promise, const T &value)
{
    promise->addResult(value);
    promise->finish();
}

} // namespace

DesktopNotifier::DesktopNotifier(QObject *parent)
    : DesktopNotifier(QDBusConnection::sessionBus(), parent)
{
}

DesktopNotifier::DesktopNotifier(const QDBusConnection &bus, QObject *parent)
    : QObject(parent), m_bus(bus)
{
}

QFuture<Result<uint>> DesktopNotifier::show(const QString &summary, const QString &body,
                                            const QList<Action> &actions, uint replacesId)
{
    auto promise = std::make_shared<QPromise<Result<uint>>>();
    promise->start();
    if (!m_bus.isConnected()) {
        finish(promise, Result<uint>(Error(ErrorCode::NotSupported,
                                           QStringLiteral("No session bus for notifications"))));
        return promise->future();
    }
    subscribe();
    if (m_bodyMarkup) {
        notify(promise, summary, body, actions, replacesId);
        return promise->future();
    }
    // Whether the body is read as markup decides how its text is escaped.
    QDBusMessage capabilities = QDBusMessage::createMethodCall(
        QString::fromLatin1(serviceName), QString::fromLatin1(servicePath),
        QString::fromLatin1(serviceInterface), QStringLiteral("GetCapabilities"));
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(capabilities), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, promise, summary, body, actions, replacesId](QDBusPendingCallWatcher *finished) {
                finished->deleteLater();
                const QDBusPendingReply<QStringList> reply = *finished;
                if (reply.isError()) {
                    finish(promise, Result<uint>(callError(reply.error(),
                                                           QStringLiteral("GetCapabilities"))));
                    return;
                }
                m_bodyMarkup = reply.value().contains(QStringLiteral("body-markup"));
                notify(promise, summary, body, actions, replacesId);
            });
    return promise->future();
}

void DesktopNotifier::notify(const std::shared_ptr<QPromise<Result<uint>>> &promise,
                             const QString &summary, const QString &body,
                             const QList<Action> &actions, uint replacesId)
{
    QStringList actionList;
    for (const Action &action : actions) {
        if (action.key.isEmpty() || action.label.isEmpty())
            continue;
        actionList << action.key << action.label;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(serviceName), QString::fromLatin1(servicePath),
        QString::fromLatin1(serviceInterface), QStringLiteral("Notify"));
    const QVariantMap hints = {{QStringLiteral("category"), QStringLiteral("device")}};
    call << QCoreApplication::applicationName() << replacesId << QString() << summary
         << (*m_bodyMarkup ? body.toHtmlEscaped() : body) << actionList << hints << defaultExpiry;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [promise](QDBusPendingCallWatcher *finished) {
                finished->deleteLater();
                const QDBusPendingReply<uint> reply = *finished;
                if (reply.isError()) {
                    finish(promise,
                           Result<uint>(callError(reply.error(), QStringLiteral("Notify"))));
                    return;
                }
                finish(promise, Result<uint>(reply.value()));
            });
}

QFuture<Result<void>> DesktopNotifier::close(uint id)
{
    auto promise = std::make_shared<QPromise<Result<void>>>();
    promise->start();
    if (!m_bus.isConnected()) {
        finish(promise, Result<void>(Error(ErrorCode::NotSupported,
                                           QStringLiteral("No session bus for notifications"))));
        return promise->future();
    }
    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(serviceName), QString::fromLatin1(servicePath),
        QString::fromLatin1(serviceInterface), QStringLiteral("CloseNotification"));
    call << id;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [promise](QDBusPendingCallWatcher *finished) {
                finished->deleteLater();
                const QDBusPendingReply<> reply = *finished;
                finish(promise, reply.isError()
                                    ? Result<void>(callError(reply.error(),
                                                             QStringLiteral("CloseNotification")))
                                    : Result<void>());
            });
    return promise->future();
}

void DesktopNotifier::onActionInvoked(uint id, const QString &key)
{
    Q_EMIT actionInvoked(id, key);
}

void DesktopNotifier::onClosed(uint id, uint reason)
{
    Q_EMIT closed(id, reason);
}

void DesktopNotifier::subscribe()
{
    if (m_subscribed)
        return;
    // Qt D-Bus offers no pointer-to-member form for bus signals, so the
    // slots are named by their signatures.
    const bool actions =
        m_bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(servicePath),
                      QString::fromLatin1(serviceInterface), QStringLiteral("ActionInvoked"), this,
                      SLOT(onActionInvoked(uint,QString)));
    const bool closings =
        m_bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(servicePath),
                      QString::fromLatin1(serviceInterface), QStringLiteral("NotificationClosed"),
                      this, SLOT(onClosed(uint,uint)));
    m_subscribed = actions && closings;
    if (!m_subscribed)
        qCInfo(core::lcPlatform) << "Signals of the notification service not subscribed";
}

} // namespace qpm
