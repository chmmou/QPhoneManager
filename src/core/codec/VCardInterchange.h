#pragma once

#include "core/Error.h"
#include "core/codec/PhoneVCard.h"

#include <QByteArray>
#include <QList>

namespace qpm::core::codec {

/// Most contacts read from one file.
inline constexpr qsizetype maxInterchangeContacts = 20000;
/// Largest file read, in bytes.
inline constexpr qsizetype maxInterchangeBytes = 64 * 1024 * 1024;

/**
 * Reads every vCard of a file in version 2.1, 3.0 or 4.0: folded lines,
 * quoted-printable with soft line breaks, base64 photos and data URIs, type
 * lists in all three spellings, group prefixes such as "item1.". The result
 * carries no LUIDs and no data ids. A vCard without anything to save is
 * skipped. Fails with ErrorCode::InvalidInput when the file holds no vCard
 * and with ErrorCode::LimitExceeded above maxInterchangeBytes or
 * maxInterchangeContacts.
 */
[[nodiscard]] Result<QList<Contact>> readVCards(const QByteArray &data);

/**
 * Writes vCard 3.0 in UTF-8 with CRLF line ends, lines folded after 75 bytes
 * without splitting a character.
 */
[[nodiscard]] QByteArray writeVCards(const QList<Contact> &contacts);

/// Language of the column titles of a contact table.
enum class CsvLanguage { English, German };

/**
 * Writes contacts as a table with one row per contact and fixed columns.
 * Several numbers or mail addresses of the same type share one field,
 * separated by ", ". Postal addresses beyond the first home and work address,
 * messengers, photos and ringtones are not written.
 */
[[nodiscard]] QByteArray writeContactsCsv(const QList<Contact> &contacts, CsvLanguage language);

/**
 * Reads a table written by writeContactsCsv() in either language. Columns are
 * matched by their titles without regard to case; unknown columns are
 * ignored. Fails with ErrorCode::InvalidInput when no title is known and
 * otherwise like readCsv().
 */
[[nodiscard]] Result<QList<Contact>> readContactsCsv(const QByteArray &data);

} // namespace qpm::core::codec
