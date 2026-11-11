#pragma once

#include "core/Error.h"
#include "core/service/MessagesService.h"
#include "core/store/MessageStore.h"
#include "ui/messages/ConversationListModel.h"
#include "ui/messages/MessageComposer.h"
#include "ui/messages/MessageListModel.h"
#include "ui/messages/ScheduledListModel.h"

#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {
class DeviceStatusService;
}

namespace qpm::core::store {
class ContactStore;
class ProfileStore;
} // namespace qpm::core::store

namespace qpm::ui {

/**
 * The messages area of the window: the conversations with their search, the
 * conversation shown, the parts of a message, archiving, export and import,
 * the form for a new or scheduled message, the scheduled messages, and the
 * requests the phone app may refuse. Every failure is reported once through
 * errorOccurred() as a translated text that says why and what to do; a
 * refusal of the phone app points to the archive. New messages from the
 * phone are announced through infoMessage().
 *
 * Owned by AppController, which hands in the profile store of the current
 * phone and the connection state. Messages are shown without a connection;
 * sending, deleting, the read state and the sync need one. No call blocks;
 * files are written and read on a worker thread.
 */
class MessagesArea : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MessagesArea)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(qpm::ui::ConversationListModel *conversations READ conversations CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::MessageListModel *messages READ messages CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ScheduledListModel *scheduled READ scheduled CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::MessageComposer *composer READ composer CONSTANT FINAL)
    /// The conversation shown; empty for none.
    Q_PROPERTY(QString currentThread READ currentThread WRITE setCurrentThread NOTIFY
                   currentThreadChanged FINAL)
    /// Narrows the conversations to those whose name, number or messages hold it.
    Q_PROPERTY(
        QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged FINAL)
    /// The form for a new or scheduled message is shown instead of a conversation.
    Q_PROPERTY(bool composing READ isComposing WRITE setComposing NOTIFY composingChanged FINAL)
    Q_PROPERTY(bool hasPhone READ hasPhone NOTIFY hasPhoneChanged FINAL)
    /// Connected to the chosen phone, so messages can be sent and synced.
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged FINAL)
    /// An export, an import, a save or a request to the phone runs.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool hasCurrent READ hasCurrent NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged FINAL)
    /// The other side of the conversation shown as the phone writes it.
    Q_PROPERTY(QString currentAddress READ currentAddress NOTIFY currentChanged FINAL)
    /// The number of the conversation shown; empty for a sender name.
    Q_PROPERTY(QString currentNumber READ currentNumber NOTIFY currentChanged FINAL)
    /// A message can be written to the conversation shown.
    Q_PROPERTY(bool currentWritable READ currentWritable NOTIFY currentChanged FINAL)
    Q_PROPERTY(int currentUnread READ currentUnread NOTIFY currentChanged FINAL)

public:
    /// The forms of an export.
    enum class ExportFormat { Csv, Html, Xml };
    Q_ENUM(ExportFormat)

    /// What the user asked for, to tell a refusal in fitting words.
    enum class Action { Sync, Send, Schedule, Delete, MarkRead, Archive, Export, Import, Save };

    /**
     * `connection` and `status` must outlive this object; `status` gives the
     * SIM cards for the form.
     */
    MessagesArea(core::protocol::PhoneConnection *connection,
                 core::service::DeviceStatusService *status, QObject *parent = nullptr);

    [[nodiscard]] ConversationListModel *conversations() const { return m_conversations; }
    [[nodiscard]] MessageListModel *messages() const { return m_messages; }
    [[nodiscard]] ScheduledListModel *scheduled() const { return m_scheduled; }
    [[nodiscard]] MessageComposer *composer() const { return m_composer; }
    [[nodiscard]] core::service::MessagesService *service() const { return m_service; }
    [[nodiscard]] QString currentThread() const { return m_currentThread; }
    void setCurrentThread(const QString &threadKey);
    [[nodiscard]] QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    [[nodiscard]] bool isComposing() const { return m_composing; }
    void setComposing(bool composing);
    [[nodiscard]] bool hasPhone() const { return m_store != nullptr; }
    [[nodiscard]] bool canEdit() const { return m_connected && m_store; }
    [[nodiscard]] bool isBusy() const { return m_busy > 0; }
    [[nodiscard]] bool hasCurrent() const;
    [[nodiscard]] QString currentTitle() const;
    [[nodiscard]] QString currentAddress() const;
    [[nodiscard]] QString currentNumber() const;
    [[nodiscard]] bool currentWritable() const;
    [[nodiscard]] int currentUnread() const;

    /**
     * Shows the messages of `profile`; null shows none. The store must stay
     * open until this is called again with another one or null.
     */
    void setProfileStore(core::store::ProfileStore *profile);
    void setConnected(bool connected);
    /// Reads the contact names again, after the contacts changed.
    void refreshNames();

    /// Reads the messages from the phone, for the sync of all areas.
    QFuture<core::Result<core::store::MessageChanges>> sync();

    /// Opens the form for a new message to `number`; false when it is no number.
    Q_INVOKABLE bool composeTo(const QString &number);
    /// Opens the empty form for a new message.
    Q_INVOKABLE void composeNew();
    /// Opens the form with the scheduled message `luid`.
    Q_INVOKABLE bool editScheduled(int luid);
    /// Opens the form for a new message to the conversation shown.
    Q_INVOKABLE bool replyToCurrent();

    /**
     * Deletes the messages. The phone app 4.15 refuses that for text
     * messages; the refusal is explained and points to the archive.
     */
    Q_INVOKABLE bool removeMessages(const QList<int> &luids);
    /// Deletes every message of the conversation shown, see removeMessages().
    Q_INVOKABLE bool removeCurrentConversation();
    /// Marks messages read or unread; a refusal of the phone app is explained.
    Q_INVOKABLE bool markRead(const QList<int> &luids, bool read);
    /// Marks every unread message of the conversation shown read.
    Q_INVOKABLE bool markCurrentRead();
    /// Keeps messages on this computer even when the phone deletes them, or stops that.
    Q_INVOKABLE bool archive(const QList<int> &luids, bool archived);
    /// Archives every message of the conversation shown.
    Q_INVOKABLE bool archiveCurrentConversation();

    /// A file name for part `position` of message `luid`, made safe from the name the phone sent.
    [[nodiscard]] Q_INVOKABLE QString suggestedFileName(int luid, int position) const;
    /// suggestedFileName() in the local folder `folder`, to preselect in a file dialog.
    [[nodiscard]] Q_INVOKABLE QUrl suggestedFileUrl(int luid, int position,
                                                    const QUrl &folder) const;
    /// Writes part `position` of message `luid` to the local file `url`.
    Q_INVOKABLE bool saveAttachment(int luid, int position, const QUrl &url);
    /// Writes every part of message `luid` into the local folder `url`, never over a file.
    Q_INVOKABLE bool saveAllAttachments(int luid, const QUrl &url);

    /// Writes the conversation shown to the local file `url`.
    Q_INVOKABLE bool exportConversation(const QUrl &url,
                                        qpm::ui::MessagesArea::ExportFormat format);
    /// Writes every message of the phone to the local file `url`.
    Q_INVOKABLE bool exportAll(const QUrl &url, qpm::ui::MessagesArea::ExportFormat format);
    /// Reads an archive file into the archive on this computer.
    Q_INVOKABLE bool importArchive(const QUrl &url);

    /**
     * The translated text for a failure of `action`: what went wrong and what
     * to do. Empty for a cancelled operation.
     */
    [[nodiscard]] static QString errorText(const core::Error &error, Action action);

    /**
     * A file name from the name `name` the phone sent for a part: without a
     * folder, without characters a file system or a shell may take amiss,
     * without leading dots, at most 200 bytes. Falls back to "Attachment
     * <position + 1>" with the extension of `mimeType`.
     */
    [[nodiscard]] static QString safeFileName(const QString &name, const QString &mimeType,
                                              int position);

Q_SIGNALS:
    void currentThreadChanged();
    void searchTextChanged();
    void composingChanged();
    void hasPhoneChanged();
    void canEditChanged();
    void busyChanged();
    void currentChanged();
    void errorOccurred(const QString &message);
    /// A finished action or new messages the user should hear about.
    void infoMessage(const QString &message);

private:
    void reload();
    void reloadCurrent();
    void applySearch();
    void updateSimCards();
    void announce(const QList<quint32> &luids);
    void report(const core::Error &error, Action action);
    void beginWork();
    void endWork();
    bool exportMessages(const QUrl &url, ExportFormat format, const QString &title,
                        const QList<core::store::StoredMessage> &messages);
    [[nodiscard]] QString contactNameFor(const QString &number) const;
    [[nodiscard]] QList<quint32> currentLuids() const;

    core::service::MessagesService *m_service = nullptr;
    core::service::DeviceStatusService *m_status = nullptr;
    std::shared_ptr<core::store::MessageStore> m_store;
    std::shared_ptr<core::store::ContactStore> m_contacts;
    ConversationListModel *m_conversations = nullptr;
    MessageListModel *m_messages = nullptr;
    ScheduledListModel *m_scheduled = nullptr;
    MessageComposer *m_composer = nullptr;
    QHash<QString, QString> m_contactNames;
    QString m_currentThread;
    QString m_searchText;
    bool m_composing = false;
    int m_busy = 0;
    bool m_connected = false;
};

} // namespace qpm::ui
