#include "ui/files/FileBrowserModel.h"

#include "core/Logging.h"
#include "core/service/FileService.h"
#include "core/service/ThumbnailChannel.h"
#include "ui/contacts/PhoneImageProvider.h"

#include <QBuffer>
#include <QFileInfo>
#include <QLocale>
#include <QTransform>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace qpm::ui {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::FolderEntry;
using core::service::FileService;
using core::service::Thumbnail;

namespace {

constexpr int thumbnailSide = 256;

using DecodedThumbnails = std::optional<QList<std::pair<QString, QImage>>>;
constexpr std::array storageOrder = {QLatin1StringView("sdcard"), QLatin1StringView("external"),
                                     QLatin1StringView("phone"), QLatin1StringView("dual")};
constexpr std::array pictureSuffixes = {QLatin1StringView("jpg"),  QLatin1StringView("jpeg"),
                                        QLatin1StringView("png"),  QLatin1StringView("gif"),
                                        QLatin1StringView("bmp"),  QLatin1StringView("webp"),
                                        QLatin1StringView("heic"), QLatin1StringView("heif")};
constexpr std::array videoSuffixes = {QLatin1StringView("mp4"),  QLatin1StringView("mov"),
                                      QLatin1StringView("3gp"),  QLatin1StringView("mkv"),
                                      QLatin1StringView("webm"), QLatin1StringView("mpg"),
                                      QLatin1StringView("mpeg"), QLatin1StringView("avi")};

QString suffixOf(const QString &name)
{
    const qsizetype dot = name.lastIndexOf(u'.');
    return dot <= 0 ? QString() : name.mid(dot + 1).toLower();
}

bool isPicture(const QString &name)
{
    const QString suffix = suffixOf(name);
    return std::find(pictureSuffixes.cbegin(), pictureSuffixes.cend(), suffix) !=
           pictureSuffixes.cend();
}

bool hasThumbnail(const QString &name)
{
    const QString suffix = suffixOf(name);
    return isPicture(name) ||
           std::find(videoSuffixes.cbegin(), videoSuffixes.cend(), suffix) != videoSuffixes.cend();
}

int storageRank(const QString &name)
{
    const auto found = std::find(storageOrder.cbegin(), storageOrder.cend(), name);
    return int(found - storageOrder.cbegin());
}

// Turns a picture upright by its EXIF orientation 1 to 8.
QImage upright(const QImage &image, int orientation)
{
    QTransform transform;
    switch (orientation) {
    case 2:
        transform.scale(-1, 1);
        break;
    case 3:
        transform.rotate(180);
        break;
    case 4:
        transform.scale(1, -1);
        break;
    case 5:
        transform.rotate(90);
        transform.scale(-1, 1);
        break;
    case 6:
        transform.rotate(90);
        break;
    case 7:
        transform.rotate(270);
        transform.scale(-1, 1);
        break;
    case 8:
        transform.rotate(270);
        break;
    default:
        return image;
    }
    return image.transformed(transform);
}

QList<std::pair<QString, QImage>> decodeThumbnails(const QList<Thumbnail> &thumbnails)
{
    QList<std::pair<QString, QImage>> decoded;
    for (const Thumbnail &thumbnail : thumbnails) {
        QByteArray bytes = thumbnail.image;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::ReadOnly))
            continue;
        const Result<QImage> image = readImageSafely(&buffer, QSize(thumbnailSide, thumbnailSide));
        if (image.hasValue())
            decoded.append({thumbnail.name, upright(image.value(), thumbnail.orientation)});
    }
    return decoded;
}

} // namespace

FileBrowserModel::FileBrowserModel(FileService *service,
                                   core::service::ThumbnailChannel *thumbnails,
                                   TransferQueue *transfers, std::shared_ptr<PhotoSource> photos,
                                   QObject *parent)
    : QAbstractListModel(parent), m_service(service), m_thumbnails(thumbnails),
      m_transfers(transfers), m_photos(std::move(photos)),
      m_emptyText(tr("Connect your phone to browse its files."))
{
    Q_ASSERT(service && thumbnails && transfers && m_photos);
    connect(transfers, &TransferQueue::phoneFolderChanged, this, [this](const QString &folder) {
        if (m_connected && folder == m_path)
            list();
    });
    connect(transfers, &TransferQueue::dragFilesReady, this, &FileBrowserModel::dragReady);
}

int FileBrowserModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant FileBrowserModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const FolderEntry &entry = m_entries.at(index.row());
    const QLocale locale;
    const QString sizeText = entry.isFolder
                                 ? QString()
                                 : locale.formattedDataSize(qint64(entry.size.value_or(0)), 1,
                                                            QLocale::DataSizeTraditionalFormat);
    const QString modifiedText =
        entry.modified.isValid()
            ? locale.toString(entry.modified.toLocalTime(), QLocale::ShortFormat)
            : QString();
    switch (role) {
    case NameRole:
        return entry.name;
    case DisplayNameRole:
    case Qt::DisplayRole:
        return atRoot() ? storageName(entry.name) : entry.name;
    case IsFolderRole:
        return entry.isFolder;
    case SizeRole:
        return double(entry.size.value_or(0));
    case SizeTextRole:
        return sizeText;
    case ModifiedRole:
        return entry.modified;
    case ModifiedTextRole:
        return modifiedText;
    case DetailsRole:
        if (entry.isFolder)
            return atRoot() ? tr("Storage") : tr("Folder");
        return modifiedText.isEmpty() ? sizeText : tr("%1, %2").arg(sizeText, modifiedText);
    case ThumbnailUrlRole: {
        const quint64 id = m_thumbnailIds.value(entry.name);
        return id == 0 ? QString() : QStringLiteral("image://qpm/thumb/%1").arg(id);
    }
    case IconNameRole:
        if (entry.isFolder && atRoot()) {
            return entry.name == QLatin1StringView("external") ? QStringLiteral("device-sd-card")
                                                               : QStringLiteral("device-mobile");
        }
        if (entry.isFolder)
            return QStringLiteral("folder");
        return isPicture(entry.name) ? QStringLiteral("photo") : QStringLiteral("file");
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> FileBrowserModel::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {DisplayNameRole, QByteArrayLiteral("displayName")},
        {IsFolderRole, QByteArrayLiteral("isFolder")},
        {SizeRole, QByteArrayLiteral("size")},
        {SizeTextRole, QByteArrayLiteral("sizeText")},
        {ModifiedRole, QByteArrayLiteral("modified")},
        {ModifiedTextRole, QByteArrayLiteral("modifiedText")},
        {DetailsRole, QByteArrayLiteral("details")},
        {ThumbnailUrlRole, QByteArrayLiteral("thumbnailUrl")},
        {IconNameRole, QByteArrayLiteral("iconName")},
    };
}

QStringList FileBrowserModel::crumbNames() const
{
    QStringList result = {tr("Phone")};
    const QStringList parts = m_path.split(u'\\', Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < parts.size(); ++i)
        result.append(i == 0 ? storageName(parts.at(i)) : parts.at(i));
    return result;
}

QStringList FileBrowserModel::crumbPaths() const
{
    QStringList result = {QString()};
    QString path;
    const QStringList parts = m_path.split(u'\\', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        path = FileService::childPath(path, part);
        result.append(path);
    }
    return result;
}

QString FileBrowserModel::storageName(const QString &name)
{
    if (name == QLatin1StringView("sdcard"))
        return tr("Internal storage");
    if (name == QLatin1StringView("external"))
        return tr("Memory card");
    if (name == QLatin1StringView("phone"))
        return tr("Phone storage");
    if (name == QLatin1StringView("dual"))
        return tr("Second profile");
    return name;
}

void FileBrowserModel::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    ++m_generation;
    m_cut.clear();
    m_path.clear();
    setEntries({});
    m_photos->clearThumbnails();
    m_thumbnailIds.clear();
    m_loaded = false;
    Q_EMIT pathChanged();
    Q_EMIT canEditChanged();
    Q_EMIT canPasteChanged();
    setEmptyText(connected ? QString() : tr("Connect your phone to browse its files."));
}

bool FileBrowserModel::load()
{
    if (!m_connected)
        return false;
    if (!m_loaded)
        list();
    return true;
}

bool FileBrowserModel::open(const QString &path)
{
    if (!m_connected)
        return false;
    if (path != m_path) {
        m_path = path;
        ++m_generation;
        m_thumbnailIds.clear();
        setEntries({});
        Q_EMIT pathChanged();
    }
    list();
    return true;
}

bool FileBrowserModel::openRow(int row)
{
    if (row < 0 || row >= m_entries.size() || !m_connected)
        return false;
    const FolderEntry &entry = m_entries.at(row);
    if (entry.isFolder)
        return open(FileService::childPath(m_path, entry.name));
    return openOnDesktop(entry.name);
}

bool FileBrowserModel::up()
{
    if (atRoot())
        return false;
    return open(FileService::parentPath(m_path));
}

bool FileBrowserModel::refresh()
{
    if (!m_connected)
        return false;
    list();
    return true;
}

bool FileBrowserModel::download(const QStringList &names, const QUrl &folder)
{
    const QList<FolderEntry> chosen = entriesNamed(names);
    if (!m_connected || atRoot() || chosen.isEmpty())
        return false;
    const QString local = folder.toLocalFile();
    if (local.isEmpty() || !QFileInfo(local).isDir()) {
        Q_EMIT errorOccurred(tr("Choose a folder on this computer."));
        return false;
    }
    m_transfers->download(m_path, chosen, local);
    return true;
}

bool FileBrowserModel::upload(const QList<QUrl> &urls)
{
    if (!m_connected || urls.isEmpty())
        return false;
    if (atRoot()) {
        Q_EMIT errorOccurred(tr("Open a folder on the phone first, then copy into it."));
        return false;
    }
    QStringList paths;
    for (const QUrl &url : urls) {
        const QString local = url.toLocalFile();
        if (!local.isEmpty() && QFileInfo::exists(local))
            paths.append(local);
    }
    if (paths.isEmpty()) {
        Q_EMIT errorOccurred(tr("Only files and folders on this computer can be copied."));
        return false;
    }
    m_transfers->upload(paths, m_path, names());
    return true;
}

bool FileBrowserModel::mkdir(const QString &name)
{
    if (!m_connected || atRoot())
        return false;
    if (const QString problem = nameProblem(name); !problem.isEmpty()) {
        Q_EMIT errorOccurred(problem);
        return false;
    }
    beginWork();
    m_service->makeFolder(m_path, name).then(this, [this, name](const Result<void> &made) {
        endWork();
        if (!made.hasValue()) {
            Q_EMIT errorOccurred(TransferQueue::messageFor(made.error()));
            return;
        }
        Q_EMIT infoMessage(tr("The folder %1 is created.").arg(name));
        list();
    });
    return true;
}

bool FileBrowserModel::rename(const QString &oldName, const QString &newName)
{
    if (!m_connected || atRoot() || rowOf(oldName) < 0)
        return false;
    if (const QString problem = nameProblem(newName, oldName); !problem.isEmpty()) {
        Q_EMIT errorOccurred(problem);
        return false;
    }
    if (newName == oldName)
        return true;
    beginWork();
    m_service->rename(FileService::childPath(m_path, oldName), newName)
        .then(this, [this](const Result<void> &renamed) {
            endWork();
            if (!renamed.hasValue())
                Q_EMIT errorOccurred(TransferQueue::messageFor(renamed.error()));
            list();
        });
    return true;
}

bool FileBrowserModel::remove(const QStringList &names)
{
    const QList<FolderEntry> chosen = entriesNamed(names);
    if (!m_connected || atRoot() || chosen.isEmpty())
        return false;
    QStringList paths;
    for (const FolderEntry &entry : chosen)
        paths.append(FileService::childPath(m_path, entry.name));
    runOneAfterAnother(
        paths, [this](const QString &path) { return m_service->remove(path); },
        tr("%n entry(s) deleted on the phone.", "", int(paths.size())));
    return true;
}

bool FileBrowserModel::openOnDesktop(const QString &name)
{
    const int row = rowOf(name);
    if (!m_connected || row < 0 || m_entries.at(row).isFolder)
        return false;
    m_transfers->openOnDesktop(m_path, m_entries.at(row));
    return true;
}

bool FileBrowserModel::cut(const QStringList &names)
{
    const QList<FolderEntry> chosen = entriesNamed(names);
    if (!m_connected || atRoot() || chosen.isEmpty())
        return false;
    m_cut.clear();
    for (const FolderEntry &entry : chosen)
        m_cut.append(FileService::childPath(m_path, entry.name));
    Q_EMIT canPasteChanged();
    Q_EMIT infoMessage(
        tr("%n entry(s) cut. Open the target folder and paste them there.", "", int(m_cut.size())));
    return true;
}

bool FileBrowserModel::paste()
{
    if (!canPaste() || atRoot())
        return false;
    QStringList paths;
    for (const QString &path : std::as_const(m_cut)) {
        if (FileService::parentPath(path) != m_path)
            paths.append(path);
    }
    m_cut.clear();
    Q_EMIT canPasteChanged();
    if (paths.isEmpty())
        return false;
    const QString target = m_path;
    runOneAfterAnother(
        paths, [this, target](const QString &path) { return m_service->move(path, target); },
        tr("%n entry(s) moved.", "", int(paths.size())));
    return true;
}

bool FileBrowserModel::prepareDrag(const QStringList &names)
{
    QList<FolderEntry> files = entriesNamed(names);
    files.removeIf([](const FolderEntry &entry) { return entry.isFolder; });
    if (!m_connected || files.isEmpty())
        return false;
    m_transfers->fetchForDrag(m_path, files);
    return true;
}

int FileBrowserModel::rowOf(const QString &name) const
{
    for (qsizetype row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).name == name)
            return int(row);
    }
    return -1;
}

QString FileBrowserModel::nameAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).name : QString();
}

bool FileBrowserModel::isFolderAt(int row) const
{
    return row >= 0 && row < m_entries.size() && m_entries.at(row).isFolder;
}

QString FileBrowserModel::nameProblem(const QString &name, const QString &oldName) const
{
    if (name.trimmed().isEmpty())
        return tr("Enter a name.");
    if (name != name.trimmed())
        return tr("A name cannot start or end with a space.");
    if (!core::codec::isUsableFileName(name))
        return tr("Use a name without / or \\ and without control characters.");
    // The shared storage of the phone does not tell upper from lower case.
    for (const FolderEntry &entry : m_entries) {
        if (entry.name.compare(name, Qt::CaseInsensitive) == 0 &&
            entry.name.compare(oldName, Qt::CaseInsensitive) != 0) {
            return tr("There is already an entry named %1 here.").arg(entry.name);
        }
    }
    return {};
}

void FileBrowserModel::list()
{
    m_loaded = true;
    const quint64 generation = ++m_generation;
    const QString path = m_path;
    beginWork();
    m_service->list(path).then(this, [this, generation](const Result<QList<FolderEntry>> &listed) {
        endWork();
        if (generation != m_generation)
            return;
        if (!listed.hasValue()) {
            setEntries({});
            setEmptyText(tr("This folder cannot be shown."));
            Q_EMIT errorOccurred(TransferQueue::messageFor(listed.error()));
            return;
        }
        QList<FolderEntry> entries = listed.value();
        const bool root = atRoot();
        std::sort(entries.begin(), entries.end(),
                  [root](const FolderEntry &a, const FolderEntry &b) {
                      if (root)
                          return storageRank(a.name) < storageRank(b.name);
                      if (a.isFolder != b.isFolder)
                          return a.isFolder;
                      return QString::localeAwareCompare(a.name, b.name) < 0;
                  });
        setEntries(entries);
        setEmptyText(root ? tr("The phone shows no storage.") : tr("This folder is empty."));
        QStringList pictures;
        for (const FolderEntry &entry : std::as_const(m_entries)) {
            if (!entry.isFolder && hasThumbnail(entry.name) && !m_thumbnailIds.contains(entry.name))
                pictures.append(entry.name);
        }
        fetchThumbnails(generation, pictures);
    });
}

void FileBrowserModel::setEntries(QList<FolderEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    Q_EMIT countChanged();
}

void FileBrowserModel::setEmptyText(const QString &text)
{
    if (m_emptyText == text)
        return;
    m_emptyText = text;
    Q_EMIT emptyTextChanged();
}

void FileBrowserModel::fetchThumbnails(quint64 generation, QStringList names)
{
    if (names.isEmpty() || generation != m_generation)
        return;
    const QStringList batch = names.mid(0, core::service::ThumbnailChannel::maxNamesPerRequest);
    names.remove(0, batch.size());
    m_thumbnails->fetch(m_path, batch)
        .then(this,
              [](const Result<QList<Thumbnail>> &fetched) -> QFuture<DecodedThumbnails> {
                  if (!fetched.hasValue()) {
                      qCInfo(core::lcUi) << "Thumbnails not loaded:" << fetched.error().message();
                      return QtFuture::makeReadyValueFuture(DecodedThumbnails());
                  }
                  // Decoding many pictures would hold up the window.
                  return QtConcurrent::run([thumbnails = fetched.value()]() {
                      return DecodedThumbnails(decodeThumbnails(thumbnails));
                  });
              })
        .unwrap()
        .then(this, [this, generation, names](const DecodedThumbnails &decoded) {
            if (generation != m_generation || !decoded)
                return;
            for (const auto &[name, image] : *decoded) {
                const int row = rowOf(name);
                if (row < 0)
                    continue;
                m_thumbnailIds.insert(name, m_photos->addThumbnail(image));
                const QModelIndex changed = index(row);
                Q_EMIT dataChanged(changed, changed, {ThumbnailUrlRole});
            }
            fetchThumbnails(generation, names);
        });
}

void FileBrowserModel::runOneAfterAnother(
    QStringList paths, std::function<QFuture<Result<void>>(const QString &)> step,
    const QString &doneText)
{
    if (paths.isEmpty())
        return;
    const QString path = paths.takeFirst();
    beginWork();
    step(path).then(this, [this, paths, step, doneText](const Result<void> &done) {
        endWork();
        if (!done.hasValue()) {
            Q_EMIT errorOccurred(TransferQueue::messageFor(done.error()));
            list();
            return;
        }
        if (!paths.isEmpty()) {
            runOneAfterAnother(paths, step, doneText);
            return;
        }
        Q_EMIT infoMessage(doneText);
        list();
    });
}

void FileBrowserModel::beginWork()
{
    if (m_busy++ == 0)
        Q_EMIT busyChanged();
}

void FileBrowserModel::endWork()
{
    if (--m_busy == 0)
        Q_EMIT busyChanged();
}

QList<FolderEntry> FileBrowserModel::entriesNamed(const QStringList &names) const
{
    QList<FolderEntry> chosen;
    for (const FolderEntry &entry : m_entries) {
        if (names.contains(entry.name))
            chosen.append(entry);
    }
    return chosen;
}

QStringList FileBrowserModel::names() const
{
    QStringList result;
    for (const FolderEntry &entry : m_entries)
        result.append(entry.name);
    return result;
}

} // namespace qpm::ui
