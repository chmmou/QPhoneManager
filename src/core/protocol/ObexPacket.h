#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace qpm::core::protocol {

namespace obex {

inline constexpr quint8 opPut = 0x02;
inline constexpr quint8 opConnect = 0x80;
inline constexpr quint8 opDisconnect = 0x81;
inline constexpr quint8 opPutFinal = 0x82;
inline constexpr quint8 opGetFinal = 0x83;
inline constexpr quint8 opMove = 0x84;
inline constexpr quint8 opSetPath = 0x85;
/// Phone-specific: upload whose data goes over a separate TCP connection.
inline constexpr quint8 opSocketPut = 0x87;
/// Phone-specific: download whose data comes over a separate TCP connection.
inline constexpr quint8 opSocketGet = 0x88;
/// Phone-specific: waits for the separate TCP transfer to end.
inline constexpr quint8 opSocketStatus = 0x89;
inline constexpr quint8 opAbort = 0xFF;

inline constexpr quint8 rspContinue = 0x90;
inline constexpr quint8 rspOk = 0xA0;
inline constexpr quint8 rspCreated = 0xA1;
inline constexpr quint8 rspBadRequest = 0xC0;
inline constexpr quint8 rspUnauthorized = 0xC1;
inline constexpr quint8 rspForbidden = 0xC3;
inline constexpr quint8 rspNotFound = 0xC4;
inline constexpr quint8 rspNotAcceptable = 0xC6;
inline constexpr quint8 rspRequestTimeout = 0xC8;
inline constexpr quint8 rspConflict = 0xC9;
inline constexpr quint8 rspPreconditionFailed = 0xCC;
inline constexpr quint8 rspInternalServerError = 0xD0;
inline constexpr quint8 rspNotImplemented = 0xD1;
inline constexpr quint8 rspDatabaseFull = 0xE0;
inline constexpr quint8 rspDatabaseLocked = 0xE1;

inline constexpr quint8 hiName = 0x01;
/// Phone-specific: destination name of a move.
inline constexpr quint8 hiTargetName = 0x30;
/// Phone-specific: 64-bit length, written as the id followed by 8 bytes
/// without a length field.
inline constexpr quint8 hiLengthBig = 0x33;
inline constexpr quint8 hiType = 0x42;
inline constexpr quint8 hiTime = 0x44;
inline constexpr quint8 hiTarget = 0x46;
inline constexpr quint8 hiBody = 0x48;
inline constexpr quint8 hiEndOfBody = 0x49;
inline constexpr quint8 hiWho = 0x4A;
inline constexpr quint8 hiAppParameters = 0x4C;
inline constexpr quint8 hiLength = 0xC3;
inline constexpr quint8 hiConnectionId = 0xCB;

/// Largest OBEX packet, including the three-byte packet header.
inline constexpr qsizetype maxPacketSize = 0xFFFF;

/// The header-less GET that asks for the next body chunk, and the answer to
/// an unsolicited keep-alive.
inline constexpr char continueRequest[] = "\x83\x00\x03";
inline constexpr qsizetype continueRequestSize = 3;

} // namespace obex

/**
 * One OBEX packet as the phone client reads and writes it: a request or a
 * response, with the headers this protocol uses. A header is written only when
 * it is set. Names are UTF-16BE with a terminating NUL, Type is ASCII with a
 * NUL, Time, Target, Who and App-Parameters are raw bytes.
 */
struct ObexPacket
{
    /// Where the headers start: after the CONNECT fields, after the SETPATH
    /// flags, or right after the packet length.
    enum class Layout { Plain, Connect, SetPath };
    enum class BodyKind { None, Body, EndOfBody };

    /// Opcode of a request or response code of a response.
    quint8 code = 0;
    Layout layout = Layout::Plain;

    /// CONNECT request and its response.
    quint8 version = 0x10;
    quint8 connectFlags = 0;
    quint16 maxPacketLength = 0xFFFF;

    /// SETPATH request.
    quint8 setPathFlags = 0;
    quint8 setPathConstants = 0;

    std::optional<quint32> connectionId;
    /// An empty name is written as a header without content, which SETPATH
    /// uses for the root folder.
    std::optional<QString> name;
    std::optional<QString> targetName;
    QByteArray type;
    QByteArray time;
    QByteArray target;
    QByteArray who;
    std::optional<QByteArray> appParameters;
    std::optional<quint32> length;
    std::optional<quint64> lengthBig;
    BodyKind bodyKind = BodyKind::None;
    QByteArray body;

    /**
     * Encodes the packet in the header order the phone client expects:
     * Target, Connection-ID, Name, TargetName, Type, Length, Length-Big, Time,
     * App-Parameters, Who, then Body or End-of-Body last. Fails with
     * ErrorCode::LimitExceeded when the packet would exceed 65535 bytes.
     */
    [[nodiscard]] Result<QByteArray> encode() const;

    /**
     * Decodes one complete packet. `bytes` must hold exactly the length that
     * the packet declares. Unknown headers are skipped. Fails with
     * ErrorCode::ProtocolError for a malformed packet.
     */
    [[nodiscard]] static Result<ObexPacket> decode(const QByteArray &bytes, Layout layout);

    /// Returns the layout of a request with the given opcode.
    [[nodiscard]] static Layout requestLayout(quint8 opcode);

    /**
     * Returns the length that a packet starting at the front of `buffer`
     * declares, or nothing while fewer than three bytes are available.
     */
    [[nodiscard]] static std::optional<quint16> declaredLength(const QByteArray &buffer);
};

} // namespace qpm::core::protocol
