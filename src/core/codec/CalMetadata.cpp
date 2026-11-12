#include "core/codec/CalMetadata.h"

#include "core/codec/SafeXml.h"

namespace qpm::core::codec {

namespace {

constexpr quint32 rgbMask = 0x00FFFFFFU;

bool flag(const XmlElement &element, const QString &name)
{
    return element.attribute(name) == QLatin1StringView("1");
}

quint32 hexValue(const QString &text)
{
    bool ok = false;
    const quint32 value = text.trimmed().toUInt(&ok, 16);
    return ok ? value : 0;
}

QString hex6(quint32 color)
{
    return QStringLiteral("%1").arg(color & rgbMask, 6, 16, QLatin1Char('0')).toUpper();
}

void collect(const XmlElement &element, const QString &section, QList<OrganizerCategory> &out)
{
    for (const XmlElement &child : element.children) {
        if (child.name != section) {
            collect(child, section, out);
            continue;
        }
        for (const XmlElement &entry : child.children) {
            if (entry.name != QLatin1StringView("category"))
                continue;
            OrganizerCategory category;
            category.name = entry.attribute(QStringLiteral("name"));
            category.color = hexValue(entry.attribute(QStringLiteral("color"))) & rgbMask;
            category.id = entry.attribute(QStringLiteral("id")).toInt();
            if (!category.name.isEmpty() && category.id >= 1)
                out.append(category);
        }
    }
}

} // namespace

QString xmlAttributeText(const QString &text)
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

QList<OrganizerCategory> categoriesIn(const XmlElement &root, const QString &section)
{
    QList<OrganizerCategory> categories;
    collect(root, section, categories);
    return categories;
}

Result<CalMetadata> parseCalMetadata(const QByteArray &xml)
{
    QByteArray document = xml;
    // The phone closes a calendar list it never opened when it has no calendar.
    if (!document.contains("<calendars") && document.contains("</calendars>"))
        document.replace("</calendars>", "");
    const Result<XmlElement> parsed = parseSafeXml(document);
    if (!parsed.hasValue())
        return parsed.error();
    const XmlElement &root = parsed.value();
    if (root.name != QLatin1StringView("metadata"))
        return Error(ErrorCode::ProtocolError, QStringLiteral("Calendar metadata without root"));

    CalMetadata metadata;
    for (const XmlElement &section : root.children) {
        if (section.name == QLatin1StringView("calendars")) {
            metadata.filterDisabled = flag(section, QStringLiteral("filter_disabled"));
            for (const XmlElement &element : section.children) {
                if (element.name != QLatin1StringView("calendar"))
                    continue;
                CalendarInfo calendar;
                calendar.url = element.attribute(QStringLiteral("url"));
                calendar.title = element.attribute(QStringLiteral("title"));
                calendar.color = hexValue(element.attribute(QStringLiteral("color")));
                calendar.hidden = flag(element, QStringLiteral("hidden"));
                calendar.timezone = element.attribute(QStringLiteral("timezone"));
                calendar.accessLevel = element.attribute(QStringLiteral("access_level")).toInt();
                calendar.selected = flag(element, QStringLiteral("selected"));
                calendar.isDefault = flag(element, QStringLiteral("default"));
                metadata.calendars.append(calendar);
            }
        } else if (section.name == QLatin1StringView("tasksupport")) {
            metadata.taskSupport = section.text.trimmed() == QLatin1StringView("1");
        }
    }
    metadata.taskCategories = categoriesIn(root, QStringLiteral("task_categories"));
    return metadata;
}

QByteArray encodeCalendarSelection(const QList<CalendarInfo> &calendars)
{
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                                 "<metadata version=\"1.0\">\r\n<calendars filter_disabled=\"0\">\r\n");
    for (const CalendarInfo &calendar : calendars) {
        out += QStringLiteral("<calendar selected=\"%1\"").arg(calendar.selected ? 1 : 0);
        if (calendar.isDefault)
            out += QStringLiteral(" default=\"1\"");
        out += QStringLiteral(" url=\"%1\" title=\"%2\" color=\"%3\"/>\r\n")
                   .arg(xmlAttributeText(calendar.url), xmlAttributeText(calendar.title),
                        QStringLiteral("%1").arg(calendar.color, 8, 16, QLatin1Char('0')).toUpper());
    }
    out += QStringLiteral("</calendars>\r\n</metadata>");
    return out.toUtf8();
}

QByteArray encodeCategoryChanges(const QList<CategoryChange> &changes, bool forNotes)
{
    const QString section = forNotes ? QStringLiteral("note_categories")
                                     : QStringLiteral("task_categories");
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                                 "<metadata version=\"1.0\">\r\n<%1>\r\n")
                      .arg(section);
    for (const CategoryChange &change : changes) {
        QString kind;
        switch (change.kind) {
        case CategoryChange::Kind::Add:
            kind = QStringLiteral("add");
            break;
        case CategoryChange::Kind::Edit:
            kind = QStringLiteral("edit");
            break;
        case CategoryChange::Kind::Delete:
            kind = QStringLiteral("delete");
            break;
        }
        out += QStringLiteral("<category name=\"%1\" color=\"%2\" flag=\"%3\"")
                   .arg(xmlAttributeText(change.category.name), hex6(change.category.color), kind);
        if (change.kind != CategoryChange::Kind::Add)
            out += QStringLiteral(" id=\"%1\"").arg(change.category.id);
        out += QStringLiteral("/>\r\n");
    }
    out += QStringLiteral("</%1>\r\n</metadata>").arg(section);
    return out.toUtf8();
}

} // namespace qpm::core::codec
