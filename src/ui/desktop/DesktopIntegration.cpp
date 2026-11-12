#include "ui/desktop/DesktopIntegration.h"

#include "core/Logging.h"
#include "core/service/ClipboardService.h"
#include "core/service/NotificationService.h"
#include "platform/DesktopNotifier.h"
#include "ui/Settings.h"

#include <QClipboard>
#include <QGuiApplication>

namespace qpm::ui {

namespace {

using core::Result;
using core::codec::PhoneNotification;
using core::service::ClipboardService;
using core::service::NotificationService;

QString dismissKey()
{
    return QStringLiteral("dismiss");
}

QString actionKey(qsizetype index)
{
    return QStringLiteral("action-%1").arg(index);
}

QClipboard *desktopClipboard()
{
    return qobject_cast<QGuiApplication *>(QCoreApplication::instance())
               ? QGuiApplication::clipboard()
               : nullptr;
}

} // namespace

DesktopIntegration::DesktopIntegration(Settings *settings, NotificationService *notifications,
                                       ClipboardService *clipboard, QObject *parent)
    : QObject(parent), m_settings(settings), m_notifications(notifications), m_clipboard(clipboard)
{
    Q_ASSERT(settings && notifications && clipboard);
    connect(settings, &Settings::clipboardSyncChanged, this, &DesktopIntegration::applySettings);
    connect(settings, &Settings::notificationsEnabledChanged, this,
            &DesktopIntegration::applySettings);
    connect(notifications, &NotificationService::notificationPosted, this,
            &DesktopIntegration::onPosted);
    connect(notifications, &NotificationService::notificationsChanged, this,
            &DesktopIntegration::onPhoneListChanged);
    connect(clipboard, &ClipboardService::phoneTextChanged, this,
            &DesktopIntegration::onPhoneClipboardChanged);
    if (QClipboard *board = desktopClipboard()) {
        connect(board, &QClipboard::dataChanged, this,
                &DesktopIntegration::onDesktopClipboardChanged);
    }
    applySettings();
}

void DesktopIntegration::setNotifier(DesktopNotifier *notifier)
{
    if (m_notifier)
        m_notifier->disconnect(this);
    m_shown.clear();
    m_notifier = notifier;
    if (!m_notifier)
        return;
    connect(m_notifier, &DesktopNotifier::actionInvoked, this,
            &DesktopIntegration::onActionInvoked);
    connect(m_notifier, &DesktopNotifier::closed, this,
            [this](uint id, uint) { m_shown.remove(id); });
}

void DesktopIntegration::applySettings()
{
    if (!m_settings)
        return;
    m_clipboard->setSyncing(m_settings->clipboardSync());
    if (m_settings->notificationsEnabled() || !m_notifier)
        return;
    for (auto it = m_shown.cbegin(); it != m_shown.cend(); ++it)
        static_cast<void>(m_notifier->close(it.key()));
    m_shown.clear();
}

void DesktopIntegration::onPosted(const PhoneNotification &notification)
{
    if (!m_notifier || !m_settings || !m_settings->notificationsEnabled())
        return;
    QList<DesktopNotifier::Action> actions = {
        {QString::fromLatin1(DesktopNotifier::defaultActionKey), tr("Show")}};
    for (qsizetype i = 0; i < notification.actions.size(); ++i)
        actions.append({actionKey(i), notification.actions.at(i)});
    if (!notification.ongoing)
        actions.append({dismissKey(), tr("Dismiss on the phone")});
    const QString summary =
        notification.title.isEmpty() ? notification.appLabel : notification.title;
    const QString body = notification.text.isEmpty()
                             ? notification.appLabel
                             : notification.appLabel + u'\n' + notification.text;
    const QString key = notification.key();
    m_notifier->show(summary, body, actions).then(this, [this, key](const Result<uint> &shown) {
        if (!shown.hasValue()) {
            // The list in the window still has the notification.
            qCInfo(core::lcUi) << "Desktop notification not shown:" << shown.error().message();
            return;
        }
        m_shown.insert(shown.value(), key);
    });
}

void DesktopIntegration::onPhoneListChanged()
{
    if (!m_notifier)
        return;
    for (auto it = m_shown.begin(); it != m_shown.end();) {
        if (m_notifications->notification(it.value())) {
            ++it;
            continue;
        }
        static_cast<void>(m_notifier->close(it.key()));
        it = m_shown.erase(it);
    }
}

void DesktopIntegration::onActionInvoked(uint id, const QString &key)
{
    const QString notificationKey = m_shown.value(id);
    if (notificationKey.isEmpty())
        return;
    if (key == QLatin1StringView(DesktopNotifier::defaultActionKey)) {
        Q_EMIT notificationsRequested();
        return;
    }
    const std::optional<PhoneNotification> notification =
        m_notifications->notification(notificationKey);
    if (!notification)
        return;
    const auto report = [](const Result<void> &done) {
        // The window shows the notification with the same buttons again.
        if (!done.hasValue())
            qCInfo(core::lcUi) << "Notification action failed:" << done.error().message();
    };
    if (key == dismissKey()) {
        m_notifications->dismiss(notificationKey).then(this, report);
        return;
    }
    for (qsizetype i = 0; i < notification->actions.size(); ++i) {
        if (key == actionKey(i)) {
            m_notifications->trigger(notificationKey, notification->actions.at(i))
                .then(this, report);
            return;
        }
    }
}

void DesktopIntegration::onDesktopClipboardChanged()
{
    if (QClipboard *board = desktopClipboard())
        m_clipboard->offerDesktopText(board->text());
}

void DesktopIntegration::onPhoneClipboardChanged(const QString &text)
{
    if (QClipboard *board = desktopClipboard())
        board->setText(text);
}

} // namespace qpm::ui
