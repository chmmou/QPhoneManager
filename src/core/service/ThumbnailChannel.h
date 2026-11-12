#pragma once

#include "core/Error.h"
#include "core/service/FileService.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace qpm::core::service {

/// The thumbnail the phone made for one file.
struct Thumbnail
{
    QString name;
    /// EXIF orientation 1 to 8 of the picture; 0 when the phone did not know it.
    int orientation = 0;
    /// Usually JPEG; untrusted, and decoded only with the limits for pictures from the phone.
    QByteArray image;
};

/**
 * Reads the stream in which the phone sends thumbnails, one piece at a time:
 * per file "FileBegin;<folder>\<name>;Orientation=<n>" and a line end, the
 * picture, and "\r\nFileEnd"; at the end "FileBegin;EndOfStream" and a line
 * end. Pictures carry no length, so one ends where "\r\nFileEnd" is followed
 * by the next "FileBegin;".
 *
 * Only names given to the constructor are accepted, each once. A picture
 * above maxImageBytes, a header line above 4096 bytes, an unknown name and
 * anything else out of form fail with ErrorCode::LimitExceeded or
 * ErrorCode::ProtocolError, after which the reader takes nothing more.
 */
class ThumbnailStreamReader
{
public:
    /// Largest picture taken for one thumbnail.
    static constexpr qsizetype maxImageBytes = 4 * 1024 * 1024;

    explicit ThumbnailStreamReader(const QStringList &expectedNames);

    /// Takes the next bytes of the stream.
    [[nodiscard]] Result<void> feed(const QByteArray &bytes);
    /// The end marker has arrived.
    [[nodiscard]] bool isFinished() const { return m_finished; }
    /// The thumbnails complete so far, in the order the phone sent them.
    [[nodiscard]] QList<Thumbnail> thumbnails() const { return m_thumbnails; }

private:
    [[nodiscard]] Result<void> step(bool &progressed);

    QStringList m_expected;
    QByteArray m_buffer;
    QList<Thumbnail> m_thumbnails;
    Thumbnail m_current;
    bool m_inImage = false;
    bool m_finished = false;
    Error m_error;
};

/**
 * Thumbnails of pictures and videos on the phone, which the phone makes and
 * sends over its thumbnail channel. Needs a side channel; over a transport
 * without one every call fails with ErrorCode::NotSupported.
 *
 * Lives in the main thread; no call blocks. Does not own the connection,
 * which must outlive it.
 */
class ThumbnailChannel : public QObject
{
    Q_OBJECT

public:
    /// Port of the thumbnail channel as the phone names it.
    static constexpr quint16 thumbnailPort = 5217;
    /// Most names in one request; more do not fit into one packet.
    static constexpr int maxNamesPerRequest = 100;

    explicit ThumbnailChannel(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /// See FileService::setEndpointResolver().
    void setEndpointResolver(FileService::EndpointResolver resolver);

    /**
     * Asks for thumbnails of the files `names` in `folder`, at most
     * maxNamesPerRequest. The phone leaves out files it has no thumbnail
     * for, so the result may be shorter. Fails with ErrorCode::InvalidInput
     * for too many or unusable names, and with ErrorCode::Rejected when the
     * phone refuses the folder, which it does for the folders of other apps.
     */
    QFuture<Result<QList<Thumbnail>>> fetch(const QString &folder, const QStringList &names);

    /// The XML the request carries.
    [[nodiscard]] static QByteArray requestBody(const QString &folder, const QStringList &names);

private:
    protocol::PhoneConnection *m_connection = nullptr;
    FileService::EndpointResolver m_resolver;
};

} // namespace qpm::core::service
