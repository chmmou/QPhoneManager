#include "core/backup/BackupService.h"

#include "core/Logging.h"
#include "core/Version.h"
#include "core/service/CalendarService.h"
#include "core/service/ContactsService.h"
#include "core/service/MessagesService.h"
#include "core/service/NotesService.h"
#include "core/store/CallStore.h"
#include "core/store/ContactStore.h"
#include "core/store/MessageStore.h"
#include "core/store/OrganizerStore.h"
#include "core/store/ProfileStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QPromise>
#include <QSet>
#include <QTimeZone>
#include <QtConcurrentRun>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace qpm::core::backup {

namespace {

using codec::CalendarEvent;
using codec::CalendarTask;
using codec::Contact;
using codec::Note;
using service::SaveMode;

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

Error noStore()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("No phone is chosen"));
}

Error busy()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("A backup or a restore is running"));
}

Error cancelled()
{
    return Error(ErrorCode::Cancelled, QStringLiteral("The restore was cancelled"));
}

Error workerFailed()
{
    return Error(ErrorCode::IoFailure, QStringLiteral("The backup work stopped unexpectedly"));
}

// After such an error every further entry of the area fails the same way.
bool endsSection(const Error &error)
{
    switch (error.code()) {
    case ErrorCode::ConnectionClosed:
    case ErrorCode::Timeout:
    case ErrorCode::Cancelled:
    case ErrorCode::AccessDenied:
    case ErrorCode::PermissionDenied:
    case ErrorCode::NotSupported:
    case ErrorCode::IoFailure:
        return true;
    default:
        return false;
    }
}

template <typename T> Result<void> withoutValue(const Result<T> &result)
{
    if (!result.hasValue())
        return result.error();
    return {};
}

// One entry written.
template <typename T> Result<int> one(const Result<T> &saved)
{
    if (!saved.hasValue())
        return saved.error();
    return 1;
}

template <typename T>
BackupStep stepFor(QPromise<T> &promise, const std::shared_ptr<std::atomic_bool> &stop)
{
    auto range = std::make_shared<int>(-1);
    return [&promise, stop, range](int done, int total) {
        if (*range != total) {
            promise.setProgressRange(0, total);
            *range = total;
        }
        promise.setProgressValue(done);
        return !stop->load();
    };
}

codec::Message codecMessage(const store::StoredMessage &stored)
{
    codec::Message message;
    message.luid = stored.luid;
    message.kind = stored.kind;
    message.box = stored.box;
    message.incoming = stored.incoming;
    message.read = stored.read;
    message.date = stored.date;
    message.addresses = stored.addresses;
    message.subject = stored.subject;
    message.body = stored.body;
    message.simSlot = stored.simSlot;
    for (const store::StoredAttachment &attachment : stored.attachments) {
        codec::MessageAttachment part;
        part.name = attachment.name;
        part.mimeType = attachment.mimeType;
        part.size = attachment.size;
        part.onPhoneOnly = attachment.blob.isEmpty();
        message.attachments.append(part);
    }
    return message;
}

// `blobs` holds the blob name of every part, in the order of the messages.
void loadParts(QList<codec::Message> &messages, const QList<QStringList> &blobs,
               const QString &blobDirectory)
{
    const QDir directory(blobDirectory);
    for (qsizetype index = 0; index < messages.size(); ++index) {
        QList<codec::MessageAttachment> &parts = messages[index].attachments;
        for (qsizetype position = 0; position < parts.size(); ++position) {
            const QString blob = blobs.at(index).value(position);
            if (blob.isEmpty())
                continue;
            QFile file(directory.filePath(blob));
            if (!file.open(QIODevice::ReadOnly) || file.size() > codec::maxAttachmentBytes) {
                // The backup still names the part; only its content is missing here.
                qCWarning(lcStore) << "A part of a message is missing on this computer";
                parts[position].onPhoneOnly = true;
                continue;
            }
            parts[position].data = file.readAll();
            parts[position].size = parts[position].data.size();
        }
    }
}

// Reads the chosen sections of the profile and writes the archive. Runs on a
// worker thread with a read-only connection of its own.
Result<BackupInfo> createArchive(const QString &profilesDirectory, const QString &deviceId,
                                 const QString &path, const BackupInfo &info,
                                 const QList<BackupSection> &sections, codec::CsvLanguage language,
                                 const BackupStep &step)
{
    store::ProfileStore profile(profilesDirectory, deviceId);
    if (const Result<void> opened = profile.openReadOnly(); !opened.hasValue())
        return opened.error();
    store::ContactStore contacts(&profile);
    store::OrganizerStore organizer(&profile);
    store::CallStore calls(&profile);
    store::MessageStore messages(&profile);

    BackupData data;
    QList<QStringList> blobs;
    for (const BackupSection section : sections) {
        Error error;
        switch (section) {
        case BackupSection::Contacts:
            if (const Result<QList<Contact>> read = contacts.allContacts(); read.hasValue())
                data.contacts = read.value();
            else
                error = read.error();
            break;
        case BackupSection::Calendar:
            if (const Result<QList<CalendarEvent>> read = organizer.allEvents(); read.hasValue())
                data.events = read.value();
            else
                error = read.error();
            break;
        case BackupSection::Tasks:
            if (const Result<QList<CalendarTask>> read = organizer.tasks(); read.hasValue())
                data.tasks = read.value();
            else
                error = read.error();
            break;
        case BackupSection::Notes:
            if (const Result<QList<Note>> read = organizer.notes(); read.hasValue())
                data.notes = read.value();
            else
                error = read.error();
            break;
        case BackupSection::Calls:
            if (const Result<QList<codec::CallEntry>> read = calls.calls(); read.hasValue())
                data.calls = read.value();
            else
                error = read.error();
            break;
        case BackupSection::Messages:
            if (const Result<QList<store::StoredMessage>> read = messages.allMessages();
                read.hasValue()) {
                for (const store::StoredMessage &stored : read.value()) {
                    data.messages.append(codecMessage(stored));
                    QStringList names;
                    for (const store::StoredAttachment &attachment : stored.attachments)
                        names.append(attachment.blob);
                    blobs.append(names);
                }
            } else {
                error = read.error();
            }
            break;
        }
        if (error.isError())
            return error;
    }
    loadParts(data.messages, blobs, QDir(profile.directory()).filePath(QStringLiteral("blobs")));
    return writeBackupArchive(path, info, data, sections, language, step);
}

// A contact as it compares with one created again from it: the phone gives a
// new contact keys, a display name and preferred entries of its own.
Contact comparable(const Contact &contact)
{
    Contact plain = contact;
    plain.luid = 0;
    plain.nicknameId = 0;
    plain.displayName.clear();
    plain.accountType.clear();
    plain.accountName.clear();
    for (const codec::ContactValueKind kind : codec::allContactValueKinds) {
        for (codec::ContactValue &entry : plain.values(kind)) {
            entry.dataId = 0;
            entry.primary = false;
        }
    }
    return plain;
}

bool sameContact(const Contact &a, const Contact &b)
{
    return codec::sameContent(comparable(a), comparable(b));
}

QString contactKey(const Contact &contact)
{
    return codec::displayNameOf(comparable(contact));
}

QString eventKey(const CalendarEvent &event)
{
    return event.summary + u'\x1f' + QString::number(event.start.toSecsSinceEpoch());
}

QString taskKey(const CalendarTask &task)
{
    return task.summary;
}

QString noteKey(const Note &note)
{
    return note.body;
}

QString messageKey(const QString &threadKey, const QDateTime &date, codec::MessageBox box,
                   const QString &body)
{
    return threadKey + u'\x1f' + QString::number(date.isValid() ? date.toSecsSinceEpoch() : 0) +
           u'\x1f' + QString::number(int(box)) + u'\x1f' + body;
}

/**
 * Sorts the backup's entries into those `stored` lacks, those it holds in
 * another form under the same LUID, and those it already holds. An entry
 * with an unknown LUID counts as held when one with the same content is
 * there, as after an earlier restore under a new LUID. `sameByLuid` decides
 * for the same LUID, `sameElsewhere` for the rest; `key` must be equal for
 * entries `sameElsewhere` finds equal.
 */
template <typename T, typename Key, typename SameByLuid, typename SameElsewhere>
void classify(const QList<T> &backup, const QList<T> &stored, Key key, SameByLuid sameByLuid,
              SameElsewhere sameElsewhere, QList<T> &added, QList<T> &changed, int &unchanged)
{
    QHash<quint32, qsizetype> byLuid;
    QMultiHash<QString, qsizetype> byKey;
    for (qsizetype index = 0; index < stored.size(); ++index) {
        byLuid.insert(stored.at(index).luid, index);
        byKey.insert(key(stored.at(index)), index);
    }
    for (const T &item : backup) {
        const auto own = item.luid != 0 ? byLuid.constFind(item.luid) : byLuid.cend();
        if (own != byLuid.cend()) {
            if (sameByLuid(stored.at(own.value()), item))
                ++unchanged;
            else
                changed.append(item);
            continue;
        }
        const QString itemKey = key(item);
        bool held = false;
        for (auto it = byKey.constFind(itemKey); it != byKey.cend() && it.key() == itemKey; ++it) {
            if (sameElsewhere(stored.at(it.value()), item)) {
                held = true;
                break;
            }
        }
        if (held)
            ++unchanged;
        else
            added.append(item);
    }
}

} // namespace

struct BackupService::Plan
{
    /// The backup is of another phone, so its LUIDs were not used.
    bool otherPhone = false;
    QList<Contact> newContacts;
    QList<Contact> changedContacts;
    int unchangedContacts = 0;
    QList<CalendarEvent> newEvents;
    QList<CalendarEvent> changedEvents;
    int unchangedEvents = 0;
    QList<CalendarTask> newTasks;
    QList<CalendarTask> changedTasks;
    int unchangedTasks = 0;
    QList<Note> newNotes;
    QList<Note> changedNotes;
    int unchangedNotes = 0;
    QList<codec::Message> newMessages;
    int knownMessages = 0;
    /// The accounts of the phone, for new contacts; the error when they could not be read.
    QList<codec::PbAccount> accounts;
    Error accountsError;
    /// The calendars of the phone, for new events; the error when they could not be read.
    QList<codec::CalendarInfo> calendars;
    Error calendarsError;
};

struct BackupService::RestoreRun
{
    struct Job
    {
        BackupSection section = BackupSection::Contacts;
        /// Entries the job writes; a job for messages writes them all at once.
        int weight = 1;
        std::function<QFuture<Result<int>>()> start;
    };

    QPromise<Result<RestoreReport>> promise;
    QList<Job> jobs;
    qsizetype next = 0;
    int done = 0;
    int total = 0;
    RestoreReport report;
    QSet<BackupSection> ended;
    std::shared_ptr<std::atomic_bool> stop;
};

BackupService::BackupService(const Services &services, QObject *parent)
    : QObject(parent), m_services(services), m_cancel(std::make_shared<std::atomic_bool>(false))
{
    Q_ASSERT(services.contacts && services.calendar && services.notes && services.messages);
}

BackupService::~BackupService()
{
    m_cancel->store(true);
}

void BackupService::setProfileStore(store::ProfileStore *profile)
{
    m_profile = profile;
}

bool BackupService::isRestorable(BackupSection section)
{
    return section != BackupSection::Calls;
}

void BackupService::begin()
{
    m_running = true;
    m_cancel = std::make_shared<std::atomic_bool>(false);
}

void BackupService::cancel()
{
    m_cancel->store(true);
}

template <typename T> QFuture<Result<T>> BackupService::track(QFuture<Result<T>> work)
{
    auto *watcher = new QFutureWatcher<Result<T>>(this);
    connect(
        watcher, &QFutureWatcherBase::progressValueChanged, watcher,
        [this, watcher](int value) { Q_EMIT progressChanged(value, watcher->progressMaximum()); });
    connect(watcher, &QFutureWatcherBase::finished, watcher, &QObject::deleteLater);
    watcher->setFuture(work);
    return work
        .then(this,
              [this](const Result<T> &result) {
                  m_running = false;
                  return result;
              })
        .onFailed(this, [this]() {
            m_running = false;
            return Result<T>(workerFailed());
        });
}

QFuture<Result<BackupInfo>> BackupService::create(const QString &path,
                                                  const QList<BackupSection> &sections,
                                                  codec::CsvLanguage language)
{
    if (m_running)
        return failed<BackupInfo>(busy());
    if (!m_profile)
        return failed<BackupInfo>(noStore());
    if (sections.isEmpty())
        return failed<BackupInfo>(
            Error(ErrorCode::InvalidInput, QStringLiteral("No section is chosen")));

    BackupInfo info;
    // Whole seconds, as the manifest keeps them.
    info.createdAt =
        QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), QTimeZone::UTC);
    info.app = QStringLiteral("QPhoneManager ") + applicationVersion();
    info.deviceId = m_profile->deviceId();
    const Result<store::DeviceProfile> profile = m_profile->loadProfile();
    if (!profile.hasValue())
        return failed<BackupInfo>(profile.error());
    info.deviceName = profile.value().displayName;

    const QString profilesDirectory = QFileInfo(m_profile->directory()).absolutePath();
    const QString deviceId = m_profile->deviceId();
    begin();
    const std::shared_ptr<std::atomic_bool> stop = m_cancel;
    return track(QtConcurrent::run([path, info, profilesDirectory, deviceId, sections, language,
                                    stop](QPromise<Result<BackupInfo>> &promise) {
        // A failure must not escape the worker; the caller gets an error instead.
        try {
            promise.addResult(createArchive(profilesDirectory, deviceId, path, info, sections,
                                            language, stepFor(promise, stop)));
        } catch (...) {
            promise.addResult(workerFailed());
        }
    }));
}

QFuture<Result<Backup>> BackupService::open(const QString &path)
{
    if (m_running)
        return failed<Backup>(busy());
    begin();
    const std::shared_ptr<std::atomic_bool> stop = m_cancel;
    return track(QtConcurrent::run([path, stop](QPromise<Result<Backup>> &promise) {
        // A failure must not escape the worker; the caller gets an error instead.
        try {
            promise.addResult(readBackupArchive(path, stepFor(promise, stop)));
        } catch (...) {
            promise.addResult(workerFailed());
        }
    }));
}

Result<BackupService::Plan> BackupService::computePlan(const QString &profilesDirectory,
                                                       const QString &deviceId,
                                                       const Backup &backup)
{
    store::ProfileStore profile(profilesDirectory, deviceId);
    if (const Result<void> opened = profile.openReadOnly(); !opened.hasValue())
        return opened.error();
    store::ContactStore contactStore(&profile);
    store::OrganizerStore organizerStore(&profile);
    store::MessageStore messageStore(&profile);

    Plan result;
    // The LUIDs of another phone name other entries here; only the content counts then.
    result.otherPhone = backup.info.deviceId != deviceId;
    const auto own = [&result](auto items) {
        if (result.otherPhone) {
            for (auto &item : items)
                item.luid = 0;
        }
        return items;
    };
    const auto holds = [&backup](BackupSection section) {
        return backup.info.counts.contains(section);
    };
    if (holds(BackupSection::Contacts)) {
        const Result<QList<Contact>> stored = contactStore.allContacts();
        if (!stored.hasValue())
            return stored.error();
        classify(own(backup.data.contacts), stored.value(), &contactKey, &codec::sameContent,
                 &sameContact, result.newContacts, result.changedContacts,
                 result.unchangedContacts);
        const Result<codec::PbMetadata> metadata = contactStore.metadata();
        if (metadata.hasValue())
            result.accounts = metadata.value().accounts;
        else
            result.accountsError = metadata.error();
    }
    if (holds(BackupSection::Calendar)) {
        const Result<QList<CalendarEvent>> stored = organizerStore.allEvents();
        if (!stored.hasValue())
            return stored.error();
        classify(own(backup.data.events), stored.value(), &eventKey, &codec::sameEventContent,
                 &codec::sameEventContent, result.newEvents, result.changedEvents,
                 result.unchangedEvents);
        const Result<codec::CalMetadata> metadata = organizerStore.calendarMetadata();
        if (metadata.hasValue())
            result.calendars = metadata.value().calendars;
        else
            result.calendarsError = metadata.error();
    }
    if (holds(BackupSection::Tasks)) {
        const Result<QList<CalendarTask>> stored = organizerStore.tasks();
        if (!stored.hasValue())
            return stored.error();
        classify(own(backup.data.tasks), stored.value(), &taskKey, &codec::sameTaskContent,
                 &codec::sameTaskContent, result.newTasks, result.changedTasks,
                 result.unchangedTasks);
    }
    if (holds(BackupSection::Notes)) {
        const Result<QList<Note>> stored = organizerStore.notes();
        if (!stored.hasValue())
            return stored.error();
        classify(own(backup.data.notes), stored.value(), &noteKey, &codec::sameNoteContent,
                 &codec::sameNoteContent, result.newNotes, result.changedNotes,
                 result.unchangedNotes);
    }
    if (holds(BackupSection::Messages)) {
        const Result<QList<store::StoredMessage>> stored = messageStore.allMessages();
        if (!stored.hasValue())
            return stored.error();
        QSet<QString> known;
        for (const store::StoredMessage &message : stored.value())
            known.insert(messageKey(message.threadKey, message.date, message.box, message.body));
        for (const codec::Message &message : backup.data.messages) {
            const QString key =
                messageKey(store::MessageStore::threadKeyOf(message.addresses.value(0)),
                           message.date, message.box, message.body);
            if (known.contains(key)) {
                ++result.knownMessages;
            } else {
                known.insert(key);
                result.newMessages.append(message);
            }
        }
    }
    return result;
}

QFuture<Result<BackupService::Plan>> BackupService::startPlan(const Backup &backup)
{
    const QString profilesDirectory = QFileInfo(m_profile->directory()).absolutePath();
    const QString deviceId = m_profile->deviceId();
    return QtConcurrent::run([profilesDirectory, deviceId, backup]() -> Result<Plan> {
               // A failure must not escape the worker; the caller gets an error instead.
               try {
                   return computePlan(profilesDirectory, deviceId, backup);
               } catch (...) {
                   return workerFailed();
               }
           })
        .onFailed(this, []() { return Result<Plan>(workerFailed()); });
}

QFuture<Result<BackupPreview>> BackupService::preview(const Backup &backup)
{
    if (m_running)
        return failed<BackupPreview>(busy());
    if (!m_profile)
        return failed<BackupPreview>(noStore());

    using Pull = std::function<QFuture<Result<void>>()>;
    QList<Pull> pulls;
    if (backup.info.counts.contains(BackupSection::Contacts)) {
        pulls.append([this]() {
            return m_services.contacts->sync().then(
                this,
                [](const Result<store::ContactChanges> &pulled) { return withoutValue(pulled); });
        });
    }
    if (backup.info.counts.contains(BackupSection::Calendar) ||
        backup.info.counts.contains(BackupSection::Tasks)) {
        pulls.append([this]() {
            return m_services.calendar->sync().then(
                this,
                [](const Result<store::OrganizerChanges> &pulled) { return withoutValue(pulled); });
        });
    }
    if (backup.info.counts.contains(BackupSection::Notes)) {
        pulls.append([this]() {
            return m_services.notes->sync().then(
                this,
                [](const Result<store::OrganizerChanges> &pulled) { return withoutValue(pulled); });
        });
    }

    begin();
    struct Job
    {
        QPromise<Result<BackupPreview>> promise;
        QList<Pull> pulls;
        Backup backup;
        bool fromPhone = true;
        std::function<void()> next;
    };
    auto job = std::make_shared<Job>();
    job->promise.start();
    job->pulls = pulls;
    job->backup = backup;
    const auto finish = [this](const std::shared_ptr<Job> &done,
                               const Result<BackupPreview> &result) {
        m_running = false;
        done->promise.addResult(result);
        done->promise.finish();
    };
    const auto compare = [this, finish](const std::shared_ptr<Job> &current) {
        startPlan(current->backup).then(this, [current, finish](const Result<Plan> &planned) {
            if (!planned.hasValue()) {
                finish(current, planned.error());
                return;
            }
            const Plan &result = planned.value();
            BackupPreview preview;
            preview.comparedWithPhone = current->fromPhone;
            preview.fromOtherPhone = result.otherPhone;
            const QMap<BackupSection, int> &counts = current->backup.info.counts;
            for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
                SectionPreview &section = preview.sections[it.key()];
                switch (it.key()) {
                case BackupSection::Contacts:
                    section = {int(result.newContacts.size()), int(result.changedContacts.size()),
                               result.unchangedContacts};
                    break;
                case BackupSection::Calendar:
                    section = {int(result.newEvents.size()), int(result.changedEvents.size()),
                               result.unchangedEvents};
                    break;
                case BackupSection::Tasks:
                    section = {int(result.newTasks.size()), int(result.changedTasks.size()),
                               result.unchangedTasks};
                    break;
                case BackupSection::Notes:
                    section = {int(result.newNotes.size()), int(result.changedNotes.size()),
                               result.unchangedNotes};
                    break;
                case BackupSection::Calls:
                    break;
                case BackupSection::Messages:
                    section = {int(result.newMessages.size()), 0, result.knownMessages};
                    break;
                }
            }
            finish(current, preview);
        });
    };
    // The callback refers to the job only weakly, so that the job is freed
    // once the last pull has answered.
    job->next = [this, weak = std::weak_ptr<Job>(job), finish, compare]() {
        const std::shared_ptr<Job> current = weak.lock();
        if (!current)
            return;
        if (m_cancel->load()) {
            finish(current,
                   Error(ErrorCode::Cancelled, QStringLiteral("The preview was cancelled")));
            return;
        }
        if (!m_profile) {
            finish(current, noStore());
            return;
        }
        if (current->pulls.isEmpty() || !current->fromPhone) {
            compare(current);
            return;
        }
        const Pull pull = current->pulls.takeFirst();
        pull().then(this, [current, finish](const Result<void> &pulled) {
            if (!pulled.hasValue()) {
                if (pulled.error().code() != ErrorCode::ConnectionClosed) {
                    finish(current, pulled.error());
                    return;
                }
                current->fromPhone = false;
            }
            if (current->next)
                current->next();
        });
    };
    job->next();
    return job->promise.future();
}

QFuture<Result<RestoreReport>> BackupService::restore(const Backup &backup,
                                                      const QList<BackupSection> &sections)
{
    if (m_running)
        return failed<RestoreReport>(busy());
    if (!m_profile)
        return failed<RestoreReport>(noStore());
    for (const BackupSection section : sections) {
        if (!isRestorable(section)) {
            return failed<RestoreReport>(
                Error(ErrorCode::InvalidInput,
                      QStringLiteral("Section %1 cannot be restored").arg(sectionKey(section))));
        }
        if (!backup.info.counts.contains(section)) {
            return failed<RestoreReport>(
                Error(ErrorCode::InvalidInput,
                      QStringLiteral("The backup holds no section %1").arg(sectionKey(section))));
        }
    }

    auto run = std::make_shared<RestoreRun>();
    run->promise.start();
    QFuture<Result<RestoreReport>> future = run->promise.future();
    for (const BackupSection section : sections)
        run->report.sections.insert(section, SectionOutcome());
    begin();
    run->stop = m_cancel;
    startPlan(backup).then(this, [this, run, sections](const Result<Plan> &planned) {
        if (!planned.hasValue()) {
            m_running = false;
            run->promise.addResult(planned.error());
            run->promise.finish();
            return;
        }
        addJobs(run, planned.value(), sections);
        Q_EMIT progressChanged(0, run->total);
        runNext(run);
    });
    return future;
}

void BackupService::addJobs(const std::shared_ptr<RestoreRun> &run, const Plan &todo,
                            const QList<BackupSection> &sections)
{
    const auto add = [&run](BackupSection section, int weight,
                            std::function<QFuture<Result<int>>()> start) {
        run->jobs.append({section, weight, std::move(start)});
        run->total += weight;
    };
    // New entries need what could not be read; they count as failed with that error.
    const auto failAll = [&run](BackupSection section, qsizetype count, const Error &error) {
        if (count == 0)
            return;
        SectionOutcome &outcome = run->report.sections[section];
        outcome.failed += int(count);
        if (!outcome.firstError.isError())
            outcome.firstError = error;
    };
    service::ContactsService *contacts = m_services.contacts;
    service::CalendarService *calendar = m_services.calendar;
    service::NotesService *notes = m_services.notes;

    if (sections.contains(BackupSection::Contacts)) {
        if (todo.accountsError.isError()) {
            failAll(BackupSection::Contacts, todo.newContacts.size(), todo.accountsError);
        } else {
            for (Contact contact : todo.newContacts) {
                contact.luid = 0;
                const bool knownAccount =
                    std::any_of(todo.accounts.cbegin(), todo.accounts.cend(),
                                [&contact](const codec::PbAccount &account) {
                                    return account.type == contact.accountType &&
                                           account.name == contact.accountName;
                                });
                // An account this phone lacks, as after a change of phone, gives way to its
                // default.
                if (!knownAccount) {
                    contact.accountType.clear();
                    contact.accountName.clear();
                }
                // A photo larger than the phone takes would cost the whole contact.
                if (contact.photo.size() > codec::maxPhotoBytesToSend)
                    contact.photo.clear();
                add(BackupSection::Contacts, 1, [this, contacts, contact]() {
                    return contacts->save(contact, true, SaveMode::Overwrite)
                        .then(this,
                              [](const Result<service::SaveOutcome> &saved) { return one(saved); });
                });
            }
        }
        for (const Contact &contact : todo.changedContacts) {
            const bool photoFits = contact.photo.size() <= codec::maxPhotoBytesToSend;
            add(BackupSection::Contacts, 1, [this, contacts, contact, photoFits]() {
                return contacts->save(contact, photoFits, SaveMode::Overwrite)
                    .then(this,
                          [](const Result<service::SaveOutcome> &saved) { return one(saved); });
            });
        }
    }
    if (sections.contains(BackupSection::Calendar)) {
        if (todo.calendarsError.isError()) {
            failAll(BackupSection::Calendar, todo.newEvents.size(), todo.calendarsError);
        } else {
            std::optional<codec::CalendarInfo> fallback;
            for (const codec::CalendarInfo &info : todo.calendars) {
                if (info.isWritable() && (!fallback || info.isDefault))
                    fallback = info;
            }
            for (CalendarEvent event : todo.newEvents) {
                event.luid = 0;
                const bool ownCalendar = std::any_of(todo.calendars.cbegin(), todo.calendars.cend(),
                                                     [&event](const codec::CalendarInfo &info) {
                                                         return info.isWritable() &&
                                                                info.url == event.calendarUrl &&
                                                                info.title == event.calendarTitle;
                                                     });
                if (!ownCalendar && fallback) {
                    event.calendarUrl = fallback->url;
                    event.calendarTitle = fallback->title;
                }
                add(BackupSection::Calendar, 1, [this, calendar, event]() {
                    return calendar->saveEvent(event, SaveMode::Overwrite)
                        .then(this, [](const Result<service::EventSaveOutcome> &saved) {
                            return one(saved);
                        });
                });
            }
        }
        for (const CalendarEvent &event : todo.changedEvents) {
            add(BackupSection::Calendar, 1, [this, calendar, event]() {
                return calendar->saveEvent(event, SaveMode::Overwrite)
                    .then(this, [](const Result<service::EventSaveOutcome> &saved) {
                        return one(saved);
                    });
            });
        }
    }
    if (sections.contains(BackupSection::Tasks)) {
        for (CalendarTask task : todo.newTasks) {
            task.luid = 0;
            add(BackupSection::Tasks, 1, [this, calendar, task]() {
                return calendar->saveTask(task, SaveMode::Overwrite)
                    .then(this,
                          [](const Result<service::TaskSaveOutcome> &saved) { return one(saved); });
            });
        }
        for (const CalendarTask &task : todo.changedTasks) {
            add(BackupSection::Tasks, 1, [this, calendar, task]() {
                return calendar->saveTask(task, SaveMode::Overwrite)
                    .then(this,
                          [](const Result<service::TaskSaveOutcome> &saved) { return one(saved); });
            });
        }
    }
    if (sections.contains(BackupSection::Notes)) {
        for (Note note : todo.newNotes) {
            note.luid = 0;
            add(BackupSection::Notes, 1, [this, notes, note]() {
                return notes->save(note, SaveMode::Overwrite)
                    .then(this,
                          [](const Result<service::NoteSaveOutcome> &saved) { return one(saved); });
            });
        }
        for (const Note &note : todo.changedNotes) {
            add(BackupSection::Notes, 1, [this, notes, note]() {
                return notes->save(note, SaveMode::Overwrite)
                    .then(this,
                          [](const Result<service::NoteSaveOutcome> &saved) { return one(saved); });
            });
        }
    }
    if (sections.contains(BackupSection::Messages) && !todo.newMessages.isEmpty()) {
        const QList<codec::Message> messages = todo.newMessages;
        service::MessagesService *archive = m_services.messages;
        add(BackupSection::Messages, int(messages.size()),
            [archive, messages]() { return archive->importToArchive(messages); });
    }
}

void BackupService::runNext(const std::shared_ptr<RestoreRun> &run)
{
    while (run->next < run->jobs.size() && run->ended.contains(run->jobs.at(run->next).section)) {
        const RestoreRun::Job &skipped = run->jobs.at(run->next++);
        run->report.sections[skipped.section].failed += skipped.weight;
        run->done += skipped.weight;
    }
    const bool stopped = run->stop->load();
    if (stopped || run->next >= run->jobs.size()) {
        run->report.cancelled = stopped && run->next < run->jobs.size();
        m_running = false;
        run->promise.addResult(run->report);
        run->promise.finish();
        return;
    }
    const RestoreRun::Job job = run->jobs.at(run->next++);
    job.start()
        .onFailed(this, []() { return Result<int>(workerFailed()); })
        .onCanceled(this, []() { return Result<int>(cancelled()); })
        .then(this, [this, run, job](const Result<int> &result) {
            SectionOutcome &outcome = run->report.sections[job.section];
            if (result.hasValue()) {
                outcome.restored += result.value();
            } else {
                outcome.failed += job.weight;
                if (!outcome.firstError.isError())
                    outcome.firstError = result.error();
                if (endsSection(result.error()))
                    run->ended.insert(job.section);
            }
            run->done += job.weight;
            Q_EMIT progressChanged(run->done, run->total);
            runNext(run);
        });
}

} // namespace qpm::core::backup
