#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * Tells whether the focus of the program is to be drawn: true while the user
 * works the windows from the keyboard, false while with the mouse or by
 * touch. A key press other than a lone modifier, a shortcut, and the focus
 * arriving through Tab, Backtab or a shortcut, turn it on; a mouse button or a touch
 * turns it off. Focus set by the program for any other reason, as a dialog
 * does when it opens, leaves it as it was, so a dialog opened from the
 * keyboard shows its focus and one opened with the mouse does not, until the
 * next key press.
 *
 * Watches every window of the application from the moment the QML engine
 * creates it, and never consumes an event. Lives in the main thread.
 */
class FocusVisibility : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FocusVisibility)
    QML_SINGLETON

    Q_PROPERTY(bool keyboard READ isKeyboard NOTIFY keyboardChanged FINAL)

public:
    explicit FocusVisibility(QObject *parent = nullptr);

    [[nodiscard]] bool isKeyboard() const { return m_keyboard; }

Q_SIGNALS:
    void keyboardChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setKeyboard(bool keyboard);

    bool m_keyboard = false;
};

} // namespace qpm::ui
