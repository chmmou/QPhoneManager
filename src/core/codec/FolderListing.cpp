#include "core/codec/FolderListing.h"

#include "core/codec/Limits.h"
#include "core/codec/PhoneTime.h"

#include <QHash>

#include <algorithm>

namespace qpm::core::codec {

namespace {

struct Tag
{
    QString name;
    QHash<QString, QString> attributes;
};

bool isSpace(QChar c)
{
    return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n';
}

// The phone escapes "&" and the quote and nothing else.
QString decoded(QStringView value)
{
    static constexpr QStringView amp(u"&amp;");
    static constexpr QStringView quot(u"&quot;");
    QString result;
    result.reserve(value.size());
    qsizetype i = 0;
    while (i < value.size()) {
        const QStringView rest = value.sliced(i);
        if (rest.startsWith(amp)) {
            result += u'&';
            i += amp.size();
        } else if (rest.startsWith(quot)) {
            result += u'"';
            i += quot.size();
        } else {
            result += value.at(i);
            ++i;
        }
    }
    return result;
}

Error truncated()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Folder listing ends inside a tag"));
}

// Reads the element tag that starts at `start` with "<"; `end` gets the
// position after its ">".
Result<Tag> readTag(const QString &text, qsizetype start, qsizetype &end)
{
    Tag tag;
    qsizetype i = start + 1;
    while (i < text.size() && !isSpace(text.at(i)) && text.at(i) != u'/' && text.at(i) != u'>')
        ++i;
    tag.name = text.mid(start + 1, i - start - 1);
    while (true) {
        while (i < text.size() && isSpace(text.at(i)))
            ++i;
        if (i >= text.size())
            return truncated();
        if (text.at(i) == u'>') {
            end = i + 1;
            return tag;
        }
        if (text.at(i) == u'/') {
            if (i + 1 >= text.size() || text.at(i + 1) != u'>')
                return Error(ErrorCode::InvalidInput,
                             QStringLiteral("Malformed folder listing tag"));
            end = i + 2;
            return tag;
        }
        const qsizetype equals = text.indexOf(u'=', i);
        if (equals < 0)
            return truncated();
        const QString attribute = text.mid(i, equals - i).trimmed();
        i = equals + 1;
        while (i < text.size() && isSpace(text.at(i)))
            ++i;
        if (i >= text.size())
            return truncated();
        const QChar quote = text.at(i);
        if (quote != u'"' && quote != u'\'')
            return Error(ErrorCode::InvalidInput,
                         QStringLiteral("Unquoted attribute in a folder listing"));
        const qsizetype close = text.indexOf(quote, i + 1);
        if (close < 0)
            return truncated();
        tag.attributes.insert(attribute, decoded(QStringView(text).sliced(i + 1, close - i - 1)));
        i = close + 1;
    }
}

std::optional<quint64> parseSize(const QString &text)
{
    if (text.isEmpty() ||
        !std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c >= u'0' && c <= u'9'; })) {
        return std::nullopt;
    }
    bool ok = false;
    const qulonglong value = text.toULongLong(&ok);
    if (!ok)
        return std::nullopt;
    return quint64(value);
}

} // namespace

bool isUsableFileName(const QString &name)
{
    if (name.isEmpty() || name == QLatin1StringView(".") || name == QLatin1StringView("..") ||
        name.toUtf8().size() > limits::maxFileNameBytes) {
        return false;
    }
    return std::none_of(name.cbegin(), name.cend(), [](QChar c) {
        return c.unicode() < 0x20 || c.unicode() == 0x7F || c == u'/' || c == u'\\' ||
               c.category() == QChar::Other_Control || c.category() == QChar::Other_Format;
    });
}

Result<FolderListings> parseFolderListing(const QByteArray &body)
{
    if (body.size() > maxFolderListingBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("Folder listing of %1 bytes is too large").arg(body.size()));
    }
    const QString text = QString::fromUtf8(body);
    FolderListings result;
    FolderListing *current = nullptr;
    qsizetype entries = 0;
    qsizetype pos = 0;
    while (true) {
        const qsizetype start = text.indexOf(u'<', pos);
        if (start < 0)
            break;
        const QStringView rest = QStringView(text).sliced(start);
        if (rest.startsWith(u"<?")) {
            const qsizetype end = text.indexOf(QLatin1StringView("?>"), start);
            if (end < 0)
                return truncated();
            pos = end + 2;
            continue;
        }
        if (rest.startsWith(u"<!")) {
            const qsizetype end = text.indexOf(u'>', start);
            if (end < 0)
                return truncated();
            // An internal subset could declare entities.
            if (QStringView(text).sliced(start, end - start).contains(u'['))
                return Error(ErrorCode::InvalidInput,
                             QStringLiteral("Folder listing declares its own entities"));
            pos = end + 1;
            continue;
        }
        if (rest.startsWith(u"</")) {
            const qsizetype end = text.indexOf(u'>', start);
            if (end < 0)
                return truncated();
            if (QStringView(text).sliced(start + 2, end - start - 2).trimmed() ==
                QLatin1StringView("folder-listing")) {
                current = nullptr;
            }
            pos = end + 1;
            continue;
        }

        qsizetype end = 0;
        const Result<Tag> read = readTag(text, start, end);
        if (!read.hasValue())
            return read.error();
        pos = end;
        const Tag &tag = read.value();
        if (tag.name == QLatin1StringView("folder-listing")) {
            if (result.folders.size() >= maxFolderListingEntries)
                return Error(ErrorCode::LimitExceeded,
                             QStringLiteral("Folder listing with too many folders"));
            result.folders.append(FolderListing());
            current = &result.folders.last();
            continue;
        }
        if (!current)
            continue;
        if (tag.name == QLatin1StringView("parent-folder")) {
            current->parentPath = tag.attributes.value(QStringLiteral("path"));
            continue;
        }
        const bool folder = tag.name == QLatin1StringView("folder");
        if (!folder && tag.name != QLatin1StringView("file"))
            continue;
        if (++entries > maxFolderListingEntries) {
            return Error(ErrorCode::LimitExceeded,
                         QStringLiteral("Folder listing with more than %1 entries")
                             .arg(maxFolderListingEntries));
        }
        FolderEntry entry;
        entry.name = tag.attributes.value(QStringLiteral("name"));
        entry.isFolder = folder;
        entry.userPerm = tag.attributes.value(QStringLiteral("user-perm"));
        if (!isUsableFileName(entry.name)) {
            ++result.skipped;
            continue;
        }
        if (!folder) {
            entry.size = parseSize(tag.attributes.value(QStringLiteral("size")));
            if (!entry.size) {
                ++result.skipped;
                continue;
            }
        }
        if (const std::optional<QDateTime> modified =
                parseUtcStamp(tag.attributes.value(QStringLiteral("modified")))) {
            entry.modified = *modified;
        }
        current->entries.append(entry);
    }
    if (result.folders.isEmpty())
        return Error(ErrorCode::InvalidInput, QStringLiteral("No folder listing found"));
    return result;
}

} // namespace qpm::core::codec
