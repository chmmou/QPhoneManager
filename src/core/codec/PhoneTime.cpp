#include "core/codec/PhoneTime.h"

#include <QTimeZone>

namespace qpm::core::codec {

namespace {

constexpr int callLogCenturyStart = 2000;

// Reads `count` ASCII digits starting at `position`; returns -1 when any of
// them is not a digit or the text is too short.
int readDigits(QStringView text, qsizetype position, int count)
{
    if (position + count > text.size())
        return -1;
    int value = 0;
    for (int i = 0; i < count; ++i) {
        const char16_t c = text.at(position + i).unicode();
        if (c < u'0' || c > u'9')
            return -1;
        value = value * 10 + (c - u'0');
    }
    return value;
}

std::optional<QDateTime> makeDateTime(int year, int month, int day, int hour, int minute,
                                      int second, const QTimeZone &zone)
{
    if (year < 0 || month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0)
        return std::nullopt;
    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid())
        return std::nullopt;
    return QDateTime(date, time, zone);
}

// Shared by the local and the UTC stamp: "yyyyMMddTHHmmss" at the start of
// `text`, which must have exactly `expectedSize` characters.
std::optional<QDateTime> parseStamp(QStringView text, qsizetype expectedSize, const QTimeZone &zone)
{
    if (text.size() != expectedSize || text.at(8) != u'T')
        return std::nullopt;
    return makeDateTime(readDigits(text, 0, 4), readDigits(text, 4, 2), readDigits(text, 6, 2),
                        readDigits(text, 9, 2), readDigits(text, 11, 2), readDigits(text, 13, 2),
                        zone);
}

} // namespace

std::optional<QDateTime> parseCallLogTime(QStringView text)
{
    static constexpr qsizetype size = 17;
    if (text.size() != size || text.at(2) != u'/' || text.at(5) != u'/' || text.at(8) != u',' ||
        text.at(11) != u':' || text.at(14) != u':') {
        return std::nullopt;
    }
    const int year = readDigits(text, 0, 2);
    return makeDateTime(year < 0 ? -1 : callLogCenturyStart + year, readDigits(text, 3, 2),
                        readDigits(text, 6, 2), readDigits(text, 9, 2), readDigits(text, 12, 2),
                        readDigits(text, 15, 2), QTimeZone::LocalTime);
}

QString formatCallLogTime(const QDateTime &time)
{
    return time.toLocalTime().toString(QStringLiteral("yy/MM/dd,HH:mm:ss"));
}

std::optional<QDateTime> parseLocalStamp(QStringView text)
{
    return parseStamp(text, 15, QTimeZone::LocalTime);
}

QString formatLocalStamp(const QDateTime &time)
{
    return time.toLocalTime().toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
}

std::optional<QDateTime> parseUtcStamp(QStringView text)
{
    if (text.isEmpty() || text.back() != u'Z')
        return std::nullopt;
    return parseStamp(text.chopped(1), 15, QTimeZone::UTC);
}

QString formatUtcStamp(const QDateTime &time)
{
    return time.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

std::optional<QDate> parseDate(QStringView text)
{
    if (text.size() != 8)
        return std::nullopt;
    const int year = readDigits(text, 0, 4);
    const int month = readDigits(text, 4, 2);
    const int day = readDigits(text, 6, 2);
    if (year < 0 || month < 0 || day < 0)
        return std::nullopt;
    const QDate date(year, month, day);
    if (!date.isValid())
        return std::nullopt;
    return date;
}

QString formatDate(QDate date)
{
    return date.toString(QStringLiteral("yyyyMMdd"));
}

} // namespace qpm::core::codec
