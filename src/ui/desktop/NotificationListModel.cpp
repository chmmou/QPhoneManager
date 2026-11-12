#include "ui/desktop/NotificationListModel.h"

#include "core/Logging.h"
#include "core/service/NotificationService.h"
#include "ui/contacts/PhoneImageProvider.h"

#include <QBuffer>
#include <QDate>
#include <QLocale>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::PhoneNotification;
using core::service::NotificationService;

qsizetype indexOfKey(const QList<PhoneNotification> &list, const QString &key, qsizetype from = 0)
{
    for (qsizetype i = from; i < list.size(); ++i) {
        if (list.at(i).key() == key)
            return i;
    }
    return -1;
}

} // namespace

NotificationListModel::NotificationListModel(NotificationService *service,
                                             std::shared_ptr<PhotoSource> photos, QObject *parent)
    : QAbstractListModel(parent), m_service(service), m_photos(std::move(photos))
{
    Q_ASSERT(service && m_photos);
    connect(service, &NotificationService::notificationsChanged, this,
            &NotificationListModel::sync);
    connect(service, &NotificationService::accessChanged, this,
            &NotificationListModel::stateChanged);
}

int NotificationListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant NotificationListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const PhoneNotification &row = m_rows.at(index.row());
    switch (role) {
    case KeyRole:
        return row.key();
    case AppRole:
        return row.appLabel;
    case Qt::DisplayRole:
    case TitleRole:
        return row.title.isEmpty() ? row.appLabel : row.title;
    case TextRole:
        return row.text;
    case TimeRole:
        return row.postTime;
    case TimeTextRole: {
        if (!row.postTime.isValid())
            return QString();
        const QDateTime local = row.postTime.toLocalTime();
        const QLocale locale;
        return local.date() == QDate::currentDate()
                   ? locale.toString(local.time(), QLocale::ShortFormat)
                   : locale.toString(local, QLocale::ShortFormat);
    }
    case ActionsRole:
        return row.actions;
    case ImageUrlRole: {
        const auto image = m_images.constFind(row.key());
        return image == m_images.cend() ? QString()
                                        : QStringLiteral("image://qpm/thumb/%1").arg(*image);
    }
    case CanDismissRole:
        return !row.ongoing;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> NotificationListModel::roleNames() const
{
    return {
        {KeyRole, QByteArrayLiteral("notificationKey")},
        {AppRole, QByteArrayLiteral("app")},
        {TitleRole, QByteArrayLiteral("title")},
        {TextRole, QByteArrayLiteral("text")},
        {TimeRole, QByteArrayLiteral("time")},
        {TimeTextRole, QByteArrayLiteral("timeText")},
        {ActionsRole, QByteArrayLiteral("actions")},
        {ImageUrlRole, QByteArrayLiteral("imageUrl")},
        {CanDismissRole, QByteArrayLiteral("canDismiss")},
    };
}

bool NotificationListModel::canEdit() const
{
    return m_connected && m_service->access() == NotificationService::Access::Granted;
}

QString NotificationListModel::emptyText() const
{
    if (!m_connected)
        return tr("Connect your phone to see its notifications.");
    switch (m_service->access()) {
    case NotificationService::Access::Disabled:
        return tr("MyPhoneExplorer Client may not read the notifications of the phone. Allow "
                  "it on the phone under notification access.");
    case NotificationService::Access::NotBound:
        return tr("The notification access of MyPhoneExplorer Client is not running. Turn it "
                  "off and on again on the phone.");
    case NotificationService::Access::NotSupported:
        return tr("This phone cannot pass on its notifications.");
    case NotificationService::Access::Unknown:
        return tr("Loading the notifications of the phone.");
    case NotificationService::Access::Granted:
        break;
    }
    return tr("The phone shows no notifications.");
}

bool NotificationListModel::isAccessMissing() const
{
    const NotificationService::Access access = m_service->access();
    return m_connected && (access == NotificationService::Access::Disabled ||
                           access == NotificationService::Access::NotBound);
}

void NotificationListModel::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    if (!connected) {
        m_imagesAsked.clear();
        m_images.clear();
    }
    sync();
    Q_EMIT stateChanged();
}

bool NotificationListModel::dismiss(const QString &key)
{
    if (!canEdit())
        return false;
    m_service->dismiss(key).then(this, [this](const Result<void> &dismissed) {
        if (!dismissed.hasValue())
            Q_EMIT errorOccurred(messageFor(dismissed.error()));
    });
    return true;
}

bool NotificationListModel::trigger(const QString &key, const QString &action)
{
    if (!canEdit())
        return false;
    m_service->trigger(key, action).then(this, [this](const Result<void> &triggered) {
        if (!triggered.hasValue()) {
            Q_EMIT errorOccurred(messageFor(triggered.error()));
            return;
        }
        Q_EMIT infoMessage(tr("Done on the phone."));
    });
    return true;
}

bool NotificationListModel::refresh()
{
    if (!m_connected)
        return false;
    m_service->refresh().then(this, [this](const Result<void> &refreshed) {
        // A missing access shows in emptyText(). Other failures are told.
        if (!refreshed.hasValue() && refreshed.error().code() != ErrorCode::PermissionDenied &&
            refreshed.error().code() != ErrorCode::NotSupported) {
            Q_EMIT errorOccurred(messageFor(refreshed.error()));
        }
    });
    return true;
}

bool NotificationListModel::openAccessSettings()
{
    if (!m_connected)
        return false;
    m_service->showAccessSettings().then(this, [this](const Result<void> &shown) {
        if (!shown.hasValue()) {
            Q_EMIT errorOccurred(messageFor(shown.error()));
            return;
        }
        Q_EMIT infoMessage(tr("The phone shows the setting now. Allow MyPhoneExplorer Client "
                              "there, then load the list again."));
    });
    return true;
}

int NotificationListModel::rowOf(const QString &key) const
{
    return int(indexOfKey(m_rows, key));
}

void NotificationListModel::sync()
{
    const QList<PhoneNotification> target =
        m_connected ? m_service->notifications() : QList<PhoneNotification>();
    const int before = int(m_rows.size());
    for (qsizetype row = m_rows.size() - 1; row >= 0; --row) {
        const QString key = m_rows.at(row).key();
        if (indexOfKey(target, key) >= 0)
            continue;
        beginRemoveRows({}, int(row), int(row));
        m_rows.removeAt(row);
        m_images.remove(key);
        endRemoveRows();
    }
    for (qsizetype row = 0; row < target.size(); ++row) {
        const PhoneNotification &wanted = target.at(row);
        const qsizetype found = indexOfKey(m_rows, wanted.key(), row);
        if (found == row) {
            if (!(m_rows.at(row) == wanted)) {
                m_rows[row] = wanted;
                Q_EMIT dataChanged(index(int(row)), index(int(row)));
            }
            continue;
        }
        if (found > row) {
            beginMoveRows({}, int(found), int(found), {}, int(row));
            m_rows.move(found, row);
            endMoveRows();
            if (!(m_rows.at(row) == wanted)) {
                m_rows[row] = wanted;
                Q_EMIT dataChanged(index(int(row)), index(int(row)));
            }
            continue;
        }
        beginInsertRows({}, int(row), int(row));
        m_rows.insert(row, wanted);
        endInsertRows();
    }
    if (before != m_rows.size())
        Q_EMIT countChanged();
    Q_EMIT stateChanged();
    fetchNextImage();
}

void NotificationListModel::fetchNextImage()
{
    if (m_fetching || !m_connected)
        return;
    for (const PhoneNotification &row : std::as_const(m_rows)) {
        const QString key = row.key();
        if (!row.hasPicture || m_imagesAsked.contains(key))
            continue;
        m_imagesAsked.insert(key);
        m_fetching = true;
        m_service->fetchImage(key).then(this, [this, key](const Result<QByteArray> &fetched) {
            m_fetching = false;
            if (!fetched.hasValue()) {
                // The notification stays usable without its picture.
                qCInfo(core::lcUi) << "Notification picture not read:" << fetched.error().message();
            } else {
                QByteArray bytes = fetched.value();
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::ReadOnly);
                const Result<QImage> image =
                    readImageSafely(&buffer, QSize(core::codec::notificationImageSide,
                                                   core::codec::notificationImageSide));
                const int row = rowOf(key);
                if (!image.hasValue()) {
                    qCInfo(core::lcUi)
                        << "Notification picture not readable:" << image.error().message();
                } else if (row >= 0) {
                    m_images.insert(key, m_photos->addThumbnail(image.value()));
                    Q_EMIT dataChanged(index(row), index(row), {ImageUrlRole});
                }
            }
            fetchNextImage();
        });
        return;
    }
}

QString NotificationListModel::messageFor(const Error &error) const
{
    switch (error.code()) {
    case ErrorCode::ConnectionClosed:
        return tr("The phone is not connected. Connect it, then try again.");
    case ErrorCode::PermissionDenied:
        return tr("MyPhoneExplorer Client may not read the notifications of the phone. Allow "
                  "it on the phone under notification access.");
    case ErrorCode::NotFound:
        return tr("The notification is no longer on the phone.");
    case ErrorCode::Rejected:
        return tr("The phone keeps this notification. Close it on the phone.");
    case ErrorCode::Timeout:
        return tr("The phone does not answer. Check that MyPhoneExplorer Client is open on the "
                  "phone, then try again.");
    default:
        break;
    }
    return tr("The phone did not do that. Try again.");
}

} // namespace qpm::ui
