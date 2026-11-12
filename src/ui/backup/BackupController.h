#pragma once

#include "core/Error.h"
#include "core/backup/BackupService.h"
#include "ui/backup/BackupSectionModel.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace qpm::core::store {
class ProfileStore;
}

namespace qpm::ui {

/**
 * Backing up and restoring for the window: the sections to back up, the
 * preview of a backup file per section, the restore, its progress and its
 * outcome. Every failure ends in statusText as a translated text that says
 * what went wrong and what to do, with statusIsError set.
 *
 * Owned by AppController, which hands in the profile store of the current
 * phone and the connection state. Backing up and reading a backup need no
 * connection; restoring needs one, except for the messages, which only go
 * into the archive on this computer. The call log is never restored. No call
 * blocks; files are read and written on a worker thread.
 */
class BackupController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BackupController)
    QML_UNCREATABLE("Provided by AppController")

    /// The sections to back up.
    Q_PROPERTY(qpm::ui::BackupSectionModel *sections READ sections CONSTANT FINAL)
    /// The sections of the opened backup, with what restoring each would do.
    Q_PROPERTY(qpm::ui::BackupSectionModel *restoreSections READ restoreSections CONSTANT FINAL)
    /// A phone is chosen, so there is something to back up and restore to.
    Q_PROPERTY(bool hasPhone READ hasPhone NOTIFY hasPhoneChanged FINAL)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged FINAL)
    /// A backup, a preview or a restore runs.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// From 0 to 1 while the length of the running step is known, otherwise -1.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged FINAL)
    /// A backup file was opened and compared; restoreSections shows the result.
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged FINAL)
    /// Which phone the opened backup is of and when it was made.
    Q_PROPERTY(QString archiveText READ archiveText NOTIFY previewChanged FINAL)
    /// The opened backup is of another phone, so restoring only adds what this phone lacks.
    Q_PROPERTY(bool fromOtherPhone READ isFromOtherPhone NOTIFY previewChanged FINAL)

public:
    /// The services must outlive this object.
    explicit BackupController(const core::backup::BackupService::Services &services,
                              QObject *parent = nullptr);

    [[nodiscard]] BackupSectionModel *sections() const { return m_sections; }
    [[nodiscard]] BackupSectionModel *restoreSections() const { return m_restoreSections; }
    [[nodiscard]] bool hasPhone() const { return m_profile != nullptr; }
    [[nodiscard]] bool isConnected() const { return m_connected; }
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] double progress() const { return m_progress; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] bool statusIsError() const { return m_statusIsError; }
    [[nodiscard]] bool hasPreview() const { return m_backup.has_value(); }
    [[nodiscard]] QString archiveText() const;
    [[nodiscard]] bool isFromOtherPhone() const { return m_preview && m_preview->fromOtherPhone; }
    /// The service, for tests that wait on it.
    [[nodiscard]] core::backup::BackupService *service() const { return m_service; }

    /**
     * Backs up and restores the data of `profile`; null for none. The store
     * must stay open until this is called again with another one or null.
     * Forgets an opened backup, while a step runs as soon as it ends.
     */
    void setProfileStore(core::store::ProfileStore *profile);
    void setConnected(bool connected);

    /// A file name in `folder` for a new backup of the current phone; empty without a phone.
    [[nodiscard]] Q_INVOKABLE QUrl suggestedFileUrl(const QUrl &folder) const;

    /**
     * Writes the sections `sections`, keys of BackupSectionModel, to the local
     * file `url`. False when nothing is started.
     */
    Q_INVOKABLE bool create(const QUrl &url, const QStringList &sections);
    /**
     * Opens the backup in the local file `url` and compares it with the
     * phone, see core::backup::BackupService::preview(). False when nothing
     * is started.
     */
    Q_INVOKABLE bool preview(const QUrl &url);
    /**
     * Restores the sections `sections` of the backup in `url`, which must be
     * the one opened by preview(). False when nothing is started.
     */
    Q_INVOKABLE bool restore(const QUrl &url, const QStringList &sections);
    /**
     * Stops the running step at its next entry. A stop between reading a
     * backup and comparing it keeps the comparison from starting.
     */
    Q_INVOKABLE void cancel();
    /// Forgets the opened backup and the last message, as when the dialog opens anew.
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void hasPhoneChanged();
    void connectedChanged();
    void busyChanged();
    void progressChanged();
    void statusChanged();
    void previewChanged();

private:
    enum class Step { Write, Read, Compare, Restore };

    /// What went wrong in `step` and what to do, translated.
    [[nodiscard]] static QString errorText(const core::Error &error, Step step);
    /// Forgets the opened backup and its preview, but not the last message.
    void forgetBackup();
    void setBusy(bool busy);
    void setStatus(const QString &text, bool isError);
    void setProgress(double progress);
    void showPreview(const core::backup::BackupPreview &preview);
    void showReport(const core::backup::RestoreReport &report);
    void updateRestoreChoice();
    [[nodiscard]] static std::optional<QList<core::backup::BackupSection>>
    sectionsFrom(const QStringList &keys);

    core::backup::BackupService *m_service = nullptr;
    BackupSectionModel *m_sections = nullptr;
    BackupSectionModel *m_restoreSections = nullptr;
    core::store::ProfileStore *m_profile = nullptr;
    std::optional<core::backup::Backup> m_backup;
    std::optional<core::backup::BackupPreview> m_preview;
    QUrl m_backupUrl;
    QString m_statusText;
    double m_progress = -1;
    /// Counts the changes of the profile, so that a step can tell it was switched meanwhile.
    quint64 m_profileGeneration = 0;
    bool m_stopRequested = false;
    bool m_statusIsError = false;
    bool m_connected = false;
    bool m_busy = false;
};

} // namespace qpm::ui
