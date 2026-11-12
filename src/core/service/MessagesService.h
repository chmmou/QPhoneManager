#pragma once

#include "core/Error.h"
#include "core/codec/VMessage.h"
#include "core/store/MessageStore.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QStringList>

#include <memory>

class QTimer;

namespace qpm::core::protocol {
class ObexClient;
class PhoneConnection;
} // namespace qpm::core::protocol

namespace qpm::core::service {

/// The outcome of sending one text to several recipients.
struct SendReport
{
    int sent = 0;
    int failed = 0;
    /// Why the first failed recipient failed; no error when all went out.
    Error firstError;
};

/**
 * The messages of the connected phone: pulling them into the message store,
 * reloading when the phone reports a new one, sending text messages,
 * scheduled messages, and the requests the phone app may refuse: deleting
 * and the read state. Archiving and everything on messages only on this
 * computer stays here.
 *
 * Every call that needs the phone fails with ErrorCode::ConnectionClosed when
 * it is not connected, and every call that needs the store with
 * ErrorCode::InvalidInput when none is set. A refusal of the phone app is
 * ErrorCode::PermissionDenied; a request the phone app offers no way for,
 * such as deleting a multimedia message, is ErrorCode::NotSupported without
 * asking the phone. Lives in the main thread; no call blocks, and the
 * messages are read on a worker thread. Does not own the connection, which
 * must outlive it.
 */
class MessagesService : public QObject
{
    Q_OBJECT

public:
    /// Longest AT command for sending; the phone app reads at most 4096 bytes at once.
    static constexpr qsizetype maxSendCommandBytes = 4000;
    /// Most recipients of one text.
    static constexpr qsizetype maxRecipients = 20;

    explicit MessagesService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /**
     * The store of the current phone; null when none is chosen. The service
     * keeps only a weak reference: a call that finishes after the store is
     * gone fails with ErrorCode::Cancelled and changes nothing.
     */
    void setStore(const std::shared_ptr<store::MessageStore> &store);

    /**
     * Pulls what changed on the phone; everything on the first pull. A call
     * while a pull runs gets the result of that pull. New unread messages are
     * also reported through messagesArrived().
     */
    QFuture<Result<store::MessageChanges>> sync();

    /**
     * Sends `text` to each number in turn, on SIM card `simSlot` or the
     * phone's default for -1, and pulls the messages afterwards so that the
     * sent ones show. Numbers are reduced like ContactsService::dialableNumber().
     * Fails without sending anything with ErrorCode::InvalidInput for an empty
     * text, no number or a number that is none, with ErrorCode::LimitExceeded
     * above maxRecipients or when the command would exceed
     * maxSendCommandBytes, and with ErrorCode::NotSupported when the phone app
     * cannot send. A recipient the phone refuses is counted in the report and
     * the others are still sent to.
     */
    QFuture<Result<SendReport>> send(const QStringList &numbers, const QString &text, int simSlot);

    /**
     * Creates the scheduled message `message` on the phone when its LUID is 0,
     * otherwise replaces it, and returns its LUID. Uses the recipients, the
     * text, the time and the SIM card. Fails with ErrorCode::InvalidInput for
     * a time less than a minute ahead, so that nothing goes out at once, and
     * for what codec::encodeScheduledMessage() refuses.
     */
    QFuture<Result<quint32>> saveScheduled(const codec::Message &message);

    /**
     * Deletes the messages: on the phone and here, and those only on this
     * computer only here. The phone app 4.15 refuses to delete text messages
     * (ErrorCode::PermissionDenied); after the first refusal no more are
     * asked for. Multimedia messages fail with ErrorCode::NotSupported. Other
     * messages of the list are still handled; the first failure is returned.
     */
    QFuture<Result<void>> remove(const QList<quint32> &luids);

    /**
     * Marks received text messages read or unread on the phone and here; the
     * same refusals as remove() apply. Messages only on this computer change
     * here only.
     */
    QFuture<Result<void>> setRead(const QList<quint32> &luids, bool read);

    /// Archives messages or takes them out of the archive, on this computer only.
    [[nodiscard]] Result<void> setArchived(const QList<quint32> &luids, bool archived);

    /**
     * Adds `messages` to the archive on this computer only, the way
     * store::MessageStore::importPrepared() does, and returns how many were
     * added; the phone is not asked. Their parts are written on a worker
     * thread. Works without a connection. Fails with ErrorCode::Cancelled when
     * another phone was chosen meanwhile; nothing is added then.
     */
    QFuture<Result<int>> importToArchive(const QList<codec::Message> &messages);

    /**
     * The AT command that sends `text` to `number` in text mode, with the SIM
     * card when `withSim` and `simSlot` is not -1. Both values are escaped the
     * way the phone app reads them.
     */
    [[nodiscard]] static Result<QString> sendCommand(const QString &number, const QString &text,
                                                     int simSlot, bool withSim);

Q_SIGNALS:
    /// The stored messages changed.
    void messagesChanged();
    /// A pull found new unread received messages.
    void messagesArrived(const QList<quint32> &luids);

private:
    void onUnsolicited(const QString &line);
    void reloadAfterNotice();
    void finishSync(const Result<store::MessageChanges> &result);
    QFuture<Result<void>> removeScheduled(const QList<quint32> &luids);

    protocol::PhoneConnection *m_connection = nullptr;
    std::weak_ptr<store::MessageStore> m_store;
    /// Callers of sync() waiting for the pull that runs.
    QList<std::shared_ptr<QPromise<Result<store::MessageChanges>>>> m_syncWaiters;
    QTimer *m_reloadTimer = nullptr;
    bool m_syncRunning = false;
    /// The phone reported a new message while a pull ran.
    bool m_reloadPending = false;
};

} // namespace qpm::core::service
