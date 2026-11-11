#include "core/codec/AtText.h"

namespace qpm::core::codec {

namespace {

bool needsQpEscape(QChar c)
{
    const char16_t code = c.unicode();
    return code < 0x20 || code == u'=' || code == u',' || code == u':' || code == u';' ||
           code == u'"';
}

int hexValue(QChar c)
{
    const char16_t code = c.unicode();
    if (code >= u'0' && code <= u'9')
        return code - u'0';
    if (code >= u'A' && code <= u'F')
        return code - u'A' + 10;
    if (code >= u'a' && code <= u'f')
        return code - u'a' + 10;
    return -1;
}

} // namespace

QString encodeQp(const QString &text)
{
    static constexpr char hexDigits[] = "0123456789ABCDEF";
    QString result;
    result.reserve(text.size());
    for (const QChar c : text) {
        if (needsQpEscape(c)) {
            const char16_t code = c.unicode();
            result += u'=';
            result += QLatin1Char(hexDigits[(code >> 4) & 0xF]);
            result += QLatin1Char(hexDigits[code & 0xF]);
        } else {
            result += c;
        }
    }
    return result;
}

QString decodeQp(const QString &text)
{
    QString result;
    result.reserve(text.size());
    const qsizetype size = text.size();
    for (qsizetype i = 0; i < size; ++i) {
        const QChar c = text.at(i);
        if (c == u'=' && i + 2 < size) {
            const int high = hexValue(text.at(i + 1));
            const int low = hexValue(text.at(i + 2));
            if (high >= 0 && low >= 0) {
                result += QChar(char16_t(high * 16 + low));
                i += 2;
                continue;
            }
        }
        result += c;
    }
    return result;
}

QStringList splitFields(const QString &text)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;
    for (const QChar c : text) {
        if (c == u'"') {
            inQuotes = !inQuotes;
            current += c;
        } else if (c == u',' && !inQuotes) {
            fields.append(current);
            current.clear();
        } else {
            current += c;
        }
    }
    fields.append(current);
    return fields;
}

} // namespace qpm::core::codec
