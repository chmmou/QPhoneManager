#pragma once

#include "core/Error.h"

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

#include <memory>

class QIODevice;

namespace qpm::ui {

/// Most pixels of a side of a contact photo that is sent to the phone.
inline constexpr int maxContactPhotoSide = 480;

/**
 * Reads an image from an untrusted source: the format is taken from the
 * content, the size is checked before decoding and decoding stops at
 * limits::maxImageAllocationMiB. With a valid `fitInto`, the image is decoded
 * at most that large, keeping its aspect ratio. Fails with
 * ErrorCode::InvalidInput for anything that is not a readable image and with
 * ErrorCode::LimitExceeded for an image above limits::maxImageWidth or
 * limits::maxImageHeight.
 */
[[nodiscard]] core::Result<QImage> readImageSafely(QIODevice *device, const QSize &fitInto = {});

/**
 * The pictures the image provider hands out, shared between the main thread
 * and the threads the QML engine loads images on: the folder of the photo
 * blobs of the current phone, the photo being edited and the picture being
 * cropped. Every member may be called from any thread.
 */
class PhotoSource
{
public:
    /// The folder of the photo blobs; empty when no phone is chosen.
    void setBlobDirectory(const QString &directory);
    [[nodiscard]] QString blobDirectory() const;

    /// Replaces the photo being edited; returns a number that is new each time.
    quint64 setDraft(const QImage &image);
    [[nodiscard]] QImage draft() const;

    /// Replaces the picture being cropped; returns a number that is new each time.
    quint64 setCandidate(const QImage &image);
    [[nodiscard]] QImage candidate() const;

private:
    mutable QMutex m_mutex;
    QString m_blobDirectory;
    QImage m_draft;
    QImage m_candidate;
    quint64 m_serial = 0;
};

/**
 * Hands out the program's pictures under "image://qpm/": "photo/<luid>/<blob>"
 * for a stored contact photo, "part/<blob>" for a picture sent in a message,
 * "draft/<n>" for the photo being edited and "candidate/<n>" for the picture
 * being cropped. Stored pictures are read with readImageSafely(). An unknown
 * or malformed id, and a blob that is no picture, yield an empty image.
 * requestImage() may run on a loader thread of the engine.
 */
class PhoneImageProvider : public QQuickImageProvider
{
    Q_OBJECT

public:
    /**
     * A provider handed to QQmlEngine::addImageProvider() is owned by the
     * engine, so `parent` stays null there.
     */
    explicit PhoneImageProvider(std::shared_ptr<PhotoSource> source, QObject *parent = nullptr);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    std::shared_ptr<PhotoSource> m_source;
};

} // namespace qpm::ui
