#pragma once

#include "core/Error.h"
#include "core/codec/VCardInterchange.h"
#include "core/codec/VMessage.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace qpm::core::codec {

/// A message for an export, with what the export shows beside it.
struct ExportedMessage
{
    Message message;
    /// The contact's name for the other side; empty when there is none.
    QString contactName;
    bool archived = false;
};

/// Bounds for reading an archive file.
inline constexpr qsizetype maxArchiveBytes = 512 * 1024 * 1024;
inline constexpr qsizetype maxArchiveMessages = 200000;

/**
 * Writes the messages as a table: date and time in local time, direction,
 * folder, contact, number, subject, text, the names of the parts, the SIM
 * card counted from 1 and whether the message is archived. Column titles and
 * fixed words are in `language`. See writeCsv() for the form, which also keeps
 * a spreadsheet from running a text as a formula.
 */
[[nodiscard]] QByteArray writeMessagesCsv(const QList<ExportedMessage> &messages,
                                          CsvLanguage language);

/**
 * Writes the messages as one HTML page with `title` as heading, grouped by
 * the other side in the order given. Every text from the messages is escaped,
 * and the page forbids scripts and loading anything, so a message cannot run
 * code in a browser. Parts are listed by name, not embedded.
 */
[[nodiscard]] QByteArray writeMessagesHtml(const QList<ExportedMessage> &messages,
                                           const QString &title, CsvLanguage language);

/**
 * Writes the messages in the archive format of this program, version 1, with
 * the parts that are on this computer in Base64. Characters that XML 1.0 does
 * not allow become U+FFFD. Scheduled messages are left out.
 */
[[nodiscard]] QByteArray writeMessagesXml(const QList<ExportedMessage> &messages);

/**
 * Reads an archive written by writeMessagesXml(). The messages come without
 * LUID. Fails with ErrorCode::InvalidInput for anything that is not such an
 * archive, for a document type declaration, for an unknown kind or folder and
 * for parts that are not Base64, and with ErrorCode::LimitExceeded above
 * maxArchiveBytes, maxArchiveMessages or the limits for one message in
 * VMessage.h.
 */
[[nodiscard]] Result<QList<Message>> readMessagesXml(const QByteArray &data);

} // namespace qpm::core::codec
