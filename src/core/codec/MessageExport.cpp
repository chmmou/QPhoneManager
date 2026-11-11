#include "core/codec/MessageExport.h"

#include "core/codec/CsvTable.h"

#include <QHash>
#include <QStringList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace qpm::core::codec {

namespace {

constexpr int archiveVersion = 1;

QString boxName(MessageBox box)
{
    switch (box) {
    case MessageBox::Inbox:
        return QStringLiteral("inbox");
    case MessageBox::Sent:
        return QStringLiteral("sent");
    case MessageBox::Draft:
        return QStringLiteral("draft");
    case MessageBox::Outbox:
        return QStringLiteral("outbox");
    case MessageBox::Failed:
        return QStringLiteral("failed");
    case MessageBox::Queued:
        return QStringLiteral("queued");
    case MessageBox::Scheduled:
        return QStringLiteral("scheduled");
    case MessageBox::Other:
        break;
    }
    return QStringLiteral("other");
}

std::optional<MessageBox> boxFromName(QStringView name)
{
    static const QHash<QString, MessageBox> boxes = {
        {QStringLiteral("inbox"), MessageBox::Inbox},
        {QStringLiteral("sent"), MessageBox::Sent},
        {QStringLiteral("draft"), MessageBox::Draft},
        {QStringLiteral("outbox"), MessageBox::Outbox},
        {QStringLiteral("failed"), MessageBox::Failed},
        {QStringLiteral("queued"), MessageBox::Queued},
        {QStringLiteral("other"), MessageBox::Other},
    };
    const auto found = boxes.constFind(name.toString());
    if (found == boxes.cend())
        return std::nullopt;
    return found.value();
}

QString boxText(MessageBox box, CsvLanguage language)
{
    const bool german = language == CsvLanguage::German;
    switch (box) {
    case MessageBox::Inbox:
        return german ? QStringLiteral("Eingang") : QStringLiteral("Inbox");
    case MessageBox::Sent:
        return german ? QStringLiteral("Gesendet") : QStringLiteral("Sent");
    case MessageBox::Draft:
        return german ? QStringLiteral("Entwurf") : QStringLiteral("Draft");
    case MessageBox::Outbox:
        return german ? QStringLiteral("Postausgang") : QStringLiteral("Outbox");
    case MessageBox::Failed:
        return german ? QStringLiteral("Fehlgeschlagen") : QStringLiteral("Failed");
    case MessageBox::Queued:
        return german ? QStringLiteral("Wartet") : QStringLiteral("Queued");
    case MessageBox::Scheduled:
        return german ? QStringLiteral("Geplant") : QStringLiteral("Scheduled");
    case MessageBox::Other:
        break;
    }
    return german ? QStringLiteral("Sonstige") : QStringLiteral("Other");
}

QString directionText(const Message &message, CsvLanguage language)
{
    const bool german = language == CsvLanguage::German;
    if (message.incoming)
        return german ? QStringLiteral("Empfangen") : QStringLiteral("Received");
    return german ? QStringLiteral("Gesendet") : QStringLiteral("Sent");
}

QString localTimeText(const QDateTime &date)
{
    return date.isValid() ? date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                          : QString();
}

QStringList attachmentNames(const Message &message)
{
    QStringList names;
    for (const MessageAttachment &attachment : message.attachments)
        names.append(attachment.name.isEmpty() ? attachment.mimeType : attachment.name);
    return names;
}

// XML 1.0 allows tab, line feed, carriage return and everything from U+0020
// on except the surrogates out of pairs and U+FFFE, U+FFFF.
QString xmlSafe(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            out.append(c);
            out.append(text.at(++i));
            continue;
        }
        const char16_t code = c.unicode();
        const bool allowed = code == u'\t' || code == u'\n' || code == u'\r' ||
                             (code >= 0x20 && !c.isSurrogate() && code != 0xFFFE && code != 0xFFFF);
        out.append(allowed ? c : QChar(QChar::ReplacementCharacter));
    }
    return out;
}

QString htmlText(const QString &text)
{
    QString escaped = text.toHtmlEscaped();
    escaped.replace(u'\'', QLatin1StringView("&#39;"));
    escaped.replace(QLatin1StringView("\r\n"), QLatin1StringView("\n"));
    escaped.replace(u'\n', QLatin1StringView("<br>"));
    return escaped;
}

Error invalidArchive(const QString &what)
{
    return Error(ErrorCode::InvalidInput, what);
}

} // namespace

QByteArray writeMessagesCsv(const QList<ExportedMessage> &messages, CsvLanguage language)
{
    const bool german = language == CsvLanguage::German;
    CsvRows rows;
    rows.reserve(messages.size() + 1);
    rows.append(german ? QStringList{QStringLiteral("Zeit"), QStringLiteral("Richtung"),
                                     QStringLiteral("Ordner"), QStringLiteral("Kontakt"),
                                     QStringLiteral("Nummer"), QStringLiteral("Betreff"),
                                     QStringLiteral("Text"), QStringLiteral("Anhänge"),
                                     QStringLiteral("SIM"), QStringLiteral("Archiviert")}
                       : QStringList{QStringLiteral("Time"), QStringLiteral("Direction"),
                                     QStringLiteral("Folder"), QStringLiteral("Contact"),
                                     QStringLiteral("Number"), QStringLiteral("Subject"),
                                     QStringLiteral("Text"), QStringLiteral("Attachments"),
                                     QStringLiteral("SIM"), QStringLiteral("Archived")});
    const QString yes = german ? QStringLiteral("ja") : QStringLiteral("yes");
    const QString no = german ? QStringLiteral("nein") : QStringLiteral("no");
    for (const ExportedMessage &exported : messages) {
        const Message &message = exported.message;
        rows.append({localTimeText(message.date), directionText(message, language),
                     boxText(message.box, language), exported.contactName,
                     message.addresses.join(QLatin1StringView(", ")), message.subject, message.body,
                     attachmentNames(message).join(QLatin1StringView(", ")),
                     message.simSlot >= 0 ? QString::number(message.simSlot + 1) : QString(),
                     exported.archived ? yes : no});
    }
    return writeCsv(rows);
}

QByteArray writeMessagesHtml(const QList<ExportedMessage> &messages, const QString &title,
                             CsvLanguage language)
{
    const bool german = language == CsvLanguage::German;
    QString page;
    page +=
        QStringLiteral("<!DOCTYPE html>\n<html lang=\"%1\">\n<head>\n<meta charset=\"utf-8\">\n")
            .arg(german ? QStringLiteral("de") : QStringLiteral("en"));
    // No script, no font, no picture, no request at all: only the style below.
    page += QStringLiteral("<meta http-equiv=\"Content-Security-Policy\" content=\"default-src "
                           "'none'; style-src 'unsafe-inline'\">\n");
    page += QStringLiteral("<title>") + htmlText(title) + QStringLiteral("</title>\n");
    page += QStringLiteral(
        "<style>\nbody{font-family:system-ui,sans-serif;max-width:48em;margin:2em auto;"
        "padding:0 1em;color:#171D1C;background:#F7FAF9}\nh2{margin-top:2em;color:#006A6A}\n"
        ".msg{margin:.5em 0;padding:.6em .9em;border-radius:12px;max-width:80%}\n"
        ".in{background:#DAE5E3}\n.out{background:#CDE8E7;margin-left:auto}\n"
        ".meta{font-size:.8em;color:#3F4947}\n.text{white-space:normal;overflow-wrap:anywhere}\n"
        ".parts{font-size:.8em;margin-top:.3em}\n</style>\n</head>\n<body>\n");
    page += QStringLiteral("<h1>") + htmlText(title) + QStringLiteral("</h1>\n");

    QString lastSide;
    bool first = true;
    for (const ExportedMessage &exported : messages) {
        const Message &message = exported.message;
        const QString number = message.addresses.join(QLatin1StringView(", "));
        const QString side = exported.contactName.isEmpty() ? number : exported.contactName;
        if (first || side != lastSide) {
            page += QStringLiteral("<h2>") + htmlText(side.isEmpty() ? QStringLiteral("?") : side);
            if (!exported.contactName.isEmpty() && !number.isEmpty())
                page += QStringLiteral(" (") + htmlText(number) + u')';
            page += QStringLiteral("</h2>\n");
            lastSide = side;
            first = false;
        }
        page += QStringLiteral("<div class=\"msg %1\">\n<div class=\"meta\">")
                    .arg(message.incoming ? QStringLiteral("in") : QStringLiteral("out"));
        page += htmlText(localTimeText(message.date)) + QStringLiteral(", ") +
                htmlText(directionText(message, language));
        if (exported.archived)
            page += QStringLiteral(", ") +
                    (german ? QStringLiteral("archiviert") : QStringLiteral("archived"));
        page += QStringLiteral("</div>\n");
        if (!message.subject.isEmpty())
            page += QStringLiteral("<div class=\"text\"><b>") + htmlText(message.subject) +
                    QStringLiteral("</b></div>\n");
        page += QStringLiteral("<div class=\"text\">") + htmlText(message.body) +
                QStringLiteral("</div>\n");
        const QStringList parts = attachmentNames(message);
        if (!parts.isEmpty()) {
            page +=
                QStringLiteral("<div class=\"parts\">") +
                htmlText((german ? QStringLiteral("Anhänge: ") : QStringLiteral("Attachments: ")) +
                         parts.join(QLatin1StringView(", "))) +
                QStringLiteral("</div>\n");
        }
        page += QStringLiteral("</div>\n");
    }
    page += QStringLiteral("</body>\n</html>\n");
    return page.toUtf8();
}

QByteArray writeMessagesXml(const QList<ExportedMessage> &messages)
{
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("messages"));
    writer.writeAttribute(QStringLiteral("version"), QString::number(archiveVersion));
    for (const ExportedMessage &exported : messages) {
        const Message &message = exported.message;
        if (message.kind == MessageKind::Scheduled)
            continue;
        writer.writeStartElement(QStringLiteral("message"));
        writer.writeAttribute(QStringLiteral("kind"), message.kind == MessageKind::Mms
                                                          ? QStringLiteral("mms")
                                                          : QStringLiteral("sms"));
        writer.writeAttribute(QStringLiteral("box"), boxName(message.box));
        writer.writeAttribute(QStringLiteral("read"),
                              message.read ? QStringLiteral("1") : QStringLiteral("0"));
        if (message.date.isValid())
            writer.writeAttribute(QStringLiteral("date"),
                                  message.date.toUTC().toString(Qt::ISODate));
        writer.writeAttribute(QStringLiteral("sim"), QString::number(message.simSlot));
        for (const QString &address : message.addresses)
            writer.writeTextElement(QStringLiteral("address"), xmlSafe(address));
        writer.writeTextElement(QStringLiteral("subject"), xmlSafe(message.subject));
        writer.writeTextElement(QStringLiteral("body"), xmlSafe(message.body));
        for (const MessageAttachment &attachment : message.attachments) {
            writer.writeStartElement(QStringLiteral("attachment"));
            writer.writeAttribute(QStringLiteral("name"), xmlSafe(attachment.name));
            writer.writeAttribute(QStringLiteral("mime"), xmlSafe(attachment.mimeType));
            writer.writeAttribute(QStringLiteral("size"), QString::number(attachment.size));
            if (attachment.onPhoneOnly || attachment.data.isEmpty())
                writer.writeAttribute(QStringLiteral("onPhone"), QStringLiteral("1"));
            else
                writer.writeCharacters(QString::fromLatin1(attachment.data.toBase64()));
            writer.writeEndElement();
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndDocument();
    return data;
}

Result<QList<Message>> readMessagesXml(const QByteArray &data)
{
    if (data.size() > maxArchiveBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("Archive larger than %1 bytes").arg(maxArchiveBytes));
    }
    QXmlStreamReader reader(data);
    QList<Message> messages;
    bool rootSeen = false;

    const auto readText = [&reader]() -> Result<QString> {
        const QString text = reader.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement);
        if (reader.hasError())
            return invalidArchive(QStringLiteral("Archive with an unreadable element"));
        if (text.size() > maxMessageTextLength)
            return Error(ErrorCode::LimitExceeded, QStringLiteral("Text in the archive too long"));
        return text;
    };

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::DTD)
            return invalidArchive(QStringLiteral("Archive with a document type declaration"));
        if (token == QXmlStreamReader::EndElement || token != QXmlStreamReader::StartElement)
            continue;
        if (!rootSeen) {
            if (reader.name() != QLatin1StringView("messages") ||
                reader.attributes().value(QStringLiteral("version")) != QLatin1StringView("1")) {
                return invalidArchive(QStringLiteral("Not a message archive of version 1"));
            }
            rootSeen = true;
            continue;
        }
        if (reader.name() != QLatin1StringView("message"))
            return invalidArchive(QStringLiteral("Unknown element in the archive"));
        if (messages.size() >= maxArchiveMessages) {
            return Error(
                ErrorCode::LimitExceeded,
                QStringLiteral("More than %1 messages in the archive").arg(maxArchiveMessages));
        }

        Message message;
        const QXmlStreamAttributes attributes = reader.attributes();
        const QStringView kind = attributes.value(QStringLiteral("kind"));
        if (kind == QLatin1StringView("mms"))
            message.kind = MessageKind::Mms;
        else if (kind != QLatin1StringView("sms"))
            return invalidArchive(QStringLiteral("Unknown kind of message in the archive"));
        const std::optional<MessageBox> box = boxFromName(attributes.value(QStringLiteral("box")));
        if (!box)
            return invalidArchive(QStringLiteral("Unknown folder in the archive"));
        message.box = *box;
        message.incoming = message.box == MessageBox::Inbox;
        message.read = attributes.value(QStringLiteral("read")) != QLatin1StringView("0");
        const QStringView date = attributes.value(QStringLiteral("date"));
        if (!date.isEmpty()) {
            message.date = QDateTime::fromString(date.toString(), Qt::ISODate).toUTC();
            if (!message.date.isValid())
                return invalidArchive(QStringLiteral("Unreadable time in the archive"));
        }
        bool simOk = false;
        const int sim = attributes.value(QStringLiteral("sim")).toInt(&simOk);
        message.simSlot = simOk && sim >= 0 && sim <= 9 ? sim : -1;

        while (reader.readNextStartElement()) {
            const QStringView name = reader.name();
            if (name == QLatin1StringView("address")) {
                const Result<QString> address = readText();
                if (!address.hasValue())
                    return address.error();
                if (message.addresses.size() >= maxMessageAddresses ||
                    address.value().size() > maxMessageAddressLength) {
                    return Error(ErrorCode::LimitExceeded, QStringLiteral("Address too long"));
                }
                message.addresses.append(address.value());
            } else if (name == QLatin1StringView("subject")) {
                const Result<QString> subject = readText();
                if (!subject.hasValue())
                    return subject.error();
                message.subject = subject.value();
            } else if (name == QLatin1StringView("body")) {
                const Result<QString> body = readText();
                if (!body.hasValue())
                    return body.error();
                message.body = body.value();
            } else if (name == QLatin1StringView("attachment")) {
                if (message.attachments.size() >= maxMessageAttachments) {
                    return Error(ErrorCode::LimitExceeded,
                                 QStringLiteral("More than %1 parts").arg(maxMessageAttachments));
                }
                MessageAttachment attachment;
                const QXmlStreamAttributes partAttributes = reader.attributes();
                attachment.name = partAttributes.value(QStringLiteral("name")).toString();
                attachment.mimeType = partAttributes.value(QStringLiteral("mime")).toString();
                attachment.size = partAttributes.value(QStringLiteral("size")).toLongLong();
                attachment.onPhoneOnly =
                    partAttributes.value(QStringLiteral("onPhone")) == QLatin1StringView("1");
                if (attachment.name.size() > maxMessageAddressLength ||
                    attachment.mimeType.size() > maxMessageAddressLength) {
                    return Error(ErrorCode::LimitExceeded, QStringLiteral("Part name too long"));
                }
                const QString encoded =
                    reader.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement).trimmed();
                if (reader.hasError())
                    return invalidArchive(QStringLiteral("Archive with an unreadable part"));
                if (encoded.size() / 4 * 3 > maxAttachmentBytes) {
                    return Error(
                        ErrorCode::LimitExceeded,
                        QStringLiteral("Part larger than %1 bytes").arg(maxAttachmentBytes));
                }
                if (!attachment.onPhoneOnly) {
                    const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(
                        encoded.toLatin1(),
                        QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
                    if (!decoded)
                        return invalidArchive(QStringLiteral("Part in the archive is not Base64"));
                    attachment.data = *decoded;
                    attachment.size = attachment.data.size();
                }
                message.attachments.append(attachment);
            } else {
                return invalidArchive(QStringLiteral("Unknown element in a message"));
            }
        }
        if (reader.hasError())
            break;
        messages.append(message);
    }
    if (reader.hasError())
        return invalidArchive(QStringLiteral("Archive is not well-formed XML"));
    if (!rootSeen)
        return invalidArchive(QStringLiteral("Not a message archive"));
    return messages;
}

} // namespace qpm::core::codec
