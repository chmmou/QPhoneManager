#pragma once

#include "core/Error.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QDBusMessage;

namespace qpm {

/**
 * Keeps the PIN of a phone in the freedesktop Secret Service, one item per
 * phone with the attributes application=qphonemanager and device=<deviceId>.
 * The secret travels over a "plain" session, which the specification allows
 * on the local session bus.
 *
 * Without a reachable Secret Service nothing is stored anywhere and every
 * call fails with ErrorCode::NotSupported. A locked collection or item is
 * unlocked through the prompt of the service, which may ask the user; a
 * dismissed prompt fails with ErrorCode::Cancelled. The PIN never appears in
 * an error message or in the log.
 *
 * Lives in the main thread; no call blocks. Each future finishes once.
 */
class SecretStore : public QObject
{
    Q_OBJECT

public:
    /// Uses the session bus.
    explicit SecretStore(QObject *parent = nullptr);
    /// Uses `bus`, which must stay connected while calls run.
    explicit SecretStore(const QDBusConnection &bus, QObject *parent = nullptr);

    /**
     * Stores `pin` for the phone, replacing an earlier one. Fails with
     * ErrorCode::InvalidInput when either argument is empty.
     */
    QFuture<core::Result<void>> storePin(const QString &deviceId, const QString &pin);

    /// The stored PIN of the phone, or nothing when none is stored.
    QFuture<core::Result<std::optional<QString>>> pin(const QString &deviceId);

    /// Removes the stored PIN of the phone; succeeds when none was stored.
    QFuture<core::Result<void>> removePin(const QString &deviceId);

private:
    template <typename T> using Continuation = std::function<void(const core::Result<T> &)>;
    using ReplyHandler = std::function<void(const QDBusMessage &)>;

    void call(const QString &path, const QString &interface, const QString &method,
              const QList<QVariant> &arguments, const ReplyHandler &handler);
    void openSession(const Continuation<QDBusObjectPath> &next);
    void closeSession(const QDBusObjectPath &session);
    void findItems(const QString &deviceId, const Continuation<QList<QDBusObjectPath>> &next);
    void unlock(const QList<QDBusObjectPath> &objects,
                const Continuation<QList<QDBusObjectPath>> &next);
    void runPrompt(const QDBusObjectPath &prompt, const Continuation<QDBusVariant> &next);
    void deleteItems(QList<QDBusObjectPath> items, const Continuation<void> &next);

    QDBusConnection m_bus;
};

} // namespace qpm
