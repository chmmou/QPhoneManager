#include "core/protocol/ObexPacket.h"

#include <QtEndian>

namespace qpm::core::protocol {

namespace {

constexpr qsizetype packetHeaderSize = 3;
constexpr qsizetype connectFieldsSize = 4;
constexpr qsizetype setPathFieldsSize = 2;
constexpr qsizetype headerPrefixSize = 3;
constexpr quint8 encodingMask = 0xC0;
constexpr quint8 encodingUnicode = 0x00;
constexpr quint8 encodingBytes = 0x40;
constexpr quint8 encodingOneByte = 0x80;
constexpr quint8 encodingFourBytes = 0xC0;

void appendUint16(QByteArray &out, quint16 value)
{
    out.append(char(value >> 8));
    out.append(char(value & 0xFF));
}

void appendUint32(QByteArray &out, quint32 value)
{
    char bytes[4];
    qToBigEndian(value, bytes);
    out.append(bytes, 4);
}

void appendUint64(QByteArray &out, quint64 value)
{
    char bytes[8];
    qToBigEndian(value, bytes);
    out.append(bytes, 8);
}

void appendSequence(QByteArray &out, quint8 id, const QByteArray &content)
{
    out.append(char(id));
    appendUint16(out, quint16(headerPrefixSize + content.size()));
    out.append(content);
}

QByteArray utf16BigEndian(const QString &text)
{
    QByteArray result;
    if (text.isEmpty())
        return result;
    result.reserve((text.size() + 1) * 2);
    for (const QChar c : text) {
        result.append(char(c.unicode() >> 8));
        result.append(char(c.unicode() & 0xFF));
    }
    result.append('\0');
    result.append('\0');
    return result;
}

QString fromUtf16BigEndian(const QByteArray &bytes)
{
    QString result;
    result.reserve(bytes.size() / 2);
    for (qsizetype i = 0; i + 1 < bytes.size(); i += 2) {
        const char16_t unit = char16_t((quint8(bytes.at(i)) << 8) | quint8(bytes.at(i + 1)));
        result.append(QChar(unit));
    }
    while (result.endsWith(QChar(u'\0')))
        result.chop(1);
    return result;
}

QByteArray withoutTrailingNul(QByteArray bytes)
{
    while (bytes.endsWith('\0'))
        bytes.chop(1);
    return bytes;
}

Error malformed(const QString &what)
{
    return Error(ErrorCode::ProtocolError, QStringLiteral("Malformed OBEX packet: %1").arg(what));
}

qsizetype headerStart(ObexPacket::Layout layout)
{
    switch (layout) {
    case ObexPacket::Layout::Connect:
        return packetHeaderSize + connectFieldsSize;
    case ObexPacket::Layout::SetPath:
        return packetHeaderSize + setPathFieldsSize;
    case ObexPacket::Layout::Plain:
        break;
    }
    return packetHeaderSize;
}

} // namespace

Result<QByteArray> ObexPacket::encode() const
{
    QByteArray out;
    out.append(char(code));
    appendUint16(out, 0);
    if (layout == Layout::Connect) {
        out.append(char(version));
        out.append(char(connectFlags));
        appendUint16(out, maxPacketLength);
    } else if (layout == Layout::SetPath) {
        out.append(char(setPathFlags));
        out.append(char(setPathConstants));
    }

    if (!target.isEmpty())
        appendSequence(out, obex::hiTarget, target);
    if (connectionId) {
        out.append(char(obex::hiConnectionId));
        appendUint32(out, *connectionId);
    }
    if (name)
        appendSequence(out, obex::hiName, utf16BigEndian(*name));
    if (targetName)
        appendSequence(out, obex::hiTargetName, utf16BigEndian(*targetName));
    if (!type.isEmpty())
        appendSequence(out, obex::hiType, type + '\0');
    if (length) {
        out.append(char(obex::hiLength));
        appendUint32(out, *length);
    }
    if (lengthBig) {
        out.append(char(obex::hiLengthBig));
        appendUint64(out, *lengthBig);
    }
    if (!time.isEmpty())
        appendSequence(out, obex::hiTime, time);
    if (appParameters)
        appendSequence(out, obex::hiAppParameters, *appParameters);
    if (!who.isEmpty())
        appendSequence(out, obex::hiWho, who);
    if (bodyKind != BodyKind::None)
        appendSequence(out, bodyKind == BodyKind::Body ? obex::hiBody : obex::hiEndOfBody, body);

    if (out.size() > obex::maxPacketSize) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("OBEX packet of %1 bytes exceeds %2 bytes")
                         .arg(out.size())
                         .arg(obex::maxPacketSize));
    }
    const quint16 size = quint16(out.size());
    out[1] = char(size >> 8);
    out[2] = char(size & 0xFF);
    return out;
}

Result<ObexPacket> ObexPacket::decode(const QByteArray &bytes, Layout layout)
{
    const std::optional<quint16> declared = declaredLength(bytes);
    if (!declared)
        return malformed(QStringLiteral("shorter than three bytes"));
    if (*declared != bytes.size()) {
        return malformed(
            QStringLiteral("declares %1 bytes but has %2").arg(*declared).arg(bytes.size()));
    }
    const qsizetype start = headerStart(layout);
    if (bytes.size() < start)
        return malformed(QStringLiteral("too short for its layout"));

    ObexPacket packet;
    packet.code = quint8(bytes.at(0));
    packet.layout = layout;
    if (layout == Layout::Connect) {
        packet.version = quint8(bytes.at(3));
        packet.connectFlags = quint8(bytes.at(4));
        packet.maxPacketLength = qFromBigEndian<quint16>(bytes.constData() + 5);
    } else if (layout == Layout::SetPath) {
        packet.setPathFlags = quint8(bytes.at(3));
        packet.setPathConstants = quint8(bytes.at(4));
    }

    qsizetype position = start;
    const qsizetype size = bytes.size();
    while (position < size) {
        const quint8 id = quint8(bytes.at(position));
        if (id == obex::hiLengthBig) {
            if (position + 9 > size)
                return malformed(QStringLiteral("truncated Length-Big header"));
            packet.lengthBig = qFromBigEndian<quint64>(bytes.constData() + position + 1);
            position += 9;
            continue;
        }
        switch (id & encodingMask) {
        case encodingFourBytes: {
            if (position + 5 > size)
                return malformed(QStringLiteral("truncated four-byte header 0x%1")
                                     .arg(uint(id), 2, 16, QLatin1Char('0')));
            const quint32 value = qFromBigEndian<quint32>(bytes.constData() + position + 1);
            if (id == obex::hiConnectionId)
                packet.connectionId = value;
            else if (id == obex::hiLength)
                packet.length = value;
            position += 5;
            break;
        }
        case encodingOneByte:
            if (position + 2 > size)
                return malformed(QStringLiteral("truncated one-byte header"));
            position += 2;
            break;
        case encodingUnicode:
        case encodingBytes: {
            if (position + headerPrefixSize > size)
                return malformed(
                    QStringLiteral("truncated header 0x%1").arg(uint(id), 2, 16, QLatin1Char('0')));
            const quint16 headerLength = qFromBigEndian<quint16>(bytes.constData() + position + 1);
            if (headerLength < headerPrefixSize || position + headerLength > size) {
                return malformed(QStringLiteral("header 0x%1 has an invalid length")
                                     .arg(uint(id), 2, 16, QLatin1Char('0')));
            }
            const QByteArray content =
                bytes.mid(position + headerPrefixSize, headerLength - headerPrefixSize);
            switch (id) {
            case obex::hiName:
            case obex::hiTargetName:
                if (content.size() % 2 != 0)
                    return malformed(QStringLiteral("name with an odd byte count"));
                if (id == obex::hiName)
                    packet.name = fromUtf16BigEndian(content);
                else
                    packet.targetName = fromUtf16BigEndian(content);
                break;
            case obex::hiType:
                packet.type = withoutTrailingNul(content);
                break;
            case obex::hiTime:
                packet.time = withoutTrailingNul(content);
                break;
            case obex::hiTarget:
                packet.target = content;
                break;
            case obex::hiWho:
                packet.who = content;
                break;
            case obex::hiAppParameters:
                packet.appParameters = content;
                break;
            case obex::hiBody:
            case obex::hiEndOfBody:
                packet.bodyKind = id == obex::hiBody ? BodyKind::Body : BodyKind::EndOfBody;
                packet.body.append(content);
                break;
            default:
                break;
            }
            position += headerLength;
            break;
        }
        }
    }
    return packet;
}

ObexPacket::Layout ObexPacket::requestLayout(quint8 opcode)
{
    if (opcode == obex::opConnect)
        return Layout::Connect;
    if (opcode == obex::opSetPath)
        return Layout::SetPath;
    return Layout::Plain;
}

std::optional<quint16> ObexPacket::declaredLength(const QByteArray &buffer)
{
    if (buffer.size() < packetHeaderSize)
        return std::nullopt;
    return quint16((quint8(buffer.at(1)) << 8) | quint8(buffer.at(2)));
}

} // namespace qpm::core::protocol
