#include "ui/PhoneNumberText.h"

#include <QStringList>

#include <array>

namespace qpm::ui {

namespace {

constexpr std::array<int, 2> oneDigitCodes = {1, 7};
constexpr std::array<int, 43> twoDigitCodes = {
    20, 27, 30, 31, 32, 33, 34, 36, 39, 40, 41, 43, 44, 45, 46, 47, 48, 49, 51, 52, 53, 54,
    55, 56, 57, 58, 60, 61, 62, 63, 64, 65, 66, 81, 82, 84, 86, 90, 91, 92, 93, 94, 95,
};
// Area codes of two digits after the leading zero in Germany: Berlin,
// Hamburg, Frankfurt and Munich.
constexpr std::array<int, 4> germanCityCodes = {30, 40, 69, 89};

int countryCodeLength(const QString &digits)
{
    const int first = digits.left(1).toInt();
    for (const int code : oneDigitCodes) {
        if (first == code)
            return 1;
    }
    const int firstTwo = digits.left(2).toInt();
    for (const int code : twoDigitCodes) {
        if (firstTwo == code)
            return 2;
    }
    return 3;
}

// Length of the prefix to split off a national number without its zero; 0 when unknown.
int prefixLength(const QString &countryCode, const QString &national)
{
    if (countryCode == QLatin1StringView("49")) {
        if (national.startsWith(QLatin1StringView("15")) || national.startsWith(QLatin1StringView("16")) ||
            national.startsWith(QLatin1StringView("17"))) {
            return 3;
        }
        const int city = national.left(2).toInt();
        for (const int code : germanCityCodes) {
            if (city == code)
                return 2;
        }
        return 0;
    }
    if (countryCode == QLatin1StringView("43") && national.startsWith(u'6'))
        return 3;
    if (countryCode == QLatin1StringView("41") && national.startsWith(u'7'))
        return 2;
    return 0;
}

QString grouped(const QString &prefix, const QString &rest)
{
    return rest.isEmpty() ? prefix : prefix + u' ' + rest;
}

} // namespace

QString PhoneNumberText::display(const QString &number)
{
    const QString trimmed = number.simplified();
    QString digits;
    bool separated = false;
    for (qsizetype i = 0; i < trimmed.size(); ++i) {
        const QChar c = trimmed.at(i);
        if (c.isDigit() && c.unicode() < 0x80) {
            digits.append(c);
        } else if (c == u'+' && i == 0) {
            digits.append(c);
        } else if (c == u' ' || c == u'-' || c == u'/' || c == u'(' || c == u')' || c == u'.') {
            separated = true;
        } else {
            return trimmed;
        }
    }
    if (digits.size() < 5 || digits == QLatin1StringView("+"))
        return trimmed;
    if (separated) {
        QString spaced = trimmed;
        spaced.replace(u'-', u' ').replace(u'/', u' ');
        return spaced.simplified();
    }

    QString international;
    if (digits.startsWith(u'+'))
        international = digits.mid(1);
    else if (digits.startsWith(QLatin1StringView("00")))
        international = digits.mid(2);
    if (!international.isEmpty()) {
        const int codeLength = countryCodeLength(international);
        const QString code = international.left(codeLength);
        const QString national = international.mid(codeLength);
        const int prefix = prefixLength(code, national);
        const QString rest = prefix > 0 && national.size() > prefix
                                 ? grouped(national.left(prefix), national.mid(prefix))
                                 : national;
        return grouped(u'+' + code, rest);
    }
    if (digits.startsWith(u'0') && !digits.startsWith(QLatin1StringView("00"))) {
        const QString national = digits.mid(1);
        const int prefix = prefixLength(QStringLiteral("49"), national);
        if (prefix > 0 && national.size() > prefix)
            return grouped(u'0' + national.left(prefix), national.mid(prefix));
    }
    return digits;
}

} // namespace qpm::ui
