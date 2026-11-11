#include "ui/TintedIcon.h"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

namespace qpm::ui {

namespace {
constexpr int defaultSize = 24;
// Only names of the bundled icons are looked up.
bool isIconName(const QString &name)
{
    if (name.isEmpty())
        return false;
    for (const QChar c : name) {
        if (!(c >= u'a' && c <= u'z') && !(c >= u'0' && c <= u'9') && c != u'-')
            return false;
    }
    return true;
}
} // namespace

TintedIcon::TintedIcon(QQuickItem *parent)
    : QQuickPaintedItem(parent), m_renderer(new QSvgRenderer(this))
{
    setImplicitSize(defaultSize, defaultSize);
}

TintedIcon::~TintedIcon() = default;

void TintedIcon::setName(const QString &name)
{
    if (name == m_name)
        return;
    m_name = name;
    reload();
    Q_EMIT nameChanged();
}

void TintedIcon::setColor(const QColor &color)
{
    if (color == m_color)
        return;
    m_color = color;
    reload();
    Q_EMIT colorChanged();
}

void TintedIcon::reload()
{
    QByteArray svg;
    if (isIconName(m_name)) {
        QFile file(QStringLiteral(":/icons/tabler/%1.svg").arg(m_name));
        if (file.open(QIODevice::ReadOnly))
            svg = file.readAll();
    }
    svg.replace("currentColor", m_color.name(QColor::HexRgb).toLatin1());
    m_renderer->load(svg);
    update();
}

void TintedIcon::paint(QPainter *painter)
{
    if (!m_renderer->isValid())
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    m_renderer->render(painter, boundingRect());
}

} // namespace qpm::ui
