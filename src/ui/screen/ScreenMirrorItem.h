#pragma once

#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace qpm::ui {

class ScreenMirror;

/**
 * Shows the screen of a ScreenMirror and passes the mouse and the keyboard on
 * to the phone.
 *
 * The picture fits into the item, keeping its proportions, centred, and
 * turned clockwise by `turns` quarter turns. A left button press, drag and
 * release become a touch, a swipe or, held still, a long touch; the right
 * button gives a long touch at once; the wheel scrolls. With
 * `keyboardInput`, typed text goes to the focused field of the phone, and
 * Backspace, Enter and the arrow keys become the keys of the phone. Tab,
 * Escape and keys with Ctrl, Alt or Meta are left to the window, so that
 * the focus can always leave the item.
 *
 * Positions outside the picture are ignored. The texture is made from the
 * picture the mirror decoded beforehand, on the render thread.
 */
class ScreenMirrorItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qpm::ui::ScreenMirror *mirror READ mirror WRITE setMirror NOTIFY mirrorChanged FINAL)
    /// Quarter turns clockwise, 0 to 3; other values are taken modulo 4.
    Q_PROPERTY(int turns READ turns WRITE setTurns NOTIFY turnsChanged FINAL)
    Q_PROPERTY(bool keyboardInput READ keyboardInput WRITE setKeyboardInput NOTIFY
                   keyboardInputChanged FINAL)
    /// Where the picture is drawn, in item coordinates; empty without a picture.
    Q_PROPERTY(QRectF pictureRect READ pictureRect NOTIFY pictureRectChanged FINAL)

public:
    explicit ScreenMirrorItem(QQuickItem *parent = nullptr);

    [[nodiscard]] ScreenMirror *mirror() const { return m_mirror; }
    void setMirror(ScreenMirror *mirror);
    [[nodiscard]] int turns() const { return m_turns; }
    void setTurns(int turns);
    [[nodiscard]] bool keyboardInput() const { return m_keyboardInput; }
    void setKeyboardInput(bool enabled);
    [[nodiscard]] QRectF pictureRect() const;

    /**
     * The phone pixel under the item position `position`, or nothing outside
     * the picture.
     */
    [[nodiscard]] std::optional<QPoint> toPhone(const QPointF &position) const;
    /// The same, clamped to the picture, for a drag that leaves it.
    [[nodiscard]] QPoint toPhoneClamped(const QPointF &position) const;

    /// The phone pixel under `x`, `y`, or (-1, -1) outside the picture; for tests and QML.
    [[nodiscard]] Q_INVOKABLE QPoint phonePointAt(qreal x, qreal y) const;

    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

Q_SIGNALS:
    void mirrorChanged();
    void turnsChanged();
    void keyboardInputChanged();
    void pictureRectChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;

private:
    void onFrameChanged();
    void updateImplicitSize();
    /// The size of the picture as shown, after the turns.
    [[nodiscard]] QSizeF turnedSize() const;

    QPointer<ScreenMirror> m_mirror;
    QPointF m_lastDragPosition;
    int m_turns = 0;
    int m_wheelAngle = 0;
    bool m_keyboardInput = false;
    bool m_dragging = false;
    bool m_textureDirty = false;
};

} // namespace qpm::ui
