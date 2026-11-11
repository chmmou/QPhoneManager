#include "core/service/MessagesService.h"

#include "core/Logging.h"
#include "core/codec/AtText.h"
#include "core/codec/SmsLength.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/ContactsService.h"
#include "core/sync/IrmcSync.h"

#include <QDateTime>
#include <QPointer>
#include <QPromise>
#include <QTimer>
#include <QtConcurrentRun>

#include <chrono>
#include <functional>
#include <utility>

namespace qpm::core::service {

namespace {

using protocol::AtReply;
using protocol::ObexClient;
using protocol::ObexResponse;
using store::MessageChanges;
using store::MessageStore;
using store::StoredMessage;
using sync::IrmcDomain;
using sync::RawObject;

constexpr std::chrono::seconds objectTimeout{20};
constexpr std::chrono::seconds commandTimeout{20};
constexpr std::chrono::seconds sendBaseTimeout{30};
constexpr std::chrono::seconds sendPerPartTimeout{20};
constexpr std::chrono::milliseconds noticeDelay{400};
constexpr qint64 minimumScheduleLeadSeconds = 60;
constexpr int maxSimSlot = 2;

QString objectPath(quint32 luid)
{
    return QStringLiteral("telecom/msg/luid/%1.vmg").arg(sync::IrmcSync::luidText(luid));
}

QString createPath()
{
    return QStringLiteral("telecom/msg/luid/.vmg");
}

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

Error noStore()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("No phone is chosen"));
}

Error storeGone()
{
    return Error(ErrorCode::Cancelled, QStringLiteral("The phone was switched meanwhile"));
}

Error notConnected()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected"));
}

Error clientGone()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The OBEX session ended"));
}

Error notOffered(const QString &what)
{
    return Error(ErrorCode::NotSupported,
                 QStringLiteral("The phone app offers no way to %1").arg(what));
}

// `what` names the request without its arguments.
Error replyError(const QFuture<AtReply> &future, const QString &what)
{
    if (future.isCanceled() || future.resultCount() == 0)
        return notConnected();
    const AtReply reply = future.result();
    switch (reply.status) {
    case AtReply::Status::Ok:
        return {};
    case AtReply::Status::Timeout:
        return Error(ErrorCode::Timeout, QStringLiteral("The phone did not answer %1").arg(what));
    case AtReply::Status::Closed:
    case AtReply::Status::AccessDenied:
        return notConnected();
    case AtReply::Status::PermissionDenied:
        return Error(ErrorCode::PermissionDenied,
                     QStringLiteral("The phone app may not do %1").arg(what));
    case AtReply::Status::NotSupported:
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("The phone does not support %1").arg(what));
    case AtReply::Status::CmsError:
        // 321 and 500: no such message; 401: the app is not the SMS app of the phone.
        if (reply.detail == QLatin1StringView("321") || reply.detail == QLatin1StringView("500"))
            return Error(ErrorCode::NotFound,
                         QStringLiteral("The phone does not know the message"));
        if (reply.detail == QLatin1StringView("401")) {
            return Error(ErrorCode::PermissionDenied,
                         QStringLiteral("The phone app may not do %1").arg(what));
        }
        return Error(ErrorCode::Rejected,
                     QStringLiteral("The phone refused %1 with code %2").arg(what, reply.detail));
    case AtReply::Status::Dump:
        if (reply.detail.contains(QLatin1StringView("SecurityException"))) {
            return Error(ErrorCode::PermissionDenied,
                         QStringLiteral("The phone app may not do %1").arg(what));
        }
        break;
    case AtReply::Status::Error:
        break;
    }
    return Error(ErrorCode::Rejected, QStringLiteral("The phone refused %1").arg(what));
}

// Forwards to the store while it exists; IrmcSync may outlive a switch of
// the phone, and then must not touch the store of another phone.
class GuardedStore : public sync::SyncStoreAdapter
{
public:
    explicit GuardedStore(std::weak_ptr<MessageStore> store) : m_store(std::move(store)) {}

    [[nodiscard]] std::optional<quint64> changeCounter(IrmcDomain domain) const override
    {
        const std::shared_ptr<MessageStore> store = m_store.lock();
        return store ? store->changeCounter(domain) : std::nullopt;
    }

    [[nodiscard]] std::optional<QByteArray> objectHash(IrmcDomain domain,
                                                       quint32 luid) const override
    {
        const std::shared_ptr<MessageStore> store = m_store.lock();
        return store ? store->objectHash(domain, luid) : std::nullopt;
    }

    [[nodiscard]] Result<void> apply(IrmcDomain domain, const sync::SyncDelta &delta) override
    {
        const std::shared_ptr<MessageStore> store = m_store.lock();
        if (!store)
            return storeGone();
        return store->apply(domain, delta);
    }

private:
    std::weak_ptr<MessageStore> m_store;
};

Result<RawObject> objectFrom(const Result<ObexResponse> &response, quint32 luid)
{
    if (!response.hasValue())
        return response.error();
    const Result<QList<RawObject>> objects =
        sync::IrmcSync::splitObjects(response.value().body, IrmcDomain::Messages);
    if (!objects.hasValue())
        return objects.error();
    if (objects.value().isEmpty()) {
        return Error(
            ErrorCode::ProtocolError,
            QStringLiteral("Message %1 came without content").arg(sync::IrmcSync::luidText(luid)));
    }
    RawObject object = objects.value().constFirst();
    object.luid = luid;
    return object;
}

// Runs `commands` one after another and calls `done` for each answer; a
// command whose `done` returns false ends the run. `finished` gets nothing
// back; the callbacks keep what they need.
struct CommandRun
{
    QPointer<protocol::AtSession> session;
    QStringList commands;
    std::function<bool(qsizetype index, const QFuture<AtReply> &reply)> done;
    std::function<void()> finished;
    QObject *context = nullptr;

    static void next(const std::shared_ptr<CommandRun> &run, qsizetype index)
    {
        if (index >= run->commands.size() || !run->session) {
            run->finished();
            return;
        }
        run->session->send(run->commands.at(index), commandTimeout)
            .then(run->context, [run, index](const QFuture<AtReply> &reply) {
                if (!run->done(index, reply)) {
                    run->finished();
                    return;
                }
                next(run, index + 1);
            });
    }
};

} // namespace

MessagesService::MessagesService(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection), m_reloadTimer(new QTimer(this))
{
    Q_ASSERT(connection);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(noticeDelay);
    connect(m_reloadTimer, &QTimer::timeout, this, &MessagesService::reloadAfterNotice);
    connect(m_connection, &protocol::PhoneConnection::unsolicited, this,
            &MessagesService::onUnsolicited);
}

void MessagesService::setStore(const std::shared_ptr<MessageStore> &store)
{
    m_store = store;
}

QFuture<Result<MessageChanges>> MessagesService::sync()
{
    const std::shared_ptr<MessageStore> store = m_store.lock();
    if (!store)
        return failed<MessageChanges>(noStore());
    if (!m_connection->session())
        return failed<MessageChanges>(notConnected());

    auto promise = std::make_shared<QPromise<Result<MessageChanges>>>();
    promise->start();
    m_syncWaiters.append(promise);
    if (m_syncRunning)
        return promise->future();
    m_syncRunning = true;

    const std::weak_ptr<MessageStore> target = m_store;
    const QString directory = store->profileDirectory();
    m_connection
        ->withObex<sync::SyncDelta>(
            ObexClient::Target::IrmcSync,
            [this, target](ObexClient &obex) {
                auto adapter = std::make_shared<GuardedStore>(target);
                auto *pull = new sync::IrmcSync(IrmcDomain::Messages, adapter.get(), this);
                return pull->pull(obex).then(this,
                                             [pull, adapter](const Result<sync::SyncDelta> &delta) {
                                                 pull->deleteLater();
                                                 return delta;
                                             });
            })
        .then(this, [this, target, directory](const Result<sync::SyncDelta> &delta) {
            if (!delta.hasValue()) {
                finishSync(delta.error());
                return;
            }
            const sync::SyncDelta pulled = delta.value();
            QtConcurrent::run([pulled, directory]() {
                return MessageStore::prepare(pulled, directory);
            }).then(this, [this, target](const Result<store::PreparedMessages> &prepared) {
                if (!prepared.hasValue()) {
                    finishSync(prepared.error());
                    return;
                }
                const std::shared_ptr<MessageStore> current = target.lock();
                if (!current) {
                    finishSync(storeGone());
                    return;
                }
                const Result<MessageChanges> changes = current->applyPrepared(prepared.value());
                if (changes.hasValue()) {
                    if (changes.value().unreadable > 0) {
                        qCWarning(lcSync)
                            << "Messages left out, not readable:" << changes.value().unreadable;
                    }
                    Q_EMIT messagesChanged();
                    if (!changes.value().arrived.isEmpty())
                        Q_EMIT messagesArrived(changes.value().arrived);
                }
                finishSync(changes);
            });
        });
    return promise->future();
}

void MessagesService::finishSync(const Result<MessageChanges> &result)
{
    m_syncRunning = false;
    const auto waiters = std::exchange(m_syncWaiters, {});
    for (const auto &waiter : waiters) {
        waiter->addResult(result);
        waiter->finish();
    }
    if (std::exchange(m_reloadPending, false))
        m_reloadTimer->start();
}

QFuture<Result<SendReport>> MessagesService::send(const QStringList &numbers, const QString &text,
                                                  int simSlot)
{
    if (numbers.isEmpty())
        return failed<SendReport>(Error(ErrorCode::InvalidInput, QStringLiteral("No recipient")));
    if (numbers.size() > maxRecipients) {
        return failed<SendReport>(
            Error(ErrorCode::LimitExceeded,
                  QStringLiteral("More than %1 recipients").arg(maxRecipients)));
    }
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<SendReport>(notConnected());
    const QString capabilities = m_connection->identity().smsCapabilities.toUpper();
    if (!capabilities.contains(QLatin1StringView("TXT")))
        return failed<SendReport>(notOffered(QStringLiteral("send text messages")));
    const bool withSim = capabilities.contains(QLatin1StringView("TXTV2"));

    QStringList commands;
    for (const QString &number : numbers) {
        const Result<QString> command = sendCommand(number, text, simSlot, withSim);
        if (!command.hasValue())
            return failed<SendReport>(command.error());
        commands.append(command.value());
    }
    const std::chrono::seconds timeout =
        sendBaseTimeout + sendPerPartTimeout * codec::smsLength(text).parts;
    QList<QFuture<AtReply>> replies;
    for (const QString &command : std::as_const(commands))
        replies.append(session->send(command, timeout));

    auto promise = std::make_shared<QPromise<Result<SendReport>>>();
    promise->start();
    QtFuture::whenAll(replies.begin(), replies.end())
        .then(this, [this, promise](const QList<QFuture<AtReply>> &done) {
            SendReport report;
            for (const QFuture<AtReply> &reply : done) {
                const Error error = replyError(reply, QStringLiteral("sending a text message"));
                if (!error.isError()) {
                    ++report.sent;
                    continue;
                }
                ++report.failed;
                if (!report.firstError.isError())
                    report.firstError = error;
            }
            qCInfo(lcSync) << "Text messages sent:" << report.sent << "failed:" << report.failed;
            // The phone files a sent message on its own; the next pull brings it.
            if (report.sent > 0)
                m_reloadTimer->start();
            promise->addResult(Result<SendReport>(report));
            promise->finish();
        });
    return promise->future();
}

QFuture<Result<quint32>> MessagesService::saveScheduled(const codec::Message &message)
{
    if (m_store.expired())
        return failed<quint32>(noStore());
    if (!message.date.isValid() ||
        message.date < QDateTime::currentDateTimeUtc().addSecs(minimumScheduleLeadSeconds)) {
        return failed<quint32>(
            Error(ErrorCode::InvalidInput,
                  QStringLiteral("The time to send lies less than a minute ahead")));
    }
    const quint32 luid = message.luid;
    if (luid != 0 && codec::kindOfLuid(luid) != codec::MessageKind::Scheduled)
        return failed<quint32>(
            Error(ErrorCode::InvalidInput, QStringLiteral("Not a scheduled message")));
    const Result<QByteArray> body = codec::encodeScheduledMessage(message);
    if (!body.hasValue())
        return failed<quint32>(body.error());
    if (!m_connection->session())
        return failed<quint32>(notConnected());

    const QByteArray data = body.value();
    return m_connection->withObex<quint32>(
        ObexClient::Target::IrmcSync, [this, data, luid](ObexClient &obex) {
            auto promise = std::make_shared<QPromise<Result<quint32>>>();
            promise->start();
            QPointer<ObexClient> client = &obex;
            const auto finish = [promise](const Result<quint32> &result) {
                promise->addResult(result);
                promise->finish();
            };
            obex.put(luid == 0 ? createPath() : objectPath(luid), data)
                .then(this, [this, client, finish, luid](const Result<ObexResponse> &put) {
                    if (!put.hasValue()) {
                        finish(put.error());
                        return;
                    }
                    const std::optional<quint32> written =
                        luid != 0 ? std::optional<quint32>(luid)
                                  : ObexClient::createdLuid(put.value().appParams);
                    if (!written || *written == 0) {
                        finish(Error(ErrorCode::ProtocolError,
                                     QStringLiteral("The phone did not say which message it "
                                                    "created")));
                        return;
                    }
                    if (!client) {
                        finish(clientGone());
                        return;
                    }
                    client->get(objectPath(*written), {}, objectTimeout)
                        .then(this, [this, finish, written](const Result<ObexResponse> &read) {
                            const Result<RawObject> object = objectFrom(read, *written);
                            if (!object.hasValue()) {
                                finish(object.error());
                                return;
                            }
                            const std::shared_ptr<MessageStore> store = m_store.lock();
                            if (!store) {
                                finish(storeGone());
                                return;
                            }
                            const Result<void> stored = store->storeObject(object.value());
                            if (!stored.hasValue()) {
                                // Without an earlier pull there is no state to add
                                // the message to; the pull brings it.
                                if (stored.error().code() != ErrorCode::InvalidInput) {
                                    finish(stored.error());
                                    return;
                                }
                                m_reloadTimer->start();
                            } else {
                                Q_EMIT messagesChanged();
                            }
                            finish(*written);
                        });
                });
            return promise->future();
        });
}

QFuture<Result<void>> MessagesService::remove(const QList<quint32> &luids)
{
    const std::shared_ptr<MessageStore> store = m_store.lock();
    if (!store)
        return failed<void>(noStore());

    Error first;
    const auto note = [&first](const Error &error) {
        if (!first.isError())
            first = error;
    };
    QList<quint32> local;
    QList<quint32> texts;
    QList<quint32> scheduled;
    for (const quint32 luid : luids) {
        const Result<StoredMessage> found = store->message(luid);
        if (!found.hasValue()) {
            if (found.error().code() != ErrorCode::NotFound)
                note(found.error());
            continue;
        }
        const StoredMessage &message = found.value();
        if (!message.isOnPhone())
            local.append(luid);
        else if (message.kind == codec::MessageKind::Scheduled)
            scheduled.append(luid);
        else if (message.kind == codec::MessageKind::Mms || (luid & codec::storeLuidBit) != 0)
            note(notOffered(QStringLiteral("delete this message")));
        else
            texts.append(luid);
    }
    if (!local.isEmpty()) {
        const Result<void> removed = store->removeLocal(local);
        if (removed.hasValue())
            Q_EMIT messagesChanged();
        else
            note(removed.error());
    }
    if (texts.isEmpty() && scheduled.isEmpty())
        return QtFuture::makeReadyValueFuture(Result<void>(first));
    protocol::AtSession *session = m_connection->session();
    if (!session) {
        note(notConnected());
        return QtFuture::makeReadyValueFuture(Result<void>(first));
    }

    auto promise = std::make_shared<QPromise<Result<void>>>();
    promise->start();
    auto firstError = std::make_shared<Error>(first);
    const auto finishAll = [this, promise, firstError, scheduled]() {
        const auto deliver = [promise, firstError](const Result<void> &last) {
            if (!firstError->isError() && !last.hasValue())
                *firstError = last.error();
            promise->addResult(Result<void>(*firstError));
            promise->finish();
        };
        if (scheduled.isEmpty()) {
            deliver({});
            return;
        }
        removeScheduled(scheduled).then(this, deliver);
    };

    auto run = std::make_shared<CommandRun>();
    run->session = session;
    run->context = this;
    for (const quint32 luid : std::as_const(texts))
        run->commands.append(QStringLiteral("AT+CMGD=%1").arg(luid));
    run->done = [this, texts, firstError](qsizetype index, const QFuture<AtReply> &reply) {
        const Error error = replyError(reply, QStringLiteral("deleting a message"));
        if (!error.isError() || error.code() == ErrorCode::NotFound) {
            const std::shared_ptr<MessageStore> current = m_store.lock();
            if (!current) {
                if (!firstError->isError())
                    *firstError = storeGone();
                return false;
            }
            if (const Result<void> gone = current->removeObject(texts.at(index));
                !gone.hasValue()) {
                if (!firstError->isError())
                    *firstError = gone.error();
            } else {
                Q_EMIT messagesChanged();
            }
            return true;
        }
        if (!firstError->isError())
            *firstError = error;
        // The phone app refuses every further one for the same reason.
        return error.code() != ErrorCode::PermissionDenied &&
               error.code() != ErrorCode::NotSupported &&
               error.code() != ErrorCode::ConnectionClosed;
    };
    run->finished = finishAll;
    CommandRun::next(run, 0);
    return promise->future();
}

QFuture<Result<void>> MessagesService::removeScheduled(const QList<quint32> &luids)
{
    return m_connection->withObex<void>(
        ObexClient::Target::IrmcSync, [this, luids](ObexClient &obex) {
            struct Job
            {
                QPromise<Result<void>> promise;
                QPointer<ObexClient> client;
                QList<quint32> pending;
                std::function<void()> next;
            };
            auto job = std::make_shared<Job>();
            job->promise.start();
            job->client = &obex;
            job->pending = luids;
            const auto finish = [](const std::shared_ptr<Job> &done, const Result<void> &result) {
                done->promise.addResult(result);
                done->promise.finish();
            };
            // The callback refers to the job only weakly, so that the job is
            // freed once the last request has answered.
            job->next = [this, weak = std::weak_ptr<Job>(job), finish]() {
                const std::shared_ptr<Job> current = weak.lock();
                if (!current)
                    return;
                if (current->pending.isEmpty()) {
                    finish(current, {});
                    return;
                }
                if (!current->client) {
                    finish(current, clientGone());
                    return;
                }
                const quint32 luid = current->pending.takeFirst();
                current->client->remove(objectPath(luid))
                    .then(this, [this, current, finish, luid](const Result<ObexResponse> &removed) {
                        if (!removed.hasValue() && removed.error().code() != ErrorCode::NotFound) {
                            finish(current, removed.error());
                            return;
                        }
                        const std::shared_ptr<MessageStore> store = m_store.lock();
                        if (!store) {
                            finish(current, storeGone());
                            return;
                        }
                        if (const Result<void> gone = store->removeObject(luid); !gone.hasValue()) {
                            finish(current, gone);
                            return;
                        }
                        Q_EMIT messagesChanged();
                        if (current->next)
                            current->next();
                    });
            };
            job->next();
            return job->promise.future();
        });
}

QFuture<Result<void>> MessagesService::setRead(const QList<quint32> &luids, bool read)
{
    const std::shared_ptr<MessageStore> store = m_store.lock();
    if (!store)
        return failed<void>(noStore());

    Error first;
    QList<quint32> local;
    QList<quint32> texts;
    for (const quint32 luid : luids) {
        const Result<StoredMessage> found = store->message(luid);
        if (!found.hasValue())
            continue;
        const StoredMessage &message = found.value();
        if (!message.incoming)
            continue;
        if (!message.isOnPhone())
            local.append(luid);
        else if (message.kind == codec::MessageKind::Sms && (luid & codec::storeLuidBit) == 0)
            texts.append(luid);
        else if (!first.isError())
            first = notOffered(QStringLiteral("change the read state of this message"));
    }
    if (!local.isEmpty()) {
        const Result<void> changed = store->setRead(local, read);
        if (changed.hasValue())
            Q_EMIT messagesChanged();
        else if (!first.isError())
            first = changed.error();
    }
    if (texts.isEmpty())
        return QtFuture::makeReadyValueFuture(Result<void>(first));
    protocol::AtSession *session = m_connection->session();
    if (!session) {
        if (!first.isError())
            first = notConnected();
        return QtFuture::makeReadyValueFuture(Result<void>(first));
    }

    auto promise = std::make_shared<QPromise<Result<void>>>();
    promise->start();
    auto firstError = std::make_shared<Error>(first);
    auto run = std::make_shared<CommandRun>();
    run->session = session;
    run->context = this;
    for (const quint32 luid : std::as_const(texts))
        run->commands.append(QStringLiteral("AT+CMGE=%1,%2").arg(luid).arg(read ? 1 : 0));
    run->done = [this, texts, read, firstError](qsizetype index, const QFuture<AtReply> &reply) {
        const Error error = replyError(reply, QStringLiteral("changing the read state"));
        if (!error.isError()) {
            const std::shared_ptr<MessageStore> current = m_store.lock();
            const Result<void> changed =
                current ? current->setRead({texts.at(index)}, read) : Result<void>(storeGone());
            if (changed.hasValue())
                Q_EMIT messagesChanged();
            else if (!firstError->isError())
                *firstError = changed.error();
            return true;
        }
        if (!firstError->isError())
            *firstError = error;
        return error.code() != ErrorCode::PermissionDenied &&
               error.code() != ErrorCode::NotSupported &&
               error.code() != ErrorCode::ConnectionClosed;
    };
    run->finished = [promise, firstError]() {
        promise->addResult(Result<void>(*firstError));
        promise->finish();
    };
    CommandRun::next(run, 0);
    return promise->future();
}

Result<void> MessagesService::setArchived(const QList<quint32> &luids, bool archived)
{
    const std::shared_ptr<MessageStore> store = m_store.lock();
    if (!store)
        return noStore();
    const Result<void> changed = store->setArchived(luids, archived);
    if (changed.hasValue())
        Q_EMIT messagesChanged();
    return changed;
}

Result<QString> MessagesService::sendCommand(const QString &number, const QString &text,
                                             int simSlot, bool withSim)
{
    const QString dialable = ContactsService::dialableNumber(number);
    if (dialable.isEmpty()) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("A recipient has no number the phone can send to"));
    }
    if (text.trimmed().isEmpty())
        return Error(ErrorCode::InvalidInput, QStringLiteral("The text is empty"));
    if (simSlot < -1 || simSlot > maxSimSlot)
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("SIM index %1 out of range").arg(simSlot));
    // Number, text, a delivery report and saving in the outbox, as the
    // original desktop program sends it.
    QString command = QStringLiteral("AT+CMGS=") + codec::encodeQp(dialable) + u',' +
                      codec::encodeQp(text) + QStringLiteral(",1,1");
    if (withSim && simSlot >= 0)
        command += u',' + QString::number(simSlot);
    if (command.toUtf8().size() > maxSendCommandBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("The text is too long for one request to the phone"));
    }
    return command;
}

void MessagesService::onUnsolicited(const QString &line)
{
    if (line.startsWith(QLatin1StringView("+CMTI:")))
        m_reloadTimer->start();
}

void MessagesService::reloadAfterNotice()
{
    if (m_store.expired() || !m_connection->session())
        return;
    if (m_syncRunning) {
        m_reloadPending = true;
        return;
    }
    // The result arrives through messagesChanged() and messagesArrived().
    static_cast<void>(sync());
}

} // namespace qpm::core::service
