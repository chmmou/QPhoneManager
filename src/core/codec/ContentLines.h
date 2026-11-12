#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace qpm::core::codec {

/// One "NAME;PARAM=...:value" line of a vCalendar, iCalendar or vNote text.
struct ContentLine
{
    /// Upper case.
    QString name;
    /// As written, without the separating semicolons.
    QStringList parameters;
    /// Quoted-printable already decoded; escapes such as "\n" are left as they are.
    QString value;

    /// The value of the parameter `key` ("TZID", ...), case-insensitive; empty when absent.
    [[nodiscard]] QString parameter(const QString &key) const;
};

/**
 * Splits `data` into content lines: a line that starts with a space or a tab
 * continues the one before it, line ends may be CRLF or LF alone, and a value
 * whose parameters say ENCODING=QUOTED-PRINTABLE is decoded, its soft line
 * breaks included. Text is read as UTF-8. Lines without a colon are skipped.
 */
[[nodiscard]] QList<ContentLine> readContentLines(const QByteArray &data);

/**
 * Writes one line the way the phone client writes and reads it: bytes below
 * 0x20 and "=" as quoted-printable, everything else as raw UTF-8, with
 * ENCODING and CHARSET parameters saying so, ended by CRLF and never folded.
 */
[[nodiscard]] QByteArray encodePhoneLine(const QString &header, const QString &value);

/**
 * Splits at ";" not preceded by a backslash and turns "\;" into ";", as the
 * phone escapes lists of names.
 */
[[nodiscard]] QStringList splitEscapedList(const QString &value);

/// Joins with ";" and escapes ";" inside the parts as "\;".
[[nodiscard]] QString joinEscapedList(const QStringList &parts);

} // namespace qpm::core::codec
