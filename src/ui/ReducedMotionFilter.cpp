#include "ui/ReducedMotionFilter.h"

#include "ui/Settings.h"

#include <QEvent>
#include <QVariant>

namespace qpm::ui {

ReducedMotionFilter::ReducedMotionFilter(const Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
}

bool ReducedMotionFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::ContextMenu)
        return false;
    // The menu hangs on the attached ContextMenu object, a child of the item;
    // the style gives it no other way in.
    const QObjectList children = watched->children();
    for (QObject *child : children) {
        QObject *menu = child->inherits("QQuickContextMenu")
                            ? child->property("menu").value<QObject *>()
                            : nullptr;
        if (!menu)
            continue;
        for (const char *name : {"enter", "exit"}) {
            // The transition of the style is kept on the menu for when the setting is off again.
            const QByteArray kept = QByteArrayLiteral("qpmStyle") + name;
            if (!menu->property(kept.constData()).isValid())
                menu->setProperty(kept.constData(), menu->property(name));
            const QVariant style = menu->property(kept.constData());
            menu->setProperty(name,
                              m_settings->reduceMotion() ? QVariant(style.metaType()) : style);
        }
    }
    return false;
}

} // namespace qpm::ui
