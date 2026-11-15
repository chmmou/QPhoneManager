#include "ui/FocusVisibility.h"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>

namespace qpm::ui {

namespace {

bool isModifierKey(int key)
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        return true;
    default:
        return false;
    }
}

} // namespace

FocusVisibility::FocusVisibility(QObject *parent) : QObject(parent)
{
    if (QCoreApplication *application = QCoreApplication::instance())
        application->installEventFilter(this);
}

bool FocusVisibility::eventFilter(QObject *, QEvent *event)
{
    switch (event->type()) {
    // A key that triggers a shortcut never arrives as a key press; the window
    // sees it as a shortcut override first, and the shortcut gets it.
    case QEvent::KeyPress:
    case QEvent::ShortcutOverride:
        if (!isModifierKey(static_cast<const QKeyEvent *>(event)->key()))
            setKeyboard(true);
        break;
    case QEvent::Shortcut:
        setKeyboard(true);
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::TouchBegin:
        setKeyboard(false);
        break;
    case QEvent::FocusIn:
        switch (static_cast<const QFocusEvent *>(event)->reason()) {
        case Qt::TabFocusReason:
        case Qt::BacktabFocusReason:
        case Qt::ShortcutFocusReason:
            setKeyboard(true);
            break;
        case Qt::MouseFocusReason:
            setKeyboard(false);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return false;
}

void FocusVisibility::setKeyboard(bool keyboard)
{
    if (m_keyboard == keyboard)
        return;
    m_keyboard = keyboard;
    Q_EMIT keyboardChanged();
}

} // namespace qpm::ui
