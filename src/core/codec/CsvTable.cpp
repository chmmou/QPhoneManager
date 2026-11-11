#include "core/codec/CsvTable.h"

#include <QStringDecoder>

#include <array>

namespace qpm::core::codec {

namespace {

constexpr QChar quote = u'"';
constexpr QChar guardMark = u'\'';

bool startsLikeFormula(QStringView field)
{
    if (field.isEmpty())
        return false;
    const QChar first = field.front();
    return first == u'=' || first == u'+' || first == u'-' || first == u'@' || first == u'\t' ||
           first == u'\r';
}

// Apostrophes in front count as part of the field, so that a field that
// already starts with one keeps it after writing and reading again.
bool needsGuard(QStringView field)
{
    qsizetype start = 0;
    while (start < field.size() && field.at(start) == guardMark)
        ++start;
    return startsLikeFormula(field.sliced(start));
}

bool needsQuotes(const QString &field)
{
    if (field.isEmpty())
        return false;
    return field.contains(u';') || field.contains(quote) || field.contains(u'\n') ||
           field.contains(u'\r') || field.front().isSpace() || field.back().isSpace();
}

QChar detectSeparator(const QString &text)
{
    std::array<int, 3> counts{};
    const std::array<QChar, 3> candidates = {u';', u',', u'\t'};
    bool inQuotes = false;
    for (const QChar c : text) {
        if (c == quote) {
            inQuotes = !inQuotes;
        } else if (!inQuotes && (c == u'\n' || c == u'\r')) {
            break;
        } else if (!inQuotes) {
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                if (c == candidates.at(i))
                    ++counts.at(i);
            }
        }
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        if (counts.at(i) > counts.at(best))
            best = i;
    }
    return candidates.at(best);
}

} // namespace

QByteArray writeCsv(const CsvRows &rows)
{
    QString out;
    for (const QStringList &row : rows) {
        QStringList fields;
        fields.reserve(row.size());
        for (const QString &field : row) {
            const bool guarded = needsGuard(field);
            if (guarded || needsQuotes(field)) {
                QString doubled = guarded ? guardMark + field : field;
                doubled.replace(quote, QLatin1StringView("\"\""));
                fields.append(quote + doubled + quote);
            } else {
                fields.append(field);
            }
        }
        out += fields.join(u';') + QLatin1StringView("\r\n");
    }
    return QByteArrayLiteral("\xEF\xBB\xBF") + out.toUtf8();
}

Result<CsvRows> readCsv(const QByteArray &data)
{
    if (data.size() > maxCsvBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("Table larger than %1 bytes").arg(maxCsvBytes));
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(data);
    if (decoder.hasError())
        return Error(ErrorCode::InvalidInput, QStringLiteral("Table is not UTF-8 text"));
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    const QChar separator = detectSeparator(text);
    CsvRows rows;
    QStringList row;
    QString field;
    bool inQuotes = false;
    bool fieldStarted = false;
    const auto endField = [&]() -> Result<void> {
        if (row.size() >= maxCsvFields) {
            return Error(ErrorCode::LimitExceeded,
                         QStringLiteral("Row with more than %1 fields").arg(maxCsvFields));
        }
        if (field.startsWith(guardMark) && needsGuard(QStringView(field).sliced(1)))
            field.remove(0, 1);
        row.append(field);
        field.clear();
        fieldStarted = false;
        return {};
    };
    const auto endRow = [&]() -> Result<void> {
        if (const Result<void> ended = endField(); !ended.hasValue())
            return ended;
        if (rows.size() >= maxCsvRows) {
            return Error(ErrorCode::LimitExceeded,
                         QStringLiteral("Table with more than %1 rows").arg(maxCsvRows));
        }
        const bool blank = row.size() == 1 && row.constFirst().isEmpty();
        if (!blank)
            rows.append(row);
        row.clear();
        return {};
    };

    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inQuotes) {
            if (c == quote) {
                if (i + 1 < text.size() && text.at(i + 1) == quote) {
                    field.append(quote);
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field.append(c);
            }
            continue;
        }
        if (c == quote && !fieldStarted) {
            inQuotes = true;
            fieldStarted = true;
        } else if (c == separator) {
            if (const Result<void> ended = endField(); !ended.hasValue())
                return ended.error();
        } else if (c == u'\r' || c == u'\n') {
            if (c == u'\r' && i + 1 < text.size() && text.at(i + 1) == u'\n')
                ++i;
            if (const Result<void> ended = endRow(); !ended.hasValue())
                return ended.error();
        } else {
            field.append(c);
            fieldStarted = true;
        }
    }
    if (inQuotes)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Quote not closed in the table"));
    if (!field.isEmpty() || !row.isEmpty()) {
        if (const Result<void> ended = endRow(); !ended.hasValue())
            return ended.error();
    }
    return rows;
}

} // namespace qpm::core::codec
