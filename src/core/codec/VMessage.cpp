#include "core/codec/VMessage.h"

#include "core/codec/AtText.h"
#include "core/codec/PhoneTime.h"

#include <QTimeZone>

#include <limits>
#include <optional>

namespace qpm::core::codec {

namespace {

constexpr quint32 luidKindMask = 0x30000000;
constexpr qsizetype luidDigits = 12;
constexpr int maxSimSlot = 9;
constexpr qsizetype minNumberLength = 3;
constexpr qsizetype maxNumberLength = 32;

struct Property
{
    QByteArray name;
    QList<QByteArray> parameters;
    QByteArray value;

    [[nodiscard]] bool hasParameter(QByteArrayView parameter) const
    {
        for (const QByteArray &candidate : parameters) {
            if (candidate.compare(parameter, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    }

    /// The value of "KEY=value" among the parameters, or a null array.
    [[nodiscard]] QByteArray parameter(QByteArrayView key) const
    {
        for (const QByteArray &candidate : parameters) {
            const qsizetype equals = candidate.indexOf('=');
            if (equals > 0 && candidate.first(equals).compare(key, Qt::CaseInsensitive) == 0)
                return candidate.sliced(equals + 1);
        }
        return {};
    }

    [[nodiscard]] bool isQuotedPrintable() const
    {
        return hasParameter("ENCODING=QUOTED-PRINTABLE");
    }
};

Error invalid(const QString &what)
{
    return Error(ErrorCode::InvalidInput, what);
}

Error tooLarge(const QString &what)
{
    return Error(ErrorCode::LimitExceeded, what);
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

// The phone app replaces every byte below 0x20 and "=" with "=XX" and writes
// all other bytes of the UTF-8 text as they are.
QString decodeValue(const Property &property)
{
    if (!property.isQuotedPrintable())
        return QString::fromUtf8(property.value);
    const QByteArray &raw = property.value;
    QByteArray bytes;
    bytes.reserve(raw.size());
    for (qsizetype i = 0; i < raw.size(); ++i) {
        if (raw.at(i) == '=' && i + 2 < raw.size()) {
            const int high = hexValue(raw.at(i + 1));
            const int low = hexValue(raw.at(i + 2));
            if (high >= 0 && low >= 0) {
                bytes.append(char(high * 16 + low));
                i += 2;
                continue;
            }
        }
        bytes.append(raw.at(i));
    }
    return QString::fromUtf8(bytes);
}

// Parameters such as NAME and TYPE use the escaping of AT values.
QString decodeParameter(const QByteArray &value)
{
    return decodeQp(QString::fromUtf8(value));
}

std::optional<Property> splitProperty(const QByteArray &line)
{
    const qsizetype colon = line.indexOf(':');
    if (colon <= 0)
        return std::nullopt;
    QList<QByteArray> head = line.first(colon).split(';');
    Property property;
    property.name = head.takeFirst().trimmed().toUpper();
    property.parameters = head;
    property.value = line.sliced(colon + 1);
    return property;
}

std::optional<quint32> parseLuid(const QByteArray &text)
{
    const QByteArray digits = text.trimmed();
    if (digits.isEmpty() || digits.size() > luidDigits)
        return std::nullopt;
    bool ok = false;
    const qulonglong value = digits.toULongLong(&ok, 16);
    if (!ok || value > std::numeric_limits<quint32>::max())
        return std::nullopt;
    return quint32(value);
}

std::optional<QDateTime> parseStamp(QStringView text)
{
    if (const std::optional<QDateTime> utc = parseUtcStamp(text))
        return utc;
    if (const std::optional<QDateTime> local = parseLocalStamp(text))
        return local->toUTC();
    if (const std::optional<QDate> day = parseDate(text))
        return QDateTime(*day, QTime(0, 0), QTimeZone::UTC);
    return std::nullopt;
}

MessageBox boxFromText(const QString &text)
{
    const QString upper = text.trimmed().toUpper();
    if (upper == QLatin1StringView("INBOX"))
        return MessageBox::Inbox;
    if (upper == QLatin1StringView("SENT"))
        return MessageBox::Sent;
    if (upper == QLatin1StringView("DRAFT"))
        return MessageBox::Draft;
    if (upper == QLatin1StringView("OUTBOX"))
        return MessageBox::Outbox;
    if (upper == QLatin1StringView("FAILED"))
        return MessageBox::Failed;
    if (upper == QLatin1StringView("QUEUED"))
        return MessageBox::Queued;
    // The phone app spells it this way.
    if (upper == QLatin1StringView("SHEDULED"))
        return MessageBox::Scheduled;
    return MessageBox::Other;
}

// Splits at semicolons that are not escaped with a backslash and removes the
// escapes.
QStringList splitEscaped(const QString &text)
{
    QStringList fields;
    QString current;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\\' && i + 1 < text.size() && text.at(i + 1) == u';') {
            current.append(u';');
            ++i;
        } else if (c == u';') {
            fields.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    fields.append(current);
    return fields;
}

// "<name>;<number>;<SHEDULED|SENT|FAILED>;<date or empty>;<error>"
MessageRecipient parseRecipient(const QString &value)
{
    const QStringList fields = splitEscaped(value);
    MessageRecipient recipient;
    recipient.name = fields.value(0).trimmed();
    recipient.number = fields.value(1).trimmed();
    const QString status = fields.value(2).trimmed().toUpper();
    if (status == QLatin1StringView("SENT"))
        recipient.status = MessageRecipient::Status::Sent;
    else if (status == QLatin1StringView("FAILED"))
        recipient.status = MessageRecipient::Status::Failed;
    if (const std::optional<QDateTime> sent = parseStamp(fields.value(3).trimmed()))
        recipient.sentAt = *sent;
    recipient.error = fields.value(4).trimmed();
    return recipient;
}

Result<void> addAttachment(Message &message, const Property &property, QStringList &texts)
{
    const QString mimeType = decodeParameter(property.parameter("TYPE")).trimmed();
    const QString name = decodeParameter(property.parameter("NAME")).trimmed();
    const QByteArray encoding = property.parameter("ENCODING").trimmed().toUpper();
    const QByteArray uri = property.parameter("URI");

    MessageAttachment attachment;
    attachment.name = name;
    attachment.mimeType = mimeType;
    if (!uri.isNull()) {
        bool ok = false;
        attachment.size = property.parameter("SIZE").trimmed().toLongLong(&ok);
        if (!ok || attachment.size < 0)
            attachment.size = 0;
        attachment.onPhoneOnly = true;
    } else if (encoding == "BASE64") {
        const QByteArray encoded = property.value.trimmed();
        if (encoded.size() / 4 * 3 > maxAttachmentBytes)
            return tooLarge(
                QStringLiteral("Message part larger than %1 bytes").arg(maxAttachmentBytes));
        const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(
            encoded, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return invalid(QStringLiteral("Message part is not valid Base64"));
        attachment.data = *decoded;
        attachment.size = attachment.data.size();
    } else {
        const QString text = decodeValue(property);
        if (text.size() > maxMessageTextLength)
            return tooLarge(
                QStringLiteral("Message text longer than %1").arg(maxMessageTextLength));
        if (mimeType.compare(QLatin1StringView("text/plain"), Qt::CaseInsensitive) == 0) {
            texts.append(text);
            return {};
        }
        attachment.data = text.toUtf8();
        attachment.size = attachment.data.size();
    }
    if (message.attachments.size() >= maxMessageAttachments)
        return tooLarge(QStringLiteral("More than %1 message parts").arg(maxMessageAttachments));
    message.attachments.append(attachment);
    return {};
}

// Like the phone app: only bytes below 0x20 and "=" are escaped, and the two
// parameters are added when needed, CHARSET also on request.
QByteArray encodeLine(QByteArrayView head, const QString &value, bool alwaysCharset)
{
    const QByteArray bytes = value.toUtf8();
    QByteArray out;
    out.reserve(bytes.size());
    bool escaped = false;
    bool high = false;
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || c == '=') {
            out.append('=');
            out.append(QByteArray::number(byte, 16).rightJustified(2, '0').toUpper());
            escaped = true;
        } else {
            out.append(c);
        }
        if (byte >= 0x80)
            high = true;
    }
    QByteArray line(head);
    if (escaped)
        line.append(";ENCODING=QUOTED-PRINTABLE");
    if (high || alwaysCharset)
        line.append(";CHARSET=UTF-8");
    return line + ':' + out + "\r\n";
}

bool isSendableNumber(const QString &number)
{
    if (number.size() < minNumberLength || number.size() > maxNumberLength)
        return false;
    for (qsizetype i = 0; i < number.size(); ++i) {
        const QChar c = number.at(i);
        const bool digit = c >= u'0' && c <= u'9';
        if (!digit && c != u'*' && c != u'#' && !(c == u'+' && i == 0))
            return false;
    }
    return true;
}

} // namespace

MessageKind kindOfLuid(quint32 luid)
{
    switch (luid & luidKindMask) {
    case mmsLuidBit:
        return MessageKind::Mms;
    case storeLuidBit:
        return MessageKind::Scheduled;
    default:
        break;
    }
    return MessageKind::Sms;
}

Result<Message> parseVMessage(const QByteArray &data)
{
    if (data.size() > maxMessageBytes)
        return tooLarge(QStringLiteral("Message larger than %1 bytes").arg(maxMessageBytes));

    const QList<QByteArray> rawLines = data.split('\n');
    if (rawLines.size() > maxMessageLines)
        return tooLarge(QStringLiteral("Message with more than %1 lines").arg(maxMessageLines));

    Message message;
    QStringList texts;
    QString typeText;
    bool begun = false;
    bool ended = false;
    for (qsizetype i = 0; i < rawLines.size() && !ended; ++i) {
        QByteArray line = rawLines.at(i);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.trimmed().isEmpty())
            continue;
        if (!begun) {
            if (line.trimmed().toUpper() == "BEGIN:VMESSAGE")
                begun = true;
            continue;
        }
        if (line.trimmed().toUpper() == "END:VMESSAGE") {
            ended = true;
            continue;
        }
        std::optional<Property> property = splitProperty(line);
        if (!property)
            continue;
        // A soft line break of quoted-printable carries the value on.
        while (property->isQuotedPrintable() && property->value.endsWith('=') &&
               i + 1 < rawLines.size()) {
            property->value.chop(1);
            QByteArray next = rawLines.at(++i);
            if (next.endsWith('\r'))
                next.chop(1);
            property->value.append(next);
        }

        const QByteArray &name = property->name;
        if (name == "TYP") {
            typeText = QString::fromUtf8(property->value).trimmed().toUpper();
        } else if (name == "BOX") {
            message.box = boxFromText(QString::fromUtf8(property->value));
        } else if (name == "READ") {
            message.read = property->value.trimmed() != "0";
        } else if (name == "SIM") {
            bool ok = false;
            const int slot = property->value.trimmed().toInt(&ok);
            message.simSlot = ok && slot >= 0 && slot <= maxSimSlot ? slot : -1;
        } else if (name == "DATE") {
            if (const std::optional<QDateTime> date =
                    parseStamp(QString::fromLatin1(property->value.trimmed())))
                message.date = *date;
        } else if (name == "NUMBER") {
            const QString address = decodeValue(*property).trimmed();
            if (address.size() > maxMessageAddressLength)
                return tooLarge(
                    QStringLiteral("Address longer than %1").arg(maxMessageAddressLength));
            if (!address.isEmpty())
                message.addresses.append(address);
        } else if (name == "SUBJECT") {
            message.subject = decodeValue(*property);
            if (message.subject.size() > maxMessageTextLength)
                return tooLarge(QStringLiteral("Subject too long"));
        } else if (name == "BODY") {
            const QString body = decodeValue(*property);
            if (body.size() > maxMessageTextLength)
                return tooLarge(
                    QStringLiteral("Message text longer than %1").arg(maxMessageTextLength));
            texts.append(body);
        } else if (name == "ATT") {
            if (const Result<void> added = addAttachment(message, *property, texts);
                !added.hasValue())
                return added.error();
        } else if (name == "RECIPIENT") {
            if (message.recipients.size() >= maxMessageAddresses)
                return tooLarge(QStringLiteral("More than %1 recipients").arg(maxMessageAddresses));
            const MessageRecipient recipient = parseRecipient(decodeValue(*property));
            if (recipient.number.size() > maxMessageAddressLength ||
                recipient.name.size() > maxMessageAddressLength)
                return tooLarge(
                    QStringLiteral("Recipient longer than %1").arg(maxMessageAddressLength));
            message.recipients.append(recipient);
        } else if (name == "X-IRMC-LUID") {
            const std::optional<quint32> luid = parseLuid(property->value);
            if (!luid)
                return invalid(QStringLiteral("Message with an unreadable LUID"));
            message.luid = *luid;
        }
    }
    if (!begun || !ended)
        return invalid(QStringLiteral("No complete vMessage"));

    QString joined;
    for (const QString &text : std::as_const(texts)) {
        if (text.isEmpty())
            continue;
        if (!joined.isEmpty())
            joined.append(u'\n');
        joined.append(text);
        if (joined.size() > maxMessageTextLength)
            return tooLarge(
                QStringLiteral("Message text longer than %1").arg(maxMessageTextLength));
    }
    message.body = joined;

    const QStringList typeParts = typeText.split(u';');
    const bool typeMms = typeParts.value(0) == QLatin1StringView("MMS");
    if (!typeText.isEmpty())
        message.incoming = typeParts.value(1) == QLatin1StringView("IN");
    else
        message.incoming = message.box == MessageBox::Inbox;

    if (message.box == MessageBox::Scheduled) {
        message.kind = MessageKind::Scheduled;
        message.incoming = false;
        message.addresses.clear();
        for (const MessageRecipient &recipient : std::as_const(message.recipients)) {
            if (!recipient.number.isEmpty())
                message.addresses.append(recipient.number);
        }
    } else if (typeMms || kindOfLuid(message.luid) == MessageKind::Mms) {
        message.kind = MessageKind::Mms;
    } else {
        message.kind = MessageKind::Sms;
    }
    if (!message.incoming)
        message.read = true;
    if (message.addresses.size() > maxMessageAddresses)
        return tooLarge(QStringLiteral("More than %1 addresses").arg(maxMessageAddresses));
    return message;
}

Result<QByteArray> encodeScheduledMessage(const Message &message)
{
    if (message.recipients.isEmpty())
        return invalid(QStringLiteral("A scheduled message needs a recipient"));
    if (message.recipients.size() > maxScheduledRecipients)
        return tooLarge(QStringLiteral("More than %1 recipients").arg(maxScheduledRecipients));
    if (message.body.trimmed().isEmpty())
        return invalid(QStringLiteral("A scheduled message needs a text"));
    if (message.body.size() > maxScheduledTextLength)
        return tooLarge(QStringLiteral("Text longer than %1").arg(maxScheduledTextLength));
    if (!message.date.isValid())
        return invalid(QStringLiteral("A scheduled message needs a time"));
    if (message.simSlot < -1 || message.simSlot > maxSimSlot)
        return invalid(QStringLiteral("SIM index %1 out of range").arg(message.simSlot));

    QByteArray out = "BEGIN:VMESSAGE\r\nTYP:SMS;OUT\r\nBOX:SHEDULED\r\n";
    if (message.simSlot >= 0)
        out += "SIM:" + QByteArray::number(message.simSlot) + "\r\n";
    for (const MessageRecipient &recipient : message.recipients) {
        if (!isSendableNumber(recipient.number))
            return invalid(QStringLiteral("A recipient has no number the phone can send to"));
        QString name = recipient.name.trimmed();
        name.replace(u';', u',');
        // The phone app reads name, number, the time sent and the state as a digit.
        out +=
            encodeLine("RECIPIENT", name + u';' + recipient.number + QStringLiteral(";;0"), true);
    }
    out += "DATE:" + formatUtcStamp(message.date).toLatin1() + "\r\n";
    out += encodeLine("BODY", message.body, true);
    out += "END:VMESSAGE\r\n";
    return out;
}

} // namespace qpm::core::codec
