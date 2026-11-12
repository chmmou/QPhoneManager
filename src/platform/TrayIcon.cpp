#include "platform/TrayIcon.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QSystemTrayIcon>

namespace qpm {

TrayIcon::TrayIcon(const QIcon &icon, QObject *parent)
    : QObject(parent), m_menu(std::make_unique<QMenu>()),
      m_icon(std::make_unique<QSystemTrayIcon>(icon))
{
    QAction *showWindow = m_menu->addAction(tr("Show window"));
    m_writeMessage = m_menu->addAction(tr("Write message"));
    m_disconnect = m_menu->addAction(tr("Disconnect"));
    m_menu->addSeparator();
    QAction *quit = m_menu->addAction(tr("Quit"));
    connect(showWindow, &QAction::triggered, this, &TrayIcon::showWindowRequested);
    connect(m_writeMessage, &QAction::triggered, this, &TrayIcon::writeMessageRequested);
    connect(m_disconnect, &QAction::triggered, this, &TrayIcon::disconnectRequested);
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);
    setConnected(false);

    m_icon->setToolTip(QCoreApplication::applicationName());
    m_icon->setContextMenu(m_menu.get());
    connect(m_icon.get(), &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    Q_EMIT showWindowRequested();
            });
}

TrayIcon::~TrayIcon() = default;

bool TrayIcon::shouldStartHidden(bool startMinimized, bool trayAvailable)
{
    return startMinimized && trayAvailable;
}

bool TrayIcon::show()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return false;
    m_icon->show();
    return true;
}

bool TrayIcon::isShown() const
{
    return m_icon->isVisible();
}

void TrayIcon::setConnected(bool connected)
{
    m_writeMessage->setEnabled(connected);
    m_disconnect->setEnabled(connected);
}

} // namespace qpm
