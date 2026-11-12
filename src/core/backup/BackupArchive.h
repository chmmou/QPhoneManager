#pragma once

#include "core/Error.h"
#include "core/codec/CallLogCodec.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/PhoneVCard.h"
#include "core/codec/VCardInterchange.h"
#include "core/codec/VMessage.h"
#include "core/codec/VNote.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringView>

#include <functional>
#include <optional>

namespace qpm::core::backup {

/// The areas a backup holds, each in a folder of its own.
enum class BackupSection { Contacts, Calendar, Tasks, Notes, Calls, Messages };

/// Every section, in the order the program shows them.
inline constexpr BackupSection allBackupSections[] = {
    BackupSection::Contacts, BackupSection::Calendar, BackupSection::Tasks,
    BackupSection::Notes,    BackupSection::Calls,    BackupSection::Messages,
};

/// The format version this program writes and the newest one it reads.
inline constexpr int backupFormatVersion = 1;
/// The value of "format" in manifest.json.
inline constexpr QLatin1StringView backupFormatName("qphonemanager-backup");
/// Largest manifest.json read, in bytes.
inline constexpr qsizetype maxManifestBytes = 64 * 1024;

/// The key of the section in manifest.json: "contacts", "calendar", "tasks" and so on.
[[nodiscard]] QString sectionKey(BackupSection section);

/// The section for a key of manifest.json; nothing for an unknown key.
[[nodiscard]] std::optional<BackupSection> sectionFromKey(QStringView key);

/// What manifest.json says about a backup.
struct BackupInfo
{
    int formatVersion = backupFormatVersion;
    /// UTC.
    QDateTime createdAt;
    /// Program and version that wrote it, for example "QPhoneManager 1.0.0".
    QString app;
    QString deviceId;
    QString deviceName;
    /// Entries per section; a section the backup does not hold is missing.
    QMap<BackupSection, int> counts;
};

/// The content of a backup, one list per section.
struct BackupData
{
    QList<codec::Contact> contacts;
    QList<codec::CalendarEvent> events;
    QList<codec::CalendarTask> tasks;
    QList<codec::Note> notes;
    /// Written as a table only; empty after reading a backup.
    QList<codec::CallEntry> calls;
    /// With the content of the parts this computer has.
    QList<codec::Message> messages;
};

struct Backup
{
    BackupInfo info;
    BackupData data;
};

/**
 * Called between the steps of a long run with the number of steps done out
 * of `total`. Returning false stops the run, which then fails with
 * ErrorCode::Cancelled.
 */
using BackupStep = std::function<bool(int done, int total)>;

/**
 * Writes the sections `sections` of `data` to the archive `path`, format
 * version 1: manifest.json and per section the standard format beside the
 * form of the phone client, the calls as a table in `language`, the messages
 * as the archive XML of this program and their parts as files of their own.
 * `info` gives the manifest everything but the counts, which come from
 * `data`. Returns the manifest written.
 *
 * Runs on any thread. Fails like ZipWriter, and with ErrorCode::Cancelled
 * when `step` says so; the file at `path` is then left as it was.
 */
[[nodiscard]] Result<BackupInfo> writeBackupArchive(const QString &path, const BackupInfo &info,
                                                    const BackupData &data,
                                                    const QList<BackupSection> &sections,
                                                    codec::CsvLanguage language,
                                                    const BackupStep &step);

/**
 * Reads the archive `path` back: the manifest, and for each section it names
 * the form of the phone client, the archive XML of the messages, and the
 * table of the calls, whose rows are only counted.
 *
 * Runs on any thread. Fails like ZipReader, except that an archive ZipReader
 * does not support counts as no backup; with ErrorCode::InvalidInput
 * for a file that is not a backup, a missing or unreadable manifest, a
 * missing or unreadable section file and counts that differ from the
 * manifest; with ErrorCode::NotSupported for a format version newer than
 * backupFormatVersion; with ErrorCode::LimitExceeded for a manifest above
 * maxManifestBytes or a section above the limits of its reader; and with
 * ErrorCode::Cancelled when `step` says so.
 */
[[nodiscard]] Result<Backup> readBackupArchive(const QString &path, const BackupStep &step);

} // namespace qpm::core::backup
