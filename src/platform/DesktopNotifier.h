#pragma once

#include "core/Error.h"

#include <QDBusConnection>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QPromise>
#include <QString>

#include <memory>
#include <optional>

namespace qpm {

/**
 * Shows notifications on the desktop through the freedesktop notification
 * service on the session bus, with buttons for actions, and reports which
 * one was chosen and when a notification closed.
 *
 * Summary, body and action labels may come from the phone; they are shown as
 * plain text, the body escaped where the service reads it as markup. Without a reachable
 * notification service nothing is shown and every call fails with
 * ErrorCode::NotSupported.
 *
 * Lives in the main thread; no call blocks. Each future finishes once.
 */
class DesktopNotifier : public QObject
{
    Q_OBJECT

public:
    /// A button of a notification; `key` comes back through actionInvoked().
    struct Action
    {
        QString key;
        QString label;
    };

    /// The key the service reports when the notification itself was clicked.
    static constexpr char defaultActionKey[] = "default";

    /// Uses the session bus.
    explicit DesktopNotifier(QObject *parent = nullptr);
    /// Uses `bus`, which must stay connected while calls run.
    explicit DesktopNotifier(const QDBusConnection &bus, QObject *parent = nullptr);

    /**
     * Shows a notification and returns the id the service gave it. With a
     * `replacesId` other than 0 it takes the place of that one. An action
     * with an empty key or label is left out.
     */
    QFuture<core::Result<uint>> show(const QString &summary, const QString &body,
                                     const QList<Action> &actions, uint replacesId = 0);

    /// Closes the notification `id`; succeeds also when it is gone already.
    QFuture<core::Result<void>> close(uint id);

Q_SIGNALS:
    /// The user chose the action `key` of notification `id`.
    void actionInvoked(uint id, const QString &key);
    /**
     * Notification `id` closed: `reason` 1 expired, 2 dismissed by the user,
     * 3 closed through close(), 4 for any other reason.
     */
    void closed(uint id, uint reason);

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &key);
    void onClosed(uint id, uint reason);

private:
    void subscribe();
    void notify(const std::shared_ptr<QPromise<core::Result<uint>>> &promise,
                const QString &summary, const QString &body, const QList<Action> &actions,
                uint replacesId);

    QDBusConnection m_bus;
    /// Whether the service reads the body as markup; unknown until it was asked.
    std::optional<bool> m_bodyMarkup;
    bool m_subscribed = false;
};

} // namespace qpm
