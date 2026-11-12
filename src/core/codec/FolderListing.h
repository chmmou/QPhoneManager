#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace qpm::core::codec {

/// One file or folder of a folder listing.
struct FolderEntry
{
    /// A single name: never empty, ".", "..", and without slash, backslash or control character.
    QString name;
    bool isFolder = false;
    /// Bytes; unset for a folder and for a file whose size the phone did not give.
    std::optional<quint64> size;
    /// UTC; invalid when the phone did not give a readable time.
    QDateTime modified;
    /// "RWD" or "R" for folders the phone reaches through the storage access framework.
    QString userPerm;

    bool operator==(const FolderEntry &other) const = default;
};

/// The entries of one folder.
struct FolderListing
{
    /**
     * The folder as the phone names it, such as "\sdcard\DCIM\" with a
     * trailing backslash; empty for the root, and a path with slashes for
     * folders below "\phone".
     */
    QString parentPath;
    QList<FolderEntry> entries;
};

/// What a folder listing holds, and how many of its entries were left out.
struct FolderListings
{
    /// One block per folder; a listing with subfolders has one for each of them.
    QList<FolderListing> folders;
    /// Entries left out because their name could not be used.
    int skipped = 0;
};

/// Most entries read from one folder listing, subfolders included.
inline constexpr qsizetype maxFolderListingEntries = 500000;
/// Largest folder listing that is read.
inline constexpr qsizetype maxFolderListingBytes = 64 * 1024 * 1024;

/**
 * Reads the folder listing of the phone's file browser: one or, with
 * subfolders, several "folder-listing" blocks one after another.
 *
 * The phone escapes only "&" and the quote, so a name may hold "<" or ">";
 * the reader decodes "&amp;" and "&quot;" and leaves every other "&" as it
 * stands. No entity is declared or loaded. An entry whose name is empty, "."
 * or "..", or holds a slash, a backslash or a control character is left out
 * and counted in FolderListings::skipped, as is a file with an unreadable
 * size.
 *
 * Fails with ErrorCode::LimitExceeded above maxFolderListingBytes or
 * maxFolderListingEntries, and with ErrorCode::InvalidInput when the text
 * holds no folder-listing block or ends inside a tag.
 */
[[nodiscard]] Result<FolderListings> parseFolderListing(const QByteArray &body);

/// Returns whether `name` may name a file or folder: see FolderEntry::name.
[[nodiscard]] bool isUsableFileName(const QString &name);

} // namespace qpm::core::codec
