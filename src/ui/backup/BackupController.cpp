#include "ui/backup/BackupController.h"

#include "core/Logging.h"
#include "core/store/ProfileStore.h"

#include <QDate>
#include <QDir>
#include <QLocale>
#include <QStandardPaths>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::backup::Backup;
using core::backup::BackupInfo;
using core::backup::BackupPreview;
using core::backup::BackupSection;
using core::backup::BackupService;
using core::backup::RestoreReport;
using core::backup::SectionOutcome;
using core::backup::SectionPreview;

bool needsPhone(BackupSection section)
{
    return section != BackupSection::Messages && section != BackupSection::Calls;
}

// A device name as part of a file name: no folder separators, no control characters.
QString fileNamePart(const QString &name)
{
    QString part;
    for (const QChar c : name) {
        const bool control = c.unicode() < 0x20 || c.unicode() == 0x7F;
        part.append(control || c == u'/' || c == u'\\' || c == u':' ? QChar(u' ') : c);
    }
    return part.simplified();
}

} // namespace

BackupController::BackupController(const BackupService::Services &services, QObject *parent)
    : QObject(parent), m_service(new BackupService(services, this)),
      m_sections(new BackupSectionModel(this)), m_restoreSections(new BackupSectionModel(this))
{
    connect(m_service, &BackupService::progressChanged, this,
            [this](int done, int total) { setProgress(total > 0 ? double(done) / total : -1); });
    for (const BackupSection section : core::backup::allBackupSections)
        m_restoreSections->setState(section, false, false);
}

QString BackupController::archiveText() const
{
    if (!m_backup)
        return {};
    const BackupInfo &info = m_backup->info;
    const QString when = QLocale().toString(info.createdAt.toLocalTime(), QLocale::ShortFormat);
    if (info.deviceName.isEmpty())
        return tr("Backup from %1").arg(when);
    return tr("Backup of %1 from %2").arg(info.deviceName, when);
}

void BackupController::setProfileStore(core::store::ProfileStore *profile)
{
    const bool changed = (profile != nullptr) != (m_profile != nullptr);
    m_profile = profile;
    ++m_profileGeneration;
    m_service->setProfileStore(profile);
    m_sections->clearDetails();
    // A running step forgets the backup when it ends, see its completion.
    reset();
    if (changed)
        Q_EMIT hasPhoneChanged();
}

void BackupController::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    updateRestoreChoice();
    Q_EMIT connectedChanged();
}

QUrl BackupController::suggestedFileUrl(const QUrl &folder) const
{
    if (!m_profile)
        return {};
    const Result<core::store::DeviceProfile> profile = m_profile->loadProfile();
    // Without the name the suggestion still works; it only lacks the phone.
    if (!profile.hasValue())
        qCWarning(core::lcUi) << "No phone name for the backup file:" << profile.error().message();
    const QString device =
        fileNamePart(profile.hasValue() ? profile.value().displayName : QString());
    const QString date = QDate::currentDate().toString(Qt::ISODate);
    const QString name = device.isEmpty()
                             ? QStringLiteral("QPhoneManager %1.qpmbackup").arg(date)
                             : QStringLiteral("QPhoneManager %1 %2.qpmbackup").arg(device, date);
    const QString directory =
        folder.isLocalFile() ? folder.toLocalFile()
                             : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(QDir(directory).filePath(name));
}

bool BackupController::create(const QUrl &url, const QStringList &sections)
{
    if (m_busy)
        return false;
    if (!m_profile) {
        setStatus(tr("Choose a phone first."), true);
        return false;
    }
    if (!url.isLocalFile()) {
        setStatus(tr("Choose a file on this computer."), true);
        return false;
    }
    const std::optional<QList<BackupSection>> chosen = sectionsFrom(sections);
    if (!chosen || chosen->isEmpty()) {
        setStatus(tr("Choose at least one part to back up."), true);
        return false;
    }
    const core::codec::CsvLanguage language = QLocale().language() == QLocale::German
                                                  ? core::codec::CsvLanguage::German
                                                  : core::codec::CsvLanguage::English;
    m_sections->clearDetails();
    m_stopRequested = false;
    setBusy(true);
    setProgress(0);
    setStatus(tr("Saving the backup."), false);
    const quint64 generation = m_profileGeneration;
    m_service->create(url.toLocalFile(), *chosen, language)
        .then(this, [this, generation](const Result<BackupInfo> &created) {
            setBusy(false);
            setProgress(-1);
            if (generation != m_profileGeneration)
                forgetBackup();
            if (!created.hasValue()) {
                const bool stopped = created.error().code() == ErrorCode::Cancelled;
                setStatus(stopped ? tr("Stopped. No backup was saved.")
                                  : errorText(created.error(), Step::Write),
                          !stopped);
                return;
            }
            const QMap<BackupSection, int> &counts = created.value().counts;
            for (auto it = counts.cbegin(); it != counts.cend(); ++it)
                m_sections->setDetail(it.key(), tr("%n entry(s) saved", nullptr, it.value()));
            setStatus(tr("The backup is saved. It is not encrypted, so keep it in a safe place."),
                      false);
        });
    return true;
}

bool BackupController::preview(const QUrl &url)
{
    if (m_busy)
        return false;
    if (!m_profile) {
        setStatus(tr("Choose a phone first."), true);
        return false;
    }
    if (!url.isLocalFile()) {
        setStatus(tr("Choose a file on this computer."), true);
        return false;
    }
    reset();
    m_stopRequested = false;
    setBusy(true);
    setProgress(0);
    setStatus(tr("Reading the backup."), false);
    const quint64 generation = m_profileGeneration;
    m_service->open(url.toLocalFile())
        .then(this, [this, url, generation](const Result<Backup> &opened) {
            if (!opened.hasValue()) {
                setBusy(false);
                setProgress(-1);
                const bool stopped = opened.error().code() == ErrorCode::Cancelled;
                setStatus(stopped ? tr("Stopped.") : errorText(opened.error(), Step::Read),
                          !stopped);
                return;
            }
            // A stop that came while the file was read keeps the comparison from starting.
            if (m_stopRequested) {
                setBusy(false);
                setProgress(-1);
                setStatus(tr("Stopped."), false);
                return;
            }
            if (generation != m_profileGeneration) {
                setBusy(false);
                setProgress(-1);
                setStatus(tr("Another phone was chosen meanwhile. Open the backup again."), false);
                return;
            }
            setProgress(-1);
            setStatus(tr("Comparing the backup with the phone."), false);
            const Backup backup = opened.value();
            m_service->preview(backup).then(this, [this, url, backup, generation](
                                                      const Result<BackupPreview> &compared) {
                setBusy(false);
                if (!compared.hasValue()) {
                    const bool stopped = compared.error().code() == ErrorCode::Cancelled;
                    setStatus(stopped ? tr("Stopped.") : errorText(compared.error(), Step::Compare),
                              !stopped);
                    return;
                }
                if (generation != m_profileGeneration) {
                    setStatus(tr("Another phone was chosen meanwhile. Open the backup again."),
                              false);
                    return;
                }
                m_backup = backup;
                m_backupUrl = url;
                m_preview = compared.value();
                updateRestoreChoice();
                Q_EMIT previewChanged();
                setStatus(compared.value().comparedWithPhone
                              ? tr("Choose what to restore.")
                              : tr("The phone is not connected, so the backup was compared with "
                                   "the data stored on this computer. Connect the phone to "
                                   "restore contacts, calendar, tasks and notes."),
                          false);
            });
        });
    return true;
}

bool BackupController::restore(const QUrl &url, const QStringList &sections)
{
    if (m_busy)
        return false;
    if (!m_backup || url != m_backupUrl) {
        setStatus(tr("Open the backup first."), true);
        return false;
    }
    const std::optional<QList<BackupSection>> chosen = sectionsFrom(sections);
    if (!chosen || chosen->isEmpty()) {
        setStatus(tr("Choose at least one part to restore."), true);
        return false;
    }
    for (const BackupSection section : *chosen) {
        if (needsPhone(section) && !m_connected) {
            setStatus(tr("Connect the phone first, then restore."), true);
            return false;
        }
    }
    m_stopRequested = false;
    setBusy(true);
    setProgress(0);
    setStatus(tr("Restoring. Keep the phone connected."), false);
    const quint64 generation = m_profileGeneration;
    m_service->restore(*m_backup, *chosen)
        .then(this, [this, generation](const Result<RestoreReport> &done) {
            setBusy(false);
            setProgress(-1);
            if (generation != m_profileGeneration) {
                forgetBackup();
                setStatus(tr("Another phone was chosen meanwhile. Open the backup again."), false);
                return;
            }
            if (!done.hasValue()) {
                setStatus(errorText(done.error(), Step::Restore), true);
                return;
            }
            showReport(done.value());
        });
    return true;
}

void BackupController::cancel()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    m_service->cancel();
}

void BackupController::reset()
{
    if (m_busy)
        return;
    forgetBackup();
    setStatus(QString(), false);
}

void BackupController::forgetBackup()
{
    const bool hadPreview = m_backup.has_value();
    m_backup.reset();
    m_preview.reset();
    m_backupUrl.clear();
    m_restoreSections->clearDetails();
    updateRestoreChoice();
    if (hadPreview)
        Q_EMIT previewChanged();
}

void BackupController::updateRestoreChoice()
{
    for (const BackupSection section : core::backup::allBackupSections) {
        if (!m_preview || !m_backup) {
            m_restoreSections->setState(section, false, false);
            continue;
        }
        const bool held = m_backup->info.counts.contains(section);
        const SectionPreview found = m_preview->sections.value(section);
        const bool work = found.added + found.changed > 0;
        bool selectable = false;
        QString detail;
        if (!held) {
            detail = tr("Not in this backup.");
        } else if (section == BackupSection::Calls) {
            detail = tr("%n call(s). They stay in the backup, because the app on the phone may "
                        "not change the call log.",
                        nullptr, m_backup->info.counts.value(section));
        } else if (section == BackupSection::Messages) {
            selectable = work;
            detail = tr("New: %1, already here: %2. They go to the archive on this computer, "
                        "not to the phone.")
                         .arg(found.added)
                         .arg(found.unchanged);
        } else {
            selectable = work && m_connected;
            detail = tr("New: %1, changed: %2, unchanged: %3.")
                         .arg(found.added)
                         .arg(found.changed)
                         .arg(found.unchanged);
        }
        m_restoreSections->setState(section, selectable, selectable);
        m_restoreSections->setDetail(section, detail);
    }
}

void BackupController::showReport(const RestoreReport &report)
{
    // The counts of the preview are out of date now; a new preview shows the state.
    m_preview.reset();
    for (const BackupSection section : core::backup::allBackupSections)
        m_restoreSections->setState(section, false, false);
    bool anyFailed = false;
    for (auto it = report.sections.cbegin(); it != report.sections.cend(); ++it) {
        const SectionOutcome &outcome = it.value();
        if (outcome.failed == 0) {
            m_restoreSections->setDetail(it.key(),
                                         tr("%n entry(s) restored.", nullptr, outcome.restored));
            continue;
        }
        anyFailed = true;
        m_restoreSections->setDetail(it.key(),
                                     tr("Restored: %1, failed: %2. %3")
                                         .arg(outcome.restored)
                                         .arg(outcome.failed)
                                         .arg(errorText(outcome.firstError, Step::Restore)));
    }
    if (report.cancelled)
        setStatus(tr("The restore was stopped. What was restored until then stays."), false);
    else if (anyFailed)
        setStatus(tr("The restore is done, but some entries failed. The list says why."), true);
    else
        setStatus(tr("The restore is done."), false);
}

std::optional<QList<BackupSection>> BackupController::sectionsFrom(const QStringList &keys)
{
    QList<BackupSection> sections;
    for (const QString &key : keys) {
        const std::optional<BackupSection> section = core::backup::sectionFromKey(key);
        if (!section)
            return std::nullopt;
        if (!sections.contains(*section))
            sections.append(*section);
    }
    return sections;
}

void BackupController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void BackupController::setStatus(const QString &text, bool isError)
{
    if (m_statusText == text && m_statusIsError == isError)
        return;
    m_statusText = text;
    m_statusIsError = isError;
    Q_EMIT statusChanged();
}

void BackupController::setProgress(double progress)
{
    if (m_progress == progress)
        return;
    m_progress = progress;
    Q_EMIT progressChanged();
}

QString BackupController::errorText(const Error &error, Step step)
{
    const bool phoneRefused =
        error.code() == ErrorCode::ConnectionClosed || error.code() == ErrorCode::Timeout ||
        error.code() == ErrorCode::AccessDenied || error.code() == ErrorCode::PermissionDenied;
    if (step == Step::Compare && error.isError() && error.code() != ErrorCode::Cancelled &&
        !phoneRefused) {
        return tr("The backup could not be compared with the data of this phone. Open it again.");
    }
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::NotSupported:
        if (step == Step::Read) {
            return tr("This backup was made by a newer version of QPhoneManager. Update "
                      "QPhoneManager, then open it again.");
        }
        return tr("The phone does not support this. Update MyPhoneExplorer Client on the phone.");
    case ErrorCode::InvalidInput:
        if (step == Step::Read) {
            return tr("This file is not a QPhoneManager backup, or it is damaged. Choose another "
                      "file.");
        }
        if (step == Step::Write)
            return tr("The backup cannot be saved there. Choose another folder.");
        return tr("The phone did not take an entry. Check it on the phone.");
    case ErrorCode::LimitExceeded:
        if (step == Step::Write)
            return tr("The backup is too large for one file. Choose fewer parts.");
        if (step == Step::Read)
            return tr("This backup holds more than QPhoneManager can read. Choose another file.");
        return tr("An entry is too large for the phone.");
    case ErrorCode::IoFailure:
        if (step == Step::Write)
            return tr("The backup cannot be saved there. Choose another folder.");
        if (step == Step::Read)
            return tr("The file cannot be opened. Check that it exists and that you may read it.");
        return tr("The data on this computer cannot be changed. Check the storage folder in the "
                  "settings.");
    case ErrorCode::ConnectionClosed:
    case ErrorCode::Timeout:
        return tr("The connection to the phone ended. Connect it again, then try again.");
    case ErrorCode::AccessDenied:
    case ErrorCode::PermissionDenied:
        return tr("MyPhoneExplorer Client lacks a permission on the phone. Allow it in the "
                  "Android settings under Apps.");
    case ErrorCode::NotFound:
        return tr("An entry is no longer on the phone. Open the backup again.");
    case ErrorCode::WrongPin:
    case ErrorCode::AlreadyConnected:
    case ErrorCode::WifiDisabled:
    case ErrorCode::Rejected:
    case ErrorCode::ProtocolError:
        break;
    }
    return tr("The phone did not take an entry. Check it on the phone.");
}

} // namespace qpm::ui
