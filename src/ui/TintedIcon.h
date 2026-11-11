#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class QSvgRenderer;

namespace qpm::ui {

/**
 * Draws one of the program's icons in `color`. The icons draw with the
 * current text color, which a plain Image cannot set; this item replaces it.
 * Shows nothing for an unknown name. The item is decorative: it takes no
 * focus and reports nothing to assistive tools.
 */
class TintedIcon : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TintedIcon)

    /// File name without extension below the icon resources, for example "battery".
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)

public:
    explicit TintedIcon(QQuickItem *parent = nullptr);
    ~TintedIcon() override;

    [[nodiscard]] QString name() const { return m_name; }
    void setName(const QString &name);
    [[nodiscard]] QColor color() const { return m_color; }
    void setColor(const QColor &color);

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void nameChanged();
    void colorChanged();

private:
    void reload();

    QString m_name;
    QColor m_color = Qt::black;
    QSvgRenderer *m_renderer = nullptr;
};

} // namespace qpm::ui
