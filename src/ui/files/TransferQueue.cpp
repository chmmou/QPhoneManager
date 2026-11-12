#include "ui/files/TransferQueue.h"

#include "core/Logging.h"
#include "core/codec/Limits.h"
#include "core/service/FileService.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace qpm::ui {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::FolderEntry;
using core::codec::FolderListings;
using core::service::FileService;

namespace {

constexpr int maxFreeNameTries = 9999;

Error unusable()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Unusable name or path"));
}

// The phone writes "\sdcard\a\" and, below "\phone", "/a/".
QString normalizedFolder(const QString &path)
{
    QString folder = path;
    folder.replace(u'/', u'\\');
    while (folder.endsWith(u'\\'))
        folder.chop(1);
    return folder;
}

bool isFinal(TransferQueue::State state)
{
    return state == TransferQueue::State::Done || state == TransferQueue::State::Skipped ||
           state == TransferQueue::State::Failed || state == TransferQueue::State::Cancelled;
}

bool containsName(const QStringList &names, const QString &name)
{
    // The shared storage of the phone does not tell upper from lower case.
    return names.contains(name, Qt::CaseInsensitive);
}

} // namespace

TransferQueue::TransferQueue(FileService *service, QObject *parent)
    : QAbstractListModel(parent), m_service(service),
      m_opener([](const QUrl &url) { return QDesktopServices::openUrl(url); })
{
    Q_ASSERT(service);
    connect(service, &FileService::transferProgress, this, [this](quint64 done, quint64 total) {
        Job *running = job(m_runningId);
        if (!running)
            return;
        running->done = done;
        running->size = std::max(running->size, total);
        emitRow(running->id);
        Q_EMIT progressChanged();
    });
}

TransferQueue::~TransferQueue() = default;

int TransferQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_jobs.size());
}

QVariant TransferQueue::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Job &entry = m_jobs.at(index.row());
    switch (role) {
    case JobIdRole:
        return entry.id;
    case NameRole:
    case Qt::DisplayRole:
        return entry.name;
    case DirectionRole:
        return QVariant::fromValue(entry.direction);
    case StateRole:
        return QVariant::fromValue(entry.state);
    case StateTextRole:
        return stateText(entry);
    case ProgressRole:
        if (entry.state == State::Done)
            return 1.0;
        return entry.size > 0 ? double(entry.done) / double(entry.size) : 0.0;
    case CanCancelRole:
        return !isFinal(entry.state);
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> TransferQueue::roleNames() const
{
    return {
        {JobIdRole, QByteArrayLiteral("jobId")},
        {NameRole, QByteArrayLiteral("name")},
        {DirectionRole, QByteArrayLiteral("direction")},
        {StateRole, QByteArrayLiteral("state")},
        {StateTextRole, QByteArrayLiteral("stateText")},
        {ProgressRole, QByteArrayLiteral("progress")},
        {CanCancelRole, QByteArrayLiteral("canCancel")},
    };
}

bool TransferQueue::isActive() const
{
    return std::any_of(m_jobs.cbegin(), m_jobs.cend(),
                       [](const Job &entry) { return !isFinal(entry.state); });
}

bool TransferQueue::hasFinished() const
{
    return std::any_of(m_jobs.cbegin(), m_jobs.cend(),
                       [](const Job &entry) { return isFinal(entry.state); });
}

QString TransferQueue::statusText() const
{
    if (!isActive())
        return {};
    int total = 0;
    int position = 0;
    for (const Job &entry : m_jobs) {
        if (entry.batch != m_batch || entry.kind != Kind::File)
            continue;
        ++total;
        if (isFinal(entry.state) || entry.id == m_runningId)
            ++position;
    }
    position = std::max(position, 1);
    total = std::max(total, position);
    return tr("Transfer %1 of %2, %3 %").arg(position).arg(total).arg(qRound(progress() * 100));
}

double TransferQueue::progress() const
{
    double finished = 0;
    int total = 0;
    for (const Job &entry : m_jobs) {
        if (entry.batch != m_batch || entry.kind != Kind::File)
            continue;
        ++total;
        if (isFinal(entry.state))
            finished += 1;
        else if (entry.size > 0)
            finished += double(entry.done) / double(entry.size);
    }
    return total > 0 ? finished / total : 0.0;
}

QString TransferQueue::conflictName() const
{
    const auto found = std::find_if(m_jobs.cbegin(), m_jobs.cend(),
                                    [this](const Job &entry) { return entry.id == m_conflictId; });
    return found == m_jobs.cend() ? QString() : found->name;
}

bool TransferQueue::conflictOnPhone() const
{
    const auto found = std::find_if(m_jobs.cbegin(), m_jobs.cend(),
                                    [this](const Job &entry) { return entry.id == m_conflictId; });
    return found != m_jobs.cend() && found->direction == Direction::Upload;
}

void TransferQueue::download(const QString &remoteFolder, const QList<FolderEntry> &entries,
                             const QString &localFolder)
{
    QList<Job> jobs;
    for (const FolderEntry &entry : entries) {
        Job added;
        added.direction = Direction::Download;
        added.kind = entry.isFolder ? Kind::Folder : Kind::File;
        added.name = entry.name;
        added.remotePath = FileService::childPath(remoteFolder, entry.name);
        added.size = entry.size.value_or(0);
        added.modified = entry.modified;
        added.localFolder = localFolder;
        jobs.append(added);
    }
    enqueue(jobs);
}

void TransferQueue::upload(const QStringList &localPaths, const QString &remoteFolder,
                           const QStringList &namesThere)
{
    auto names = std::make_shared<QStringList>(namesThere);
    QList<Job> jobs;
    for (const QString &path : localPaths) {
        const QFileInfo info(path);
        Job added;
        added.direction = Direction::Upload;
        added.kind = info.isDir() ? Kind::Folder : Kind::File;
        added.name = info.fileName();
        added.localPath = info.absoluteFilePath();
        added.size = info.isDir() ? 0 : quint64(std::max<qint64>(info.size(), 0));
        added.remoteFolder = remoteFolder;
        added.namesThere = names;
        jobs.append(added);
    }
    enqueue(jobs);
}

void TransferQueue::openOnDesktop(const QString &remoteFolder, const FolderEntry &entry)
{
    Job added;
    added.purpose = Purpose::Open;
    added.name = entry.name;
    added.remotePath = FileService::childPath(remoteFolder, entry.name);
    added.size = entry.size.value_or(0);
    added.modified = entry.modified;
    enqueue({added});
}

void TransferQueue::fetchForDrag(const QString &remoteFolder, const QList<FolderEntry> &entries)
{
    QList<Job> jobs;
    for (const FolderEntry &entry : entries) {
        if (entry.isFolder)
            continue;
        Job added;
        added.purpose = Purpose::Drag;
        added.name = entry.name;
        added.remotePath = FileService::childPath(remoteFolder, entry.name);
        added.size = entry.size.value_or(0);
        added.modified = entry.modified;
        jobs.append(added);
    }
    enqueue(jobs);
}

void TransferQueue::setDesktopOpener(DesktopOpener opener)
{
    m_opener = std::move(opener);
}

void TransferQueue::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    if (connected)
        return;
    // A running transfer fails through its service; what waits fails here.
    failWaiting();
}

void TransferQueue::failWaiting()
{
    const QString message = messageFor(Error(ErrorCode::ConnectionClosed, {}));
    m_holdStart = true;
    if (m_conflictId != 0)
        complete(m_conflictId, State::Failed, message);
    QList<int> waiting;
    for (const Job &entry : std::as_const(m_jobs)) {
        if (entry.state == State::Queued)
            waiting.append(entry.id);
    }
    for (const int id : std::as_const(waiting))
        complete(id, State::Failed, message);
    m_holdStart = false;
    finishBatch();
}

Result<QString> TransferQueue::targetFolder(const QString &baseFolder,
                                            const QStringList &subfolders)
{
    const QString base = QFileInfo(baseFolder).canonicalFilePath();
    if (base.isEmpty() || !QFileInfo(base).isDir())
        return Error(ErrorCode::NotFound, QStringLiteral("Target folder does not exist"));
    QString folder = base;
    for (const QString &part : subfolders) {
        if (!core::codec::isUsableFileName(part))
            return unusable();
        const QString next = QDir(folder).filePath(part);
        const QFileInfo info(next);
        if (info.isSymLink() || (info.exists() && !info.isDir()))
            return unusable();
        if (!info.exists() && !QDir(folder).mkdir(part))
            return Error(ErrorCode::IoFailure, QStringLiteral("Folder cannot be created"));
        const QString canonical = QFileInfo(next).canonicalFilePath();
        if (canonical != next || !canonical.startsWith(base + u'/'))
            return unusable();
        folder = canonical;
    }
    return folder;
}

Result<QString> TransferQueue::targetPath(const QString &baseFolder, const QStringList &subfolders,
                                          const QString &name)
{
    if (!core::codec::isUsableFileName(name))
        return unusable();
    const Result<QString> folder = targetFolder(baseFolder, subfolders);
    if (!folder.hasValue())
        return folder.error();
    return core::limits::safeDownloadPath(folder.value(), name);
}

QString TransferQueue::freeName(const QString &name,
                                const std::function<bool(const QString &)> &taken)
{
    const qsizetype dot = name.lastIndexOf(u'.');
    const QString stem = dot <= 0 ? name : name.left(dot);
    const QString suffix = dot <= 0 ? QString() : name.mid(dot);
    for (int i = 1; i <= maxFreeNameTries; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(suffix);
        if (!taken(candidate))
            return candidate;
    }
    return {};
}

QString TransferQueue::messageFor(const Error &error)
{
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::PermissionDenied:
        return tr("The app on the phone may not access the files. Allow it in the Android "
                  "settings and try again.");
    case ErrorCode::AccessDenied:
        return tr("The phone refused access to this folder. Allow it on the phone and try again.");
    case ErrorCode::NotFound:
        return tr("The file or folder is no longer on the phone. Reload the folder.");
    case ErrorCode::Rejected:
        return tr("This name is already taken. Choose another name.");
    case ErrorCode::IoFailure:
        return tr("The file cannot be stored. Check the target folder and the free space.");
    case ErrorCode::LimitExceeded:
        return tr("The file is too large.");
    case ErrorCode::NotSupported:
        return tr("This needs a connection over Wi-Fi or USB. Connect the phone that way and "
                  "try again.");
    case ErrorCode::InvalidInput:
        return tr("The name cannot be used here. The file was left out.");
    case ErrorCode::Timeout:
        return tr("The phone did not answer in time. Try again.");
    case ErrorCode::ConnectionClosed:
        return tr("The connection to the phone ended. Connect again and try again.");
    default:
        break;
    }
    return tr("The phone could not do this. Try again.");
}

int TransferQueue::idAt(int row) const
{
    return row >= 0 && row < m_jobs.size() ? m_jobs.at(row).id : 0;
}

void TransferQueue::cancel(int id)
{
    Job *entry = job(id);
    if (!entry || isFinal(entry->state))
        return;
    if (entry->state == State::Queued || entry->state == State::WaitingForDecision) {
        complete(id, State::Cancelled, QString());
        return;
    }
    entry->cancelRequested = true;
    if (entry->kind == Kind::File)
        m_service->cancelTransfer();
}

void TransferQueue::cancelAll()
{
    QList<int> open;
    for (const Job &entry : std::as_const(m_jobs)) {
        if (!isFinal(entry.state))
            open.append(entry.id);
    }
    // The running one last, so that nothing queued starts in between.
    std::stable_partition(open.begin(), open.end(), [this](int id) { return id != m_runningId; });
    for (const int id : std::as_const(open))
        cancel(id);
}

void TransferQueue::resolveConflict(int id, Choice choice, bool forAll)
{
    if (id == 0 || id != m_conflictId)
        return;
    Job *entry = job(id);
    if (!entry)
        return;
    entry->choice = choice;
    if (forAll)
        m_choiceForAll = choice;
    m_conflictId = 0;
    Q_EMIT conflictChanged();
    setState(*entry, State::Running);
    const std::function<void(Choice)> decide = std::exchange(m_pendingDecision, {});
    if (decide)
        decide(choice);
}

void TransferQueue::clearFinished()
{
    for (int row = int(m_jobs.size()) - 1; row >= 0; --row) {
        if (!isFinal(m_jobs.at(row).state))
            continue;
        beginRemoveRows({}, row, row);
        m_jobs.removeAt(row);
        endRemoveRows();
    }
    Q_EMIT countChanged();
}

void TransferQueue::enqueue(QList<Job> jobs, int after)
{
    if (jobs.isEmpty())
        return;
    const bool wasActive = isActive();
    if (!wasActive) {
        // A new batch starts; what the last one left in the list goes.
        clearFinished();
        ++m_batch;
        m_choiceForAll.reset();
        m_failures.clear();
        m_changedFolders.clear();
    }
    for (Job &added : jobs) {
        added.id = m_nextId++;
        added.batch = m_batch;
        if (added.purpose == Purpose::Drag)
            m_dragJobs.append(added.id);
    }
    const int first = after < 0 ? int(m_jobs.size()) : after + 1;
    beginInsertRows({}, first, first + int(jobs.size()) - 1);
    for (qsizetype i = 0; i < jobs.size(); ++i)
        m_jobs.insert(first + i, jobs.at(i));
    endInsertRows();
    Q_EMIT countChanged();
    if (!wasActive)
        Q_EMIT activeChanged();
    Q_EMIT progressChanged();
    startNext();
}

void TransferQueue::startNext()
{
    if (m_runningId != 0)
        return;
    const auto next = std::find_if(m_jobs.begin(), m_jobs.end(),
                                   [](const Job &entry) { return entry.state == State::Queued; });
    if (next == m_jobs.end()) {
        finishBatch();
        return;
    }
    if (!m_connected) {
        failWaiting();
        return;
    }
    Job &entry = *next;
    m_runningId = entry.id;
    setState(entry, State::Running);
    if (entry.direction == Direction::Download && entry.kind == Kind::File)
        runDownload(entry);
    else if (entry.direction == Direction::Download)
        runDownloadFolder(entry);
    else if (entry.kind == Kind::File)
        runUpload(entry);
    else
        runUploadFolder(entry);
}

void TransferQueue::askOrDecide(Job &entry, const std::function<void(Choice)> &go)
{
    if (entry.choice) {
        go(*entry.choice);
        return;
    }
    if (m_choiceForAll) {
        go(*m_choiceForAll);
        return;
    }
    m_pendingDecision = go;
    m_conflictId = entry.id;
    setState(entry, State::WaitingForDecision);
    Q_EMIT conflictChanged();
}

void TransferQueue::runDownload(Job &entry)
{
    const int id = entry.id;
    const QString folder = entry.purpose == Purpose::Keep ? entry.localFolder : temporaryFolder();
    if (folder.isEmpty()) {
        finishJob(id, Error(ErrorCode::IoFailure, QStringLiteral("No temporary folder")));
        return;
    }
    const Result<QString> path = targetPath(folder, {}, entry.name);
    if (!path.hasValue()) {
        finishJob(id, path.error());
        return;
    }
    const auto go = [this, id, folder, path = path.value()](Choice choice) {
        Job *current = job(id);
        if (!current)
            return;
        if (choice == Choice::Skip) {
            complete(id, State::Skipped, QString());
            return;
        }
        QString target = path;
        if (choice == Choice::KeepBoth) {
            const QDir dir(folder);
            const QString name = freeName(current->name, [&dir](const QString &candidate) {
                return QFileInfo::exists(dir.filePath(candidate));
            });
            const Result<QString> free = targetPath(folder, {}, name);
            if (!free.hasValue()) {
                finishJob(id, free.error());
                return;
            }
            target = free.value();
        }
        current->resultPath = target;
        m_service->download({current->remotePath, current->size, current->modified}, target)
            .then(this, [this, id](const Result<void> &done) { finishJob(id, done); });
    };
    // Temporary copies to open or drag are replaced without asking.
    if (entry.purpose == Purpose::Keep && QFileInfo::exists(path.value()))
        askOrDecide(entry, go);
    else
        go(Choice::Replace);
}

void TransferQueue::runDownloadFolder(Job &entry)
{
    const int id = entry.id;
    const Result<QString> root = targetFolder(entry.localFolder, {entry.name});
    if (!root.hasValue()) {
        finishJob(id, root.error());
        return;
    }
    m_service->listTree(entry.remotePath)
        .then(this, [this, id, root = root.value()](const Result<FolderListings> &listed) {
            Job *current = job(id);
            if (!current)
                return;
            if (current->cancelRequested) {
                complete(id, State::Cancelled, QString());
                return;
            }
            if (!listed.hasValue()) {
                finishJob(id, listed.error());
                return;
            }
            const QString base = normalizedFolder(listed.value().folders.constFirst().parentPath);
            int skipped = listed.value().skipped;
            QList<Job> children;
            for (const auto &folder : listed.value().folders) {
                const QString path = normalizedFolder(folder.parentPath);
                if (path != base && !path.startsWith(base + u'\\')) {
                    skipped += int(folder.entries.size());
                    continue;
                }
                const QStringList below = path.mid(base.size()).split(u'\\', Qt::SkipEmptyParts);
                const Result<QString> local = targetFolder(root, below);
                if (!local.hasValue()) {
                    skipped += int(folder.entries.size());
                    continue;
                }
                const QString remote = below.isEmpty()
                                           ? current->remotePath
                                           : current->remotePath + u'\\' + below.join(u'\\');
                for (const FolderEntry &item : folder.entries) {
                    if (item.isFolder) {
                        // Keeps empty folders; the listing of a full one follows on its own.
                        if (!targetFolder(local.value(), {item.name}).hasValue())
                            ++skipped;
                        continue;
                    }
                    Job child;
                    child.direction = Direction::Download;
                    child.name = item.name;
                    child.remotePath = FileService::childPath(remote, item.name);
                    child.size = item.size.value_or(0);
                    child.modified = item.modified;
                    child.localFolder = local.value();
                    children.append(child);
                }
            }
            if (skipped > 0) {
                m_failures.append(tr("%n entry(s) of the folder %1 were left out, because their "
                                     "name cannot be used here.",
                                     "", skipped)
                                      .arg(current->name));
            }
            // The files follow right after their folder, before anything queued later.
            enqueue(children, rowOf(id));
            finishJob(id, Result<void>());
        });
}

void TransferQueue::runUpload(Job &entry)
{
    const int id = entry.id;
    if (!core::codec::isUsableFileName(entry.name)) {
        finishJob(id, unusable());
        return;
    }
    const auto send = [this, id]() {
        Job *current = job(id);
        if (!current)
            return;
        if (current->cancelRequested) {
            complete(id, State::Cancelled, QString());
            return;
        }
        m_service->upload(current->localPath, current->remoteFolder, current->name)
            .then(this, [this, id](const Result<QString> &sent) {
                Job *done = job(id);
                if (done && sent.hasValue()) {
                    done->resultPath = FileService::childPath(done->remoteFolder, sent.value());
                    if (done->namesThere)
                        done->namesThere->append(sent.value());
                    if (!m_changedFolders.contains(done->remoteFolder))
                        m_changedFolders.append(done->remoteFolder);
                }
                finishJob(id, sent.hasValue() ? Result<void>() : Result<void>(sent.error()));
            });
    };
    const auto go = [this, id, send](Choice choice) {
        Job *current = job(id);
        if (!current)
            return;
        if (choice == Choice::Skip) {
            complete(id, State::Skipped, QString());
            return;
        }
        // The phone keeps both on its own: it gives the new file a free name.
        if (choice == Choice::KeepBoth) {
            send();
            return;
        }
        QString existing = current->name;
        if (current->namesThere) {
            for (const QString &name : std::as_const(*current->namesThere)) {
                if (name.compare(current->name, Qt::CaseInsensitive) == 0)
                    existing = name;
            }
        }
        m_service->remove(FileService::childPath(current->remoteFolder, existing))
            .then(this, [this, id, send, existing](const Result<void> &removed) {
                if (!removed.hasValue() && removed.error().code() != ErrorCode::NotFound) {
                    finishJob(id, removed.error());
                    return;
                }
                if (Job *current = job(id); current && current->namesThere)
                    current->namesThere->removeAll(existing);
                send();
            });
    };
    if (entry.namesThere && containsName(*entry.namesThere, entry.name))
        askOrDecide(entry, go);
    else
        send();
}

void TransferQueue::runUploadFolder(Job &entry)
{
    const int id = entry.id;
    if (!core::codec::isUsableFileName(entry.name)) {
        finishJob(id, unusable());
        return;
    }
    const QString remote = FileService::childPath(entry.remoteFolder, entry.name);
    m_service->makeFolder(entry.remoteFolder, entry.name)
        .then(this,
              [this, id, remote](const Result<void> &made) -> QFuture<Result<QList<FolderEntry>>> {
                  Job *current = job(id);
                  if (!current)
                      return QtFuture::makeReadyValueFuture(
                          Result<QList<FolderEntry>>(Error(ErrorCode::Cancelled, {})));
                  if (!made.hasValue())
                      return QtFuture::makeReadyValueFuture(
                          Result<QList<FolderEntry>>(made.error()));
                  if (!m_changedFolders.contains(current->remoteFolder))
                      m_changedFolders.append(current->remoteFolder);
                  return m_service->list(remote);
              })
        .unwrap()
        .then(this, [this, id, remote](const Result<QList<FolderEntry>> &listed) {
            Job *current = job(id);
            if (!current)
                return;
            if (current->cancelRequested) {
                complete(id, State::Cancelled, QString());
                return;
            }
            if (!listed.hasValue()) {
                finishJob(id, listed.error());
                return;
            }
            auto names = std::make_shared<QStringList>();
            for (const FolderEntry &item : listed.value())
                names->append(item.name);
            // Symbolic links are left out, so nothing outside the chosen folder goes along.
            const QFileInfoList local =
                QDir(current->localPath)
                    .entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden |
                                       QDir::NoSymLinks,
                                   QDir::Name | QDir::DirsFirst);
            QList<Job> children;
            for (const QFileInfo &info : local) {
                Job child;
                child.direction = Direction::Upload;
                child.kind = info.isDir() ? Kind::Folder : Kind::File;
                child.name = info.fileName();
                child.localPath = info.absoluteFilePath();
                child.size = info.isDir() ? 0 : quint64(std::max<qint64>(info.size(), 0));
                child.remoteFolder = remote;
                child.namesThere = names;
                children.append(child);
            }
            // The files follow right after their folder, before anything queued later.
            enqueue(children, rowOf(id));
            finishJob(id, Result<void>());
        });
}

void TransferQueue::finishJob(int id, const Result<void> &result)
{
    Job *entry = job(id);
    if (!entry)
        return;
    if (result.hasValue()) {
        complete(id, State::Done, QString());
        return;
    }
    if (result.error().code() == ErrorCode::Cancelled || entry->cancelRequested) {
        complete(id, State::Cancelled, QString());
        return;
    }
    qCInfo(core::lcUi) << "Transfer failed:" << result.error().message();
    complete(id, State::Failed, messageFor(result.error()));
}

void TransferQueue::complete(int id, State state, const QString &errorText)
{
    Job *entry = job(id);
    if (!entry || isFinal(entry->state))
        return;
    entry->errorText = errorText;
    if (state == State::Done && entry->kind == Kind::File)
        entry->done = entry->size;
    setState(*entry, state);
    if (state == State::Failed)
        m_failures.append(QStringLiteral("%1: %2").arg(entry->name, errorText));
    if (m_conflictId == id) {
        m_conflictId = 0;
        m_pendingDecision = {};
        Q_EMIT conflictChanged();
    }
    if (state == State::Done && entry->purpose == Purpose::Open &&
        !m_opener(QUrl::fromLocalFile(entry->resultPath))) {
        m_failures.append(tr("No program on this computer opens %1.").arg(entry->name));
    }
    if (m_dragJobs.contains(id)) {
        const bool allDone = std::all_of(m_dragJobs.cbegin(), m_dragJobs.cend(), [this](int drag) {
            const Job *found = job(drag);
            return !found || isFinal(found->state);
        });
        if (allDone) {
            QList<QUrl> urls;
            for (const int drag : std::as_const(m_dragJobs)) {
                const Job *found = job(drag);
                if (found && found->state == State::Done)
                    urls.append(QUrl::fromLocalFile(found->resultPath));
            }
            m_dragJobs.clear();
            if (!urls.isEmpty())
                Q_EMIT dragFilesReady(urls);
        }
    }
    if (m_runningId == id)
        m_runningId = 0;
    Q_EMIT progressChanged();
    if (!m_holdStart)
        startNext();
}

void TransferQueue::finishBatch()
{
    if (isActive())
        return;
    const QStringList changed = std::exchange(m_changedFolders, {});
    for (const QString &folder : changed)
        Q_EMIT phoneFolderChanged(folder);
    const QStringList failures = std::exchange(m_failures, {});
    m_choiceForAll.reset();
    Q_EMIT activeChanged();
    Q_EMIT progressChanged();
    if (failures.isEmpty())
        return;
    Q_EMIT errorOccurred(failures.size() == 1
                             ? failures.constFirst()
                             : tr("%n transfer(s) did not work. %1", "", int(failures.size()))
                                   .arg(failures.constFirst()));
}

void TransferQueue::setState(Job &entry, State state)
{
    entry.state = state;
    emitRow(entry.id);
}

TransferQueue::Job *TransferQueue::job(int id)
{
    const auto found = std::find_if(m_jobs.begin(), m_jobs.end(),
                                    [id](const Job &entry) { return entry.id == id; });
    return found == m_jobs.end() ? nullptr : &*found;
}

int TransferQueue::rowOf(int id) const
{
    for (qsizetype row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row).id == id)
            return int(row);
    }
    return -1;
}

QString TransferQueue::temporaryFolder()
{
    if (!m_temporary) {
        m_temporary = std::make_unique<QTemporaryDir>(
            QDir(QDir::tempPath()).filePath(QStringLiteral("qphonemanager-XXXXXX")));
    }
    return m_temporary->isValid() ? m_temporary->path() : QString();
}

QString TransferQueue::stateText(const Job &entry) const
{
    switch (entry.state) {
    case State::Queued:
        return tr("Waiting");
    case State::Running:
        if (entry.kind == Kind::Folder)
            return tr("Reading the folder");
        return entry.size > 0
                   ? tr("%1 %").arg(qRound(100.0 * double(entry.done) / double(entry.size)))
                   : tr("Running");
    case State::WaitingForDecision:
        return tr("Waiting for your choice");
    case State::Done:
        return tr("Done");
    case State::Skipped:
        return tr("Skipped");
    case State::Failed:
        return entry.errorText;
    case State::Cancelled:
        return tr("Cancelled");
    }
    return {};
}

void TransferQueue::emitRow(int id)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    const QModelIndex changed = index(row);
    Q_EMIT dataChanged(changed, changed);
}

} // namespace qpm::ui
