#include "ui/screen/ScreenMirrorItem.h"

#include "ui/screen/ScreenMirror.h"

#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTransformNode>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace qpm::ui {

namespace {

constexpr int quarterTurns = 4;
constexpr qreal quarterTurnDegrees = 90.0;
// One notch of a mouse wheel, in eighths of a degree.
constexpr int wheelNotch = 120;

} // namespace

ScreenMirrorItem::ScreenMirrorItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
}

void ScreenMirrorItem::setMirror(ScreenMirror *mirror)
{
    if (m_mirror == mirror)
        return;
    if (m_mirror)
        m_mirror->disconnect(this);
    m_mirror = mirror;
    if (mirror)
        connect(mirror, &ScreenMirror::frameChanged, this, &ScreenMirrorItem::onFrameChanged);
    Q_EMIT mirrorChanged();
    onFrameChanged();
}

void ScreenMirrorItem::setTurns(int turns)
{
    const int normalised = ((turns % quarterTurns) + quarterTurns) % quarterTurns;
    if (m_turns == normalised)
        return;
    m_turns = normalised;
    updateImplicitSize();
    Q_EMIT turnsChanged();
    Q_EMIT pictureRectChanged();
    update();
}

void ScreenMirrorItem::setKeyboardInput(bool enabled)
{
    if (m_keyboardInput == enabled)
        return;
    m_keyboardInput = enabled;
    setFlag(ItemAcceptsInputMethod, enabled);
    Q_EMIT keyboardInputChanged();
}

QSizeF ScreenMirrorItem::turnedSize() const
{
    const QSize frame = m_mirror ? m_mirror->frameSize() : QSize();
    if (frame.isEmpty())
        return {};
    return m_turns % 2 == 0 ? QSizeF(frame) : QSizeF(frame.height(), frame.width());
}

QRectF ScreenMirrorItem::pictureRect() const
{
    const QSizeF shown = turnedSize();
    if (shown.isEmpty() || width() <= 0 || height() <= 0)
        return {};
    const QSizeF fitted = shown.scaled(size(), Qt::KeepAspectRatio);
    return {QPointF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted};
}

std::optional<QPoint> ScreenMirrorItem::toPhone(const QPointF &position) const
{
    const QRectF picture = pictureRect();
    if (picture.isEmpty() || !picture.contains(position))
        return std::nullopt;
    return toPhoneClamped(position);
}

QPoint ScreenMirrorItem::toPhoneClamped(const QPointF &position) const
{
    const QRectF picture = pictureRect();
    const QSize frame = m_mirror ? m_mirror->frameSize() : QSize();
    if (picture.isEmpty() || frame.isEmpty())
        return {};
    const qreal u = std::clamp((position.x() - picture.x()) / picture.width(), 0.0, 1.0);
    const qreal v = std::clamp((position.y() - picture.y()) / picture.height(), 0.0, 1.0);
    // The share across and down the shown picture, turned back to the phone's own axes.
    qreal across = u;
    qreal down = v;
    switch (m_turns) {
    case 1:
        across = v;
        down = 1.0 - u;
        break;
    case 2:
        across = 1.0 - u;
        down = 1.0 - v;
        break;
    case 3:
        across = 1.0 - v;
        down = u;
        break;
    default:
        break;
    }
    const int x = std::min(int(std::floor(across * frame.width())), frame.width() - 1);
    const int y = std::min(int(std::floor(down * frame.height())), frame.height() - 1);
    return {x, y};
}

QPoint ScreenMirrorItem::phonePointAt(qreal x, qreal y) const
{
    return toPhone(QPointF(x, y)).value_or(QPoint(-1, -1));
}

QVariant ScreenMirrorItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled)
        return m_keyboardInput;
    return QQuickItem::inputMethodQuery(query);
}

void ScreenMirrorItem::onFrameChanged()
{
    m_textureDirty = true;
    updateImplicitSize();
    Q_EMIT pictureRectChanged();
    update();
}

void ScreenMirrorItem::updateImplicitSize()
{
    const QSizeF shown = turnedSize();
    setImplicitSize(shown.width(), shown.height());
}

void ScreenMirrorItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        Q_EMIT pictureRectChanged();
        update();
    }
}

QSGNode *ScreenMirrorItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data)
{
    Q_UNUSED(data)
    const QImage image = m_mirror ? m_mirror->frame() : QImage();
    const QRectF target = pictureRect();
    if (image.isNull() || target.isEmpty() || !window()) {
        delete oldNode;
        return nullptr;
    }
    auto *root = static_cast<QSGTransformNode *>(oldNode);
    if (!root) {
        root = new QSGTransformNode();
        m_textureDirty = true;
    }
    auto *picture = static_cast<QSGImageNode *>(root->firstChild());
    if (m_textureDirty || !picture) {
        // A new node for each new picture: the old one takes its texture with it.
        if (picture) {
            root->removeChildNode(picture);
            delete picture;
        }
        picture = window()->createImageNode();
        picture->setOwnsTexture(true);
        picture->setTexture(window()->createTextureFromImage(image));
        picture->setFiltering(QSGTexture::Linear);
        root->appendChildNode(picture);
        m_textureDirty = false;
    }
    const QSizeF drawn = m_turns % 2 == 0 ? target.size() : QSizeF(target.height(), target.width());
    picture->setRect(
        QRectF(-drawn.width() / 2, -drawn.height() / 2, drawn.width(), drawn.height()));
    QMatrix4x4 matrix;
    matrix.translate(float(target.center().x()), float(target.center().y()));
    matrix.rotate(float(quarterTurnDegrees * m_turns), 0.0f, 0.0f, 1.0f);
    root->setMatrix(matrix);
    return root;
}

void ScreenMirrorItem::mousePressEvent(QMouseEvent *event)
{
    const std::optional<QPoint> phone = toPhone(event->position());
    if (!m_mirror || !m_mirror->canControl() || !phone || m_dragging) {
        event->ignore();
        return;
    }
    if (activeFocusOnTab())
        forceActiveFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::RightButton) {
        m_mirror->longPress(phone->x(), phone->y());
    } else {
        m_dragging = true;
        m_lastDragPosition = event->position();
        m_mirror->pressPointer(*phone);
    }
    event->accept();
}

void ScreenMirrorItem::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !m_mirror) {
        event->ignore();
        return;
    }
    m_lastDragPosition = event->position();
    m_mirror->movePointer(toPhoneClamped(event->position()));
    event->accept();
}

void ScreenMirrorItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || event->button() != Qt::LeftButton || !m_mirror) {
        event->ignore();
        return;
    }
    m_dragging = false;
    m_mirror->releasePointer(toPhoneClamped(event->position()));
    event->accept();
}

void ScreenMirrorItem::mouseUngrabEvent()
{
    // A finger left down would block the next touch on the phone.
    if (m_dragging && m_mirror)
        m_mirror->releasePointer(toPhoneClamped(m_lastDragPosition));
    m_dragging = false;
}

void ScreenMirrorItem::wheelEvent(QWheelEvent *event)
{
    const std::optional<QPoint> phone = toPhone(event->position());
    if (!m_mirror || !m_mirror->canControl() || !phone || event->angleDelta().y() == 0) {
        event->ignore();
        return;
    }
    m_wheelAngle += event->angleDelta().y();
    while (std::abs(m_wheelAngle) >= wheelNotch) {
        const bool up = m_wheelAngle > 0;
        m_mirror->scroll(phone->x(), phone->y(), up);
        m_wheelAngle += up ? -wheelNotch : wheelNotch;
    }
    event->accept();
}

void ScreenMirrorItem::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers commands = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    if (!m_keyboardInput || !m_mirror || !m_mirror->canControl() ||
        (event->modifiers() & commands)) {
        event->ignore();
        return;
    }
    QString name;
    switch (event->key()) {
    case Qt::Key_Backspace:
        name = QStringLiteral("backspace");
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        name = QStringLiteral("enter");
        break;
    case Qt::Key_Left:
        name = QStringLiteral("left");
        break;
    case Qt::Key_Right:
        name = QStringLiteral("right");
        break;
    case Qt::Key_Up:
        name = QStringLiteral("up");
        break;
    case Qt::Key_Down:
        name = QStringLiteral("down");
        break;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Escape:
        event->ignore();
        return;
    default:
        break;
    }
    if (!name.isEmpty()) {
        m_mirror->key(name);
        event->accept();
        return;
    }
    const QString text = event->text();
    if (text.isEmpty() || text.front().unicode() < u' ' || !m_mirror->typeText(text)) {
        event->ignore();
        return;
    }
    event->accept();
}

void ScreenMirrorItem::inputMethodEvent(QInputMethodEvent *event)
{
    if (m_keyboardInput && m_mirror && !event->commitString().isEmpty())
        m_mirror->typeText(event->commitString());
    event->accept();
}

} // namespace qpm::ui
