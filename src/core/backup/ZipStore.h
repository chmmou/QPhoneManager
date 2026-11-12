#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QList>
#include <QSaveFile>
#include <QSet>
#include <QString>

namespace qpm::core::backup {

/// Most entries ZipReader accepts in one archive, folders included.
inline constexpr qsizetype maxZipEntries = 20000;
/// Largest entry ZipReader and ZipWriter take, in bytes.
inline constexpr qint64 maxZipEntryBytes = 512LL * 1024 * 1024;

/**
 * The CRC-32 of ZIP, ISO 3309 with the polynomial 0xEDB88320. Pass the
 * result of an earlier call as `crc` to continue over more data.
 */
[[nodiscard]] quint32 crc32(QByteArrayView data, quint32 crc = 0);

/**
 * True for a control character, a format character such as a change of the
 * writing direction, and a line or paragraph separator, all of which can
 * hide what a name says.
 */
[[nodiscard]] bool isControlOrFormat(uint codePoint);

/**
 * True when `name` may name a file inside an archive: not empty, segments
 * separated by "/" that are neither empty, "." nor "..", no leading "/", no
 * backslash, no colon that could name a drive, and no character that
 * isControlOrFormat() reports.
 */
[[nodiscard]] bool isSafeEntryName(const QString &name);

/// One file of an archive, as its central directory lists it.
struct ZipEntryInfo
{
    QString name;
    qint64 size = 0;
    quint32 crc = 0;
    /// Where the data starts in the archive file.
    qint64 dataOffset = 0;
};

/**
 * Writes a ZIP archive whose entries are stored without compression, through
 * QSaveFile: the file appears at `path` only after commit() succeeded, and an
 * archive left without commit() is discarded. Names are written as UTF-8.
 * Neither ZIP64 nor encryption is written, so the archive stays below 4 GiB.
 *
 * Every call fails with ErrorCode::IoFailure when the file cannot be written;
 * after a failure, nothing more is written.
 */
class ZipWriter
{
public:
    /// `modified` is the time every entry carries, in local time.
    explicit ZipWriter(const QString &path,
                       const QDateTime &modified = QDateTime::currentDateTime());

    [[nodiscard]] Result<void> open();

    /**
     * Appends an entry. Fails with ErrorCode::InvalidInput for a name that
     * isSafeEntryName() refuses or that was added before, and with
     * ErrorCode::LimitExceeded above maxZipEntries entries, above
     * maxZipEntryBytes, or when the archive would outgrow 4 GiB.
     */
    [[nodiscard]] Result<void> add(const QString &name, QByteArrayView data);

    /// Writes the central directory and moves the file into place.
    [[nodiscard]] Result<void> commit();

private:
    struct Written
    {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };

    [[nodiscard]] Result<void> write(QByteArrayView bytes);

    QSaveFile m_file;
    QList<Written> m_entries;
    QSet<QString> m_names;
    quint16 m_dosTime = 0;
    quint16 m_dosDate = 0;
    bool m_failed = false;
};

/**
 * Reads a ZIP archive whose entries are stored without compression, the form
 * ZipWriter writes and "zip -0" writes. open() checks the whole archive
 * before anything is read from it; read() checks the CRC of what it returns.
 *
 * open() fails with ErrorCode::IoFailure when the file cannot be read, with
 * ErrorCode::NotSupported for compression, encryption, ZIP64 and archives
 * spread over several disks, with ErrorCode::LimitExceeded above
 * maxZipEntries entries or maxZipEntryBytes in one entry, and with
 * ErrorCode::InvalidInput for anything damaged or cut short, a name that
 * isSafeEntryName() refuses and a name that occurs twice. Folder entries are
 * checked and then left out of entries().
 */
class ZipReader
{
public:
    explicit ZipReader(const QString &path);

    [[nodiscard]] Result<void> open();

    /// The files of the archive in the order of its central directory.
    [[nodiscard]] const QList<ZipEntryInfo> &entries() const { return m_entries; }
    [[nodiscard]] bool contains(const QString &name) const { return m_index.contains(name); }

    /**
     * The content of the entry `name`. Fails with ErrorCode::NotFound for a
     * name the archive does not hold, ErrorCode::InvalidInput when the CRC
     * does not match, and ErrorCode::IoFailure when the file cannot be read.
     */
    [[nodiscard]] Result<QByteArray> read(const QString &name);

private:
    [[nodiscard]] Result<QByteArray> readAt(qint64 offset, qint64 length);

    QFile m_file;
    QList<ZipEntryInfo> m_entries;
    QHash<QString, qsizetype> m_index;
};

} // namespace qpm::core::backup
