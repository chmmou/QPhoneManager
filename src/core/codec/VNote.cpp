#include "core/codec/VNote.h"

#include "core/codec/ContentLines.h"
#include "core/codec/PhoneTime.h"
#include "core/codec/SafeXml.h"

#include <QTimeZone>

#include <algorithm>

namespace qpm::core::codec {

namespace {

constexpr int luidDigits = 12;
constexpr quint32 rgbMask = 0x00FFFFFFU;

QString hex(quint32 value, int digits)
{
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QByteArray categoriesLine(const QStringList &categories,
                          const QList<std::optional<quint32>> &colors)
{
    if (categories.isEmpty())
        return {};
    QString header = QStringLiteral("CATEGORIES");
    const bool allColored =
        colors.size() == categories.size() &&
        std::all_of(colors.cbegin(), colors.cend(),
                    [](const std::optional<quint32> &color) { return color.has_value(); });
    if (allColored) {
        QStringList texts;
        for (const std::optional<quint32> &color : colors)
            texts.append(hex(*color & rgbMask, 6));
        // The phone reads this parameter only as the first one.
        header += QStringLiteral(";COLOR=") + texts.join(u',');
    }
    return encodePhoneLine(header, joinEscapedList(categories));
}

} // namespace

Result<Note> parsePhoneNote(const QByteArray &data)
{
    const QList<ContentLine> lines = readContentLines(data);
    Note note;
    bool inside = false;
    bool found = false;
    bool bodySeen = false;
    for (const ContentLine &line : lines) {
        const QString value = line.value.trimmed().toUpper();
        if (line.name == QLatin1StringView("BEGIN") && value == QLatin1StringView("VNOTE")) {
            inside = true;
            found = true;
            continue;
        }
        if (line.name == QLatin1StringView("END") && value == QLatin1StringView("VNOTE"))
            break;
        if (!inside)
            continue;
        if (line.name == QLatin1StringView("BODY") && !bodySeen && !line.value.isEmpty()) {
            note.body = line.value;
            bodySeen = true;
        } else if (line.name == QLatin1StringView("LAST-MODIFIED")) {
            const QString stamp = line.value.trimmed().toUpper();
            if (const std::optional<QDateTime> time = parseUtcStamp(stamp))
                note.lastModified = *time;
            else if (const std::optional<QDate> date = parseDate(stamp))
                note.lastModified = QDateTime(*date, QTime(0, 0), QTimeZone::UTC);
        } else if (line.name == QLatin1StringView("CATEGORIES")) {
            const QStringList names = splitEscapedList(line.value);
            for (const QString &name : names) {
                if (!name.trimmed().isEmpty())
                    note.categories.append(name.trimmed());
            }
        } else if (line.name == QLatin1StringView("X-IRMC-LUID")) {
            bool ok = false;
            const QString digits = line.value.trimmed();
            const qulonglong luid = digits.size() <= luidDigits ? digits.toULongLong(&ok, 16) : 0;
            note.luid = ok && luid <= 0xFFFFFFFFULL ? quint32(luid) : 0;
        }
    }
    if (!found)
        return Error(ErrorCode::ProtocolError, QStringLiteral("No VNOTE in the object"));
    return note;
}

QByteArray encodePhoneNote(const Note &note, const QList<std::optional<quint32>> &categoryColors)
{
    QByteArray out = "BEGIN:VNOTE\r\n";
    if (!note.body.isEmpty())
        out += encodePhoneLine(QStringLiteral("BODY"), note.body);
    out += categoriesLine(note.categories, categoryColors);
    out += "END:VNOTE\r\n";
    return out;
}

QByteArray encodeNoteAsPhoneSendsIt(const Note &note)
{
    QByteArray out = "BEGIN:VNOTE\r\n";
    if (!note.body.isEmpty())
        out += encodePhoneLine(QStringLiteral("BODY"), note.body);
    if (note.lastModified.isValid())
        out += "LAST-MODIFIED:" + formatUtcStamp(note.lastModified).toLatin1() + "\r\n";
    if (!note.categories.isEmpty())
        out += encodePhoneLine(QStringLiteral("CATEGORIES"), joinEscapedList(note.categories));
    out += "X-IRMC-LUID:" + hex(note.luid, luidDigits).toLatin1() + "\r\n";
    out += "END:VNOTE";
    return out;
}

bool sameNoteContent(const Note &a, const Note &b)
{
    return a.body == b.body && a.categories == b.categories;
}

Result<QList<OrganizerCategory>> parseNoteMetadata(const QByteArray &xml)
{
    const Result<XmlElement> parsed = parseSafeXml(xml);
    if (!parsed.hasValue())
        return parsed.error();
    if (parsed.value().name != QLatin1StringView("metadata"))
        return Error(ErrorCode::ProtocolError, QStringLiteral("Note metadata without root"));
    return categoriesIn(parsed.value(), QStringLiteral("note_categories"));
}

} // namespace qpm::core::codec
