#pragma once

#include <QString>

namespace qpm::core::codec {

/// How a text fills text messages.
struct SmsLength
{
    /// Characters as the network counts them: GSM 7-bit units, where a
    /// character of the extension table counts two, or UTF-16 units.
    int units = 0;
    /// Messages the text is sent as; 0 for an empty text.
    int parts = 0;
    /// The text needs UCS-2 because a character is not in the GSM 7-bit alphabet.
    bool unicode = false;
    /// Units left in the last message before another one starts.
    int remaining = 0;
};

/**
 * Splits `text` the way a phone does: in the GSM 7-bit default alphabet 160
 * units fit one message and 153 each of several, otherwise 70 and 67 UTF-16
 * units. A character of the extension table and a surrogate pair are never
 * split between two messages. National language tables, which some phones
 * use to stay in 7-bit, are not taken into account, so the count may be
 * higher than on such a phone, never lower.
 */
[[nodiscard]] SmsLength smsLength(const QString &text);

} // namespace qpm::core::codec
