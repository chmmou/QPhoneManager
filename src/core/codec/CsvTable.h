#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

namespace qpm::core::codec {

/// Rows of a table; the first row usually holds the column titles.
using CsvRows = QList<QStringList>;

/// Most rows read from one file.
inline constexpr qsizetype maxCsvRows = 200000;
/// Most fields read in one row.
inline constexpr qsizetype maxCsvFields = 1000;
/// Largest file read, in bytes.
inline constexpr qsizetype maxCsvBytes = 64 * 1024 * 1024;

/**
 * Writes UTF-8 with a byte order mark, fields separated by semicolons and rows
 * by CRLF. A field is put in double quotes when it holds a semicolon, a quote,
 * a line break or leading or trailing spaces; quotes inside are doubled.
 */
[[nodiscard]] QByteArray writeCsv(const CsvRows &rows);

/**
 * Reads a table written by writeCsv() or by a spreadsheet program. The
 * separator is the one of semicolon, comma and tab that occurs most often
 * outside quotes in the first line. A byte order mark is skipped, and quoted
 * fields may hold line breaks. Fails with ErrorCode::InvalidInput for text
 * that is not UTF-8 or a quote that is never closed, and with
 * ErrorCode::LimitExceeded above maxCsvBytes, maxCsvRows or maxCsvFields.
 */
[[nodiscard]] Result<CsvRows> readCsv(const QByteArray &data);

} // namespace qpm::core::codec
