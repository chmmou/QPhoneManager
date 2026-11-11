#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace qpm::core::codec {

/// What a message is: a text message, a multimedia message or one planned for later.
enum class MessageKind { Sms, Mms, Scheduled };

/// The folder the phone files a message in.
enum class MessageBox { Inbox, Sent, Draft, Outbox, Failed, Queued, Scheduled, Other };

/// One part of a multimedia message other than its text.
struct MessageAttachment
{
    /// As the phone names it; untrusted, never a path to write to as it is.
    QString name;
    QString mimeType;
    /// Size in bytes as the phone reports it.
    qint64 size = 0;
    /// The content; empty when the phone kept it, see onPhoneOnly.
    QByteArray data;
    /// The phone sent only a reference, for parts of 10 MiB and more.
    bool onPhoneOnly = false;

    bool operator==(const MessageAttachment &other) const = default;
};

/// One recipient of a scheduled message.
struct MessageRecipient
{
    enum class Status { Scheduled, Sent, Failed };

    QString name;
    QString number;
    Status status = Status::Scheduled;
    /// UTC; invalid while not sent.
    QDateTime sentAt;
    /// The phone's reason for a failure, for example "NO_SERVICE"; empty otherwise.
    QString error;

    bool operator==(const MessageRecipient &other) const = default;
};

/// One message as the phone keeps it.
struct Message
{
    /// X-IRMC-LUID: the SMS id, the MMS id with bit 0x10000000, or a store item
    /// of the phone app with bit 0x20000000. 0 for a message not on the phone.
    quint32 luid = 0;
    MessageKind kind = MessageKind::Sms;
    MessageBox box = MessageBox::Inbox;
    /// Received rather than sent.
    bool incoming = true;
    bool read = true;
    /// UTC; for a scheduled message the time it is sent. Invalid when the phone sent none.
    QDateTime date;
    /// The other side: the sender of a received message, the recipients of a sent one.
    QStringList addresses;
    QString subject;
    QString body;
    QList<MessageAttachment> attachments;
    /// Index of the SIM card; -1 when the phone does not say.
    int simSlot = -1;
    /// Scheduled messages only.
    QList<MessageRecipient> recipients;

    bool operator==(const Message &other) const = default;
};

/// LUID bits the phone app uses to tell its kinds of message apart.
inline constexpr quint32 mmsLuidBit = 0x10000000;
inline constexpr quint32 storeLuidBit = 0x20000000;

/// Bounds for one message from the phone.
inline constexpr qsizetype maxMessageBytes = 48 * 1024 * 1024;
inline constexpr qsizetype maxMessageLines = 10000;
inline constexpr qsizetype maxMessageAttachments = 50;
inline constexpr qsizetype maxAttachmentBytes = 16 * 1024 * 1024;
inline constexpr qsizetype maxMessageTextLength = 4 * 1024 * 1024;
inline constexpr qsizetype maxMessageAddresses = 100;
inline constexpr qsizetype maxMessageAddressLength = 256;
/// Bounds for a scheduled message written to the phone.
inline constexpr qsizetype maxScheduledRecipients = 100;
inline constexpr qsizetype maxScheduledTextLength = 10000;

/**
 * Reads one vMessage in the form the phone app sends: a text message, a
 * multimedia message with its parts, or a scheduled message with its
 * recipients. Values may be quoted-printable with UTF-8 bytes; attachments
 * come in Base64 or as a reference to a part left on the phone. The text parts
 * of a multimedia message make up its body; the SMIL layout part is dropped.
 *
 * The kind comes from the TYP line and the LUID; a store item of the phone
 * app that is not scheduled is a sent text message. Lines it does not know
 * are passed over. Fails with ErrorCode::InvalidInput without BEGIN and END
 * lines, for an unreadable LUID and for Base64 that cannot be read, and with
 * ErrorCode::LimitExceeded above the limits of this file.
 */
[[nodiscard]] Result<Message> parseVMessage(const QByteArray &data);

/**
 * Writes a scheduled message for the phone app: BOX:SHEDULED, the
 * recipients in the form the app reads, the time to send in UTC, the SIM
 * card when chosen and the text. Uses `recipients`, `body`, `date` and
 * `simSlot` of `message`. A semicolon in a recipient's name becomes a comma.
 *
 * Fails with ErrorCode::InvalidInput without a recipient, a recipient without
 * number or with a number that holds anything but digits, "+", "*" and "#",
 * an empty text, an invalid time or a SIM index above 9, and with
 * ErrorCode::LimitExceeded above maxScheduledRecipients or
 * maxScheduledTextLength.
 */
[[nodiscard]] Result<QByteArray> encodeScheduledMessage(const Message &message);

/// The kind the phone app means with `luid`, before the content is known.
[[nodiscard]] MessageKind kindOfLuid(quint32 luid);

} // namespace qpm::core::codec
