#pragma once

#include "core/Error.h"
#include "core/codec/NotificationCodec.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

namespace qpm::core::protocol {
class PhoneConnection;
struct AtReply;
} // namespace qpm::core::protocol

namespace qpm::core::service {

/**
 * The notifications shown on the connected phone: the list, the ones the
 * phone posts and removes while connected, dismissing one, running one of its
 * actions and fetching its picture.
 *
 * The phone client reads notifications only when its user allowed it access
 * in the Android settings. Without that access every request fails with
 * ErrorCode::PermissionDenied and access() tells why: the access is off
 * (Access::Disabled) or on but not running (Access::NotBound). A phone that
 * cannot read notifications at all fails with ErrorCode::NotSupported. The
 * list empties as soon as the access is found missing.
 *
 * The list is read when the connection is ready. The phone sends posted and
 * removed notifications on its own only while no OBEX session runs; the
 * connection reads the list again after each one when its option
 * PhoneConnection::Options::notifications is on, which also asks the phone
 * for every change of a notification rather than only new ones. Every call that
 * needs the phone fails with ErrorCode::ConnectionClosed when it is not
 * connected, and a notification the list does not hold fails with
 * ErrorCode::NotFound before anything is sent. Everything from the phone is
 * checked against the limits of codec/NotificationCodec.h. Lives in the main
 * thread; no call blocks. Does not own the connection, which must outlive it.
 */
class NotificationService : public QObject
{
    Q_OBJECT

public:
    enum class Access { Unknown, Granted, Disabled, NotBound, NotSupported };
    Q_ENUM(Access)

    explicit NotificationService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /// The notifications on the phone, newest first; empty while not connected.
    [[nodiscard]] QList<codec::PhoneNotification> notifications() const { return m_notifications; }
    /// The notification with codec::PhoneNotification::key() `key`, if the list holds it.
    [[nodiscard]] std::optional<codec::PhoneNotification> notification(const QString &key) const;
    [[nodiscard]] Access access() const { return m_access; }

    /// Reads the whole list again and replaces the one held here.
    QFuture<Result<void>> refresh();

    /**
     * Dismisses the notification on the phone; it leaves the list here once
     * the phone accepted. Fails with ErrorCode::Rejected for an ongoing one,
     * which the phone keeps.
     */
    QFuture<Result<void>> dismiss(const QString &key);

    /**
     * Runs the action titled `action` of the notification on the phone, or
     * its tap when `action` is empty. The phone accepts this also when the
     * notification is gone meanwhile; nothing happens there then.
     */
    QFuture<Result<void>> trigger(const QString &key, const QString &action);

    /**
     * The picture of the notification as JPEG, scaled by the phone to fit
     * codec::notificationImageSide, at most codec::maxNotificationImageBytes.
     * Fails with ErrorCode::NotFound when it carries none, and with
     * ErrorCode::LimitExceeded for a larger one. Not decoded or checked yet.
     */
    QFuture<Result<QByteArray>> fetchImage(const QString &key);

    /**
     * Opens the Android settings for notification access on the phone, so
     * that its user can allow it there.
     */
    QFuture<Result<void>> showAccessSettings();

Q_SIGNALS:
    /// The list changed.
    void notificationsChanged();
    /**
     * The phone posted a notification this program had not seen before.
     * Not emitted for those of the first list after connecting.
     */
    void notificationPosted(const qpm::core::codec::PhoneNotification &notification);
    void accessChanged();

private:
    void onStateChanged();
    void onUnsolicited(const QString &line);
    void onListRead(const protocol::AtReply &reply);
    void applyList(const QStringList &lines);
    void setAccess(Access access);
    [[nodiscard]] Error accessError(const protocol::AtReply &reply, const QString &what);
    void upsert(const codec::PhoneNotification &notification, bool announce);

    protocol::PhoneConnection *m_connection = nullptr;
    QList<codec::PhoneNotification> m_notifications;
    Access m_access = Access::Unknown;
    /// The first list of this connection was asked for.
    bool m_listRequested = false;
    /// A list was read since the connection started.
    bool m_listKnown = false;
};

} // namespace qpm::core::service
