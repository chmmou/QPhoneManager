#include "core/codec/VCardInterchange.h"

#include "core/codec/CsvTable.h"

#include <QHash>
#include <QSet>
#include <QStringDecoder>

#include <array>
#include <functional>
#include <utility>

namespace qpm::core::codec {

namespace {

constexpr qsizetype foldWidth = 75;

// ---- reading ----

struct Property
{
    QString name;
    QSet<QString> types;
    QHash<QString, QString> parameters;
    bool quotedPrintable = false;
    bool base64 = false;
    bool preferred = false;
    QString value;
};

QByteArray decodeQuotedPrintable(const QByteArray &value)
{
    QByteArray out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const char c = value.at(i);
        if (c == '=' && i + 2 < value.size()) {
            bool ok = false;
            const int byte = value.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                out.append(char(byte));
                i += 2;
                continue;
            }
        }
        out.append(c);
    }
    return out;
}

QString decodeText(const QByteArray &bytes, const QString &charset)
{
    if (!charset.isEmpty() &&
        charset.compare(QLatin1StringView("UTF-8"), Qt::CaseInsensitive) != 0) {
        QStringDecoder decoder(charset.toLatin1().constData());
        if (decoder.isValid())
            return decoder.decode(bytes);
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8.decode(bytes);
    // Old files often carry Latin-1 without saying so.
    return utf8.hasError() ? QString::fromLatin1(bytes) : text;
}

// Splits at `separator` outside double quotes.
QStringList splitOutsideQuotes(const QString &text, QChar separator)
{
    QStringList parts;
    QString current;
    bool inQuotes = false;
    for (const QChar c : text) {
        if (c == u'"') {
            inQuotes = !inQuotes;
            current.append(c);
        } else if (c == separator && !inQuotes) {
            parts.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    parts.append(current);
    return parts;
}

QString unquoted(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(u'"') && text.endsWith(u'"'))
        return text.mid(1, text.size() - 2);
    return text;
}

QString normalizedType(QString type)
{
    type = unquoted(type).toLower();
    if (type.startsWith(QLatin1StringView("x-")))
        type.remove(0, 2);
    return type;
}

std::optional<Property> parseProperty(const QByteArray &line)
{
    // The value starts at the first colon outside a quoted parameter value.
    qsizetype colon = -1;
    bool inQuotes = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (line.at(i) == '"')
            inQuotes = !inQuotes;
        else if (line.at(i) == ':' && !inQuotes) {
            colon = i;
            break;
        }
    }
    if (colon <= 0)
        return std::nullopt;

    const QString header = QString::fromUtf8(line.left(colon));
    QStringList segments = splitOutsideQuotes(header, u';');
    Property property;
    property.name = segments.takeFirst().trimmed().toUpper();
    if (const qsizetype dot = property.name.lastIndexOf(u'.'); dot >= 0)
        property.name = property.name.mid(dot + 1);

    QString charset;
    for (const QString &segment : std::as_const(segments)) {
        const qsizetype equals = segment.indexOf(u'=');
        if (equals < 0) {
            const QString bare = segment.trimmed().toUpper();
            if (bare == QLatin1StringView("QUOTED-PRINTABLE"))
                property.quotedPrintable = true;
            else if (bare == QLatin1StringView("BASE64") || bare == QLatin1StringView("B"))
                property.base64 = true;
            else if (bare == QLatin1StringView("PREF"))
                property.preferred = true;
            else if (!bare.isEmpty())
                property.types.insert(normalizedType(bare));
            continue;
        }
        const QString key = segment.left(equals).trimmed().toUpper();
        const QString value = segment.mid(equals + 1);
        if (key == QLatin1StringView("TYPE")) {
            const QStringList types = splitOutsideQuotes(unquoted(value), u',');
            for (const QString &type : types) {
                const QString normalized = normalizedType(type);
                if (normalized == QLatin1StringView("pref"))
                    property.preferred = true;
                else if (!normalized.isEmpty())
                    property.types.insert(normalized);
            }
        } else if (key == QLatin1StringView("ENCODING")) {
            const QString encoding = unquoted(value).toUpper();
            property.quotedPrintable = encoding == QLatin1StringView("QUOTED-PRINTABLE");
            property.base64 =
                encoding == QLatin1StringView("B") || encoding == QLatin1StringView("BASE64");
        } else if (key == QLatin1StringView("CHARSET")) {
            charset = unquoted(value);
        } else if (key == QLatin1StringView("PREF")) {
            property.preferred = true;
        } else {
            property.parameters.insert(key, unquoted(value));
        }
    }

    QByteArray valueBytes = line.mid(colon + 1);
    if (property.quotedPrintable)
        valueBytes = decodeQuotedPrintable(valueBytes);
    property.value =
        property.base64 ? QString::fromLatin1(valueBytes) : decodeText(valueBytes, charset);
    return property;
}

QString unescapeText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\\' && i + 1 < text.size()) {
            const QChar next = text.at(i + 1);
            if (next == u'n' || next == u'N')
                out.append(u'\n');
            else
                out.append(next);
            ++i;
        } else {
            out.append(c);
        }
    }
    return out;
}

// Splits at `separator` that is not escaped and unescapes each part.
QStringList splitText(const QString &text, QChar separator)
{
    QStringList parts;
    QString current;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\\' && i + 1 < text.size()) {
            current.append(c);
            current.append(text.at(i + 1));
            ++i;
        } else if (c == separator) {
            parts.append(unescapeText(current));
            current.clear();
        } else {
            current.append(c);
        }
    }
    parts.append(unescapeText(current));
    return parts;
}

int phoneTypeOf(const Property &property)
{
    const auto has = [&property](const char *type) {
        return property.types.contains(QLatin1StringView(type));
    };
    if (property.parameters.contains(QStringLiteral("X-LABEL")))
        return phoneType::custom;
    if (has("fax")) {
        if (has("home"))
            return phoneType::faxHome;
        if (has("other"))
            return phoneType::otherFax;
        return phoneType::faxWork;
    }
    if (has("pager"))
        return has("work") ? phoneType::workPager : phoneType::pager;
    if (has("cell") || has("mobile"))
        return has("work") ? phoneType::workMobile : phoneType::mobile;
    if (has("callback"))
        return phoneType::callback;
    if (has("car"))
        return phoneType::car;
    if (has("company"))
        return phoneType::companyMain;
    if (has("isdn"))
        return phoneType::isdn;
    if (has("main"))
        return phoneType::main;
    if (has("radio"))
        return phoneType::radio;
    if (has("telex"))
        return phoneType::telex;
    if (has("textphone") || has("tty"))
        return phoneType::tty;
    if (has("assistant"))
        return phoneType::assistant;
    if (has("mms"))
        return phoneType::mms;
    if (has("home"))
        return phoneType::home;
    if (has("work"))
        return phoneType::work;
    return phoneType::other;
}

int emailTypeOf(const Property &property)
{
    if (property.parameters.contains(QStringLiteral("X-LABEL")))
        return emailType::custom;
    if (property.types.contains(QStringLiteral("home")))
        return emailType::home;
    if (property.types.contains(QStringLiteral("work")))
        return emailType::work;
    if (property.types.contains(QStringLiteral("cell")) ||
        property.types.contains(QStringLiteral("mobile")))
        return emailType::mobile;
    return emailType::other;
}

int addressTypeOf(const Property &property)
{
    if (property.parameters.contains(QStringLiteral("X-LABEL")))
        return addressType::custom;
    if (property.types.contains(QStringLiteral("home")))
        return addressType::home;
    if (property.types.contains(QStringLiteral("work")))
        return addressType::work;
    return addressType::other;
}

int websiteTypeOf(const Property &property)
{
    if (property.parameters.contains(QStringLiteral("X-LABEL")))
        return websiteType::custom;
    static const QHash<QString, int> types = {
        {QStringLiteral("homepage"), websiteType::homepage},
        {QStringLiteral("blog"), websiteType::blog},
        {QStringLiteral("profile"), websiteType::profile},
        {QStringLiteral("home"), websiteType::home},
        {QStringLiteral("work"), websiteType::work},
        {QStringLiteral("ftp"), websiteType::ftp},
    };
    for (auto it = types.cbegin(); it != types.cend(); ++it) {
        if (property.types.contains(it.key()))
            return it.value();
    }
    return websiteType::other;
}

int eventTypeOf(const Property &property)
{
    if (property.parameters.contains(QStringLiteral("X-LABEL")))
        return eventType::custom;
    if (property.types.contains(QStringLiteral("birthday")))
        return eventType::birthday;
    if (property.types.contains(QStringLiteral("anniversary")))
        return eventType::anniversary;
    return eventType::other;
}

struct Scheme
{
    QLatin1StringView name;
    int protocol;
};

constexpr std::array imSchemes = {
    Scheme{QLatin1StringView("aim"), imProtocol::aim},
    Scheme{QLatin1StringView("msnim"), imProtocol::msn},
    Scheme{QLatin1StringView("msn"), imProtocol::msn},
    Scheme{QLatin1StringView("ymsgr"), imProtocol::yahoo},
    Scheme{QLatin1StringView("yahoo"), imProtocol::yahoo},
    Scheme{QLatin1StringView("skype"), imProtocol::skype},
    Scheme{QLatin1StringView("qq"), imProtocol::qq},
    Scheme{QLatin1StringView("gtalk"), imProtocol::googleTalk},
    Scheme{QLatin1StringView("icq"), imProtocol::icq},
    Scheme{QLatin1StringView("xmpp"), imProtocol::jabber},
    Scheme{QLatin1StringView("jabber"), imProtocol::jabber},
    Scheme{QLatin1StringView("netmeet"), imProtocol::netMeeting},
};

// Messenger properties of older programs, such as X-JABBER.
constexpr std::array imProperties = {
    Scheme{QLatin1StringView("X-AIM"), imProtocol::aim},
    Scheme{QLatin1StringView("X-MSN"), imProtocol::msn},
    Scheme{QLatin1StringView("X-YAHOO"), imProtocol::yahoo},
    Scheme{QLatin1StringView("X-SKYPE"), imProtocol::skype},
    Scheme{QLatin1StringView("X-SKYPE-USERNAME"), imProtocol::skype},
    Scheme{QLatin1StringView("X-QQ"), imProtocol::qq},
    Scheme{QLatin1StringView("X-GOOGLE-TALK"), imProtocol::googleTalk},
    Scheme{QLatin1StringView("X-GTALK"), imProtocol::googleTalk},
    Scheme{QLatin1StringView("X-ICQ"), imProtocol::icq},
    Scheme{QLatin1StringView("X-JABBER"), imProtocol::jabber},
};

QString schemeOf(int protocol)
{
    for (const Scheme &scheme : imSchemes) {
        if (scheme.protocol == protocol)
            return scheme.name;
    }
    return {};
}

QDate parseDate(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QLatin1StringView("--")))
        return {};
    QDate date = QDate::fromString(trimmed.left(10), Qt::ISODate);
    if (!date.isValid())
        date = QDate::fromString(trimmed.left(8), QStringLiteral("yyyyMMdd"));
    return date;
}

std::optional<QByteArray> decodePhoto(const Property &property)
{
    QByteArray encoded;
    if (property.base64) {
        encoded = property.value.toLatin1();
    } else if (property.value.startsWith(QLatin1StringView("data:"), Qt::CaseInsensitive)) {
        const qsizetype comma = property.value.indexOf(u',');
        if (comma < 0 || !property.value.left(comma).contains(QLatin1StringView(";base64"),
                                                              Qt::CaseInsensitive)) {
            return std::nullopt;
        }
        encoded = property.value.mid(comma + 1).toLatin1();
    } else {
        // A link to a picture elsewhere is never fetched.
        return std::nullopt;
    }
    encoded.replace(' ', QByteArray());
    encoded.replace('\t', QByteArray());
    if (encoded.size() / 4 * 3 > maxPhotoBytes)
        return std::nullopt;
    const auto decoded = QByteArray::fromBase64Encoding(encoded);
    if (!decoded)
        return std::nullopt;
    return *decoded;
}

// Collects the physical lines of one file into logical lines.
QList<QByteArray> logicalLines(const QByteArray &data)
{
    QList<QByteArray> lines;
    bool continueQuotedPrintable = false;
    const QList<QByteArray> physicalLines = data.split('\n');
    for (QByteArray physical : physicalLines) {
        if (physical.endsWith('\r'))
            physical.chop(1);
        if (continueQuotedPrintable && !lines.isEmpty()) {
            lines.last().chop(1);
            lines.last().append(physical);
        } else if (!lines.isEmpty() && !physical.isEmpty() &&
                   (physical.at(0) == ' ' || physical.at(0) == '\t')) {
            lines.last().append(physical.mid(1));
        } else {
            lines.append(physical);
        }
        const QByteArray &current = lines.last();
        const qsizetype colon = current.indexOf(':');
        continueQuotedPrintable = colon > 0 && current.endsWith('=') &&
                                  current.left(colon).toUpper().contains("QUOTED-PRINTABLE");
    }
    return lines;
}

Error tooMany(const QString &what)
{
    return Error(
        ErrorCode::LimitExceeded,
        QStringLiteral("More than %1 entries of %2 in one vCard").arg(maxValuesPerKind).arg(what));
}

Result<void> appendValue(QList<ContactValue> &list, const ContactValue &value, const QString &what)
{
    if (value.isEmpty())
        return {};
    if (list.size() >= maxValuesPerKind)
        return tooMany(what);
    list.append(value);
    return {};
}

Result<void> applyProperty(Contact &contact, const Property &property)
{
    const QString &name = property.name;
    ContactValue entry;
    entry.primary = property.preferred;
    entry.label = property.parameters.value(QStringLiteral("X-LABEL"));

    if (name == QLatin1StringView("N")) {
        const QStringList parts = splitText(property.value, u';');
        contact.familyName = parts.value(0);
        contact.givenName = parts.value(1);
        contact.middleName = parts.value(2);
        contact.prefix = parts.value(3);
        contact.suffix = parts.value(4);
    } else if (name == QLatin1StringView("FN")) {
        contact.displayName = unescapeText(property.value);
    } else if (name == QLatin1StringView("NICKNAME")) {
        if (contact.nickname.isEmpty())
            contact.nickname = splitText(property.value, u',').value(0);
    } else if (name == QLatin1StringView("ORG")) {
        const QStringList parts = splitText(property.value, u';');
        contact.organization = parts.value(0);
        contact.department = parts.mid(1).join(QLatin1StringView(", "));
    } else if (name == QLatin1StringView("TITLE")) {
        contact.title = unescapeText(property.value);
    } else if (name == QLatin1StringView("NOTE")) {
        contact.note = unescapeText(property.value);
    } else if (name == QLatin1StringView("TEL")) {
        entry.type = phoneTypeOf(property);
        entry.value = unescapeText(property.value).trimmed();
        if (entry.value.startsWith(QLatin1StringView("tel:"), Qt::CaseInsensitive))
            entry.value = entry.value.mid(4);
        return appendValue(contact.phones, entry, QStringLiteral("phone numbers"));
    } else if (name == QLatin1StringView("EMAIL")) {
        entry.type = emailTypeOf(property);
        entry.value = unescapeText(property.value).trimmed();
        return appendValue(contact.emails, entry, QStringLiteral("mail addresses"));
    } else if (name == QLatin1StringView("ADR")) {
        entry.type = addressTypeOf(property);
        entry.address = splitText(property.value, u';').mid(0, addressPart::count);
        while (entry.address.size() < addressPart::count)
            entry.address.append(QString());
        return appendValue(contact.addresses, entry, QStringLiteral("addresses"));
    } else if (name == QLatin1StringView("URL")) {
        entry.type = websiteTypeOf(property);
        entry.value = unescapeText(property.value).trimmed();
        return appendValue(contact.websites, entry, QStringLiteral("websites"));
    } else if (name == QLatin1StringView("IMPP")) {
        const QString value = unescapeText(property.value).trimmed();
        const qsizetype colon = value.indexOf(u':');
        const QString scheme = colon < 0 ? QString() : value.left(colon).toLower();
        entry.value = colon < 0 ? value : value.mid(colon + 1);
        entry.type = imProtocol::custom;
        for (const Scheme &known : imSchemes) {
            if (scheme == known.name)
                entry.type = known.protocol;
        }
        if (entry.type == imProtocol::custom && entry.label.isEmpty()) {
            entry.label = property.parameters.value(QStringLiteral("X-SERVICE-TYPE"), scheme);
        }
        return appendValue(contact.ims, entry, QStringLiteral("messenger addresses"));
    } else if (name == QLatin1StringView("BDAY")) {
        contact.birthday = parseDate(property.value);
    } else if (name == QLatin1StringView("ANNIVERSARY") ||
               name == QLatin1StringView("X-ANNIVERSARY")) {
        contact.anniversary = parseDate(property.value);
    } else if (name == QLatin1StringView("X-EVENT")) {
        const QDate date = parseDate(property.value);
        if (!date.isValid())
            return {};
        entry.type = eventTypeOf(property);
        entry.value = date.toString(Qt::ISODate);
        return appendValue(contact.events, entry, QStringLiteral("dates"));
    } else if (name == QLatin1StringView("CATEGORIES")) {
        const QStringList groups = splitText(property.value, u',');
        for (const QString &group : groups) {
            const QString trimmed = group.trimmed();
            if (!trimmed.isEmpty() && !contact.groups.contains(trimmed))
                contact.groups.append(trimmed);
        }
    } else if (name == QLatin1StringView("PHOTO")) {
        if (const std::optional<QByteArray> photo = decodePhoto(property))
            contact.photo = *photo;
    } else {
        for (const Scheme &known : imProperties) {
            if (name == known.name) {
                entry.type = known.protocol;
                entry.value = unescapeText(property.value).trimmed();
                return appendValue(contact.ims, entry, QStringLiteral("messenger addresses"));
            }
        }
    }
    return {};
}

// ---- writing ----

QString escapeText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == u'\\') {
            out += QLatin1StringView("\\\\");
        } else if (c == u'\n') {
            out += QLatin1StringView("\\n");
        } else if (c == u',' || c == u';') {
            out += u'\\';
            out += c;
        } else if (c != u'\r') {
            out += c;
        }
    }
    return out;
}

QString parameterValue(QString value)
{
    value.remove(u'"');
    if (value.contains(u';') || value.contains(u':') || value.contains(u','))
        return u'"' + value + u'"';
    return value;
}

QByteArray folded(const QString &line)
{
    const QByteArray bytes = line.toUtf8();
    QByteArray out;
    qsizetype start = 0;
    qsizetype width = foldWidth;
    while (bytes.size() - start > width) {
        qsizetype end = start + width;
        // A UTF-8 continuation byte starts with the bits 10.
        while (end > start && (static_cast<unsigned char>(bytes.at(end)) & 0xC0) == 0x80)
            --end;
        out += bytes.mid(start, end - start) + "\r\n ";
        start = end;
        width = foldWidth - 1;
    }
    out += bytes.mid(start) + "\r\n";
    return out;
}

QString phoneTypeParameter(const ContactValue &entry)
{
    switch (entry.type) {
    case phoneType::custom:
        return QStringLiteral(";X-LABEL=") + parameterValue(entry.label);
    case phoneType::home:
        return QStringLiteral(";TYPE=HOME");
    case phoneType::mobile:
        return QStringLiteral(";TYPE=CELL");
    case phoneType::work:
        return QStringLiteral(";TYPE=WORK");
    case phoneType::faxWork:
        return QStringLiteral(";TYPE=FAX,WORK");
    case phoneType::faxHome:
        return QStringLiteral(";TYPE=FAX,HOME");
    case phoneType::pager:
        return QStringLiteral(";TYPE=PAGER");
    case phoneType::callback:
        return QStringLiteral(";TYPE=X-CALLBACK");
    case phoneType::car:
        return QStringLiteral(";TYPE=CAR");
    case phoneType::companyMain:
        return QStringLiteral(";TYPE=X-COMPANY");
    case phoneType::isdn:
        return QStringLiteral(";TYPE=ISDN");
    case phoneType::main:
        return QStringLiteral(";TYPE=X-MAIN");
    case phoneType::otherFax:
        return QStringLiteral(";TYPE=FAX,X-OTHER");
    case phoneType::radio:
        return QStringLiteral(";TYPE=X-RADIO");
    case phoneType::telex:
        return QStringLiteral(";TYPE=X-TELEX");
    case phoneType::tty:
        return QStringLiteral(";TYPE=TEXTPHONE");
    case phoneType::workMobile:
        return QStringLiteral(";TYPE=CELL,WORK");
    case phoneType::workPager:
        return QStringLiteral(";TYPE=PAGER,WORK");
    case phoneType::assistant:
        return QStringLiteral(";TYPE=X-ASSISTANT");
    case phoneType::mms:
        return QStringLiteral(";TYPE=X-MMS");
    default:
        break;
    }
    return QStringLiteral(";TYPE=VOICE");
}

QString simpleTypeParameter(ContactValueKind kind, const ContactValue &entry)
{
    if (entry.type == 0)
        return QStringLiteral(";X-LABEL=") + parameterValue(entry.label);
    switch (kind) {
    case ContactValueKind::Email:
        switch (entry.type) {
        case emailType::home:
            return QStringLiteral(";TYPE=INTERNET,HOME");
        case emailType::work:
            return QStringLiteral(";TYPE=INTERNET,WORK");
        case emailType::mobile:
            return QStringLiteral(";TYPE=INTERNET,CELL");
        default:
            return QStringLiteral(";TYPE=INTERNET");
        }
    case ContactValueKind::Address:
        switch (entry.type) {
        case addressType::home:
            return QStringLiteral(";TYPE=HOME");
        case addressType::work:
            return QStringLiteral(";TYPE=WORK");
        default:
            return {};
        }
    case ContactValueKind::Website:
        switch (entry.type) {
        case websiteType::homepage:
            return QStringLiteral(";TYPE=X-HOMEPAGE");
        case websiteType::blog:
            return QStringLiteral(";TYPE=X-BLOG");
        case websiteType::profile:
            return QStringLiteral(";TYPE=X-PROFILE");
        case websiteType::home:
            return QStringLiteral(";TYPE=HOME");
        case websiteType::work:
            return QStringLiteral(";TYPE=WORK");
        case websiteType::ftp:
            return QStringLiteral(";TYPE=X-FTP");
        default:
            return {};
        }
    case ContactValueKind::Event:
        switch (entry.type) {
        case eventType::birthday:
            return QStringLiteral(";TYPE=BIRTHDAY");
        case eventType::anniversary:
            return QStringLiteral(";TYPE=ANNIVERSARY");
        default:
            return QStringLiteral(";TYPE=OTHER");
        }
    case ContactValueKind::Phone:
    case ContactValueKind::Im:
        break;
    }
    return {};
}

QByteArray writeVCard(const Contact &contact)
{
    QByteArray out = "BEGIN:VCARD\r\nVERSION:3.0\r\n";
    QStringList name = {contact.familyName, contact.givenName, contact.middleName, contact.prefix,
                        contact.suffix};
    for (QString &part : name)
        part = escapeText(part);
    out += folded(QStringLiteral("N:") + name.join(u';'));
    out += folded(QStringLiteral("FN:") + escapeText(displayNameOf(contact)));
    if (!contact.nickname.isEmpty())
        out += folded(QStringLiteral("NICKNAME:") + escapeText(contact.nickname));
    if (!contact.organization.isEmpty() || !contact.department.isEmpty()) {
        out += folded(QStringLiteral("ORG:") + escapeText(contact.organization) + u';' +
                      escapeText(contact.department));
    }
    if (!contact.title.isEmpty())
        out += folded(QStringLiteral("TITLE:") + escapeText(contact.title));
    if (!contact.note.isEmpty())
        out += folded(QStringLiteral("NOTE:") + escapeText(contact.note));
    for (const ContactValue &phone : contact.phones) {
        if (phone.isEmpty())
            continue;
        out += folded(QStringLiteral("TEL") + phoneTypeParameter(phone) +
                      (phone.primary ? QStringLiteral(";TYPE=PREF") : QString()) + u':' +
                      escapeText(phone.value));
    }
    for (const ContactValue &email : contact.emails) {
        if (email.isEmpty())
            continue;
        out +=
            folded(QStringLiteral("EMAIL") + simpleTypeParameter(ContactValueKind::Email, email) +
                   (email.primary ? QStringLiteral(";TYPE=PREF") : QString()) + u':' +
                   escapeText(email.value));
    }
    for (const ContactValue &address : contact.addresses) {
        if (address.isEmpty())
            continue;
        QStringList parts = address.address;
        while (parts.size() < addressPart::count)
            parts.append(QString());
        for (QString &part : parts)
            part = escapeText(part);
        out +=
            folded(QStringLiteral("ADR") + simpleTypeParameter(ContactValueKind::Address, address) +
                   u':' + parts.mid(0, addressPart::count).join(u';'));
    }
    for (const ContactValue &website : contact.websites) {
        if (website.isEmpty())
            continue;
        out +=
            folded(QStringLiteral("URL") + simpleTypeParameter(ContactValueKind::Website, website) +
                   u':' + escapeText(website.value));
    }
    for (const ContactValue &im : contact.ims) {
        if (im.isEmpty())
            continue;
        const QString scheme = schemeOf(im.type);
        if (scheme.isEmpty()) {
            out += folded(QStringLiteral("IMPP;X-SERVICE-TYPE=") + parameterValue(im.label) +
                          QStringLiteral(":x-im:") + escapeText(im.value));
        } else {
            out += folded(QStringLiteral("IMPP:") + scheme + u':' + escapeText(im.value));
        }
    }
    if (contact.birthday.isValid())
        out += folded(QStringLiteral("BDAY:") + contact.birthday.toString(Qt::ISODate));
    if (contact.anniversary.isValid())
        out += folded(QStringLiteral("X-ANNIVERSARY:") + contact.anniversary.toString(Qt::ISODate));
    for (const ContactValue &event : contact.events) {
        if (event.isEmpty())
            continue;
        out += folded(QStringLiteral("X-EVENT") +
                      simpleTypeParameter(ContactValueKind::Event, event) + u':' + event.value);
    }
    if (!contact.groups.isEmpty()) {
        QStringList groups;
        for (const QString &group : contact.groups)
            groups.append(escapeText(group));
        out += folded(QStringLiteral("CATEGORIES:") + groups.join(u','));
    }
    if (!contact.photo.isEmpty()) {
        const bool png = contact.photo.startsWith("\x89PNG");
        out += folded(QStringLiteral("PHOTO;ENCODING=b;TYPE=") +
                      (png ? QStringLiteral("PNG") : QStringLiteral("JPEG")) + u':' +
                      QString::fromLatin1(contact.photo.toBase64()));
    }
    out += "END:VCARD\r\n";
    return out;
}

// ---- tables ----

enum class Column {
    GivenName,
    MiddleName,
    FamilyName,
    Prefix,
    Suffix,
    DisplayName,
    Nickname,
    Organization,
    Department,
    JobTitle,
    MobilePhone,
    HomePhone,
    WorkPhone,
    Fax,
    OtherPhone,
    HomeEmail,
    WorkEmail,
    OtherEmail,
    HomeStreet,
    HomePostcode,
    HomeCity,
    HomeRegion,
    HomeCountry,
    WorkStreet,
    WorkPostcode,
    WorkCity,
    WorkRegion,
    WorkCountry,
    Website,
    Birthday,
    Anniversary,
    Groups,
    Note,
};

struct ColumnTitle
{
    Column column;
    QLatin1StringView english;
    const char *german;
};

const std::array<ColumnTitle, 33> &columnTitles()
{
    static const std::array<ColumnTitle, 33> titles = {{
        {Column::GivenName, QLatin1StringView("First name"), "Vorname"},
        {Column::MiddleName, QLatin1StringView("Middle name"), "Zweiter Vorname"},
        {Column::FamilyName, QLatin1StringView("Last name"), "Nachname"},
        {Column::Prefix, QLatin1StringView("Name prefix"), "Namenspräfix"},
        {Column::Suffix, QLatin1StringView("Name suffix"), "Namenszusatz"},
        {Column::DisplayName, QLatin1StringView("Display name"), "Anzeigename"},
        {Column::Nickname, QLatin1StringView("Nickname"), "Spitzname"},
        {Column::Organization, QLatin1StringView("Company"), "Firma"},
        {Column::Department, QLatin1StringView("Department"), "Abteilung"},
        {Column::JobTitle, QLatin1StringView("Job title"), "Position"},
        {Column::MobilePhone, QLatin1StringView("Mobile"), "Mobil"},
        {Column::HomePhone, QLatin1StringView("Home phone"), "Telefon privat"},
        {Column::WorkPhone, QLatin1StringView("Work phone"), "Telefon geschäftlich"},
        {Column::Fax, QLatin1StringView("Fax"), "Fax"},
        {Column::OtherPhone, QLatin1StringView("Other phone"), "Telefon sonstige"},
        {Column::HomeEmail, QLatin1StringView("Home email"), "E-Mail privat"},
        {Column::WorkEmail, QLatin1StringView("Work email"), "E-Mail geschäftlich"},
        {Column::OtherEmail, QLatin1StringView("Other email"), "E-Mail sonstige"},
        {Column::HomeStreet, QLatin1StringView("Home street"), "Straße privat"},
        {Column::HomePostcode, QLatin1StringView("Home postcode"), "PLZ privat"},
        {Column::HomeCity, QLatin1StringView("Home city"), "Ort privat"},
        {Column::HomeRegion, QLatin1StringView("Home region"), "Region privat"},
        {Column::HomeCountry, QLatin1StringView("Home country"), "Land privat"},
        {Column::WorkStreet, QLatin1StringView("Work street"), "Straße geschäftlich"},
        {Column::WorkPostcode, QLatin1StringView("Work postcode"), "PLZ geschäftlich"},
        {Column::WorkCity, QLatin1StringView("Work city"), "Ort geschäftlich"},
        {Column::WorkRegion, QLatin1StringView("Work region"), "Region geschäftlich"},
        {Column::WorkCountry, QLatin1StringView("Work country"), "Land geschäftlich"},
        {Column::Website, QLatin1StringView("Website"), "Webseite"},
        {Column::Birthday, QLatin1StringView("Birthday"), "Geburtstag"},
        {Column::Anniversary, QLatin1StringView("Anniversary"), "Jahrestag"},
        {Column::Groups, QLatin1StringView("Groups"), "Gruppen"},
        {Column::Note, QLatin1StringView("Note"), "Notiz"},
    }};
    return titles;
}

QString joinedValues(const QList<ContactValue> &values, const std::function<bool(int)> &matches)
{
    QStringList parts;
    for (const ContactValue &entry : values) {
        if (!entry.isEmpty() && matches(entry.type))
            parts.append(entry.value.trimmed());
    }
    return parts.join(QLatin1StringView(", "));
}

const ContactValue *firstAddress(const Contact &contact, int type)
{
    for (const ContactValue &address : contact.addresses) {
        if (address.type == type && !address.isEmpty())
            return &address;
    }
    return nullptr;
}

QString addressField(const ContactValue *address, int part)
{
    return address ? address->address.value(part) : QString();
}

QString cell(const Contact &contact, Column column)
{
    const ContactValue *home = firstAddress(contact, addressType::home);
    const ContactValue *work = firstAddress(contact, addressType::work);
    switch (column) {
    case Column::GivenName:
        return contact.givenName;
    case Column::MiddleName:
        return contact.middleName;
    case Column::FamilyName:
        return contact.familyName;
    case Column::Prefix:
        return contact.prefix;
    case Column::Suffix:
        return contact.suffix;
    case Column::DisplayName:
        return displayNameOf(contact);
    case Column::Nickname:
        return contact.nickname;
    case Column::Organization:
        return contact.organization;
    case Column::Department:
        return contact.department;
    case Column::JobTitle:
        return contact.title;
    case Column::MobilePhone:
        return joinedValues(contact.phones, [](int type) {
            return type == phoneType::mobile || type == phoneType::workMobile;
        });
    case Column::HomePhone:
        return joinedValues(contact.phones, [](int type) { return type == phoneType::home; });
    case Column::WorkPhone:
        return joinedValues(contact.phones, [](int type) {
            return type == phoneType::work || type == phoneType::companyMain ||
                   type == phoneType::main;
        });
    case Column::Fax:
        return joinedValues(contact.phones, [](int type) {
            return type == phoneType::faxWork || type == phoneType::faxHome ||
                   type == phoneType::otherFax;
        });
    case Column::OtherPhone:
        return joinedValues(contact.phones, [](int type) {
            return type != phoneType::mobile && type != phoneType::workMobile &&
                   type != phoneType::home && type != phoneType::work &&
                   type != phoneType::companyMain && type != phoneType::main &&
                   type != phoneType::faxWork && type != phoneType::faxHome &&
                   type != phoneType::otherFax;
        });
    case Column::HomeEmail:
        return joinedValues(contact.emails, [](int type) { return type == emailType::home; });
    case Column::WorkEmail:
        return joinedValues(contact.emails, [](int type) { return type == emailType::work; });
    case Column::OtherEmail:
        return joinedValues(contact.emails, [](int type) {
            return type != emailType::home && type != emailType::work;
        });
    case Column::HomeStreet:
        return addressField(home, addressPart::street);
    case Column::HomePostcode:
        return addressField(home, addressPart::postcode);
    case Column::HomeCity:
        return addressField(home, addressPart::city);
    case Column::HomeRegion:
        return addressField(home, addressPart::region);
    case Column::HomeCountry:
        return addressField(home, addressPart::country);
    case Column::WorkStreet:
        return addressField(work, addressPart::street);
    case Column::WorkPostcode:
        return addressField(work, addressPart::postcode);
    case Column::WorkCity:
        return addressField(work, addressPart::city);
    case Column::WorkRegion:
        return addressField(work, addressPart::region);
    case Column::WorkCountry:
        return addressField(work, addressPart::country);
    case Column::Website:
        return joinedValues(contact.websites, [](int) { return true; });
    case Column::Birthday:
        return contact.birthday.isValid() ? contact.birthday.toString(Qt::ISODate) : QString();
    case Column::Anniversary:
        return contact.anniversary.isValid() ? contact.anniversary.toString(Qt::ISODate)
                                             : QString();
    case Column::Groups:
        return contact.groups.join(QLatin1StringView(", "));
    case Column::Note:
        return contact.note;
    }
    return {};
}

void appendSplit(QList<ContactValue> &list, const QString &field, int type)
{
    const QStringList parts = field.split(u',');
    for (const QString &part : parts) {
        ContactValue entry;
        entry.type = type;
        entry.value = part.trimmed();
        if (!entry.isEmpty() && list.size() < maxValuesPerKind)
            list.append(entry);
    }
}

void setAddressPart(Contact &contact, int type, int part, const QString &value)
{
    if (value.trimmed().isEmpty())
        return;
    for (ContactValue &address : contact.addresses) {
        if (address.type == type) {
            address.address[part] = value.trimmed();
            return;
        }
    }
    ContactValue address;
    address.type = type;
    address.address = QStringList(addressPart::count, QString());
    address.address[part] = value.trimmed();
    contact.addresses.append(address);
}

void applyCell(Contact &contact, Column column, const QString &value)
{
    switch (column) {
    case Column::GivenName:
        contact.givenName = value;
        break;
    case Column::MiddleName:
        contact.middleName = value;
        break;
    case Column::FamilyName:
        contact.familyName = value;
        break;
    case Column::Prefix:
        contact.prefix = value;
        break;
    case Column::Suffix:
        contact.suffix = value;
        break;
    case Column::DisplayName:
        contact.displayName = value;
        break;
    case Column::Nickname:
        contact.nickname = value;
        break;
    case Column::Organization:
        contact.organization = value;
        break;
    case Column::Department:
        contact.department = value;
        break;
    case Column::JobTitle:
        contact.title = value;
        break;
    case Column::MobilePhone:
        appendSplit(contact.phones, value, phoneType::mobile);
        break;
    case Column::HomePhone:
        appendSplit(contact.phones, value, phoneType::home);
        break;
    case Column::WorkPhone:
        appendSplit(contact.phones, value, phoneType::work);
        break;
    case Column::Fax:
        appendSplit(contact.phones, value, phoneType::faxWork);
        break;
    case Column::OtherPhone:
        appendSplit(contact.phones, value, phoneType::other);
        break;
    case Column::HomeEmail:
        appendSplit(contact.emails, value, emailType::home);
        break;
    case Column::WorkEmail:
        appendSplit(contact.emails, value, emailType::work);
        break;
    case Column::OtherEmail:
        appendSplit(contact.emails, value, emailType::other);
        break;
    case Column::HomeStreet:
        setAddressPart(contact, addressType::home, addressPart::street, value);
        break;
    case Column::HomePostcode:
        setAddressPart(contact, addressType::home, addressPart::postcode, value);
        break;
    case Column::HomeCity:
        setAddressPart(contact, addressType::home, addressPart::city, value);
        break;
    case Column::HomeRegion:
        setAddressPart(contact, addressType::home, addressPart::region, value);
        break;
    case Column::HomeCountry:
        setAddressPart(contact, addressType::home, addressPart::country, value);
        break;
    case Column::WorkStreet:
        setAddressPart(contact, addressType::work, addressPart::street, value);
        break;
    case Column::WorkPostcode:
        setAddressPart(contact, addressType::work, addressPart::postcode, value);
        break;
    case Column::WorkCity:
        setAddressPart(contact, addressType::work, addressPart::city, value);
        break;
    case Column::WorkRegion:
        setAddressPart(contact, addressType::work, addressPart::region, value);
        break;
    case Column::WorkCountry:
        setAddressPart(contact, addressType::work, addressPart::country, value);
        break;
    case Column::Website:
        appendSplit(contact.websites, value, websiteType::other);
        break;
    case Column::Birthday:
        contact.birthday = parseDate(value);
        break;
    case Column::Anniversary:
        contact.anniversary = parseDate(value);
        break;
    case Column::Groups: {
        const QStringList groups = value.split(u',');
        for (const QString &group : groups) {
            if (!group.trimmed().isEmpty())
                contact.groups.append(group.trimmed());
        }
        break;
    }
    case Column::Note:
        contact.note = value;
        break;
    }
}

} // namespace

Result<QList<Contact>> readVCards(const QByteArray &data)
{
    if (data.size() > maxInterchangeBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("File larger than %1 bytes").arg(maxInterchangeBytes));
    }
    QList<Contact> contacts;
    std::optional<Contact> current;
    bool anyCard = false;
    const QList<QByteArray> lines = logicalLines(data);
    for (const QByteArray &line : lines) {
        const std::optional<Property> property = parseProperty(line);
        if (!property)
            continue;
        if (property->name == QLatin1StringView("BEGIN") &&
            property->value.trimmed().compare(QLatin1StringView("VCARD"), Qt::CaseInsensitive) ==
                0) {
            current = Contact();
            anyCard = true;
            continue;
        }
        if (!current)
            continue;
        if (property->name == QLatin1StringView("END")) {
            if (isSavable(*current)) {
                if (contacts.size() >= maxInterchangeContacts) {
                    return Error(ErrorCode::LimitExceeded,
                                 QStringLiteral("More than %1 contacts in one file")
                                     .arg(maxInterchangeContacts));
                }
                contacts.append(*current);
            }
            current.reset();
            continue;
        }
        if (const Result<void> applied = applyProperty(*current, *property); !applied.hasValue())
            return applied.error();
    }
    if (!anyCard)
        return Error(ErrorCode::InvalidInput, QStringLiteral("The file holds no vCard"));
    return contacts;
}

QByteArray writeVCards(const QList<Contact> &contacts)
{
    QByteArray out;
    for (const Contact &contact : contacts)
        out += writeVCard(contact);
    return out;
}

QByteArray writeContactsCsv(const QList<Contact> &contacts, CsvLanguage language)
{
    CsvRows rows;
    QStringList header;
    for (const ColumnTitle &title : columnTitles()) {
        header.append(language == CsvLanguage::German ? QString::fromUtf8(title.german)
                                                      : QString(title.english));
    }
    rows.append(header);
    for (const Contact &contact : contacts) {
        QStringList row;
        for (const ColumnTitle &title : columnTitles())
            row.append(cell(contact, title.column));
        rows.append(row);
    }
    return writeCsv(rows);
}

Result<QList<Contact>> readContactsCsv(const QByteArray &data)
{
    const Result<CsvRows> table = readCsv(data);
    if (!table.hasValue())
        return table.error();
    if (table.value().isEmpty())
        return Error(ErrorCode::InvalidInput, QStringLiteral("The table is empty"));

    // In the order of the file, so that entries keep their order.
    QList<std::pair<qsizetype, Column>> columns;
    const QStringList &header = table.value().constFirst();
    for (qsizetype i = 0; i < header.size(); ++i) {
        const QString title = header.at(i).trimmed();
        for (const ColumnTitle &known : columnTitles()) {
            if (title.compare(known.english, Qt::CaseInsensitive) == 0 ||
                title.compare(QString::fromUtf8(known.german), Qt::CaseInsensitive) == 0) {
                columns.append({i, known.column});
                break;
            }
        }
    }
    if (columns.isEmpty())
        return Error(ErrorCode::InvalidInput, QStringLiteral("No known column in the table"));

    QList<Contact> contacts;
    for (qsizetype row = 1; row < table.value().size(); ++row) {
        const QStringList &fields = table.value().at(row);
        Contact contact;
        for (const auto &[index, column] : std::as_const(columns)) {
            if (index < fields.size())
                applyCell(contact, column, fields.at(index));
        }
        if (!isSavable(contact))
            continue;
        if (contacts.size() >= maxInterchangeContacts) {
            return Error(
                ErrorCode::LimitExceeded,
                QStringLiteral("More than %1 contacts in one table").arg(maxInterchangeContacts));
        }
        contacts.append(contact);
    }
    return contacts;
}

} // namespace qpm::core::codec
