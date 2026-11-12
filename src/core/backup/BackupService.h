#pragma once

#include "core/Error.h"
#include "core/backup/BackupArchive.h"

#include <QFuture>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

namespace qpm::core::service {
class CalendarService;
class ContactsService;
class MessagesService;
class NotesService;
} // namespace qpm::core::service

namespace qpm::core::store {
class ProfileStore;
}

namespace qpm::core::backup {

/// What restoring one section would do.
struct SectionPreview
{
    /// Entries the phone, or for messages this computer, does not have.
    int added = 0;
    /// Entries the phone has in another form; restoring writes the backup's form.
    int changed = 0;
    int unchanged = 0;
};

/// What restoring a backup would do, per section it holds.
struct BackupPreview
{
    QMap<BackupSection, SectionPreview> sections;
    /// The areas were pulled from the phone first; otherwise the comparison
    /// is with what this computer stored at the last sync.
    bool comparedWithPhone = false;
    /// The backup was made of another phone, so entries were compared by
    /// content only and none counts as changed.
    bool fromOtherPhone = false;
};

/// What restoring one section did.
struct SectionOutcome
{
    int restored = 0;
    int failed = 0;
    /// Why the first failed entry failed; no error when none failed.
    Error firstError;
};

/// What a restore did, per section it was asked for.
struct RestoreReport
{
    QMap<BackupSection, SectionOutcome> sections;
    /// cancel() stopped the restore; the sections hold what was done until then.
    bool cancelled = false;
};

/**
 * Backs up the stored data of the current phone into a ".qpmbackup" archive,
 * see writeBackupArchive(), and restores from one. A backup holds what this
 * computer stored at the last sync; it does not ask the phone.
 *
 * Restoring goes through the services: contacts, appointments, tasks and
 * notes are written to the phone, each new entry created and each changed
 * one replaced by the backup's form. An entry counts as changed only when the
 * backup was made of the same phone and holds it under the same number; a
 * backup of another phone only adds what the phone lacks. Messages go only into the archive on
 * this computer, since the phone app does not take messages. The call log is
 * kept in the backup only: the phone app may not change the call log of the
 * phone, and the call log on this computer always mirrors the phone's.
 *
 * Only one operation runs at a time; a second call fails with
 * ErrorCode::InvalidInput. Every call that needs the stored data fails with
 * ErrorCode::InvalidInput when no phone is chosen. Reading the stored data
 * and reading and writing the archive happen on a worker thread with a
 * read-only connection of its own; the archive reports through
 * progressChanged(). Everything else lives in the main thread, and no call
 * blocks. A failure of the worker ends the operation with
 * ErrorCode::IoFailure. Does not own the services, which must outlive it.
 */
class BackupService : public QObject
{
    Q_OBJECT

public:
    /// The services restoring writes through; none may be null.
    struct Services
    {
        service::ContactsService *contacts = nullptr;
        service::CalendarService *calendar = nullptr;
        service::NotesService *notes = nullptr;
        service::MessagesService *messages = nullptr;
    };

    explicit BackupService(const Services &services, QObject *parent = nullptr);
    /// Asks a running read or write of an archive to stop.
    ~BackupService() override;

    /**
     * The stored data of the current phone; null when none is chosen. The
     * store must stay open until this is called again with another one or
     * null.
     */
    void setProfileStore(store::ProfileStore *profile);

    [[nodiscard]] bool isRunning() const { return m_running; }

    /// Whether a section can be restored at all; false for the call log.
    [[nodiscard]] static bool isRestorable(BackupSection section);

    /**
     * Writes the sections `sections` of what this computer stores for the
     * current phone to `path`, the call log as a table in `language`, and
     * returns the manifest written. Fails with ErrorCode::InvalidInput when
     * `sections` is empty, with the error of the profile store when its data
     * cannot be read, like writeBackupArchive() otherwise, and with
     * ErrorCode::Cancelled after cancel().
     */
    QFuture<Result<BackupInfo>> create(const QString &path, const QList<BackupSection> &sections,
                                       codec::CsvLanguage language);

    /// Reads the backup at `path`, see readBackupArchive(). Needs no phone.
    QFuture<Result<Backup>> open(const QString &path);

    /**
     * Compares `backup` with the current phone: first pulls contacts,
     * calendar with tasks, and notes from the phone, as far as the backup
     * holds them, then counts per section what a restore would add and
     * change. Without a connection the comparison is with what this computer
     * stored last. Fails with the error of a pull other than a missing
     * connection.
     */
    QFuture<Result<BackupPreview>> preview(const Backup &backup);

    /**
     * Restores the sections `sections` of `backup` as described for the
     * class, one entry after another, compared with what this computer
     * stores now. An entry the phone refuses is counted as failed and the
     * next one follows; after a lost connection or a refusal of the whole
     * area the rest of the section counts as failed. The report holds an
     * outcome for each section asked for. When the accounts or calendars of
     * the phone cannot be read, the new entries of that section count as
     * failed with that error. Fails with ErrorCode::InvalidInput for a
     * section the backup does not hold or that cannot be restored.
     */
    QFuture<Result<RestoreReport>> restore(const Backup &backup,
                                           const QList<BackupSection> &sections);

    /// Stops the running operation at the next step; does nothing when none runs.
    void cancel();

Q_SIGNALS:
    /// `done` of `total` steps of the running operation are finished.
    void progressChanged(int done, int total);

private:
    struct Plan;
    struct RestoreRun;

    /// Sorts the entries of `backup` against the data of the profile, on the calling thread.
    [[nodiscard]] static Result<Plan> computePlan(const QString &profilesDirectory,
                                                  const QString &deviceId, const Backup &backup);
    /// Runs computePlan() for the current profile on a worker thread.
    QFuture<Result<Plan>> startPlan(const Backup &backup);
    void addJobs(const std::shared_ptr<RestoreRun> &run, const Plan &plan,
                 const QList<BackupSection> &sections);
    void begin();
    template <typename T> QFuture<Result<T>> track(QFuture<Result<T>> work);
    void runNext(const std::shared_ptr<RestoreRun> &run);

    Services m_services;
    store::ProfileStore *m_profile = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_running = false;
};

} // namespace qpm::core::backup
