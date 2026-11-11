#pragma once

#include "core/Error.h"

#include <QString>
#include <QtGlobal>

namespace qpm::core::limits {

// Upper bounds for data that comes from the phone. Everything the phone sends
// is untrusted; these values keep a broken or hostile client from exhausting
// memory.

inline constexpr qsizetype mebibyte = 1024 * 1024;

/// Longest AT line without a line end.
inline constexpr qsizetype maxAtLineBytes = 1 * mebibyte;
/// Largest AT reply, all lines together.
inline constexpr qsizetype maxAtReplyBytes = 64 * mebibyte;

/// Largest file body of one OBEX GET.
inline constexpr quint64 maxObexFileBytes = 512 * quint64(mebibyte);
/// Largest body of one OBEX GET of a sync object or a full sync dump.
inline constexpr quint64 maxObexSyncBytes = 64 * quint64(mebibyte);

/// Most objects in one full sync dump.
inline constexpr qsizetype maxSyncObjects = 200000;

/// Image bounds, applied before an image from the phone is decoded.
inline constexpr int maxImageWidth = 8192;
inline constexpr int maxImageHeight = 8192;
inline constexpr int maxImageAllocationMiB = 256;

/// XML documents from the phone: size, nesting depth and element count.
inline constexpr qsizetype maxXmlBytes = 16 * mebibyte;
inline constexpr int maxXmlDepth = 32;
inline constexpr qsizetype maxXmlElements = 200000;

/// Output kept from a helper process such as adb.
inline constexpr qsizetype maxProcessOutputBytes = 1 * mebibyte;

/// Largest discovery datagram that is evaluated.
inline constexpr qsizetype maxDatagramBytes = 1024;

/// Longest file name accepted for a download, in UTF-8 bytes.
inline constexpr qsizetype maxFileNameBytes = 255;

/**
 * Returns the absolute path for saving a file that the phone calls
 * `remoteName` into the existing directory `targetDirectory`.
 *
 * Fails with ErrorCode::InvalidInput when the name is empty, ".", "..", longer
 * than maxFileNameBytes, or contains a slash, a backslash or a control
 * character, and when the resulting path would lie outside the target
 * directory, for example through a symbolic link that already exists under
 * that name. Fails with ErrorCode::NotFound when the target directory does not
 * exist. The returned path never leaves the canonical target directory.
 */
[[nodiscard]] Result<QString> safeDownloadPath(const QString &targetDirectory,
                                               const QString &remoteName);

} // namespace qpm::core::limits
