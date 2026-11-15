#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

namespace qpm {
class DesktopNotifier;
}

namespace qpm::core::codec {
struct PhoneNotification;
}

namespace qpm::core::service {
class ClipboardService;
class NotificationService;
} // namespace qpm::core::service

namespace qpm::ui {

class Settings;

/**
 * Connects the phone to the desktop as the settings ask: copies the clipboard
 * both ways while clipboard sync is on, and shows each new notification of
 * the phone as a desktop notification while those are on. A desktop
 * notification offers the actions of the phone's one and a button to dismiss
 * it on the phone; clicking it asks for the window through
 * notificationsRequested(). It closes when the phone removes its one.
 *
 * The clipboard of this computer is used only under a QGuiApplication. Without
 * a notifier no desktop notification is shown. Lives in the main thread. The
 * services and `settings` must outlive it.
 */
class DesktopIntegration : public QObject
{
    Q_OBJECT

public:
    DesktopIntegration(Settings *settings, core::service::NotificationService *notifications,
                       core::service::ClipboardService *clipboard, QObject *parent = nullptr);

    /// Shows desktop notifications through `notifier`, which must outlive this object; null for
    /// none.
    void setNotifier(DesktopNotifier *notifier);

Q_SIGNALS:
    /// The user clicked a desktop notification; the window should show the notifications.
    void notificationsRequested();

private:
    void applySettings();
    void onPosted(const core::codec::PhoneNotification &notification);
    void onPhoneListChanged();
    void onActionInvoked(uint id, const QString &key);
    /// Closes desktop notification `id`; a failure is only logged.
    void closeShown(uint id);
    void onDesktopClipboardChanged();
    void onPhoneClipboardChanged(const QString &text);

    QPointer<Settings> m_settings;
    core::service::NotificationService *m_notifications = nullptr;
    core::service::ClipboardService *m_clipboard = nullptr;
    QPointer<DesktopNotifier> m_notifier;
    /// The key of the phone's notification behind each desktop notification shown.
    QHash<uint, QString> m_shown;
};

} // namespace qpm::ui
