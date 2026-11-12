#pragma once

#include "core/Error.h"
#include "core/codec/FolderListing.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>
#include <optional>

namespace qpm::core::service {
class FileService;
}

namespace qpm::ui {

/**
 * The file transfers between this computer and the phone, one after another,
 * as a list for the window: each file, and each folder until it is expanded
 * into its files, with its state and progress.
 *
 * When a file exists at the target, the queue stops at it and asks through
 * conflictId until resolveConflict() answers: replace it, keep both, skip it,
 * each also for all further conflicts of the running batch.
 *
 * A download never writes outside the folder chosen for it: a name from the
 * phone that is empty, "." or "..", holds a slash, a backslash or a control
 * character, or would end up outside the chosen folder through a symbolic
 * link, fails that file, see targetPath(). Local symbolic links are not
 * followed by an upload.
 *
 * Every failure is reported once at the end of a batch through
 * errorOccurred() as a translated text. Lives in the main thread; no call
 * blocks. `service` must outlive the queue.
 */
class TransferQueue : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TransferQueue)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// A transfer runs or waits.
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged FINAL)
    /// For the status line, such as "Transfer 3 of 10, 45 %"; empty while idle.
    Q_PROPERTY(QString statusText READ statusText NOTIFY progressChanged FINAL)
    /// 0 to 1 over the running batch.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    /// The job that waits for a decision, or 0.
    Q_PROPERTY(int conflictId READ conflictId NOTIFY conflictChanged FINAL)
    /// Name of the file that exists at the target of conflictId.
    Q_PROPERTY(QString conflictName READ conflictName NOTIFY conflictChanged FINAL)
    /// The existing file is on the phone, not on this computer.
    Q_PROPERTY(bool conflictOnPhone READ conflictOnPhone NOTIFY conflictChanged FINAL)
    /// Some finished jobs are still listed.
    Q_PROPERTY(bool hasFinished READ hasFinished NOTIFY countChanged FINAL)

public:
    enum class Direction { Download, Upload };
    Q_ENUM(Direction)

    enum class State { Queued, Running, WaitingForDecision, Done, Skipped, Failed, Cancelled };
    Q_ENUM(State)

    enum class Choice { Replace, KeepBoth, Skip };
    Q_ENUM(Choice)

    enum Role {
        JobIdRole = Qt::UserRole + 1,
        NameRole,
        DirectionRole,
        StateRole,
        StateTextRole,
        ProgressRole,
        CanCancelRole,
    };

    /// Opens a downloaded file with the program the desktop chooses for it.
    using DesktopOpener = std::function<bool(const QUrl &)>;

    explicit TransferQueue(core::service::FileService *service, QObject *parent = nullptr);
    ~TransferQueue() override;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return int(m_jobs.size()); }
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] int conflictId() const { return m_conflictId; }
    [[nodiscard]] QString conflictName() const;
    [[nodiscard]] bool conflictOnPhone() const;
    [[nodiscard]] bool hasFinished() const;

    /**
     * Copies the files and folders `entries` of the phone folder
     * `remoteFolder` into the existing local folder `localFolder`.
     */
    void download(const QString &remoteFolder, const QList<core::codec::FolderEntry> &entries,
                  const QString &localFolder);

    /**
     * Copies the local files and folders `localPaths` into the phone folder
     * `remoteFolder`, whose entries are named `namesThere`.
     */
    void upload(const QStringList &localPaths, const QString &remoteFolder,
                const QStringList &namesThere);

    /// Copies the file `entry` into a temporary folder and opens it on the desktop.
    void openOnDesktop(const QString &remoteFolder, const core::codec::FolderEntry &entry);

    /**
     * Copies the files `entries` into a temporary folder; dragFilesReady()
     * follows when all of them are there.
     */
    void fetchForDrag(const QString &remoteFolder, const QList<core::codec::FolderEntry> &entries);

    /// Replaces how files are opened on the desktop, for tests.
    void setDesktopOpener(DesktopOpener opener);

    /// Fails what waits when the connection ends.
    void setConnected(bool connected);

    /**
     * The path for the phone's file `name` in the folders `subfolders` below
     * the existing local folder `baseFolder`, creating those folders. Fails
     * with ErrorCode::InvalidInput for a name or folder that
     * codec::isUsableFileName() refuses, for an existing target that is no
     * regular file or folder, and for any path that would leave `baseFolder`,
     * through a symbolic link among them.
     */
    [[nodiscard]] static core::Result<QString>
    targetPath(const QString &baseFolder, const QStringList &subfolders, const QString &name);

    /**
     * Creates the folders `subfolders` below the existing local folder
     * `baseFolder` where missing and returns the innermost, under the same
     * rules as targetPath().
     */
    [[nodiscard]] static core::Result<QString> targetFolder(const QString &baseFolder,
                                                            const QStringList &subfolders);

    /// "a.txt" gives "a (1).txt", "a (2).txt" and so on, the first not in `taken`.
    [[nodiscard]] static QString freeName(const QString &name,
                                          const std::function<bool(const QString &)> &taken);

    /// A translated text for a failure of a file operation: what happened and what to do.
    [[nodiscard]] static QString messageFor(const core::Error &error);

    /// The id of the job in `row`, or 0.
    [[nodiscard]] Q_INVOKABLE int idAt(int row) const;
    /// Stops the job `id`: a waiting one is dropped, a running one aborted.
    Q_INVOKABLE void cancel(int id);
    Q_INVOKABLE void cancelAll();
    /// Answers the conflict of job `id`; with `forAll` the rest of the batch gets the same answer.
    Q_INVOKABLE void resolveConflict(int id, qpm::ui::TransferQueue::Choice choice, bool forAll);
    /// Takes finished jobs out of the list.
    Q_INVOKABLE void clearFinished();

Q_SIGNALS:
    void countChanged();
    void activeChanged();
    void progressChanged();
    void conflictChanged();
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);
    /// The content of the phone folder `remoteFolder` changed.
    void phoneFolderChanged(const QString &remoteFolder);
    /// The files asked for by fetchForDrag() are ready at `urls`.
    void dragFilesReady(const QList<QUrl> &urls);

private:
    enum class Kind { File, Folder };
    enum class Purpose { Keep, Open, Drag };

    struct Job
    {
        int id = 0;
        int batch = 0;
        Direction direction = Direction::Download;
        Kind kind = Kind::File;
        Purpose purpose = Purpose::Keep;
        QString name;
        /// The phone file or folder of a download.
        QString remotePath;
        quint64 size = 0;
        QDateTime modified;
        /// The folder a download goes to.
        QString localFolder;
        /// The local file or folder of an upload.
        QString localPath;
        /// The phone folder an upload goes to, and the names in it.
        QString remoteFolder;
        std::shared_ptr<QStringList> namesThere;
        State state = State::Queued;
        quint64 done = 0;
        QString errorText;
        std::optional<Choice> choice;
        bool cancelRequested = false;
        /// Where the file ended up.
        QString resultPath;
    };

    void enqueue(QList<Job> jobs, int after = -1);
    void startNext();
    void runDownload(Job &job);
    void runDownloadFolder(Job &job);
    void runUpload(Job &job);
    void runUploadFolder(Job &job);
    void finishJob(int id, const core::Result<void> &result);
    void complete(int id, State state, const QString &errorText);
    void finishBatch();
    void setState(Job &job, State state);
    void askOrDecide(Job &job, const std::function<void(Choice)> &go);
    void failWaiting();
    [[nodiscard]] Job *job(int id);
    [[nodiscard]] int rowOf(int id) const;
    [[nodiscard]] QString temporaryFolder();
    [[nodiscard]] QString stateText(const Job &job) const;
    void emitRow(int id);

    core::service::FileService *m_service = nullptr;
    QList<Job> m_jobs;
    DesktopOpener m_opener;
    std::unique_ptr<QTemporaryDir> m_temporary;
    std::optional<Choice> m_choiceForAll;
    std::function<void(Choice)> m_pendingDecision;
    QStringList m_changedFolders;
    QList<int> m_dragJobs;
    QStringList m_failures;
    int m_nextId = 1;
    int m_batch = 0;
    int m_runningId = 0;
    int m_conflictId = 0;
    bool m_connected = false;
    /// Keeps complete() from starting the next job while many are failed at once.
    bool m_holdStart = false;
};

} // namespace qpm::ui
