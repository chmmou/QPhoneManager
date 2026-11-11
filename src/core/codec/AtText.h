#pragma once

#include <QString>
#include <QStringList>

namespace qpm::core::codec {

/**
 * Encodes a value for an AT command or reply field in the "QP" form of the
 * phone client: every character below U+0020 and each of = , : ; " becomes
 * "=" followed by two upper-case hex digits. All other characters, non-ASCII
 * included, stay as they are.
 */
[[nodiscard]] QString encodeQp(const QString &text);

/**
 * Reverses encodeQp(). "=XX" with two hex digits becomes the character with
 * that code; an "=" that is not followed by two hex digits is kept.
 */
[[nodiscard]] QString decodeQp(const QString &text);

/**
 * Splits the argument list of an AT command or reply at commas that are not
 * inside a double-quoted span. Quotes are kept in the fields. An empty text
 * yields one empty field.
 */
[[nodiscard]] QStringList splitFields(const QString &text);

} // namespace qpm::core::codec
