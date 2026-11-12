#pragma once

#include <QIcon>
#include <QObject>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace qpm {

/**
 * The icon of the program in the notification area of the desktop, with a
 * menu to show the window, write a message, end the connection and quit.
 * Clicking the icon shows the window as well.
 *
 * Many desktops have no notification area, and the offscreen platform never
 * has one; show() then shows nothing and returns false, and the window has to
 * stay visible, which shouldStartHidden() takes into account.
 *
 * Needs a QApplication. Lives in the main thread.
 */
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(const QIcon &icon, QObject *parent = nullptr);
    ~TrayIcon() override;

    /// Whether the window may start hidden: only when asked for and a notification area exists.
    [[nodiscard]] static bool shouldStartHidden(bool startMinimized, bool trayAvailable);

    /// Puts the icon into the notification area; false when the desktop has none.
    bool show();
    [[nodiscard]] bool isShown() const;

    /**
     * Enables the entries that need a connected phone. They stay in the menu
     * while disabled, so that it is clear what exists.
     */
    void setConnected(bool connected);

    /// The menu, in the order: show window, write message, disconnect, quit.
    [[nodiscard]] QMenu *menu() const { return m_menu.get(); }

Q_SIGNALS:
    void showWindowRequested();
    void writeMessageRequested();
    void disconnectRequested();
    void quitRequested();

private:
    std::unique_ptr<QMenu> m_menu;
    /// Declared after the menu, so that it goes first; it refers to the menu.
    std::unique_ptr<QSystemTrayIcon> m_icon;
    QAction *m_writeMessage = nullptr;
    QAction *m_disconnect = nullptr;
};

} // namespace qpm
