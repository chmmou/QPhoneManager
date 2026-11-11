#include "core/codec/PhoneVCard.h"

#include "core/codec/AtText.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <utility>

namespace qpm::core::codec {

namespace {

constexpr int luidDigits = 12;

struct TypeToken
{
    int type;
    QLatin1StringView tokens;
};

// Type tokens the phone writes after the property name, per kind.
constexpr std::array phoneTokens = {
    TypeToken{phoneType::home, QLatin1StringView("HOME")},
    TypeToken{phoneType::mobile, QLatin1StringView("CELL")},
    TypeToken{phoneType::work, QLatin1StringView("WORK")},
    TypeToken{phoneType::faxWork, QLatin1StringView("FAX;WORK")},
    TypeToken{phoneType::faxHome, QLatin1StringView("FAX;HOME")},
    TypeToken{phoneType::pager, QLatin1StringView("PAGER")},
    TypeToken{phoneType::callback, QLatin1StringView("CALLBACK")},
    TypeToken{phoneType::car, QLatin1StringView("CAR")},
    TypeToken{phoneType::companyMain, QLatin1StringView("COMPANY")},
    TypeToken{phoneType::isdn, QLatin1StringView("ISDN")},
    TypeToken{phoneType::main, QLatin1StringView("MAIN")},
    TypeToken{phoneType::otherFax, QLatin1StringView("FAX;OTHER")},
    TypeToken{phoneType::radio, QLatin1StringView("RADIO")},
    TypeToken{phoneType::telex, QLatin1StringView("TELEX")},
    TypeToken{phoneType::tty, QLatin1StringView("TTY")},
    TypeToken{phoneType::workMobile, QLatin1StringView("CELL;WORK")},
    TypeToken{phoneType::workPager, QLatin1StringView("PAGER;WORK")},
    TypeToken{phoneType::assistant, QLatin1StringView("ASSISTANT")},
    TypeToken{phoneType::mms, QLatin1StringView("MMS")},
};

constexpr std::array emailTokens = {
    TypeToken{emailType::home, QLatin1StringView("HOME")},
    TypeToken{emailType::work, QLatin1StringView("WORK")},
    TypeToken{emailType::other, QLatin1StringView("OTHER")},
    TypeToken{emailType::mobile, QLatin1StringView("CELL")},
};

constexpr std::array addressTokens = {
    TypeToken{addressType::home, QLatin1StringView("HOME")},
    TypeToken{addressType::work, QLatin1StringView("WORK")},
    TypeToken{addressType::other, QLatin1StringView("OTHER")},
};

constexpr std::array websiteTokens = {
    TypeToken{websiteType::homepage, QLatin1StringView("HOMEPAGE")},
    TypeToken{websiteType::blog, QLatin1StringView("BLOG")},
    TypeToken{websiteType::profile, QLatin1StringView("PROFILE")},
    TypeToken{websiteType::home, QLatin1StringView("HOME")},
    TypeToken{websiteType::work, QLatin1StringView("WORK")},
    TypeToken{websiteType::ftp, QLatin1StringView("FTP")},
    TypeToken{websiteType::other, QLatin1StringView("OTHER")},
};

constexpr std::array eventTokens = {
    TypeToken{eventType::anniversary, QLatin1StringView("ANNIVERSARY")},
    TypeToken{eventType::other, QLatin1StringView("OTHER")},
    TypeToken{eventType::birthday, QLatin1StringView("BIRTHDAY")},
};

constexpr std::array imSchemes = {
    TypeToken{imProtocol::aim, QLatin1StringView("aim")},
    TypeToken{imProtocol::msn, QLatin1StringView("msnim")},
    TypeToken{imProtocol::yahoo, QLatin1StringView("ymsgr")},
    TypeToken{imProtocol::skype, QLatin1StringView("skype")},
    TypeToken{imProtocol::qq, QLatin1StringView("qq")},
    TypeToken{imProtocol::googleTalk, QLatin1StringView("gtalk")},
    TypeToken{imProtocol::icq, QLatin1StringView("icq")},
    TypeToken{imProtocol::jabber, QLatin1StringView("xmpp")},
    TypeToken{imProtocol::netMeeting, QLatin1StringView("netmeet")},
};

template <std::size_t N>
std::optional<int> typeFromTokens(const std::array<TypeToken, N> &table, const QString &tokens)
{
    for (const TypeToken &entry : table) {
        if (tokens == entry.tokens)
            return entry.type;
    }
    return std::nullopt;
}

template <std::size_t N> QString tokensFromType(const std::array<TypeToken, N> &table, int type)
{
    for (const TypeToken &entry : table) {
        if (entry.type == type)
            return entry.tokens;
    }
    return {};
}

// One property line, split the way the phone splits it.
struct Line
{
    QString name;
    QStringList parameters;
    QString value;
    QStringList components;
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

// Splits at ";" that is not preceded by a backslash and turns "\;" back into ";".
QStringList splitComponents(const QString &value)
{
    QStringList parts;
    QString current;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == u'\\' && i + 1 < value.size() && value.at(i + 1) == u';') {
            current.append(u';');
            ++i;
        } else if (c == u';') {
            parts.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    parts.append(current);
    return parts;
}

std::optional<Line> parseLine(const QByteArray &raw)
{
    const qsizetype colon = raw.indexOf(':');
    if (colon <= 0)
        return std::nullopt;
    const QString header = QString::fromUtf8(raw.left(colon));
    QByteArray valueBytes = raw.mid(colon + 1);
    if (header.contains(QLatin1StringView("ENCODING=QUOTED-PRINTABLE"), Qt::CaseInsensitive))
        valueBytes = decodeQuotedPrintable(valueBytes);

    Line line;
    QStringList segments = header.split(u';');
    line.name = segments.takeFirst().trimmed().toUpper();
    line.parameters = segments;
    line.value = QString::fromUtf8(valueBytes);
    line.components = splitComponents(line.value);
    line.value.replace(QLatin1StringView("\\;"), QLatin1StringView(";"));
    return line;
}

// What the parameters of a line say besides the type.
struct Parameters
{
    quint64 dataId = 0;
    bool primary = false;
    QString label;
    bool hasLabel = false;
    QString typeTokens;
    bool deletePhoto = false;
};

Parameters readParameters(const QStringList &parameters)
{
    Parameters result;
    QStringList tokens;
    for (const QString &parameter : parameters) {
        const QString upper = parameter.trimmed().toUpper();
        if (upper.isEmpty())
            continue;
        if (upper.startsWith(QLatin1StringView("X-SYNCMLREF"))) {
            result.dataId = upper.mid(11).toULongLong();
        } else if (upper == QLatin1StringView("PREF")) {
            result.primary = true;
        } else if (upper.startsWith(QLatin1StringView("LABEL="))) {
            result.label = decodeQp(parameter.trimmed().mid(6));
            result.hasLabel = true;
        } else if (upper.startsWith(QLatin1StringView("ENCODING=")) ||
                   upper.startsWith(QLatin1StringView("CHARSET=")) ||
                   upper.startsWith(QLatin1StringView("TYPE="))) {
            continue;
        } else if (upper.startsWith(QLatin1StringView("ACTION=DELETE"))) {
            result.deletePhoto = true;
        } else {
            tokens.append(upper);
        }
    }
    result.typeTokens = tokens.join(u';');
    return result;
}

QString isoFromCompactDate(const QString &text)
{
    const QDate date = QDate::fromString(text.trimmed(), QStringLiteral("yyyyMMdd"));
    return date.isValid() ? date.toString(Qt::ISODate) : text.trimmed();
}

QDate dateFromCompact(const QString &text)
{
    return QDate::fromString(text.trimmed(), QStringLiteral("yyyyMMdd"));
}

std::optional<quint32> parseLuid(const QString &text)
{
    const QString digits = text.trimmed();
    if (digits.isEmpty() || digits.size() > luidDigits)
        return std::nullopt;
    bool ok = false;
    const qulonglong value = digits.toULongLong(&ok, 16);
    if (!ok || value > 0xFFFFFFFFULL)
        return std::nullopt;
    return quint32(value);
}

Error tooMany(const QString &what)
{
    return Error(ErrorCode::LimitExceeded,
                 QStringLiteral("More than %1 entries of %2").arg(maxValuesPerKind).arg(what));
}

// ---- writing ----

QString escaped(const QString &text)
{
    QString out = text;
    out.replace(u';', QLatin1StringView("\\;"));
    return out;
}

// Encodes a value the way the phone does: control characters and "=" as
// quoted-printable, everything else as raw UTF-8, with the parameters that
// say so.
QByteArray encodeLine(const QString &header, const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    QByteArray out;
    out.reserve(bytes.size());
    bool quoted = false;
    bool utf8 = false;
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == '=') {
            out.append('=');
            out.append(QByteArray::number(byte, 16).rightJustified(2, '0').toUpper());
            quoted = true;
        } else {
            out.append(c);
            utf8 = utf8 || byte > 0x7F;
        }
    }
    QByteArray line = header.toUtf8();
    if (quoted)
        line += ";ENCODING=QUOTED-PRINTABLE";
    if (utf8)
        line += ";CHARSET=UTF-8";
    return line + ':' + out + "\r\n";
}

QString labelParameter(const QString &label)
{
    return QStringLiteral(";LABEL=") + encodeQp(label);
}

QString compactDate(const QString &isoText)
{
    const QDate date = QDate::fromString(isoText, Qt::ISODate);
    return date.isValid() ? date.toString(QStringLiteral("yyyyMMdd")) : isoText;
}

QString propertyName(ContactValueKind kind)
{
    switch (kind) {
    case ContactValueKind::Phone:
        return QStringLiteral("TEL");
    case ContactValueKind::Email:
        return QStringLiteral("EMAIL");
    case ContactValueKind::Address:
        return QStringLiteral("ADR");
    case ContactValueKind::Website:
        return QStringLiteral("URL");
    case ContactValueKind::Im:
        return QStringLiteral("IMPP");
    case ContactValueKind::Event:
        break;
    }
    return QStringLiteral("X-EVENT");
}

QString kindName(ContactValueKind kind)
{
    switch (kind) {
    case ContactValueKind::Phone:
        return QStringLiteral("phone numbers");
    case ContactValueKind::Email:
        return QStringLiteral("mail addresses");
    case ContactValueKind::Address:
        return QStringLiteral("addresses");
    case ContactValueKind::Website:
        return QStringLiteral("websites");
    case ContactValueKind::Im:
        return QStringLiteral("messenger addresses");
    case ContactValueKind::Event:
        break;
    }
    return QStringLiteral("dates");
}

// The type part of the header, written so that the phone's prefix match finds
// it: the type token directly after the property name.
QString typeParameters(ContactValueKind kind, const ContactValue &entry)
{
    if (kind == ContactValueKind::Im)
        return entry.type == imProtocol::custom ? labelParameter(entry.label) : QString();
    if (entry.type == 0)
        return labelParameter(entry.label);
    QString tokens;
    switch (kind) {
    case ContactValueKind::Phone:
        tokens = tokensFromType(phoneTokens, entry.type);
        break;
    case ContactValueKind::Email:
        tokens = tokensFromType(emailTokens, entry.type);
        break;
    case ContactValueKind::Address:
        tokens = tokensFromType(addressTokens, entry.type);
        break;
    case ContactValueKind::Website:
        tokens = tokensFromType(websiteTokens, entry.type);
        break;
    case ContactValueKind::Event:
        tokens = tokensFromType(eventTokens, entry.type);
        break;
    case ContactValueKind::Im:
        break;
    }
    return tokens.isEmpty() ? QString() : u';' + tokens;
}

QString entryValue(ContactValueKind kind, const ContactValue &entry)
{
    switch (kind) {
    case ContactValueKind::Address: {
        QStringList parts = entry.address;
        while (parts.size() < addressPart::count)
            parts.append(QString());
        parts = parts.mid(0, addressPart::count);
        for (QString &part : parts)
            part = escaped(part);
        return parts.join(u';');
    }
    case ContactValueKind::Im: {
        const QString scheme = tokensFromType(imSchemes, entry.type);
        return scheme + u':' + escaped(entry.value);
    }
    case ContactValueKind::Event:
        return compactDate(entry.value);
    case ContactValueKind::Phone:
    case ContactValueKind::Email:
    case ContactValueKind::Website:
        break;
    }
    return escaped(entry.value);
}

QByteArray entryLine(ContactValueKind kind, const ContactValue &entry, quint64 dataId, bool primary)
{
    QString header = propertyName(kind) + typeParameters(kind, entry);
    if (primary)
        header += QStringLiteral(";PREF");
    if (dataId != 0)
        header += QStringLiteral(";X-SYNCMLREF%1").arg(dataId);
    return encodeLine(header, entryValue(kind, entry));
}

QByteArray deleteLine(ContactValueKind kind, quint64 dataId)
{
    // The phone reads a postal address only with all seven parts.
    const QString value = kind == ContactValueKind::Address ? QStringLiteral(";;;;;;") : QString();
    return encodeLine(propertyName(kind) + QStringLiteral(";X-SYNCMLREF%1").arg(dataId), value);
}

QString composedDisplayName(const Contact &contact)
{
    QStringList parts;
    for (const QString &part : {contact.prefix, contact.givenName, contact.middleName,
                                contact.familyName, contact.suffix}) {
        if (!part.trimmed().isEmpty())
            parts.append(part.trimmed());
    }
    return parts.join(u' ');
}

QByteArray nameLines(const Contact &contact)
{
    QStringList parts = {contact.familyName, contact.givenName, contact.middleName, contact.prefix,
                         contact.suffix};
    for (QString &part : parts)
        part = escaped(part);
    QString display = contact.displayName.trimmed();
    if (display.isEmpty())
        display = composedDisplayName(contact);
    return encodeLine(QStringLiteral("N"), parts.join(u';')) +
           encodeLine(QStringLiteral("FN"), escaped(display));
}

QByteArray photoLine(const QByteArray &photo)
{
    const bool png = photo.startsWith("\x89PNG");
    return QByteArray("PHOTO;ENCODING=BASE64;TYPE=") + (png ? "PNG" : "JPEG") + ':' +
           photo.toBase64() + "\r\n\r\n";
}

QString groupsValue(const QStringList &groups)
{
    QStringList parts;
    for (const QString &group : groups)
        parts.append(escaped(group));
    return parts.join(u';');
}

Result<void> checkPhoto(const QByteArray &photo)
{
    if (photo.size() > maxPhotoBytesToSend) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("Photo of %1 bytes, the phone takes up to %2")
                         .arg(photo.size())
                         .arg(maxPhotoBytesToSend));
    }
    return {};
}

Result<void> checkCount(ContactValueKind kind, qsizetype count)
{
    if (count > maxValuesPerWrite) {
        return Error(ErrorCode::LimitExceeded, QStringLiteral("%1 %2, the phone takes up to %3")
                                                   .arg(count)
                                                   .arg(kindName(kind))
                                                   .arg(maxValuesPerWrite));
    }
    return {};
}

bool sameEntry(const ContactValue &a, const ContactValue &b)
{
    return a.type == b.type && a.label == b.label && a.value == b.value && a.address == b.address &&
           a.primary == b.primary;
}

QStringList sortedGroups(QStringList groups)
{
    groups.sort();
    return groups;
}

} // namespace

bool ContactValue::isEmpty() const
{
    if (!value.trimmed().isEmpty())
        return false;
    return std::all_of(address.cbegin(), address.cend(),
                       [](const QString &part) { return part.trimmed().isEmpty(); });
}

const QList<ContactValue> &Contact::values(ContactValueKind kind) const
{
    switch (kind) {
    case ContactValueKind::Phone:
        return phones;
    case ContactValueKind::Email:
        return emails;
    case ContactValueKind::Address:
        return addresses;
    case ContactValueKind::Website:
        return websites;
    case ContactValueKind::Im:
        return ims;
    case ContactValueKind::Event:
        break;
    }
    return events;
}

QList<ContactValue> &Contact::values(ContactValueKind kind)
{
    // Removing const is safe here: this overload only runs on a non-const
    // contact, and it saves keeping the kind switch twice.
    return const_cast<QList<ContactValue> &>(std::as_const(*this).values(kind));
}

QString displayNameOf(const Contact &contact)
{
    if (!contact.displayName.trimmed().isEmpty())
        return contact.displayName.trimmed();
    const QString composed = composedDisplayName(contact);
    if (!composed.isEmpty())
        return composed;
    if (!contact.organization.trimmed().isEmpty())
        return contact.organization.trimmed();
    for (const ContactValue &phone : contact.phones) {
        if (!phone.value.trimmed().isEmpty())
            return phone.value.trimmed();
    }
    for (const ContactValue &email : contact.emails) {
        if (!email.value.trimmed().isEmpty())
            return email.value.trimmed();
    }
    return {};
}

QString sortKeyOf(const Contact &contact)
{
    // Decomposition drops accents, so that "Ä" sorts with "A".
    const QString name = displayNameOf(contact).normalized(QString::NormalizationForm_KD);
    QString key;
    key.reserve(name.size());
    for (const QChar c : name) {
        if (c.category() != QChar::Mark_NonSpacing)
            key.append(c.toLower());
    }
    return key;
}

bool sameContent(const Contact &a, const Contact &b)
{
    Contact left = a;
    Contact right = b;
    left.photo = a.photo.isEmpty() ? QByteArray() : QByteArrayLiteral("photo");
    right.photo = b.photo.isEmpty() ? QByteArray() : QByteArrayLiteral("photo");
    left.groups = sortedGroups(left.groups);
    right.groups = sortedGroups(right.groups);
    return left == right;
}

bool isSavable(const Contact &contact)
{
    if (!contact.displayName.trimmed().isEmpty() || !composedDisplayName(contact).isEmpty() ||
        !contact.organization.trimmed().isEmpty()) {
        return true;
    }
    const auto hasText = [](const QList<ContactValue> &values) {
        return std::any_of(values.cbegin(), values.cend(),
                           [](const ContactValue &entry) { return !entry.isEmpty(); });
    };
    return hasText(contact.phones) || hasText(contact.emails);
}

Result<Contact> parsePhoneVCard(const QByteArray &data)
{
    QList<QByteArray> lines = data.split('\n');
    for (QByteArray &line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
    }

    Contact contact;
    bool begun = false;
    bool ended = false;
    bool nameSeen = false;
    bool nicknameSeen = false;
    bool organizationSeen = false;
    bool titleSeen = false;
    bool noteSeen = false;
    for (const QByteArray &raw : std::as_const(lines)) {
        const std::optional<Line> parsed = parseLine(raw);
        if (!parsed)
            continue;
        const Line &line = *parsed;
        if (line.name == QLatin1StringView("BEGIN")) {
            begun =
                line.value.trimmed().compare(QLatin1StringView("VCARD"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!begun)
            continue;
        if (line.name == QLatin1StringView("END")) {
            ended = true;
            break;
        }
        const Parameters parameters = readParameters(line.parameters);

        ContactValueKind kind = ContactValueKind::Phone;
        ContactValue entry;
        entry.dataId = parameters.dataId;
        entry.primary = parameters.primary;
        entry.label = parameters.label;
        bool isEntry = true;

        if (line.name == QLatin1StringView("N")) {
            isEntry = false;
            if (nameSeen)
                continue;
            nameSeen = true;
            const QStringList &parts = line.components;
            contact.familyName = parts.value(0);
            contact.givenName = parts.value(1);
            contact.middleName = parts.value(2);
            contact.prefix = parts.value(3);
            contact.suffix = parts.value(4);
        } else if (line.name == QLatin1StringView("FN")) {
            isEntry = false;
            contact.displayName = line.value;
        } else if (line.name == QLatin1StringView("NICKNAME")) {
            isEntry = false;
            if (!nicknameSeen) {
                nicknameSeen = true;
                contact.nickname = line.value;
                contact.nicknameId = parameters.dataId;
            }
        } else if (line.name == QLatin1StringView("ORG")) {
            isEntry = false;
            if (!organizationSeen) {
                organizationSeen = true;
                contact.organization = line.components.value(0);
                contact.department = line.components.mid(1).join(u';');
            }
        } else if (line.name == QLatin1StringView("TITLE")) {
            isEntry = false;
            if (!titleSeen) {
                titleSeen = true;
                contact.title = line.value;
            }
        } else if (line.name == QLatin1StringView("NOTE")) {
            isEntry = false;
            if (!noteSeen) {
                noteSeen = true;
                contact.note = line.value;
            }
        } else if (line.name == QLatin1StringView("TEL")) {
            kind = ContactValueKind::Phone;
            entry.type = parameters.hasLabel
                             ? phoneType::custom
                             : typeFromTokens(phoneTokens, parameters.typeTokens)
                                   .value_or(parameters.typeTokens == QLatin1StringView("FAX")
                                                 ? phoneType::faxWork
                                                 : phoneType::other);
            entry.value = line.value;
        } else if (line.name == QLatin1StringView("EMAIL")) {
            kind = ContactValueKind::Email;
            entry.type =
                parameters.hasLabel
                    ? emailType::custom
                    : typeFromTokens(emailTokens, parameters.typeTokens).value_or(emailType::other);
            entry.value = line.value;
        } else if (line.name == QLatin1StringView("ADR")) {
            kind = ContactValueKind::Address;
            entry.type = parameters.hasLabel ? addressType::custom
                                             : typeFromTokens(addressTokens, parameters.typeTokens)
                                                   .value_or(addressType::other);
            entry.address = line.components.mid(0, addressPart::count);
            while (entry.address.size() < addressPart::count)
                entry.address.append(QString());
            // The phone writes a single space for an empty post office box.
            if (entry.address.at(addressPart::poBox) == QLatin1StringView(" "))
                entry.address[addressPart::poBox].clear();
        } else if (line.name == QLatin1StringView("URL")) {
            kind = ContactValueKind::Website;
            entry.type = parameters.hasLabel ? websiteType::custom
                                             : typeFromTokens(websiteTokens, parameters.typeTokens)
                                                   .value_or(websiteType::other);
            entry.value = line.value;
        } else if (line.name == QLatin1StringView("IMPP")) {
            kind = ContactValueKind::Im;
            const qsizetype colon = line.value.indexOf(u':');
            const QString scheme =
                colon < 0 ? QString() : line.value.left(colon).trimmed().toLower();
            entry.value = colon < 0 ? line.value : line.value.mid(colon + 1);
            entry.type = parameters.hasLabel
                             ? imProtocol::custom
                             : typeFromTokens(imSchemes, scheme).value_or(imProtocol::custom);
        } else if (line.name == QLatin1StringView("X-EVENT")) {
            kind = ContactValueKind::Event;
            entry.type =
                parameters.hasLabel
                    ? eventType::custom
                    : typeFromTokens(eventTokens, parameters.typeTokens).value_or(eventType::other);
            entry.value = isoFromCompactDate(line.value);
        } else if (line.name == QLatin1StringView("BDAY")) {
            isEntry = false;
            if (!contact.birthday.isValid())
                contact.birthday = dateFromCompact(line.value);
        } else if (line.name == QLatin1StringView("X-ANNIVERSARY")) {
            isEntry = false;
            if (!contact.anniversary.isValid())
                contact.anniversary = dateFromCompact(line.value);
        } else if (line.name == QLatin1StringView("CATEGORIES")) {
            isEntry = false;
            for (const QString &group : line.components) {
                if (!group.trimmed().isEmpty() && !contact.groups.contains(group))
                    contact.groups.append(group);
            }
        } else if (line.name == QLatin1StringView("PHOTO")) {
            isEntry = false;
            if (parameters.deletePhoto)
                continue;
            QByteArray base64 = line.value.toLatin1();
            base64.replace(' ', QByteArray());
            if (base64.size() / 4 * 3 > maxPhotoBytes) {
                return Error(ErrorCode::LimitExceeded,
                             QStringLiteral("Photo larger than %1 bytes").arg(maxPhotoBytes));
            }
            const auto decoded = QByteArray::fromBase64Encoding(base64);
            if (decoded)
                contact.photo = *decoded;
        } else if (line.name == QLatin1StringView("X-IRMC-LUID")) {
            isEntry = false;
            contact.luid = parseLuid(line.value).value_or(0);
        } else if (line.name == QLatin1StringView("X-RINGTONE")) {
            isEntry = false;
            contact.ringtone = line.value;
        } else if (line.name == QLatin1StringView("X-ACCOUNT")) {
            isEntry = false;
            contact.accountType = line.components.value(0);
            contact.accountName = line.components.mid(1).join(u';');
        } else {
            isEntry = false;
        }

        if (!isEntry)
            continue;
        QList<ContactValue> &list = contact.values(kind);
        if (list.size() >= maxValuesPerKind)
            return tooMany(kindName(kind));
        list.append(entry);
    }
    if (!begun || !ended)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Not a complete vCard"));
    return contact;
}

Result<QByteArray> encodeNewContact(const Contact &contact)
{
    if (!isSavable(contact)) {
        return Error(
            ErrorCode::InvalidInput,
            QStringLiteral("A contact needs a name, a number, a mail address or a company"));
    }
    if (const Result<void> photo = checkPhoto(contact.photo); !photo.hasValue())
        return photo.error();

    QByteArray out = "BEGIN:VCARD\r\n" + nameLines(contact);
    if (!contact.nickname.isEmpty())
        out += encodeLine(QStringLiteral("NICKNAME"), escaped(contact.nickname));
    if (!contact.organization.isEmpty() || !contact.department.isEmpty()) {
        out += encodeLine(QStringLiteral("ORG"),
                          escaped(contact.organization) + u';' + escaped(contact.department));
    }
    if (!contact.title.isEmpty())
        out += encodeLine(QStringLiteral("TITLE"), escaped(contact.title));
    if (!contact.note.isEmpty())
        out += encodeLine(QStringLiteral("NOTE"), escaped(contact.note));

    for (const ContactValueKind kind : allContactValueKinds) {
        qsizetype count = 0;
        for (const ContactValue &entry : contact.values(kind)) {
            if (entry.isEmpty())
                continue;
            ++count;
            out += entryLine(kind, entry, 0, entry.primary);
        }
        if (const Result<void> checked = checkCount(kind, count); !checked.hasValue())
            return checked.error();
    }
    if (contact.birthday.isValid())
        out += encodeLine(QStringLiteral("BDAY"), contact.birthday.toString(u"yyyyMMdd"));
    if (contact.anniversary.isValid())
        out +=
            encodeLine(QStringLiteral("X-ANNIVERSARY"), contact.anniversary.toString(u"yyyyMMdd"));
    if (!contact.groups.isEmpty())
        out += encodeLine(QStringLiteral("CATEGORIES"), groupsValue(contact.groups));
    if (!contact.ringtone.isEmpty())
        out += encodeLine(QStringLiteral("X-RINGTONE"), contact.ringtone);
    if (!contact.accountType.isEmpty() || !contact.accountName.isEmpty()) {
        out += encodeLine(QStringLiteral("X-ACCOUNT"),
                          escaped(contact.accountType) + u';' + escaped(contact.accountName));
    }
    if (!contact.photo.isEmpty())
        out += photoLine(contact.photo);
    out += "END:VCARD\r\n";
    return out;
}

Result<QByteArray> encodeContactUpdate(const Contact &phone, const Contact &edited)
{
    if (!isSavable(edited)) {
        return Error(
            ErrorCode::InvalidInput,
            QStringLiteral("A contact needs a name, a number, a mail address or a company"));
    }

    QByteArray out = "BEGIN:VCARD\r\n" + nameLines(edited);
    if (edited.nickname != phone.nickname) {
        const quint64 id = phone.nicknameId;
        out += encodeLine(id ? QStringLiteral("NICKNAME;X-SYNCMLREF%1").arg(id)
                             : QStringLiteral("NICKNAME"),
                          escaped(edited.nickname));
    }
    if (edited.organization != phone.organization || edited.department != phone.department) {
        out += encodeLine(QStringLiteral("ORG"),
                          escaped(edited.organization) + u';' + escaped(edited.department));
    }
    if (edited.title != phone.title)
        out += encodeLine(QStringLiteral("TITLE"), escaped(edited.title));
    if (edited.note != phone.note)
        out += encodeLine(QStringLiteral("NOTE"), escaped(edited.note));

    for (const ContactValueKind kind : allContactValueKinds) {
        QHash<quint64, ContactValue> known;
        for (const ContactValue &entry : phone.values(kind)) {
            if (entry.dataId != 0)
                known.insert(entry.dataId, entry);
        }
        QSet<quint64> kept;
        QByteArray lines;
        qsizetype count = 0;
        for (const ContactValue &entry : edited.values(kind)) {
            const auto existing = known.constFind(entry.dataId);
            if (entry.dataId != 0 && existing != known.cend()) {
                kept.insert(entry.dataId);
                if (entry.isEmpty()) {
                    lines += deleteLine(kind, entry.dataId);
                    ++count;
                } else if (!sameEntry(entry, existing.value())) {
                    lines += entryLine(kind, entry, entry.dataId, entry.primary);
                    ++count;
                }
            } else if (!entry.isEmpty()) {
                lines += entryLine(kind, entry, 0, entry.primary);
                ++count;
            }
        }
        for (const ContactValue &entry : phone.values(kind)) {
            if (entry.dataId != 0 && !kept.contains(entry.dataId)) {
                lines += deleteLine(kind, entry.dataId);
                ++count;
            }
        }
        if (const Result<void> checked = checkCount(kind, count); !checked.hasValue())
            return checked.error();
        out += lines;
    }

    if (edited.birthday != phone.birthday) {
        out += encodeLine(QStringLiteral("BDAY"), edited.birthday.isValid()
                                                      ? edited.birthday.toString(u"yyyyMMdd")
                                                      : QString());
    }
    if (edited.anniversary != phone.anniversary) {
        out += encodeLine(QStringLiteral("X-ANNIVERSARY"),
                          edited.anniversary.isValid() ? edited.anniversary.toString(u"yyyyMMdd")
                                                       : QString());
    }
    if (sortedGroups(edited.groups) != sortedGroups(phone.groups))
        out += encodeLine(QStringLiteral("CATEGORIES"), groupsValue(edited.groups));
    if (edited.ringtone != phone.ringtone)
        out += encodeLine(QStringLiteral("X-RINGTONE"), edited.ringtone);
    if (edited.photo != phone.photo) {
        if (edited.photo.isEmpty()) {
            out += "PHOTO;ACTION=DELETE:\r\n";
        } else {
            if (const Result<void> photo = checkPhoto(edited.photo); !photo.hasValue())
                return photo.error();
            out += photoLine(edited.photo);
        }
    }
    out += "END:VCARD\r\n";
    return out;
}

QByteArray encodeAsPhoneSendsIt(const Contact &contact)
{
    QByteArray out = "BEGIN:VCARD\r\n";
    const bool hasName = !composedDisplayName(contact).isEmpty();
    if (hasName) {
        QStringList parts = {contact.familyName, contact.givenName, contact.middleName,
                             contact.prefix, contact.suffix};
        for (QString &part : parts)
            part = escaped(part);
        out += encodeLine(QStringLiteral("N"), parts.join(u';'));
    } else {
        out += "N:;\r\n";
    }
    out += encodeLine(QStringLiteral("FN"), escaped(contact.displayName));
    if (!contact.nickname.isEmpty()) {
        out += encodeLine(QStringLiteral("NICKNAME;X-SYNCMLREF%1").arg(contact.nicknameId),
                          contact.nickname);
    }
    if (!contact.organization.isEmpty() || !contact.department.isEmpty()) {
        out += encodeLine(QStringLiteral("ORG"),
                          contact.department.isEmpty()
                              ? escaped(contact.organization)
                              : escaped(contact.organization) + u';' + escaped(contact.department));
    }
    if (!contact.title.isEmpty())
        out += encodeLine(QStringLiteral("TITLE"), contact.title);
    if (!contact.note.isEmpty())
        out += encodeLine(QStringLiteral("NOTE"), contact.note);

    const bool phonePreferred = contact.phones.size() > 1;
    for (const ContactValueKind kind : allContactValueKinds) {
        bool first = true;
        for (const ContactValue &entry : contact.values(kind)) {
            bool primary = false;
            if (kind == ContactValueKind::Phone)
                primary = phonePreferred && entry.primary;
            else if (kind == ContactValueKind::Email)
                primary = first;
            out += entryLine(kind, entry, entry.dataId, primary);
            first = false;
        }
        if (kind == ContactValueKind::Address) {
            if (contact.birthday.isValid())
                out += encodeLine(QStringLiteral("BDAY"), contact.birthday.toString(u"yyyyMMdd"));
            if (contact.anniversary.isValid()) {
                out += encodeLine(QStringLiteral("X-ANNIVERSARY"),
                                  contact.anniversary.toString(u"yyyyMMdd"));
            }
        }
    }
    if (!contact.groups.isEmpty())
        out += encodeLine(QStringLiteral("CATEGORIES"), groupsValue(contact.groups));
    if (!contact.photo.isEmpty())
        out += photoLine(contact.photo);
    out += "X-IRMC-LUID:" +
           QByteArray::number(contact.luid, 16).toUpper().rightJustified(luidDigits, '0') + "\r\n";
    if (!contact.ringtone.isEmpty())
        out += encodeLine(QStringLiteral("X-RINGTONE"), contact.ringtone);
    if (!contact.accountType.isEmpty() || !contact.accountName.isEmpty()) {
        out += encodeLine(QStringLiteral("X-ACCOUNT"),
                          escaped(contact.accountType) + u';' + escaped(contact.accountName));
    }
    out += "END:VCARD\r\n";
    return out;
}

} // namespace qpm::core::codec
