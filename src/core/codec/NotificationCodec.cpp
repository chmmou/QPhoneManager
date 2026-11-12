#include "core/codec/NotificationCodec.h"

#include "core/codec/AppListCodec.h"
#include "core/codec/AtText.h"
#include "core/codec/PhoneTime.h"

namespace qpm::core::codec {

namespace {

constexpr qsizetype recordFields = 18;
constexpr qsizetype removalFields = 3;
constexpr int maxImageSide = 4096;
// Notification.FLAG_ONGOING_EVENT and Notification.FLAG_NO_CLEAR of Android.
constexpr int ongoingFlags = 0x02 | 0x20;

QString cut(const QString &text, qsizetype limit)
{
    return text.size() > limit ? text.left(limit) : text;
}

std::optional<QDateTime> stamp(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.endsWith(u'Z'))
        return parseUtcStamp(trimmed);
    return parseLocalStamp(trimmed);
}

Error malformed(const QString &what)
{
    return Error(ErrorCode::ProtocolError, QStringLiteral("Notification record with %1").arg(what));
}

// Id, tag and package, the first three fields of a record and of a removal.
Result<PhoneNotification> identity(const QStringList &fields)
{
    bool ok = false;
    const int id = fields.value(0).trimmed().toInt(&ok);
    if (!ok)
        return malformed(QStringLiteral("an unreadable id"));
    const QString tag = decodeQp(fields.value(1));
    if (tag.size() > maxNotificationTagLength) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("Notification tag longer than %1 characters")
                         .arg(maxNotificationTagLength));
    }
    const QString package = decodeQp(fields.value(2)).trimmed();
    if (!isValidPackageName(package))
        return malformed(QStringLiteral("an unusable package name"));
    PhoneNotification notification;
    notification.id = id;
    notification.tag = tag;
    notification.package = package;
    return notification;
}

QString identityArguments(const PhoneNotification &notification)
{
    return QStringLiteral("%1,%2,%3")
        .arg(notification.id)
        .arg(encodeQp(notification.tag), notification.package);
}

} // namespace

QString PhoneNotification::key() const
{
    return package + u'|' + QString::number(id) + u'|' + tag;
}

Result<PhoneNotification> parseNotificationRecord(const QString &record)
{
    const QStringList fields = splitFields(record.trimmed());
    if (fields.size() < recordFields)
        return malformed(QStringLiteral("%1 fields").arg(fields.size()));
    Result<PhoneNotification> parsed = identity(fields);
    if (!parsed.hasValue())
        return parsed;
    PhoneNotification notification = parsed.value();
    notification.isNew = fields.at(3).trimmed() == QLatin1StringView("1");
    if (const std::optional<QDateTime> posted = stamp(fields.at(4)))
        notification.postTime = *posted;
    bool ok = false;
    const int flags = fields.at(6).trimmed().toInt(&ok);
    notification.ongoing = ok && (flags & ongoingFlags) != 0;
    notification.appLabel = cut(decodeQp(fields.at(9)).trimmed(), maxNotificationLabelLength);
    if (notification.appLabel.isEmpty())
        notification.appLabel = notification.package;
    const QString ticker = decodeQp(fields.at(10));
    notification.title = cut(decodeQp(fields.at(11)), maxNotificationTitleLength);
    QStringList parts;
    for (const qsizetype index : {qsizetype(12), qsizetype(14), qsizetype(15), qsizetype(16)}) {
        const QString part = decodeQp(fields.at(index)).trimmed();
        if (!part.isEmpty() && !parts.contains(part))
            parts.append(part);
    }
    if (parts.isEmpty() && notification.title.isEmpty() && !ticker.trimmed().isEmpty())
        parts.append(ticker.trimmed());
    notification.text = cut(parts.join(u'\n'), maxNotificationTextLength);
    const QString actions = decodeQp(fields.at(13));
    if (!actions.isEmpty()) {
        const QStringList titles = actions.split(u'|');
        for (const QString &title : titles) {
            const QString trimmed = title.trimmed();
            if (trimmed.isEmpty() || trimmed.size() > maxNotificationActionLength)
                continue;
            if (notification.actions.size() == maxNotificationActions)
                break;
            notification.actions.append(trimmed);
        }
    }
    notification.hasPicture = fields.at(17).trimmed() == QLatin1StringView("1");
    return notification;
}

Result<NotificationEvent> parseNotificationEvent(const QString &line)
{
    static constexpr QLatin1StringView prefix("*NOTIFY:");
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(prefix))
        return malformed(QStringLiteral("another prefix"));
    const QString rest = trimmed.mid(prefix.size()).trimmed();
    const QStringList fields = splitFields(rest);
    NotificationEvent event;
    if (fields.size() == removalFields) {
        const Result<PhoneNotification> removed = identity(fields);
        if (!removed.hasValue())
            return removed.error();
        event.kind = NotificationEvent::Kind::Removed;
        event.notification = removed.value();
        return event;
    }
    const Result<PhoneNotification> posted = parseNotificationRecord(rest);
    if (!posted.hasValue())
        return posted.error();
    event.notification = posted.value();
    return event;
}

Result<QString> dismissNotificationCommand(const PhoneNotification &notification)
{
    if (!isValidPackageName(notification.package))
        return Error(ErrorCode::InvalidInput, QStringLiteral("Unusable package name"));
    return QStringLiteral("AT*NOTIC=") + identityArguments(notification);
}

Result<QString> notificationActionCommand(const PhoneNotification &notification,
                                          const QString &action)
{
    if (!isValidPackageName(notification.package))
        return Error(ErrorCode::InvalidInput, QStringLiteral("Unusable package name"));
    if (action.isEmpty())
        return QStringLiteral("AT*NOTIACTION=") + identityArguments(notification);
    if (!notification.actions.contains(action))
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The notification has no such action"));
    return QStringLiteral("AT*NOTIACTION=") + identityArguments(notification) + u',' +
           encodeQp(action);
}

Result<QString> notificationImageName(const PhoneNotification &notification, int side)
{
    if (!isValidPackageName(notification.package))
        return Error(ErrorCode::InvalidInput, QStringLiteral("Unusable package name"));
    if (side < 1 || side > maxImageSide)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Unusable picture size"));
    return QStringLiteral("\\notificationimage\\%1\\%2\\%3\\%3")
        .arg(notification.package)
        .arg(notification.id)
        .arg(side);
}

} // namespace qpm::core::codec
