#include "core/codec/PbMetadata.h"

#include "core/codec/SafeXml.h"

namespace qpm::core::codec {

namespace {

bool flag(const XmlElement &element, const QString &name)
{
    return element.attribute(name) == QLatin1StringView("1");
}

QString xmlEscaped(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'&':
            out += QLatin1StringView("&amp;");
            break;
        case u'<':
            out += QLatin1StringView("&lt;");
            break;
        case u'>':
            out += QLatin1StringView("&gt;");
            break;
        case u'"':
            out += QLatin1StringView("&quot;");
            break;
        default:
            out += c;
        }
    }
    return out;
}

} // namespace

Result<PbMetadata> parsePbMetadata(const QByteArray &xml)
{
    const Result<XmlElement> parsed = parseSafeXml(xml);
    if (!parsed.hasValue())
        return parsed.error();
    const XmlElement &root = parsed.value();
    if (root.name != QLatin1StringView("metadata"))
        return Error(ErrorCode::ProtocolError, QStringLiteral("Phonebook metadata without root"));

    PbMetadata metadata;
    for (const XmlElement &section : root.children) {
        if (section.name == QLatin1StringView("accounts")) {
            metadata.filterDisabled = flag(section, QStringLiteral("filter_disabled"));
            for (const XmlElement &element : section.children) {
                if (element.name != QLatin1StringView("account"))
                    continue;
                PbAccount account;
                account.type = element.attribute(QStringLiteral("type"));
                account.name = element.attribute(QStringLiteral("name"));
                account.title = element.attribute(QStringLiteral("title"));
                account.selected = flag(element, QStringLiteral("selected"));
                account.isDefault = flag(element, QStringLiteral("default"));
                metadata.accounts.append(account);
            }
        } else if (section.name == QLatin1StringView("groups")) {
            for (const XmlElement &element : section.children) {
                if (element.name != QLatin1StringView("group"))
                    continue;
                PbGroup group;
                group.title = element.attribute(QStringLiteral("title"));
                group.sourceId = element.attribute(QStringLiteral("sourceid"));
                group.accountType = element.attribute(QStringLiteral("account_type"));
                group.accountName = element.attribute(QStringLiteral("account_name"));
                if (!group.title.isEmpty())
                    metadata.groups.append(group);
            }
        } else if (section.name == QLatin1StringView("ringtones")) {
            for (const XmlElement &element : section.children) {
                if (element.name != QLatin1StringView("ringtone") ||
                    element.attribute(QStringLiteral("id")).isEmpty()) {
                    continue;
                }
                PbRingtone ringtone;
                ringtone.id = element.attribute(QStringLiteral("id"));
                ringtone.title = element.attribute(QStringLiteral("title"));
                ringtone.isDefault = flag(element, QStringLiteral("default_ringtone"));
                metadata.ringtones.append(ringtone);
            }
        }
    }
    return metadata;
}

QByteArray encodePbMetadata(const PbMetadata &metadata)
{
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                                 "<metadata version=\"1.0\">\r\n");
    out += QStringLiteral("<accounts filter_disabled=\"%1\">\r\n")
               .arg(metadata.filterDisabled ? 1 : 0);
    for (const PbAccount &account : metadata.accounts) {
        out += QStringLiteral("<account selected=\"%1\" type=\"%2\" name=\"%3\"")
                   .arg(account.selected ? 1 : 0)
                   .arg(xmlEscaped(account.type), xmlEscaped(account.name));
        if (account.isDefault)
            out += QStringLiteral("  default=\"1\"");
        out += QStringLiteral("  title=\"%1\"/>\r\n").arg(xmlEscaped(account.title));
    }
    out += QStringLiteral("</accounts>\r\n</metadata>");
    return out.toUtf8();
}

} // namespace qpm::core::codec
