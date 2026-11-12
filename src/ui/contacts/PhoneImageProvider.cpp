#include "ui/contacts/PhoneImageProvider.h"

#include "core/Logging.h"
#include "core/codec/AppListCodec.h"
#include "core/codec/Limits.h"

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QMutexLocker>

#include <algorithm>

namespace qpm::ui {

namespace {

constexpr qsizetype sha1HexLength = 40;

bool isBlobName(const QString &name)
{
    if (name.size() != sha1HexLength)
        return false;
    return std::all_of(name.cbegin(), name.cend(), [](QChar c) {
        return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
    });
}

bool isNumber(const QString &text)
{
    return !text.isEmpty() &&
           std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c >= u'0' && c <= u'9'; });
}

QImage fitted(const QImage &image, const QSize &requestedSize)
{
    if (image.isNull() || !requestedSize.isValid() || requestedSize.isEmpty())
        return image;
    return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

core::Result<QImage> readImageSafely(QIODevice *device, const QSize &fitInto)
{
    using core::Error;
    using core::ErrorCode;
    // The limit is a setting of the whole process; it is set once, and the
    // static initialisation makes that safe when images load on several threads.
    static const bool allocationLimited = []() {
        QImageReader::setAllocationLimit(core::limits::maxImageAllocationMiB);
        return true;
    }();
    Q_UNUSED(allocationLimited)
    QImageReader reader(device);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid())
        return Error(ErrorCode::InvalidInput, QStringLiteral("Not a readable image"));
    if (size.width() > core::limits::maxImageWidth ||
        size.height() > core::limits::maxImageHeight) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("Image of %1 x %2 pixels is too large")
                         .arg(size.width())
                         .arg(size.height()));
    }
    if (fitInto.isValid() && !fitInto.isEmpty() &&
        (size.width() > fitInto.width() || size.height() > fitInto.height())) {
        reader.setScaledSize(size.scaled(fitInto, Qt::KeepAspectRatio));
    }
    QImage image = reader.read();
    if (image.isNull()) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("Image cannot be decoded: %1").arg(reader.errorString()));
    }
    return image;
}

void PhotoSource::setBlobDirectory(const QString &directory)
{
    const QMutexLocker locker(&m_mutex);
    m_blobDirectory = directory;
}

QString PhotoSource::blobDirectory() const
{
    const QMutexLocker locker(&m_mutex);
    return m_blobDirectory;
}

quint64 PhotoSource::setDraft(const QImage &image)
{
    const QMutexLocker locker(&m_mutex);
    m_draft = image;
    return ++m_serial;
}

QImage PhotoSource::draft() const
{
    const QMutexLocker locker(&m_mutex);
    return m_draft;
}

quint64 PhotoSource::setCandidate(const QImage &image)
{
    const QMutexLocker locker(&m_mutex);
    m_candidate = image;
    return ++m_serial;
}

QImage PhotoSource::candidate() const
{
    const QMutexLocker locker(&m_mutex);
    return m_candidate;
}

quint64 PhotoSource::addThumbnail(const QImage &image)
{
    const QMutexLocker locker(&m_mutex);
    const quint64 id = ++m_serial;
    m_thumbnails.insert(id, image);
    m_thumbnailOrder.append(id);
    while (m_thumbnailOrder.size() > maxThumbnails)
        m_thumbnails.remove(m_thumbnailOrder.takeFirst());
    return id;
}

QImage PhotoSource::thumbnail(quint64 id) const
{
    const QMutexLocker locker(&m_mutex);
    return m_thumbnails.value(id);
}

void PhotoSource::clearThumbnails()
{
    const QMutexLocker locker(&m_mutex);
    m_thumbnails.clear();
    m_thumbnailOrder.clear();
}

void PhotoSource::setAppIcon(const QString &package, const QImage &image)
{
    const QMutexLocker locker(&m_mutex);
    m_appIcons.insert(package, image);
}

QImage PhotoSource::appIcon(const QString &package) const
{
    const QMutexLocker locker(&m_mutex);
    return m_appIcons.value(package);
}

void PhotoSource::clearAppIcons()
{
    const QMutexLocker locker(&m_mutex);
    m_appIcons.clear();
}

PhoneImageProvider::PhoneImageProvider(std::shared_ptr<PhotoSource> source, QObject *parent)
    : QQuickImageProvider(QQmlImageProviderBase::Image), m_source(std::move(source))
{
    setParent(parent);
}

QImage PhoneImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image;
    const QStringList parts = id.split(u'/');
    if (parts.value(0) == QLatin1StringView("draft")) {
        image = fitted(m_source->draft(), requestedSize);
    } else if (parts.value(0) == QLatin1StringView("candidate")) {
        image = fitted(m_source->candidate(), requestedSize);
    } else if (parts.size() == 2 && parts.at(0) == QLatin1StringView("thumb")) {
        bool ok = false;
        const quint64 thumb = parts.at(1).toULongLong(&ok);
        if (ok)
            image = fitted(m_source->thumbnail(thumb), requestedSize);
    } else if (parts.size() == 3 && parts.at(0) == QLatin1StringView("app") &&
               core::codec::isValidPackageName(parts.at(1)) && isNumber(parts.at(2))) {
        image = fitted(m_source->appIcon(parts.at(1)), requestedSize);
    } else if ((parts.size() == 3 && parts.at(0) == QLatin1StringView("photo") &&
                isBlobName(parts.at(2))) ||
               (parts.size() == 2 && parts.at(0) == QLatin1StringView("part") &&
                isBlobName(parts.at(1)))) {
        const QString directory = m_source->blobDirectory();
        if (!directory.isEmpty()) {
            QFile file(QDir(directory).filePath(parts.constLast()));
            if (file.open(QIODevice::ReadOnly)) {
                const core::Result<QImage> read = readImageSafely(&file, requestedSize);
                if (read.hasValue())
                    image = read.value();
                else
                    qCInfo(core::lcUi) << "Picture not shown:" << read.error().message();
            }
        }
    }
    if (size)
        *size = image.size();
    return image;
}

} // namespace qpm::ui
