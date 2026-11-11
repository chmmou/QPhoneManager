#include "ui/QrCodeImage.h"

#include "core/Logging.h"

#include <QPainter>

#include <qrcodegen.hpp>

#include <algorithm>
#include <exception>

namespace qpm::ui {

namespace {
// The standard asks for four light modules around the code.
constexpr int quietZone = 4;
} // namespace

QrCodeImage::QrCodeImage(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setImplicitSize(256, 256);
}

void QrCodeImage::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    m_modules = encode(text, m_size);
    Q_EMIT textChanged();
    update();
}

std::vector<bool> QrCodeImage::encode(const QString &text, int &side)
{
    side = 0;
    if (text.isEmpty())
        return {};
    try {
        const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(
            text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        side = code.getSize();
        std::vector<bool> modules(std::size_t(side) * std::size_t(side));
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x)
                modules[std::size_t(y) * std::size_t(side) + std::size_t(x)] = code.getModule(x, y);
        }
        return modules;
    } catch (const std::exception &error) {
        // The library reports a text that does not fit into a QR code by
        // throwing; the item then stays empty and says so through `valid`.
        qCWarning(core::lcUi) << "QR code not created:" << error.what();
        side = 0;
        return {};
    }
}

void QrCodeImage::paint(QPainter *painter)
{
    if (m_size == 0)
        return;
    const int modulesWithZone = m_size + 2 * quietZone;
    const qreal scale = std::min(width(), height()) / modulesWithZone;
    const qreal side = scale * modulesWithZone;
    const QPointF origin((width() - side) / 2, (height() - side) / 2);
    painter->fillRect(QRectF(origin, QSizeF(side, side)), Qt::white);
    painter->setPen(Qt::NoPen);
    painter->setBrush(Qt::black);
    for (int y = 0; y < m_size; ++y) {
        for (int x = 0; x < m_size; ++x) {
            if (!m_modules[std::size_t(y) * std::size_t(m_size) + std::size_t(x)])
                continue;
            painter->drawRect(QRectF(origin.x() + (x + quietZone) * scale,
                                     origin.y() + (y + quietZone) * scale, scale, scale));
        }
    }
}

} // namespace qpm::ui
