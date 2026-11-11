#include "core/codec/SmsLength.h"

#include <QList>

#include <utility>

namespace qpm::core::codec {

namespace {

constexpr int singleGsmUnits = 160;
constexpr int multiGsmUnits = 153;
constexpr int singleUnicodeUnits = 70;
constexpr int multiUnicodeUnits = 67;

// The default alphabet of 3GPP TS 23.038, without the escape code.
const QString &gsmBasic()
{
    static const QString characters =
        QStringLiteral("@£$¥èéùìòÇ\nØø\rÅåΔ_ΦΓΛΩΠΨΣΘΞÆæßÉ !\"#¤%&'()*+,-./0123456789:;<=>?"
                       "¡ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÑÜ§¿abcdefghijklmnopqrstuvwxyzäöñüà");
    return characters;
}

// The extension table; each of these takes the escape code and itself.
const QString &gsmExtension()
{
    static const QString characters = QStringLiteral("\f^{}\\[~]|€");
    return characters;
}

// Fills messages of `perPart` units with the unit sizes in order and returns
// how many messages and how many units are left in the last.
std::pair<int, int> fill(const QList<int> &sizes, int perPart)
{
    int parts = 1;
    int used = 0;
    for (const int size : sizes) {
        if (used + size > perPart) {
            ++parts;
            used = 0;
        }
        used += size;
    }
    return {parts, perPart - used};
}

} // namespace

SmsLength smsLength(const QString &text)
{
    SmsLength length;
    QList<int> sizes;
    sizes.reserve(text.size());
    for (const QChar c : text) {
        if (gsmBasic().contains(c)) {
            sizes.append(1);
        } else if (gsmExtension().contains(c)) {
            sizes.append(2);
        } else {
            length.unicode = true;
            break;
        }
    }
    if (length.unicode) {
        sizes.clear();
        for (qsizetype i = 0; i < text.size(); ++i) {
            const bool pair = text.at(i).isHighSurrogate() && i + 1 < text.size() &&
                              text.at(i + 1).isLowSurrogate();
            sizes.append(pair ? 2 : 1);
            if (pair)
                ++i;
        }
    }
    for (const int size : std::as_const(sizes))
        length.units += size;

    const int single = length.unicode ? singleUnicodeUnits : singleGsmUnits;
    const int multi = length.unicode ? multiUnicodeUnits : multiGsmUnits;
    if (length.units == 0) {
        length.remaining = single;
        return length;
    }
    if (length.units <= single) {
        length.parts = 1;
        length.remaining = single - length.units;
        return length;
    }
    const auto [parts, remaining] = fill(sizes, multi);
    length.parts = parts;
    length.remaining = remaining;
    return length;
}

} // namespace qpm::core::codec
