#pragma once

#include <QObject>

namespace qpm::ui {

class Settings;

/**
 * Opens the editing menu that Qt Quick Controls 6.11 and later give every
 * text field and text area without its motion while
 * Settings::reduceMotion() is on, and with the motion of the style when it
 * is off. Takes effect once installed on the application with
 * QCoreApplication::installEventFilter(); it looks at each request for a
 * context menu before the item opens its menu, and never consumes one.
 * `settings` must outlive this object.
 */
class ReducedMotionFilter : public QObject
{
    Q_OBJECT

public:
    explicit ReducedMotionFilter(const Settings *settings, QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    const Settings *m_settings;
};

} // namespace qpm::ui
