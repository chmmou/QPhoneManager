#include "core/backup/ZipStore.h"

#include <QStringDecoder>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <optional>

namespace qpm::core::backup {

namespace {

// Record layouts after PKWARE's APPNOTE.TXT, sections 4.3.7, 4.3.12 and 4.3.16.
constexpr quint32 localHeaderSignature = 0x04034b50;
constexpr quint32 centralHeaderSignature = 0x02014b50;
constexpr quint32 endOfDirectorySignature = 0x06054b50;
constexpr quint32 zip64LocatorSignature = 0x07064b50;
constexpr qint64 localHeaderSize = 30;
constexpr qint64 centralHeaderSize = 46;
constexpr qint64 endOfDirectorySize = 22;
constexpr qint64 zip64LocatorSize = 20;
constexpr qint64 maxCommentSize = 0xFFFF;
constexpr quint32 zip64Marker = 0xFFFFFFFF;
constexpr quint16 zip64CountMarker = 0xFFFF;
constexpr qint64 maxArchiveOffset = 0xFFFFFFFF;
constexpr qsizetype maxNameBytes = 0xFFFF;

constexpr quint16 flagEncrypted = 0x0001;
constexpr quint16 flagStrongEncryption = 0x0040;
constexpr quint16 flagUtf8 = 0x0800;
constexpr quint16 flagMaskedDirectory = 0x2000;
constexpr quint16 methodStored = 0;
// Version 2.0, the lowest that knows folders; the upper byte of "made by" says Unix.
constexpr quint16 versionNeeded = 20;
constexpr quint16 versionMadeBy = (3 << 8) | 20;
// A regular file with the permissions rw-r--r--, in the upper half as Unix keeps it.
constexpr quint32 externalAttributes = 0100644U << 16;

constexpr std::array<quint32, 256> crcTable = []() {
    std::array<quint32, 256> table{};
    for (quint32 i = 0; i < 256; ++i) {
        quint32 value = i;
        for (int bit = 0; bit < 8; ++bit)
            value = (value & 1U) ? 0xEDB88320U ^ (value >> 1) : value >> 1;
        table[i] = value;
    }
    return table;
}();

void put16(QByteArray &out, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    out.append(bytes, 2);
}

void put32(QByteArray &out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}

quint16 get16(const QByteArray &data, qsizetype at)
{
    return qFromLittleEndian<quint16>(data.constData() + at);
}

quint32 get32(const QByteArray &data, qsizetype at)
{
    return qFromLittleEndian<quint32>(data.constData() + at);
}

Error damaged(const QString &what)
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Damaged archive: %1").arg(what));
}

Error unsupported(const QString &what)
{
    return Error(ErrorCode::NotSupported, QStringLiteral("Unsupported archive: %1").arg(what));
}

Error writeFailed()
{
    return Error(ErrorCode::IoFailure, QStringLiteral("Cannot write the archive"));
}

std::optional<QString> decodeName(const QByteArray &raw, quint16 flags)
{
    if (!(flags & flagUtf8))
        return QString::fromLatin1(raw);
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString name = decoder.decode(raw);
    if (decoder.hasError())
        return std::nullopt;
    return name;
}

} // namespace

quint32 crc32(QByteArrayView data, quint32 crc)
{
    quint32 value = ~crc;
    for (const char byte : data)
        value = crcTable[(value ^ quint8(byte)) & 0xFFU] ^ (value >> 8);
    return ~value;
}

bool isControlOrFormat(uint codePoint)
{
    switch (QChar::category(char32_t(codePoint))) {
    case QChar::Other_Control:
    case QChar::Other_Format:
    case QChar::Separator_Line:
    case QChar::Separator_Paragraph:
        return true;
    default:
        return false;
    }
}

bool isSafeEntryName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(u'/'))
        return false;
    const QList<uint> codePoints = name.toUcs4();
    for (const uint c : codePoints) {
        if (isControlOrFormat(c) || c == u'\\' || c == u':')
            return false;
    }
    const QStringList segments = name.split(u'/');
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1StringView(".") ||
            segment == QLatin1StringView(".."))
            return false;
    }
    return true;
}

ZipWriter::ZipWriter(const QString &path, const QDateTime &modified) : m_file(path)
{
    const QDateTime local = modified.toLocalTime();
    const QDate date = local.date();
    const QTime time = local.time();
    // The DOS format starts in 1980 and counts seconds in steps of two.
    if (date.isValid() && date.year() >= 1980 && date.year() <= 2107) {
        m_dosDate = quint16(((date.year() - 1980) << 9) | (date.month() << 5) | date.day());
        m_dosTime = quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
    } else {
        m_dosDate = quint16((1 << 5) | 1);
    }
}

Result<void> ZipWriter::open()
{
    if (!m_file.open(QIODevice::WriteOnly)) {
        m_failed = true;
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot create the archive"));
    }
    return {};
}

Result<void> ZipWriter::write(QByteArrayView bytes)
{
    if (m_file.write(bytes.constData(), bytes.size()) != bytes.size()) {
        m_failed = true;
        m_file.cancelWriting();
        return writeFailed();
    }
    return {};
}

Result<void> ZipWriter::add(const QString &name, QByteArrayView data)
{
    if (m_failed || !m_file.isOpen())
        return writeFailed();
    if (!isSafeEntryName(name))
        return Error(ErrorCode::InvalidInput, QStringLiteral("Entry name not allowed"));
    if (m_names.contains(name))
        return Error(ErrorCode::InvalidInput, QStringLiteral("Entry name used twice"));
    if (m_entries.size() >= maxZipEntries) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("More than %1 entries").arg(maxZipEntries));
    }
    if (data.size() > maxZipEntryBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("Entry of %1 bytes is too large").arg(data.size()));
    }
    const QByteArray encodedName = name.toUtf8();
    if (encodedName.size() > maxNameBytes)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Entry name too long"));
    const qint64 offset = m_file.pos();
    // The central directory still has to fit behind this entry.
    if (offset + localHeaderSize + encodedName.size() + data.size() + endOfDirectorySize >
        maxArchiveOffset) {
        return Error(ErrorCode::LimitExceeded, QStringLiteral("Archive larger than 4 GiB"));
    }

    Written entry;
    entry.name = encodedName;
    entry.crc = crc32(data);
    entry.size = quint32(data.size());
    entry.offset = quint32(offset);

    QByteArray header;
    header.reserve(localHeaderSize + encodedName.size());
    put32(header, localHeaderSignature);
    put16(header, versionNeeded);
    put16(header, flagUtf8);
    put16(header, methodStored);
    put16(header, m_dosTime);
    put16(header, m_dosDate);
    put32(header, entry.crc);
    put32(header, entry.size);
    put32(header, entry.size);
    put16(header, quint16(encodedName.size()));
    put16(header, 0);
    header.append(encodedName);
    if (const Result<void> written = write(header); !written.hasValue())
        return written;
    if (const Result<void> written = write(data); !written.hasValue())
        return written;
    m_entries.append(entry);
    m_names.insert(name);
    return {};
}

Result<void> ZipWriter::commit()
{
    if (m_failed || !m_file.isOpen())
        return writeFailed();
    const qint64 directoryOffset = m_file.pos();
    QByteArray directory;
    for (const Written &entry : std::as_const(m_entries)) {
        put32(directory, centralHeaderSignature);
        put16(directory, versionMadeBy);
        put16(directory, versionNeeded);
        put16(directory, flagUtf8);
        put16(directory, methodStored);
        put16(directory, m_dosTime);
        put16(directory, m_dosDate);
        put32(directory, entry.crc);
        put32(directory, entry.size);
        put32(directory, entry.size);
        put16(directory, quint16(entry.name.size()));
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, externalAttributes);
        put32(directory, entry.offset);
        directory.append(entry.name);
    }
    const qint64 directorySize = directory.size();
    if (directoryOffset + directorySize + endOfDirectorySize > maxArchiveOffset)
        return Error(ErrorCode::LimitExceeded, QStringLiteral("Archive larger than 4 GiB"));
    put32(directory, endOfDirectorySignature);
    put16(directory, 0);
    put16(directory, 0);
    put16(directory, quint16(m_entries.size()));
    put16(directory, quint16(m_entries.size()));
    put32(directory, quint32(directorySize));
    put32(directory, quint32(directoryOffset));
    put16(directory, 0);
    if (const Result<void> written = write(directory); !written.hasValue())
        return written;
    if (!m_file.commit()) {
        m_failed = true;
        return writeFailed();
    }
    return {};
}

ZipReader::ZipReader(const QString &path) : m_file(path) {}

Result<QByteArray> ZipReader::readAt(qint64 offset, qint64 length)
{
    if (offset < 0 || length < 0 || offset + length > m_file.size())
        return damaged(QStringLiteral("a record reaches beyond the end of the file"));
    if (!m_file.seek(offset))
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot read the archive"));
    QByteArray bytes = m_file.read(length);
    if (bytes.size() != length)
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot read the archive"));
    return bytes;
}

Result<void> ZipReader::open()
{
    m_entries.clear();
    m_index.clear();
    if (!m_file.open(QIODevice::ReadOnly))
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot open the archive"));
    const qint64 fileSize = m_file.size();
    if (fileSize < endOfDirectorySize)
        return damaged(QStringLiteral("too short for an archive"));

    // The end record sits at the very end, followed only by its own comment.
    const qint64 tailSize = std::min(fileSize, endOfDirectorySize + maxCommentSize);
    const Result<QByteArray> tail = readAt(fileSize - tailSize, tailSize);
    if (!tail.hasValue())
        return tail.error();
    const QByteArray &end = tail.value();
    qsizetype endAt = -1;
    for (qsizetype at = end.size() - endOfDirectorySize; at >= 0; --at) {
        if (get32(end, at) == endOfDirectorySignature &&
            at + endOfDirectorySize + get16(end, at + 20) == end.size()) {
            endAt = at;
            break;
        }
    }
    if (endAt < 0)
        return damaged(QStringLiteral("no end of the central directory"));
    const qint64 endOffset = fileSize - tailSize + endAt;
    if (endOffset >= zip64LocatorSize) {
        const Result<QByteArray> locator = readAt(endOffset - zip64LocatorSize, zip64LocatorSize);
        if (!locator.hasValue())
            return locator.error();
        if (get32(locator.value(), 0) == zip64LocatorSignature)
            return unsupported(QStringLiteral("ZIP64"));
    }

    const quint16 diskNumber = get16(end, endAt + 4);
    const quint16 directoryDisk = get16(end, endAt + 6);
    const quint16 entriesOnDisk = get16(end, endAt + 8);
    const quint16 totalEntries = get16(end, endAt + 10);
    const quint32 directorySize = get32(end, endAt + 12);
    const quint32 directoryOffset = get32(end, endAt + 16);
    if (totalEntries == zip64CountMarker || directorySize == zip64Marker ||
        directoryOffset == zip64Marker) {
        return unsupported(QStringLiteral("ZIP64"));
    }
    if (diskNumber != 0 || directoryDisk != 0 || entriesOnDisk != totalEntries)
        return unsupported(QStringLiteral("spread over several disks"));
    if (totalEntries > maxZipEntries) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("More than %1 entries").arg(maxZipEntries));
    }
    if (qint64(directoryOffset) + directorySize > endOffset)
        return damaged(QStringLiteral("the central directory overlaps its end"));

    const Result<QByteArray> read = readAt(directoryOffset, directorySize);
    if (!read.hasValue())
        return read.error();
    const QByteArray &directory = read.value();
    qsizetype at = 0;
    for (int index = 0; index < totalEntries; ++index) {
        if (at + centralHeaderSize > directory.size() ||
            get32(directory, at) != centralHeaderSignature) {
            return damaged(QStringLiteral("entry %1 of the central directory").arg(index));
        }
        const quint16 flags = get16(directory, at + 8);
        const quint16 method = get16(directory, at + 10);
        const quint32 crc = get32(directory, at + 16);
        const quint32 packedSize = get32(directory, at + 20);
        const quint32 size = get32(directory, at + 24);
        const quint16 nameLength = get16(directory, at + 28);
        const quint16 extraLength = get16(directory, at + 30);
        const quint16 commentLength = get16(directory, at + 32);
        const quint16 startDisk = get16(directory, at + 34);
        const quint32 headerOffset = get32(directory, at + 42);
        const qsizetype recordSize = centralHeaderSize + nameLength + extraLength + commentLength;
        if (at + recordSize > directory.size())
            return damaged(QStringLiteral("entry %1 of the central directory").arg(index));
        if (flags & (flagEncrypted | flagStrongEncryption | flagMaskedDirectory))
            return unsupported(QStringLiteral("encrypted entry %1").arg(index));
        if (method != methodStored)
            return unsupported(QStringLiteral("compression method %1").arg(method));
        if (packedSize == zip64Marker || size == zip64Marker || headerOffset == zip64Marker)
            return unsupported(QStringLiteral("ZIP64"));
        if (startDisk != 0)
            return unsupported(QStringLiteral("spread over several disks"));
        if (packedSize != size)
            return damaged(QStringLiteral("stored entry %1 with two sizes").arg(index));
        if (size > maxZipEntryBytes) {
            return Error(ErrorCode::LimitExceeded,
                         QStringLiteral("Entry of %1 bytes is too large").arg(size));
        }
        const QByteArray rawName = directory.mid(at + centralHeaderSize, nameLength);
        const std::optional<QString> decoded = decodeName(rawName, flags);
        if (!decoded)
            return damaged(QStringLiteral("name of entry %1 is not UTF-8").arg(index));
        at += recordSize;

        QString name = *decoded;
        const bool folder = name.endsWith(u'/');
        if (folder)
            name.chop(1);
        if (!isSafeEntryName(name)) {
            return Error(ErrorCode::InvalidInput,
                         QStringLiteral("Entry %1 has a name that is not allowed").arg(index));
        }

        // The local header repeats the name and may carry another extra field.
        const Result<QByteArray> local = readAt(headerOffset, localHeaderSize);
        if (!local.hasValue())
            return local.error();
        if (get32(local.value(), 0) != localHeaderSignature ||
            get16(local.value(), 26) != nameLength) {
            return damaged(QStringLiteral("local header of entry %1").arg(index));
        }
        const qint64 dataOffset =
            qint64(headerOffset) + localHeaderSize + nameLength + get16(local.value(), 28);
        if (dataOffset + qint64(size) > qint64(directoryOffset))
            return damaged(QStringLiteral("entry %1 is cut short").arg(index));
        const Result<QByteArray> localName = readAt(headerOffset + localHeaderSize, nameLength);
        if (!localName.hasValue())
            return localName.error();
        if (localName.value() != rawName)
            return damaged(QStringLiteral("local name of entry %1").arg(index));

        if (folder) {
            if (size != 0)
                return damaged(QStringLiteral("folder entry %1 with content").arg(index));
            continue;
        }
        if (m_index.contains(name))
            return Error(ErrorCode::InvalidInput,
                         QStringLiteral("Entry %1 repeats an earlier name").arg(index));
        m_index.insert(name, m_entries.size());
        m_entries.append({name, qint64(size), crc, dataOffset});
    }
    if (at != directory.size())
        return damaged(QStringLiteral("the central directory has bytes left over"));
    return {};
}

Result<QByteArray> ZipReader::read(const QString &name)
{
    const auto found = m_index.constFind(name);
    if (found == m_index.cend())
        return Error(ErrorCode::NotFound, QStringLiteral("No such entry in the archive"));
    const ZipEntryInfo &entry = m_entries.at(found.value());
    Result<QByteArray> data = readAt(entry.dataOffset, entry.size);
    if (!data.hasValue())
        return data;
    if (crc32(data.value()) != entry.crc)
        return damaged(QStringLiteral("CRC mismatch"));
    return data;
}

} // namespace qpm::core::backup
