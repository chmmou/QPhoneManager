#include "core/service/CallLogService.h"

#include "core/Logging.h"
#include "core/codec/CallLogCodec.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"

#include <QPromise>
#include <QStringList>
#include <QtConcurrentRun>

#include <array>
#include <chrono>
#include <utility>

namespace qpm::core::service {

namespace {

using codec::CallDirection;
using codec::CallEntry;
using protocol::AtReply;
using store::CallChanges;
using store::CallStore;

constexpr std::chrono::seconds listTimeout{90};
constexpr std::array readOrder = {CallDirection::Outgoing, CallDirection::Incoming,
                                  CallDirection::Missed};

/// The "+CMS ERROR" numbers with which the phone says that an entry is unknown.
const QStringList &unknownEntryCodes()
{
    static const QStringList codes = {QStringLiteral("321"), QStringLiteral("500")};
    return codes;
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
                     QStringLiteral("The phone may not access the call log"));
    case AtReply::Status::NotSupported:
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("The phone does not support %1").arg(what));
    case AtReply::Status::CmsError:
        if (unknownEntryCodes().contains(reply.detail))
            return Error(ErrorCode::NotFound, QStringLiteral("The phone does not know the entry"));
        break;
    case AtReply::Status::Dump:
        // The client passes on the exception of Android; a missing permission
        // arrives as a SecurityException, for example for WRITE_CALL_LOG.
        if (reply.detail.contains(QLatin1StringView("SecurityException")))
            return Error(ErrorCode::PermissionDenied,
                         QStringLiteral("The phone may not access the call log"));
        break;
    case AtReply::Status::Error:
        break;
    }
    return Error(ErrorCode::Rejected, QStringLiteral("The phone refused %1").arg(what));
}

struct ParsedLog
{
    QList<CallEntry> entries;
    int malformed = 0;
};

Result<ParsedLog> parseLists(const QList<std::pair<CallDirection, QStringList>> &lists)
{
    ParsedLog parsed;
    for (const auto &[direction, lines] : lists) {
        const Result<codec::CallList> list = codec::parseCallList(lines, direction);
        if (!list.hasValue())
            return list.error();
        parsed.entries.append(list.value().entries);
        parsed.malformed += list.value().malformed;
    }
    return parsed;
}

} // namespace

CallLogService::CallLogService(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
}

void CallLogService::setStore(const std::shared_ptr<CallStore> &store)
{
    m_store = store;
}

QFuture<Result<CallChanges>> CallLogService::sync()
{
    if (m_store.expired())
        return failed<CallChanges>(noStore());
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<CallChanges>(notConnected());

    // All six commands are queued at once, so no other command can select
    // another list between choosing a list and reading it.
    QList<QFuture<AtReply>> replies;
    for (const CallDirection direction : readOrder) {
        replies.append(session->send(codec::selectCallListCommand(direction)));
        replies.append(session->send(codec::readCallListCommand(), listTimeout));
    }

    auto promise = std::make_shared<QPromise<Result<CallChanges>>>();
    promise->start();
    const auto finish = [promise](const Result<CallChanges> &result) {
        promise->addResult(result);
        promise->finish();
    };
    // The answer belongs to the store of the phone that was asked, even when
    // another phone is chosen meanwhile.
    const std::weak_ptr<CallStore> target = m_store;
    QtFuture::whenAll(replies.begin(), replies.end())
        .then(this, [this, finish, target](const QList<QFuture<AtReply>> &done) {
            QList<std::pair<CallDirection, QStringList>> lists;
            for (qsizetype i = 0; i < qsizetype(readOrder.size()); ++i) {
                const QFuture<AtReply> &select = done.at(2 * i);
                const QFuture<AtReply> &read = done.at(2 * i + 1);
                const Error selectError =
                    replyError(select, QStringLiteral("the choice of a call list"));
                const Error error =
                    selectError.isError()
                        ? selectError
                        : replyError(read, QStringLiteral("the request for a call list"));
                if (error.isError()) {
                    finish(error);
                    return;
                }
                lists.append({readOrder.at(i), read.result().lines});
            }
            QtConcurrent::run([lists]() {
                return parseLists(lists);
            }).then(this, [this, finish, target](const Result<ParsedLog> &parsed) {
                if (!parsed.hasValue()) {
                    finish(parsed.error());
                    return;
                }
                if (parsed.value().malformed > 0) {
                    qCWarning(lcSync)
                        << "Call log lines left out, not readable:" << parsed.value().malformed;
                }
                const std::shared_ptr<CallStore> store = target.lock();
                if (!store) {
                    finish(storeGone());
                    return;
                }
                const Result<CallChanges> replaced = store->replaceAll(parsed.value().entries);
                if (replaced.hasValue())
                    Q_EMIT callsChanged();
                finish(replaced);
            });
        });
    return promise->future();
}

QFuture<Result<void>> CallLogService::remove(const QList<quint32> &ids)
{
    if (m_store.expired())
        return failed<void>(noStore());
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    if (ids.isEmpty())
        return QtFuture::makeReadyValueFuture(Result<void>());

    QStringList commands;
    for (const quint32 id : ids) {
        const Result<QString> command = codec::encodeCallDelete(id);
        if (!command.hasValue())
            return failed<void>(command.error());
        commands.append(command.value());
    }
    QList<QFuture<AtReply>> replies;
    for (const QString &command : std::as_const(commands))
        replies.append(session->send(command));

    auto promise = std::make_shared<QPromise<Result<void>>>();
    promise->start();
    const std::weak_ptr<CallStore> target = m_store;
    QtFuture::whenAll(replies.begin(), replies.end())
        .then(this, [this, promise, ids, target](const QList<QFuture<AtReply>> &done) {
            QList<quint32> gone;
            Error first;
            for (qsizetype i = 0; i < done.size(); ++i) {
                const Error error = replyError(done.at(i), QStringLiteral("a deletion"));
                if (!error.isError() || error.code() == ErrorCode::NotFound)
                    gone.append(ids.at(i));
                else if (!first.isError())
                    first = error;
            }
            const std::shared_ptr<CallStore> store = target.lock();
            if (!store) {
                promise->addResult(Result<void>(storeGone()));
                promise->finish();
                return;
            }
            if (!gone.isEmpty()) {
                const Result<void> removed = store->remove(gone);
                if (!removed.hasValue() && !first.isError())
                    first = removed.error();
                if (removed.hasValue())
                    Q_EMIT callsChanged();
            }
            promise->addResult(Result<void>(first));
            promise->finish();
        });
    return promise->future();
}

} // namespace qpm::core::service
