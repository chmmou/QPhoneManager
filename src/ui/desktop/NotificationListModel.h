#pragma once

#include "core/codec/NotificationCodec.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace qpm::core::service {
class NotificationService;
}

namespace qpm::ui {

class PhotoSource;

/**
 * The notifications shown on the connected phone, newest first, with their
 * pictures and the actions on them: dismiss on the phone, run one of their
 * actions there, and open the settings of the phone where its user allows
 * the access to notifications.
 *
 * Nothing is kept on this computer; the list follows the phone while
 * connected and is empty without a connection. Pictures are fetched once per
 * notification, one after the other.
 *
 * Every failure is reported once through errorOccurred() as a translated text
 * that says what to do; an action that cannot start returns false. Lives in
 * the main thread; no call blocks. The service and `photos` must outlive the
 * model.
 */
class NotificationListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NotificationListModel)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Connected and allowed to read notifications, so they can be dismissed and used.
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY stateChanged FINAL)
    /// Why the list is empty when it is: no connection, no access on the phone, no notifications.
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY stateChanged FINAL)
    /// The user can allow the access on the phone, which openAccessSettings() shows there.
    Q_PROPERTY(bool accessMissing READ isAccessMissing NOTIFY stateChanged FINAL)

public:
    enum Role {
        /// Named "notificationKey", since QML takes "id" for a reserved word.
        KeyRole = Qt::UserRole + 1,
        AppRole,
        TitleRole,
        TextRole,
        TimeRole,
        /// The time in the short form of the language, or the time alone for today.
        TimeTextRole,
        ActionsRole,
        /// "image://qpm/thumb/<n>" once the picture is there, else empty.
        ImageUrlRole,
        /// It is not ongoing, so the phone lets it be dismissed.
        CanDismissRole,
    };

    NotificationListModel(core::service::NotificationService *service,
                          std::shared_ptr<PhotoSource> photos, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return int(m_rows.size()); }
    [[nodiscard]] bool canEdit() const;
    [[nodiscard]] QString emptyText() const;
    [[nodiscard]] bool isAccessMissing() const;

    /// Follows the connection; the list empties without one.
    void setConnected(bool connected);

    /// Dismisses the notification `key` on the phone.
    Q_INVOKABLE bool dismiss(const QString &key);
    /// Runs the action titled `action` of the notification on the phone, or its tap for an empty
    /// one.
    Q_INVOKABLE bool trigger(const QString &key, const QString &action);
    /// Reads the list again.
    Q_INVOKABLE bool refresh();
    /// Opens the settings for the access to notifications on the phone.
    Q_INVOKABLE bool openAccessSettings();
    /// The row of `key`, or -1 when it is not shown.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &key) const;

Q_SIGNALS:
    void countChanged();
    void stateChanged();
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);

private:
    void sync();
    void fetchNextImage();
    [[nodiscard]] QString messageFor(const core::Error &error) const;

    core::service::NotificationService *m_service = nullptr;
    std::shared_ptr<PhotoSource> m_photos;
    QList<core::codec::PhoneNotification> m_rows;
    /// The picture of each notification, by key, as the number it is kept under.
    QHash<QString, quint64> m_images;
    /// Keys whose picture was asked for in this connection, fetched or not.
    QSet<QString> m_imagesAsked;
    bool m_connected = false;
    bool m_fetching = false;
};

} // namespace qpm::ui
