#include "ui/apps/AppListModel.h"

#include "core/Logging.h"
#include "core/service/AppsService.h"
#include "core/service/FileService.h"
#include "ui/contacts/PhoneImageProvider.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QtConcurrentRun>

#include <algorithm>
#include <optional>
#include <utility>

namespace qpm::ui {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::AppInfo;
using core::service::AppIcon;

namespace {

constexpr int iconDecodeSide = 96;

using DecodedIcons = std::optional<QList<std::pair<QString, QImage>>>;

QList<std::pair<QString, QImage>> decodeIcons(const QList<AppIcon> &icons)
{
    QList<std::pair<QString, QImage>> decoded;
    for (const AppIcon &icon : icons) {
        QByteArray bytes = icon.png;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::ReadOnly))
            continue;
        const Result<QImage> image =
            readImageSafely(&buffer, QSize(iconDecodeSide, iconDecodeSide));
        if (image.hasValue())
            decoded.append({icon.package, image.value()});
    }
    return decoded;
}

bool labelLess(const AppInfo &a, const AppInfo &b)
{
    const int order = QString::localeAwareCompare(a.label, b.label);
    return order != 0 ? order < 0 : a.package < b.package;
}

bool isFileNameCharacter(QChar c)
{
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    return c.unicode() >= 0x20 && c.category() != QChar::Other_Control &&
           c.category() != QChar::Other_Format && !forbidden.contains(c);
}

} // namespace

AppListModel::AppListModel(core::service::AppsService *service, core::service::FileService *files,
                           std::shared_ptr<PhotoSource> photos, QObject *parent)
    : QAbstractListModel(parent), m_service(service), m_files(files), m_photos(std::move(photos))
{
    Q_ASSERT(service && files && m_photos);
    connect(service, &core::service::AppsService::appAdded, this, &AppListModel::onAppAdded);
    connect(service, &core::service::AppsService::appRemoved, this, &AppListModel::onAppRemoved);
    // The file service runs one copy at a time, so while this model copies,
    // the progress is that of its copy.
    connect(files, &core::service::FileService::transferProgress, this,
            [this](quint64 done, quint64 total) {
                if (m_transfer == Transfer::None)
                    return;
                m_transferDone = done;
                m_transferTotal = total;
                Q_EMIT transferChanged();
            });
}

int AppListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant AppListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const AppInfo &entry = m_apps.at(m_rows.at(index.row()));
    const QLocale locale;
    const QString sizeText =
        entry.size
            ? locale.formattedDataSize(qint64(*entry.size), 1, QLocale::DataSizeTraditionalFormat)
            : QString();
    const QString installedText =
        entry.installed.isValid()
            ? locale.toString(entry.installed.toLocalTime().date(), QLocale::ShortFormat)
            : QString();
    switch (role) {
    case PackageRole:
        return entry.package;
    case Qt::DisplayRole:
    case LabelRole:
        return entry.label;
    case VersionRole:
        return entry.version;
    case SizeRole:
        return double(entry.size.value_or(0));
    case SizeTextRole:
        return sizeText;
    case InstalledRole:
        return entry.installed;
    case InstalledTextRole:
        return installedText;
    case IconUrlRole: {
        const auto number = m_iconNumbers.constFind(entry.package);
        return number == m_iconNumbers.cend()
                   ? QString()
                   : QStringLiteral("image://qpm/app/%1/%2").arg(entry.package).arg(*number);
    }
    case IsUserAppRole:
        return entry.isUserApp;
    case CanBackupRole:
        return !entry.sourcePath.isEmpty() && entry.size.has_value();
    case DetailsRole: {
        QStringList parts;
        if (!entry.version.isEmpty())
            parts.append(tr("Version %1").arg(entry.version));
        if (!sizeText.isEmpty())
            parts.append(sizeText);
        if (!installedText.isEmpty())
            parts.append(installedText);
        return parts.join(QStringLiteral(", "));
    }
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> AppListModel::roleNames() const
{
    return {
        {PackageRole, QByteArrayLiteral("packageName")},
        {LabelRole, QByteArrayLiteral("label")},
        {VersionRole, QByteArrayLiteral("version")},
        {SizeRole, QByteArrayLiteral("size")},
        {SizeTextRole, QByteArrayLiteral("sizeText")},
        {InstalledRole, QByteArrayLiteral("installed")},
        {InstalledTextRole, QByteArrayLiteral("installedText")},
        {IconUrlRole, QByteArrayLiteral("iconUrl")},
        {IsUserAppRole, QByteArrayLiteral("isUserApp")},
        {CanBackupRole, QByteArrayLiteral("canBackup")},
        {DetailsRole, QByteArrayLiteral("details")},
    };
}

void AppListModel::setFilter(Filter filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    refilter();
    Q_EMIT filterChanged();
}

void AppListModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    refilter();
    Q_EMIT searchTextChanged();
}

QString AppListModel::emptyText() const
{
    if (!m_connected)
        return tr("Connect your phone to see its apps.");
    if (m_busy)
        return {};
    if (!m_failureText.isEmpty())
        return m_failureText;
    if (m_apps.isEmpty())
        return m_loaded ? tr("The phone lists no apps.") : QString();
    if (m_filter == Filter::User && m_searchText.isEmpty() &&
        std::none_of(m_apps.cbegin(), m_apps.cend(),
                     [](const AppInfo &entry) { return entry.isUserApp; })) {
        return tr("No apps of your own are installed. Choose System apps or All to see the "
                  "others.");
    }
    return tr("No app matches the search or filter.");
}

double AppListModel::transferProgress() const
{
    if (m_transferTotal == 0)
        return 0;
    return std::min(1.0, double(m_transferDone) / double(m_transferTotal));
}

QString AppListModel::transferText() const
{
    const int percent = qRound(transferProgress() * 100);
    switch (m_transfer) {
    case Transfer::Backup:
        return tr("Saving the APK of %1: %2 %").arg(m_transferLabel).arg(percent);
    case Transfer::Install:
        return tr("Sending %1 to the phone: %2 %").arg(m_transferLabel).arg(percent);
    case Transfer::None:
        break;
    }
    return {};
}

void AppListModel::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    ++m_generation;
    m_loaded = false;
    m_failureText.clear();
    m_pendingRemovals.clear();
    m_awaitingInstall = false;
    m_iconNumbers.clear();
    m_photos->clearAppIcons();
    setBusy(false);
    setApps({});
    Q_EMIT canEditChanged();
    Q_EMIT emptyTextChanged();
}

bool AppListModel::load()
{
    if (!m_connected)
        return false;
    if (!m_loaded)
        readApps();
    return true;
}

bool AppListModel::refresh()
{
    if (!m_connected)
        return false;
    readApps();
    return true;
}

bool AppListModel::start(const QString &package)
{
    const AppInfo *entry = app(package);
    if (!m_connected || !entry)
        return false;
    const QString label = entry->label;
    m_service->start(package).then(this, [this, label](const Result<void> &started) {
        if (started.hasValue()) {
            Q_EMIT infoMessage(tr("%1 opens on the phone.").arg(label));
        } else if (started.error().code() == ErrorCode::Rejected) {
            Q_EMIT errorOccurred(
                tr("The phone cannot open %1. The app has no window of its own.").arg(label));
        } else if (const QString message = messageFor(started.error()); !message.isEmpty()) {
            Q_EMIT errorOccurred(message);
        }
    });
    return true;
}

bool AppListModel::uninstall(const QString &package)
{
    const AppInfo *entry = app(package);
    if (!m_connected || !entry)
        return false;
    const QString label = entry->label;
    const quint64 generation = m_generation;
    m_service->requestUninstall(package).then(
        this, [this, package, label, generation](const Result<void> &asked) {
            if (!asked.hasValue()) {
                if (const QString message = messageFor(asked.error()); !message.isEmpty())
                    Q_EMIT errorOccurred(message);
                return;
            }
            if (generation == m_generation)
                m_pendingRemovals.insert(package);
            Q_EMIT infoMessage(
                tr("The phone asks whether to remove %1. Confirm it on the phone.").arg(label));
        });
    return true;
}

bool AppListModel::backup(const QString &package, const QUrl &file)
{
    const AppInfo *entry = app(package);
    if (!m_connected || !entry || !canBackup(package))
        return false;
    const QString local = file.toLocalFile();
    if (local.isEmpty() || !QFileInfo(QFileInfo(local).absolutePath()).isDir()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    if (!canStartTransfer())
        return false;
    const QString label = entry->label;
    beginTransfer(Transfer::Backup, label);
    m_service->backup(*entry, local).then(this, [this, label](const Result<void> &saved) {
        endTransfer();
        if (saved.hasValue()) {
            Q_EMIT infoMessage(tr("The APK of %1 is saved.").arg(label));
        } else if (saved.error().code() == ErrorCode::Cancelled) {
            Q_EMIT infoMessage(tr("Saving the APK was cancelled."));
        } else {
            Q_EMIT errorOccurred(messageFor(saved.error()));
        }
    });
    return true;
}

bool AppListModel::install(const QUrl &file)
{
    if (!m_connected)
        return false;
    const QString local = file.toLocalFile();
    if (local.isEmpty() || !QFileInfo(local).isFile()) {
        Q_EMIT errorOccurred(tr("Choose an APK file on this computer."));
        return false;
    }
    if (!canStartTransfer())
        return false;
    const QString name = QFileInfo(local).fileName();
    const quint64 generation = m_generation;
    beginTransfer(Transfer::Install, name);
    m_service->install(local).then(this, [this, name, generation](const Result<void> &sent) {
        endTransfer();
        if (sent.hasValue()) {
            if (generation == m_generation)
                m_awaitingInstall = true;
            Q_EMIT infoMessage(
                tr("The phone asks whether to install %1. Confirm it on the phone.").arg(name));
            return;
        }
        switch (sent.error().code()) {
        case ErrorCode::Cancelled:
            Q_EMIT infoMessage(tr("Sending the APK was cancelled."));
            break;
        case ErrorCode::InvalidInput:
            Q_EMIT errorOccurred(
                tr("%1 is no Android app. Choose an APK file that is not empty.").arg(name));
            break;
        case ErrorCode::IoFailure:
            Q_EMIT errorOccurred(tr("%1 cannot be read. Check the file and try again.").arg(name));
            break;
        default:
            Q_EMIT errorOccurred(messageFor(sent.error()));
            break;
        }
    });
    return true;
}

void AppListModel::cancelTransfer()
{
    if (m_transfer != Transfer::None)
        m_files->cancelTransfer();
}

int AppListModel::rowOf(const QString &package) const
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_apps.at(m_rows.at(row)).package == package)
            return int(row);
    }
    return -1;
}

QString AppListModel::packageAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_apps.at(m_rows.at(row)).package : QString();
}

QString AppListModel::labelOf(const QString &package) const
{
    const AppInfo *entry = app(package);
    return entry ? entry->label : QString();
}

bool AppListModel::canBackup(const QString &package) const
{
    const AppInfo *entry = app(package);
    return entry && !entry->sourcePath.isEmpty() && entry->size.has_value();
}

QString AppListModel::backupFileName(const QString &package) const
{
    const AppInfo *entry = app(package);
    if (!entry)
        return {};
    QString name = entry->version.isEmpty() ? entry->label : entry->label + u'-' + entry->version;
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (!isFileNameCharacter(name.at(i)))
            name[i] = u'_';
    }
    name = name.trimmed();
    if (name.isEmpty() || name.startsWith(u'.'))
        name = entry->package;
    return name + QStringLiteral(".apk");
}

QString AppListModel::messageFor(const Error &error)
{
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::ConnectionClosed:
        return tr("The connection to the phone ended. Connect again and try again.");
    case ErrorCode::Timeout:
        return tr("The phone did not answer in time. Try again.");
    case ErrorCode::PermissionDenied:
        return tr("The app on the phone may not do this. Allow it in the Android settings and "
                  "try again.");
    case ErrorCode::NotFound:
        return tr("The app or its APK is no longer on the phone. Load the list again.");
    case ErrorCode::NotSupported:
        return tr("This needs a connection over Wi-Fi or USB. Connect the phone that way and "
                  "try again.");
    case ErrorCode::IoFailure:
        return tr("The file cannot be stored. Check the target folder and the free space.");
    case ErrorCode::LimitExceeded:
        return tr("The phone sent more than this program reads.");
    default:
        break;
    }
    return tr("The phone could not do this. Try again.");
}

void AppListModel::readApps()
{
    m_loaded = true;
    const quint64 generation = ++m_generation;
    m_failureText.clear();
    setBusy(true);
    m_service->list().then(this, [this, generation](const Result<QList<AppInfo>> &listed) {
        if (generation != m_generation)
            return;
        setBusy(false);
        if (!listed.hasValue()) {
            m_failureText = tr("The apps cannot be shown.");
            setApps({});
            Q_EMIT errorOccurred(messageFor(listed.error()));
            return;
        }
        QList<AppInfo> apps = listed.value();
        std::sort(apps.begin(), apps.end(), labelLess);
        setApps(std::move(apps));
        QStringList packages;
        for (const qsizetype index : std::as_const(m_rows))
            packages.append(m_apps.at(index).package);
        for (const AppInfo &entry : std::as_const(m_apps)) {
            if (!packages.contains(entry.package))
                packages.append(entry.package);
        }
        fetchIcons(generation, packages);
    });
}

void AppListModel::setApps(QList<AppInfo> apps)
{
    m_apps = std::move(apps);
    QHash<QString, quint64> kept;
    for (const AppInfo &entry : std::as_const(m_apps)) {
        const auto number = m_iconNumbers.constFind(entry.package);
        if (number != m_iconNumbers.cend())
            kept.insert(entry.package, *number);
    }
    m_iconNumbers = kept;
    refilter();
}

void AppListModel::refilter()
{
    beginResetModel();
    m_rows.clear();
    for (qsizetype index = 0; index < m_apps.size(); ++index) {
        if (matches(m_apps.at(index)))
            m_rows.append(index);
    }
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT emptyTextChanged();
}

void AppListModel::fetchIcons(quint64 generation, QStringList packages)
{
    if (packages.isEmpty() || generation != m_generation)
        return;
    const QStringList batch = packages.mid(0, core::service::AppsService::maxIconsPerRequest);
    packages.remove(0, batch.size());
    m_service->fetchIcons(batch)
        .then(this,
              [](const Result<QList<AppIcon>> &fetched) -> QFuture<DecodedIcons> {
                  if (!fetched.hasValue()) {
                      qCInfo(core::lcUi) << "App icons not loaded:" << fetched.error().message();
                      return QtFuture::makeReadyValueFuture(DecodedIcons());
                  }
                  // Decoding many pictures would hold up the window.
                  return QtConcurrent::run(
                      [icons = fetched.value()]() { return DecodedIcons(decodeIcons(icons)); });
              })
        .unwrap()
        .then(this, [this, generation, packages](const DecodedIcons &decoded) {
            if (generation != m_generation || !decoded)
                return;
            for (const auto &[package, image] : *decoded) {
                if (!app(package))
                    continue;
                m_photos->setAppIcon(package, image);
                m_iconNumbers.insert(package, ++m_lastIconNumber);
                const int row = rowOf(package);
                if (row >= 0) {
                    const QModelIndex changed = index(row);
                    Q_EMIT dataChanged(changed, changed, {IconUrlRole});
                }
            }
            fetchIcons(generation, packages);
        });
}

void AppListModel::onAppAdded(const AppInfo &added)
{
    if (!m_connected)
        return;
    const bool announce = std::exchange(m_awaitingInstall, false);
    if (announce)
        Q_EMIT infoMessage(tr("%1 is installed on the phone.").arg(added.label));
    if (!m_loaded)
        return;
    QList<AppInfo> apps = m_apps;
    apps.removeIf([&added](const AppInfo &entry) { return entry.package == added.package; });
    apps.insert(std::lower_bound(apps.begin(), apps.end(), added, labelLess), added);
    setApps(std::move(apps));
    m_iconNumbers.remove(added.package);
    fetchIcons(m_generation, {added.package});
}

void AppListModel::onAppRemoved(const QString &package)
{
    if (!m_connected)
        return;
    const QString label = labelOf(package);
    if (m_pendingRemovals.remove(package)) {
        Q_EMIT infoMessage(
            tr("%1 is removed from the phone.").arg(label.isEmpty() ? package : label));
    }
    if (!app(package))
        return;
    QList<AppInfo> apps = m_apps;
    apps.removeIf([&package](const AppInfo &entry) { return entry.package == package; });
    setApps(std::move(apps));
}

void AppListModel::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
    Q_EMIT emptyTextChanged();
}

void AppListModel::beginTransfer(Transfer kind, const QString &label)
{
    m_transfer = kind;
    m_transferLabel = label;
    m_transferDone = 0;
    m_transferTotal = 0;
    Q_EMIT transferChanged();
}

void AppListModel::endTransfer()
{
    m_transfer = Transfer::None;
    m_transferLabel.clear();
    m_transferDone = 0;
    m_transferTotal = 0;
    Q_EMIT transferChanged();
}

bool AppListModel::matches(const AppInfo &entry) const
{
    if ((m_filter == Filter::User && !entry.isUserApp) ||
        (m_filter == Filter::System && entry.isUserApp)) {
        return false;
    }
    return m_searchText.isEmpty() || entry.label.contains(m_searchText, Qt::CaseInsensitive) ||
           entry.package.contains(m_searchText, Qt::CaseInsensitive);
}

const AppInfo *AppListModel::app(const QString &package) const
{
    for (const AppInfo &entry : m_apps) {
        if (entry.package == package)
            return &entry;
    }
    return nullptr;
}

bool AppListModel::canStartTransfer()
{
    if (m_transfer != Transfer::None || m_files->isTransferRunning()) {
        Q_EMIT errorOccurred(tr("Another transfer is running. Try again when it is done."));
        return false;
    }
    return true;
}

} // namespace qpm::ui
