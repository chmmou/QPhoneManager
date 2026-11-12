#pragma once

#include "core/Error.h"
#include "core/codec/CalMetadata.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace qpm::core::codec {

/// A note of the note app that comes with the phone client: plain text only.
struct Note
{
    /// A small number on the phone; 0 for a note not created yet.
    quint32 luid = 0;
    QString body;
    /// UTC; set by the phone.
    QDateTime lastModified;
    /// Category names in their order on the phone.
    QStringList categories;

    bool operator==(const Note &other) const = default;
};

/**
 * Reads one VNOTE in the dialect of the phone client. Fails with
 * ErrorCode::ProtocolError when there is no VNOTE.
 */
[[nodiscard]] Result<Note> parsePhoneNote(const QByteArray &data);

/**
 * The text the phone takes to create or replace `note`. `categoryColors`
 * holds an RGB colour per entry of `note.categories`, or nothing; the phone
 * gives a category it does not know yet that colour. The time of the last
 * change is left to the phone.
 */
[[nodiscard]] QByteArray encodePhoneNote(const Note &note,
                                         const QList<std::optional<quint32>> &categoryColors);

/// The note as the phone sends it back, X-IRMC-LUID included; for tests and the fake phone.
[[nodiscard]] QByteArray encodeNoteAsPhoneSendsIt(const Note &note);

/// True when both notes hold the same text and categories.
[[nodiscard]] bool sameNoteContent(const Note &a, const Note &b);

/**
 * Reads the categories of "telecom/notes/metadata.xml". Fails like
 * parseSafeXml() and with ErrorCode::ProtocolError when the root element is
 * not "metadata".
 */
[[nodiscard]] Result<QList<OrganizerCategory>> parseNoteMetadata(const QByteArray &xml);

} // namespace qpm::core::codec
