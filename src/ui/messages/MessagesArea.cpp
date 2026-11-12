#include "ui/messages/MessagesArea.h"

#include "core/Logging.h"
#include "core/codec/Limits.h"
#include "core/codec/MessageExport.h"
#include "core/service/ContactsService.h"
#include "core/service/DeviceStatusService.h"
#include "core/store/ContactStore.h"
#include "core/store/ProfileStore.h"
#include "ui/PhoneNumberText.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::store::StoredAttachment;
using core::store::StoredMessage;

constexpr qsizetype maxFileNameBytes = 200;
constexpr int maxUniqueNameTries = 1000;

QString blobPath(const QString &profileDirectory, const QString &blob)
{
    return QDir(profileDirectory).filePath(QStringLiteral("blobs/") + blob);
}

Result<QByteArray> readBlob(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot read a stored part"));
    if (file.size() > core::codec::maxAttachmentBytes)
        return Error(ErrorCode::LimitExceeded, QStringLiteral("Stored part too large"));
    return file.readAll();
}

Result<void> writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot write the file"));
    return {};
}

// A file of that name below `folder` that does not exist yet: "name (2).ext".
Result<QString> freePath(const QString &folder, const QString &name)
{
    const QFileInfo info(name);
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : u'.' + info.suffix();
    for (int i = 1; i <= maxUniqueNameTries; ++i) {
        const QString candidate =
            i == 1 ? name : QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(suffix);
        const Result<QString> path = core::limits::safeDownloadPath(folder, candidate);
        if (!path.hasValue())
            return path;
        if (!QFileInfo::exists(path.value()))
            return path;
    }
    return Error(ErrorCode::IoFailure, QStringLiteral("No free file name"));
}

core::codec::Message codecMessage(const StoredMessage &stored)
{
    core::codec::Message message;
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
    for (const StoredAttachment &attachment : stored.attachments) {
        core::codec::MessageAttachment part;
        part.name = attachment.name;
        part.mimeType = attachment.mimeType;
        part.size = attachment.size;
        part.onPhoneOnly = attachment.blob.isEmpty();
        message.attachments.append(part);
    }
    return message;
}

struct ExportJob
{
    QString path;
    MessagesArea::ExportFormat format = MessagesArea::ExportFormat::Csv;
    core::codec::CsvLanguage language = core::codec::CsvLanguage::English;
    QString title;
    QString profileDirectory;
    QList<core::codec::ExportedMessage> messages;
    /// Blob names per message and part, for the archive format.
    QList<QStringList> blobs;
};

Result<int> runExport(ExportJob job)
{
    QByteArray data;
    switch (job.format) {
    case MessagesArea::ExportFormat::Csv:
        data = core::codec::writeMessagesCsv(job.messages, job.language);
        break;
    case MessagesArea::ExportFormat::Html:
        data = core::codec::writeMessagesHtml(job.messages, job.title, job.language);
        break;
    case MessagesArea::ExportFormat::Xml:
        for (qsizetype i = 0; i < job.messages.size(); ++i) {
            QList<core::codec::MessageAttachment> &parts = job.messages[i].message.attachments;
            for (qsizetype p = 0; p < parts.size(); ++p) {
                const QString blob = job.blobs.at(i).value(p);
                if (blob.isEmpty())
                    continue;
                const Result<QByteArray> content = readBlob(blobPath(job.profileDirectory, blob));
                if (!content.hasValue())
                    return content.error();
                parts[p].data = content.value();
            }
        }
        data = core::codec::writeMessagesXml(job.messages);
        break;
    }
    if (const Result<void> written = writeFile(job.path, data); !written.hasValue())
        return written.error();
    return int(job.messages.size());
}

} // namespace

MessagesArea::MessagesArea(core::protocol::PhoneConnection *connection,
                           core::service::DeviceStatusService *status, QObject *parent)
    : QObject(parent), m_service(new core::service::MessagesService(connection, this)),
      m_status(status), m_conversations(new ConversationListModel(this)),
      m_messages(new MessageListModel(this)), m_scheduled(new ScheduledListModel(this)),
      m_composer(new MessageComposer(m_service, this))
{
    Q_ASSERT(status);
    const auto names = [this](const QString &number) { return contactNameFor(number); };
    m_composer->setNameLookup(names);
    m_scheduled->setNameLookup(names);
    m_composer->setScheduledLookup(
        [this](quint32 luid, QStringList &numbers, QString &text, QDateTime &when, int &simSlot) {
            const std::optional<StoredMessage> found = m_scheduled->message(luid);
            if (!found)
                return false;
            numbers = found->addresses;
            text = found->body;
            when = found->date;
            simSlot = found->simSlot;
            return true;
        });
    connect(m_service, &core::service::MessagesService::messagesChanged, this,
            &MessagesArea::reload);
    connect(m_service, &core::service::MessagesService::messagesArrived, this,
            &MessagesArea::announce);
    connect(m_composer, &MessageComposer::errorOccurred, this, &MessagesArea::errorOccurred);
    connect(m_composer, &MessageComposer::infoMessage, this, &MessagesArea::infoMessage);
    connect(m_composer, &MessageComposer::finished, this, [this]() { setComposing(false); });
    connect(m_status, &core::service::DeviceStatusService::changed, this,
            &MessagesArea::updateSimCards);
}

void MessagesArea::setCurrentThread(const QString &threadKey)
{
    if (threadKey == m_currentThread && !m_composing)
        return;
    m_currentThread = threadKey;
    setComposing(false);
    reloadCurrent();
    Q_EMIT currentThreadChanged();
}

void MessagesArea::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    applySearch();
    Q_EMIT searchTextChanged();
}

void MessagesArea::setComposing(bool composing)
{
    if (composing == m_composing)
        return;
    m_composing = composing;
    Q_EMIT composingChanged();
}

bool MessagesArea::hasCurrent() const
{
    return !m_currentThread.isEmpty() && m_conversations->find(m_currentThread);
}

QString MessagesArea::currentTitle() const
{
    const core::store::ConversationSummary *found = m_conversations->find(m_currentThread);
    return found ? m_conversations->titleFor(found->address) : QString();
}

QString MessagesArea::currentAddress() const
{
    const core::store::ConversationSummary *found = m_conversations->find(m_currentThread);
    return found ? PhoneNumberText::display(found->address) : QString();
}

QString MessagesArea::currentNumber() const
{
    const core::store::ConversationSummary *found = m_conversations->find(m_currentThread);
    return found ? core::service::ContactsService::dialableNumber(found->address) : QString();
}

bool MessagesArea::currentWritable() const
{
    return !currentNumber().isEmpty();
}

int MessagesArea::currentUnread() const
{
    const core::store::ConversationSummary *found = m_conversations->find(m_currentThread);
    return found ? found->unread : 0;
}

void MessagesArea::setProfileStore(core::store::ProfileStore *profile)
{
    m_store = profile ? std::make_shared<core::store::MessageStore>(profile) : nullptr;
    m_contacts = profile ? std::make_shared<core::store::ContactStore>(profile) : nullptr;
    m_service->setStore(m_store);
    m_currentThread.clear();
    setComposing(false);
    refreshNames();
    reload();
    Q_EMIT currentThreadChanged();
    Q_EMIT hasPhoneChanged();
    Q_EMIT canEditChanged();
}

void MessagesArea::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    m_composer->setConnected(connected);
    Q_EMIT canEditChanged();
}

void MessagesArea::refreshNames()
{
    m_contactNames.clear();
    if (m_contacts) {
        const Result<QHash<QString, QString>> names = m_contacts->namesByNumber();
        if (names.hasValue()) {
            m_contactNames = names.value();
        } else {
            // The messages stay readable with the numbers.
            qCWarning(core::lcUi) << "Contact names for the messages not read:"
                                  << names.error().message();
        }
    }
    m_conversations->setContactNames(m_contactNames);
    m_composer->setNameLookup([this](const QString &number) { return contactNameFor(number); });
    m_scheduled->setNameLookup([this](const QString &number) { return contactNameFor(number); });
    Q_EMIT currentChanged();
}

QString MessagesArea::contactNameFor(const QString &number) const
{
    const QString key = core::store::ContactStore::numberKey(number);
    return key.isEmpty() ? QString() : m_contactNames.value(key);
}

QFuture<Result<core::store::MessageChanges>> MessagesArea::sync()
{
    return m_service->sync();
}

void MessagesArea::reload()
{
    if (!m_store) {
        m_conversations->setConversations({});
        m_scheduled->setMessages({});
        m_messages->setMessages({});
        Q_EMIT currentChanged();
        return;
    }
    const Result<QList<core::store::ConversationSummary>> conversations = m_store->conversations();
    if (!conversations.hasValue())
        report(conversations.error(), Action::Sync);
    m_conversations->setConversations(conversations.hasValue()
                                          ? conversations.value()
                                          : QList<core::store::ConversationSummary>());
    const Result<QList<StoredMessage>> scheduled = m_store->scheduledMessages();
    if (!scheduled.hasValue())
        report(scheduled.error(), Action::Sync);
    m_scheduled->setMessages(scheduled.hasValue() ? scheduled.value() : QList<StoredMessage>());
    applySearch();
    reloadCurrent();
}

void MessagesArea::reloadCurrent()
{
    if (!m_store || m_currentThread.isEmpty()) {
        m_messages->setMessages({});
        Q_EMIT currentChanged();
        return;
    }
    const Result<QList<StoredMessage>> messages = m_store->conversation(m_currentThread);
    if (!messages.hasValue())
        report(messages.error(), Action::Sync);
    m_messages->setMessages(messages.hasValue() ? messages.value() : QList<StoredMessage>());
    Q_EMIT currentChanged();
}

void MessagesArea::applySearch()
{
    if (!m_store || m_searchText.trimmed().isEmpty()) {
        m_conversations->setSearch(m_searchText, {});
        return;
    }
    const Result<QSet<QString>> matches = m_store->searchConversations(m_searchText);
    if (!matches.hasValue()) {
        // The names and numbers are still searched.
        qCWarning(core::lcUi) << "Messages not searched:" << matches.error().message();
    }
    m_conversations->setSearch(m_searchText,
                               matches.hasValue() ? matches.value() : QSet<QString>());
}

void MessagesArea::updateSimCards()
{
    QStringList labels;
    QList<int> simSlots;
    for (const core::service::SimCard &sim : m_status->overview().sims) {
        const QString name = sim.name.isEmpty() ? sim.carrier : sim.name;
        labels.append(name.isEmpty() ? tr("SIM %1").arg(sim.slot + 1)
                                     : tr("SIM %1: %2").arg(sim.slot + 1).arg(name));
        simSlots.append(sim.slot);
    }
    m_composer->setSimCards(labels, simSlots);
}

void MessagesArea::announce(const QList<quint32> &luids)
{
    if (!m_store || luids.isEmpty())
        return;
    if (luids.size() == 1) {
        const Result<StoredMessage> message = m_store->message(luids.constFirst());
        if (message.hasValue()) {
            Q_EMIT infoMessage(
                tr("New message from %1.")
                    .arg(m_conversations->titleFor(message.value().addresses.value(0))));
            return;
        }
    }
    Q_EMIT infoMessage(tr("%n new message(s).", nullptr, int(luids.size())));
}

void MessagesArea::report(const Error &error, Action action)
{
    const QString message = errorText(error, action);
    if (!message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void MessagesArea::beginWork()
{
    if (m_busy++ == 0)
        Q_EMIT busyChanged();
}

void MessagesArea::endWork()
{
    if (m_busy > 0 && --m_busy == 0)
        Q_EMIT busyChanged();
}

QList<quint32> MessagesArea::currentLuids() const
{
    QList<quint32> luids;
    for (const StoredMessage &message : m_messages->messages())
        luids.append(message.luid);
    return luids;
}

bool MessagesArea::composeTo(const QString &number)
{
    const QString dialable = core::service::ContactsService::dialableNumber(number);
    if (dialable.isEmpty()) {
        Q_EMIT errorOccurred(tr("This is no phone number a message can go to."));
        return false;
    }
    m_composer->startNew({dialable});
    setComposing(true);
    return true;
}

void MessagesArea::composeNew()
{
    m_composer->startNew({});
    setComposing(true);
}

bool MessagesArea::editScheduled(int luid)
{
    if (!m_composer->startEditing(luid))
        return false;
    setComposing(true);
    return true;
}

bool MessagesArea::replyToCurrent()
{
    return hasCurrent() && composeTo(currentNumber());
}

bool MessagesArea::removeMessages(const QList<int> &luids)
{
    if (!m_store) {
        report(Error(ErrorCode::InvalidInput, QStringLiteral("No phone chosen")), Action::Delete);
        return false;
    }
    QList<quint32> wanted;
    for (const int luid : luids) {
        if (luid > 0)
            wanted.append(quint32(luid));
    }
    if (wanted.isEmpty())
        return false;
    beginWork();
    m_service->remove(wanted).then(this, [this, wanted](const Result<void> &removed) {
        endWork();
        if (!removed.hasValue()) {
            report(removed.error(), Action::Delete);
            return;
        }
        Q_EMIT infoMessage(tr("%n message(s) deleted.", nullptr, int(wanted.size())));
    });
    return true;
}

bool MessagesArea::removeCurrentConversation()
{
    QList<int> luids;
    const QList<quint32> current = currentLuids();
    for (const quint32 luid : current)
        luids.append(int(luid));
    return removeMessages(luids);
}

bool MessagesArea::markRead(const QList<int> &luids, bool read)
{
    if (!m_store)
        return false;
    QList<quint32> wanted;
    for (const int luid : luids) {
        if (luid > 0)
            wanted.append(quint32(luid));
    }
    if (wanted.isEmpty())
        return false;
    beginWork();
    m_service->setRead(wanted, read).then(this, [this](const Result<void> &changed) {
        endWork();
        if (!changed.hasValue())
            report(changed.error(), Action::MarkRead);
    });
    return true;
}

bool MessagesArea::markCurrentRead()
{
    QList<int> luids;
    const QList<quint32> unread = m_messages->unreadLuids();
    for (const quint32 luid : unread)
        luids.append(int(luid));
    return markRead(luids, true);
}

bool MessagesArea::archive(const QList<int> &luids, bool archived)
{
    QList<quint32> wanted;
    for (const int luid : luids) {
        if (luid > 0)
            wanted.append(quint32(luid));
    }
    if (wanted.isEmpty())
        return false;
    const Result<void> changed = m_service->setArchived(wanted, archived);
    if (!changed.hasValue()) {
        report(changed.error(), Action::Archive);
        return false;
    }
    Q_EMIT infoMessage(
        archived
            ? tr("%n message(s) kept in the archive on this computer.", nullptr, int(wanted.size()))
            : tr("%n message(s) taken out of the archive.", nullptr, int(wanted.size())));
    return true;
}

bool MessagesArea::archiveCurrentConversation()
{
    QList<int> luids;
    for (const StoredMessage &message : m_messages->messages()) {
        if (!message.archived)
            luids.append(int(message.luid));
    }
    if (luids.isEmpty()) {
        Q_EMIT infoMessage(tr("This conversation is already in the archive."));
        return false;
    }
    return archive(luids, true);
}

QString MessagesArea::suggestedFileName(int luid, int position) const
{
    if (!m_store || luid <= 0)
        return {};
    const Result<StoredMessage> message = m_store->message(quint32(luid));
    if (!message.hasValue())
        return {};
    for (const StoredAttachment &attachment : message.value().attachments) {
        if (attachment.position == position)
            return safeFileName(attachment.name, attachment.mimeType, position);
    }
    return {};
}

QUrl MessagesArea::suggestedFileUrl(int luid, int position, const QUrl &folder) const
{
    const QString name = suggestedFileName(luid, position);
    if (name.isEmpty() || !folder.isLocalFile())
        return {};
    return QUrl::fromLocalFile(QDir(folder.toLocalFile()).filePath(name));
}

bool MessagesArea::saveAttachment(int luid, int position, const QUrl &url)
{
    if (!m_store || luid <= 0)
        return false;
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    const Result<StoredMessage> message = m_store->message(quint32(luid));
    if (!message.hasValue()) {
        report(message.error(), Action::Save);
        return false;
    }
    QString blob;
    for (const StoredAttachment &attachment : message.value().attachments) {
        if (attachment.position == position)
            blob = attachment.blob;
    }
    if (blob.isEmpty()) {
        report(Error(ErrorCode::NotFound, QStringLiteral("Part not on this computer")),
               Action::Save);
        return false;
    }
    const QString source = blobPath(m_store->profileDirectory(), blob);
    const QString target = url.toLocalFile();
    beginWork();
    QtConcurrent::run([source, target]() -> Result<void> {
        const Result<QByteArray> content = readBlob(source);
        if (!content.hasValue())
            return content.error();
        return writeFile(target, content.value());
    }).then(this, [this, target](const Result<void> &written) {
        endWork();
        if (!written.hasValue()) {
            report(written.error(), Action::Save);
            return;
        }
        Q_EMIT infoMessage(tr("The attachment is saved as %1.").arg(QFileInfo(target).fileName()));
    });
    return true;
}

bool MessagesArea::saveAllAttachments(int luid, const QUrl &url)
{
    if (!m_store || luid <= 0)
        return false;
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a folder on this computer."));
        return false;
    }
    const Result<StoredMessage> message = m_store->message(quint32(luid));
    if (!message.hasValue()) {
        report(message.error(), Action::Save);
        return false;
    }
    QList<std::pair<QString, QString>> parts;
    for (const StoredAttachment &attachment : message.value().attachments) {
        if (!attachment.blob.isEmpty()) {
            parts.append({blobPath(m_store->profileDirectory(), attachment.blob),
                          safeFileName(attachment.name, attachment.mimeType, attachment.position)});
        }
    }
    if (parts.isEmpty()) {
        report(Error(ErrorCode::NotFound, QStringLiteral("No part on this computer")),
               Action::Save);
        return false;
    }
    const QString folder = url.toLocalFile();
    beginWork();
    QtConcurrent::run([parts, folder]() -> Result<int> {
        int saved = 0;
        for (const auto &[source, name] : parts) {
            const Result<QString> target = freePath(folder, name);
            if (!target.hasValue())
                return target.error();
            const Result<QByteArray> content = readBlob(source);
            if (!content.hasValue())
                return content.error();
            if (const Result<void> written = writeFile(target.value(), content.value());
                !written.hasValue()) {
                return written.error();
            }
            ++saved;
        }
        return saved;
    }).then(this, [this](const Result<int> &saved) {
        endWork();
        if (!saved.hasValue()) {
            report(saved.error(), Action::Save);
            return;
        }
        Q_EMIT infoMessage(tr("%n attachment(s) saved.", nullptr, saved.value()));
    });
    return true;
}

bool MessagesArea::exportConversation(const QUrl &url, ExportFormat format)
{
    if (!hasCurrent())
        return false;
    return exportMessages(url, format, currentTitle(), m_messages->messages());
}

bool MessagesArea::exportAll(const QUrl &url, ExportFormat format)
{
    if (!m_store) {
        report(Error(ErrorCode::InvalidInput, QStringLiteral("No phone chosen")), Action::Export);
        return false;
    }
    const Result<QList<StoredMessage>> all = m_store->allMessages();
    if (!all.hasValue()) {
        report(all.error(), Action::Export);
        return false;
    }
    return exportMessages(url, format, tr("Messages"), all.value());
}

bool MessagesArea::exportMessages(const QUrl &url, ExportFormat format, const QString &title,
                                  const QList<StoredMessage> &messages)
{
    if (!m_store)
        return false;
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    ExportJob job;
    job.path = url.toLocalFile();
    job.format = format;
    job.language = QLocale().language() == QLocale::German ? core::codec::CsvLanguage::German
                                                           : core::codec::CsvLanguage::English;
    job.title = title;
    job.profileDirectory = m_store->profileDirectory();
    for (const StoredMessage &stored : messages) {
        core::codec::ExportedMessage exported;
        exported.message = codecMessage(stored);
        exported.contactName = contactNameFor(stored.addresses.value(0));
        exported.archived = stored.archived;
        job.messages.append(exported);
        QStringList blobs;
        for (const StoredAttachment &attachment : stored.attachments)
            blobs.append(attachment.blob);
        job.blobs.append(blobs);
    }
    beginWork();
    QtConcurrent::run([job]() {
        return runExport(job);
    }).then(this, [this](const Result<int> &written) {
        endWork();
        if (!written.hasValue()) {
            report(written.error(), Action::Export);
            return;
        }
        Q_EMIT infoMessage(tr("%n message(s) exported.", nullptr, written.value()));
    });
    return true;
}

bool MessagesArea::importArchive(const QUrl &url)
{
    if (!m_store) {
        report(Error(ErrorCode::InvalidInput, QStringLiteral("No phone chosen")), Action::Import);
        return false;
    }
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    const QString path = url.toLocalFile();
    const QString directory = m_store->profileDirectory();
    const std::weak_ptr<core::store::MessageStore> target = m_store;
    beginWork();
    QtConcurrent::run([path, directory]() -> Result<QList<StoredMessage>> {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return Error(ErrorCode::IoFailure, QStringLiteral("Cannot open the file"));
        if (file.size() > core::codec::maxArchiveBytes)
            return Error(ErrorCode::LimitExceeded, QStringLiteral("File too large"));
        const Result<QList<core::codec::Message>> read =
            core::codec::readMessagesXml(file.readAll());
        if (!read.hasValue())
            return read.error();
        return core::store::MessageStore::prepareImport(read.value(), directory);
    }).then(this, [this, target](const Result<QList<StoredMessage>> &prepared) {
        endWork();
        const std::shared_ptr<core::store::MessageStore> store = target.lock();
        if (!store)
            return;
        if (!prepared.hasValue()) {
            report(prepared.error(), Action::Import);
            return;
        }
        const Result<int> added = store->importPrepared(prepared.value());
        if (!added.hasValue()) {
            report(added.error(), Action::Import);
            return;
        }
        reload();
        Q_EMIT infoMessage(added.value() == 0
                               ? tr("The archive holds no message that is not here already.")
                               : tr("%n message(s) added to the archive on this computer.", nullptr,
                                    added.value()));
    });
    return true;
}

QString MessagesArea::errorText(const Error &error, Action action)
{
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::ConnectionClosed:
    case ErrorCode::Timeout:
        if (action == Action::Send) {
            return tr("The phone did not confirm the message. Check in the list whether it was "
                      "sent before you send it again.");
        }
        return tr("The phone is not connected. Connect it, then try again.");
    case ErrorCode::PermissionDenied:
    case ErrorCode::AccessDenied:
        switch (action) {
        case Action::Delete:
            return tr("This version of the phone app does not allow deleting text messages. You "
                      "can archive the message on this computer.");
        case Action::MarkRead:
            return tr("This version of the phone app does not allow changing the read state. On "
                      "the phone the message stays as it is.");
        case Action::Send:
        case Action::Schedule:
            return tr("The app on the phone may not send text messages. Allow it in the Android "
                      "settings.");
        default:
            break;
        }
        return tr("The app on the phone may not read your messages. Allow it in the Android "
                  "settings.");
    case ErrorCode::NotSupported:
        switch (action) {
        case Action::Delete:
            return tr("The phone app cannot delete multimedia messages. Delete them on the phone "
                      "itself.");
        case Action::MarkRead:
            return tr("The phone app cannot change the read state of multimedia messages.");
        case Action::Send:
            return tr("The phone cannot send text messages. Update MyPhoneExplorer Client on the "
                      "phone.");
        default:
            break;
        }
        return tr("The phone does not support this. Update MyPhoneExplorer Client on the phone.");
    case ErrorCode::NotFound:
        if (action == Action::Save) {
            return tr("This attachment stayed on the phone because it is large. Save it on the "
                      "phone itself.");
        }
        return tr("This message is no longer on the phone. Sync to update the list.");
    case ErrorCode::LimitExceeded:
        if (action == Action::Send || action == Action::Schedule)
            return tr("The text is too long. Shorten it, then try again.");
        if (action == Action::Import)
            return tr("The file is too large or holds too many messages.");
        return tr("There are too many messages on the phone to read at once. Delete old messages "
                  "on the phone, then sync again.");
    case ErrorCode::IoFailure:
        if (action == Action::Export || action == Action::Save)
            return tr("The file cannot be written. Choose another folder.");
        if (action == Action::Import)
            return tr("The file cannot be read. Choose another file.");
        return tr("The messages cannot be saved on this computer. Check the storage folder in the "
                  "settings.");
    case ErrorCode::InvalidInput:
        switch (action) {
        case Action::Import:
            return tr("This file is no message archive of QPhoneManager.");
        case Action::Send:
            return tr("Enter at least one recipient with a phone number and a text.");
        case Action::Schedule:
            return tr("Check the recipients, the text and the time. The time must lie at least "
                      "one minute ahead.");
        case Action::Archive:
            return tr("This message is only on this computer, so it stays in the archive. Delete "
                      "it if you no longer need it.");
        default:
            break;
        }
        return tr("Choose a phone first.");
    case ErrorCode::Rejected:
        if (action == Action::Send) {
            return tr("The phone did not send the message. Check the number and the signal, then "
                      "try again.");
        }
        break;
    case ErrorCode::WrongPin:
    case ErrorCode::AlreadyConnected:
    case ErrorCode::WifiDisabled:
    case ErrorCode::ProtocolError:
        break;
    }
    return tr("The phone did not accept the request. Sync, then try again.");
}

QString MessagesArea::safeFileName(const QString &name, const QString &mimeType, int position)
{
    QString leaf = name;
    const qsizetype slash = std::max(leaf.lastIndexOf(u'/'), leaf.lastIndexOf(u'\\'));
    if (slash >= 0)
        leaf = leaf.mid(slash + 1);
    QString cleaned;
    for (const QChar c : std::as_const(leaf)) {
        const bool control = c.unicode() < 0x20 || c.unicode() == 0x7F ||
                             c.category() == QChar::Other_Control ||
                             c.category() == QChar::Other_Format;
        static const QString reserved = QStringLiteral(":*?\"<>|");
        cleaned.append(control || reserved.contains(c) ? QChar(u'_') : c);
    }
    cleaned = cleaned.trimmed();
    while (cleaned.startsWith(u'.') || cleaned.startsWith(u'-'))
        cleaned.remove(0, 1);
    while (cleaned.endsWith(u'.') || cleaned.endsWith(u' '))
        cleaned.chop(1);
    if (cleaned.isEmpty()) {
        const QString suffix = QMimeDatabase().mimeTypeForName(mimeType).preferredSuffix();
        cleaned = tr("Attachment %1").arg(position + 1);
        if (!suffix.isEmpty())
            cleaned += u'.' + suffix;
    }
    while (cleaned.toUtf8().size() > maxFileNameBytes) {
        const QFileInfo info(cleaned);
        const QString suffix = info.suffix().isEmpty() ? QString() : u'.' + info.suffix();
        const QString stem = info.completeBaseName();
        if (stem.size() <= 1)
            cleaned.chop(1);
        else
            cleaned = stem.left(stem.size() - 1) + suffix;
    }
    return cleaned;
}

} // namespace qpm::ui
