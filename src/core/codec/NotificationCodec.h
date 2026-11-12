#pragma once

#include "core/Error.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace qpm::core::codec {

/// One notification shown on the phone, as the phone client reports it.
struct PhoneNotification
{
    /// The id the posting app gave it; may be negative.
    int id = 0;
    /// The tag the posting app gave it; empty for none.
    QString tag;
    /// The package of the posting app; isValidPackageName() holds for it.
    QString package;
    /// Not yet sent to this computer before, as far as the phone knows.
    bool isNew = false;
    /// When it was posted, in the time of this computer; invalid when not known.
    QDateTime postTime;
    /// The name the phone shows for the posting app; the package when it gives none.
    QString appLabel;
    QString title;
    /// The text, with the sub text and the lines of an expanded view below it.
    QString text;
    /// The titles of its actions, in order.
    QStringList actions;
    /// It carries a picture that notificationImageName() can fetch.
    bool hasPicture = false;
    /// It cannot be dismissed, such as a running download or a call.
    bool ongoing = false;

    /// Package, tag and id together; they identify the notification on the phone.
    [[nodiscard]] QString key() const;

    bool operator==(const PhoneNotification &other) const = default;
};

/// What a "*NOTIFY:" line the phone sends on its own reports.
struct NotificationEvent
{
    enum class Kind { Posted, Removed };
    Kind kind = Kind::Posted;
    /// The whole notification for Posted; only id, tag and package for Removed.
    PhoneNotification notification;
};

/// Most notifications taken from one list.
inline constexpr qsizetype maxNotifications = 500;
/// Longest texts taken from the phone, in characters; longer ones are cut there.
inline constexpr qsizetype maxNotificationTitleLength = 1000;
inline constexpr qsizetype maxNotificationTextLength = 10000;
inline constexpr qsizetype maxNotificationLabelLength = 256;
inline constexpr qsizetype maxNotificationTagLength = 512;
inline constexpr qsizetype maxNotificationActionLength = 256;
/// Most actions taken from one notification; further ones are left out.
inline constexpr qsizetype maxNotificationActions = 8;
/// Largest picture of a notification read from the phone.
inline constexpr quint64 maxNotificationImageBytes = 4 * 1024 * 1024;
/// The largest side the phone scales a notification picture down to.
inline constexpr int notificationImageSide = 512;

/**
 * Reads one notification record, the text after "*NOTIR:" or "*NOTIFY:":
 * eighteen comma-separated fields in the escaped form of the phone. Texts
 * longer than the limits above are cut. Fails with ErrorCode::ProtocolError
 * for fewer fields, an id that is no 32-bit number or a package name that
 * isValidPackageName() refuses, and with ErrorCode::LimitExceeded for a tag
 * longer than maxNotificationTagLength.
 */
[[nodiscard]] Result<PhoneNotification> parseNotificationRecord(const QString &record);

/**
 * Reads a whole "*NOTIFY:" line: a record for a posted notification, or id,
 * tag and package alone for a removed one. Fails as parseNotificationRecord()
 * does, and for a line with another prefix.
 */
[[nodiscard]] Result<NotificationEvent> parseNotificationEvent(const QString &line);

/**
 * "AT*NOTIC=<id>,<tag>,<package>": dismisses the one notification on the
 * phone. Fails with ErrorCode::InvalidInput for a package name that
 * isValidPackageName() refuses. There is deliberately no form that dismisses
 * every notification.
 */
[[nodiscard]] Result<QString> dismissNotificationCommand(const PhoneNotification &notification);

/**
 * "AT*NOTIACTION=<id>,<tag>,<package>[,<action>]": runs the action with the
 * title `action` on the phone, or the tap on the notification itself when
 * `action` is empty. Fails with ErrorCode::InvalidInput for a package name
 * that isValidPackageName() refuses and for an action the notification does
 * not offer.
 */
[[nodiscard]] Result<QString> notificationActionCommand(const PhoneNotification &notification,
                                                        const QString &action);

/**
 * The Filebrowser name of the picture of the notification, scaled by the
 * phone to fit `side` by `side` pixels. Fails with ErrorCode::InvalidInput for
 * a package name that isValidPackageName() refuses and a side outside 1 to
 * 4096.
 */
[[nodiscard]] Result<QString> notificationImageName(const PhoneNotification &notification,
                                                    int side = notificationImageSide);

} // namespace qpm::core::codec
