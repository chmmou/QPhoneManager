#pragma once

#include "core/Error.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace qpm::core::protocol {
class ObexClient;
}

namespace qpm::core::sync {

/// One line of a change log "telecom/<area>/luid/<n>.log".
struct IrmcChange
{
    enum class Type { New, Edited, Deleted };

    Type type = Type::New;
    quint64 row = 0;
    quint32 luid = 0;
};

/// A parsed change log.
struct IrmcChangeLog
{
    quint64 totalRecords = 0;
    /// The phone does not know the counter any more ("*"): load everything.
    bool fullReloadRequired = false;
    QList<IrmcChange> changes;
};

/**
 * Pulls the changes of one area from the phone over IRMC-SYNC: reads
 * info.log, initsync, metadata.xml and the change counter, then either the
 * change log with a single fetch per new or edited object, or everything at
 * once when no counter is stored or the phone asks for it. For messages,
 * scheduled messages are switched on first. The result is handed to the store
 * adapter, which keeps the new counter with the data, and returned.
 *
 * Lives in the main thread. A full dump above 64 KiB is split into objects on
 * a worker thread. The store adapter must outlive this object; the OBEX client
 * need not, see pull().
 */
class IrmcSync : public QObject
{
    Q_OBJECT

public:
    IrmcSync(IrmcDomain domain, SyncStoreAdapter *store, QObject *parent = nullptr);

    /**
     * Runs one pull through `obex`, which must be connected to IRMC-SYNC.
     * Fails with the OBEX error, with ErrorCode::ProtocolError for a
     * malformed answer, or with the store's error; in every failure case
     * nothing reaches the store.
     *
     * `obex` may be deleted before the pull is done. The future then fails
     * with ErrorCode::ConnectionClosed, or ends cancelled when a request of
     * the pull was still open on the deleted client.
     */
    QFuture<Result<SyncDelta>> pull(qpm::core::protocol::ObexClient &obex);

    /// "pb", "cal", "notes" or "msg".
    [[nodiscard]] static QString areaName(IrmcDomain domain);

    /// Twelve upper-case hex digits, as the phone writes a LUID.
    [[nodiscard]] static QString luidText(quint32 luid);

    /// Parses the body of "luid/<n>.log".
    [[nodiscard]] static Result<IrmcChangeLog> parseChangeLog(const QByteArray &body);

    /**
     * Splits a dump into objects at the END line of the area and reads the
     * LUID from each "X-IRMC-LUID:" line; an object without one gets LUID 0.
     * Fails with ErrorCode::LimitExceeded above limits::maxSyncObjects
     * objects.
     */
    [[nodiscard]] static Result<QList<RawObject>> splitObjects(const QByteArray &dump,
                                                               IrmcDomain domain);

private:
    struct Job;

    void readInfo(const std::shared_ptr<Job> &job);
    void readCounter(const std::shared_ptr<Job> &job);
    void readChangeLog(const std::shared_ptr<Job> &job);
    void fetchNext(const std::shared_ptr<Job> &job);
    void loadAll(const std::shared_ptr<Job> &job);
    void finishFullLoad(const std::shared_ptr<Job> &job, const QList<RawObject> &objects);
    void applyAndFinish(const std::shared_ptr<Job> &job);
    [[nodiscard]] QString path(const QString &relative) const;

    IrmcDomain m_domain = IrmcDomain::Contacts;
    SyncStoreAdapter *m_store = nullptr;
};

} // namespace qpm::core::sync
