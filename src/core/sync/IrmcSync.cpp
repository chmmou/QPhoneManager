#include "core/sync/IrmcSync.h"

#include "core/Logging.h"
#include "core/codec/Limits.h"
#include "core/protocol/ObexClient.h"

#include <QCryptographicHash>
#include <QHash>
#include <QPromise>
#include <QtConcurrentRun>

#include <chrono>
#include <limits>

namespace qpm::core::sync {

using protocol::ObexClient;
using protocol::ObexResponse;

namespace {

constexpr std::chrono::seconds shortTimeout{20};
constexpr std::chrono::seconds longTimeout{90};
constexpr qsizetype backgroundSplitThreshold = 64 * 1024;
constexpr int luidDigits = 12;

QByteArray endMarker(IrmcDomain domain)
{
    switch (domain) {
    case IrmcDomain::Contacts:
        return QByteArrayLiteral("END:VCARD");
    case IrmcDomain::Calendar:
        return QByteArrayLiteral("END:VCALENDAR");
    case IrmcDomain::Notes:
        return QByteArrayLiteral("END:VNOTE");
    case IrmcDomain::Messages:
        break;
    }
    return QByteArrayLiteral("END:VMESSAGE");
}

QString extension(IrmcDomain domain)
{
    switch (domain) {
    case IrmcDomain::Contacts:
        return QStringLiteral("vcf");
    case IrmcDomain::Calendar:
        return QStringLiteral("vcs");
    case IrmcDomain::Notes:
        return QStringLiteral("vnt");
    case IrmcDomain::Messages:
        break;
    }
    return QStringLiteral("vmg");
}

std::optional<quint32> parseLuid(QByteArrayView text)
{
    const QByteArrayView digits = text.trimmed();
    if (digits.isEmpty() || digits.size() > luidDigits)
        return std::nullopt;
    bool ok = false;
    const qulonglong value = digits.toULongLong(&ok, 16);
    if (!ok || value > std::numeric_limits<quint32>::max())
        return std::nullopt;
    return quint32(value);
}

Result<quint64> parseCounter(const QByteArray &body)
{
    bool ok = false;
    const qulonglong value = body.trimmed().toULongLong(&ok);
    if (!ok)
        return Error(ErrorCode::ProtocolError, QStringLiteral("Change counter is not a number"));
    return quint64(value);
}

QByteArray sha1(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1);
}

Error protocolError(const QString &what)
{
    return Error(ErrorCode::ProtocolError, what);
}

} // namespace

struct IrmcSync::Job
{
    ObexClient *obex = nullptr;
    QPromise<Result<SyncDelta>> promise;
    std::optional<quint64> storedCounter;
    quint64 currentCounter = 0;
    SyncDelta delta;
    /// LUIDs still to fetch singly, in change-log order.
    QList<quint32> toFetch;

    bool stopIfCanceled()
    {
        if (!promise.isCanceled())
            return false;
        promise.finish();
        return true;
    }

    void fail(const Error &error)
    {
        qCWarning(lcSync) << "Sync failed:" << error.message();
        promise.addResult(Result<SyncDelta>(error));
        promise.finish();
    }
};

IrmcSync::IrmcSync(IrmcDomain domain, SyncStoreAdapter *store, QObject *parent)
    : QObject(parent), m_domain(domain), m_store(store)
{
    Q_ASSERT(store);
}

QFuture<Result<SyncDelta>> IrmcSync::pull(ObexClient &obex)
{
    auto job = std::make_shared<Job>();
    job->obex = &obex;
    job->promise.start();
    QFuture<Result<SyncDelta>> future = job->promise.future();
    job->storedCounter = m_store->changeCounter(m_domain);

    if (m_domain != IrmcDomain::Messages) {
        readInfo(job);
        return future;
    }
    // Scheduled messages are only listed after this switch; the original
    // desktop program sends it at the start of every message sync.
    obex.put(path(QStringLiteral("setflags")), QByteArrayLiteral("IncludeSheduleSMS"))
        .then(this, [this, job](const Result<ObexResponse> &response) {
            if (job->stopIfCanceled())
                return;
            if (!response.hasValue()) {
                job->fail(response.error());
                return;
            }
            readInfo(job);
        });
    return future;
}

QString IrmcSync::areaName(IrmcDomain domain)
{
    switch (domain) {
    case IrmcDomain::Contacts:
        return QStringLiteral("pb");
    case IrmcDomain::Calendar:
        return QStringLiteral("cal");
    case IrmcDomain::Notes:
        return QStringLiteral("notes");
    case IrmcDomain::Messages:
        break;
    }
    return QStringLiteral("msg");
}

QString IrmcSync::luidText(quint32 luid)
{
    return QStringLiteral("%1").arg(luid, luidDigits, 16, QLatin1Char('0')).toUpper();
}

Result<IrmcChangeLog> IrmcSync::parseChangeLog(const QByteArray &body)
{
    static constexpr QByteArrayView totalPrefix("Total-Records:");
    IrmcChangeLog log;
    bool totalSeen = false;
    const QList<QByteArray> lines = body.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        if (!totalSeen) {
            if (!line.startsWith(totalPrefix))
                return protocolError(QStringLiteral("Change log without Total-Records"));
            bool ok = false;
            log.totalRecords = line.mid(totalPrefix.size()).trimmed().toULongLong(&ok);
            if (!ok)
                return protocolError(QStringLiteral("Change log with an invalid record count"));
            totalSeen = true;
            continue;
        }
        if (line == "*") {
            log.fullReloadRequired = true;
            continue;
        }
        // <T>:<row>::<LUID12>
        const QList<QByteArray> parts = line.split(':');
        if (parts.size() != 4 || parts.at(0).size() != 1 || !parts.at(2).isEmpty())
            return protocolError(QStringLiteral("Malformed change log line"));
        IrmcChange change;
        switch (parts.at(0).at(0)) {
        case 'N':
            change.type = IrmcChange::Type::New;
            break;
        case 'E':
            change.type = IrmcChange::Type::Edited;
            break;
        case 'D':
            change.type = IrmcChange::Type::Deleted;
            break;
        default:
            return protocolError(QStringLiteral("Unknown change type in the change log"));
        }
        bool ok = false;
        change.row = parts.at(1).toULongLong(&ok);
        const std::optional<quint32> luid = parseLuid(parts.at(3));
        if (!ok || !luid)
            return protocolError(QStringLiteral("Malformed change log line"));
        change.luid = *luid;
        if (log.changes.size() >= limits::maxSyncObjects) {
            return Error(
                ErrorCode::LimitExceeded,
                QStringLiteral("Change log longer than %1 lines").arg(limits::maxSyncObjects));
        }
        log.changes.append(change);
    }
    if (!totalSeen)
        return protocolError(QStringLiteral("Empty change log"));
    return log;
}

Result<QList<RawObject>> IrmcSync::splitObjects(const QByteArray &dump, IrmcDomain domain)
{
    static constexpr QByteArrayView luidPrefix("X-IRMC-LUID:");
    const QByteArray end = endMarker(domain);
    QList<RawObject> objects;
    qsizetype objectStart = -1;
    std::optional<quint32> luid;
    qsizetype position = 0;
    while (position < dump.size()) {
        qsizetype lineEnd = dump.indexOf('\n', position);
        const qsizetype next = lineEnd < 0 ? dump.size() : lineEnd + 1;
        const QByteArrayView line =
            QByteArrayView(dump)
                .sliced(position, (lineEnd < 0 ? dump.size() : lineEnd) - position)
                .trimmed();
        if (objectStart < 0) {
            if (line.startsWith("BEGIN:")) {
                objectStart = position;
                luid.reset();
            }
        } else if (line.startsWith(luidPrefix)) {
            luid = parseLuid(line.sliced(luidPrefix.size()));
        } else if (line == end) {
            if (objects.size() >= limits::maxSyncObjects) {
                return Error(ErrorCode::LimitExceeded,
                             QStringLiteral("More than %1 objects").arg(limits::maxSyncObjects));
            }
            RawObject object;
            object.luid = luid.value_or(0);
            object.data = dump.mid(objectStart, next - objectStart);
            if (!object.data.endsWith('\n'))
                object.data.append("\r\n");
            object.hash = sha1(object.data);
            objects.append(object);
            objectStart = -1;
        }
        position = next;
    }
    return objects;
}

QString IrmcSync::path(const QString &relative) const
{
    return QStringLiteral("telecom/%1/%2").arg(areaName(m_domain), relative);
}

void IrmcSync::readInfo(const std::shared_ptr<Job> &job)
{
    job->obex->get(path(QStringLiteral("info.log")), {}, shortTimeout)
        .then(this, [this, job](const Result<ObexResponse> &info) {
            if (job->stopIfCanceled())
                return;
            if (!info.hasValue()) {
                job->fail(info.error());
                return;
            }
            // The phone only updates its change log during initsync.
            job->obex->get(path(QStringLiteral("initsync")), {}, longTimeout)
                .then(this, [this, job](const Result<ObexResponse> &initsync) {
                    if (job->stopIfCanceled())
                        return;
                    if (!initsync.hasValue()) {
                        job->fail(initsync.error());
                        return;
                    }
                    if (m_domain == IrmcDomain::Messages) {
                        readCounter(job);
                        return;
                    }
                    job->obex->get(path(QStringLiteral("metadata.xml")), {}, shortTimeout)
                        .then(this, [this, job](const Result<ObexResponse> &metadata) {
                            if (job->stopIfCanceled())
                                return;
                            if (!metadata.hasValue()) {
                                job->fail(metadata.error());
                                return;
                            }
                            job->delta.metadata = metadata.value().body;
                            readCounter(job);
                        });
                });
        });
}

void IrmcSync::readCounter(const std::shared_ptr<Job> &job)
{
    job->obex->get(path(QStringLiteral("luid/cc.log")), {}, shortTimeout)
        .then(this, [this, job](const Result<ObexResponse> &response) {
            if (job->stopIfCanceled())
                return;
            if (!response.hasValue()) {
                job->fail(response.error());
                return;
            }
            const Result<quint64> counter = parseCounter(response.value().body);
            if (!counter.hasValue()) {
                job->fail(counter.error());
                return;
            }
            job->currentCounter = counter.value();
            if (!job->storedCounter) {
                loadAll(job);
            } else if (*job->storedCounter == job->currentCounter) {
                job->delta.newCounter = job->currentCounter;
                applyAndFinish(job);
            } else {
                readChangeLog(job);
            }
        });
}

void IrmcSync::readChangeLog(const std::shared_ptr<Job> &job)
{
    const QString name = path(QStringLiteral("luid/%1.log").arg(*job->storedCounter));
    job->obex->get(name, {}, shortTimeout)
        .then(this, [this, job](const Result<ObexResponse> &response) {
            if (job->stopIfCanceled())
                return;
            if (!response.hasValue()) {
                job->fail(response.error());
                return;
            }
            const Result<IrmcChangeLog> log = parseChangeLog(response.value().body);
            if (!log.hasValue()) {
                job->fail(log.error());
                return;
            }
            if (log.value().fullReloadRequired) {
                loadAll(job);
                return;
            }
            // Only the last change of an object counts; a deletion followed by
            // a new entry for the same LUID is a change.
            QHash<quint32, IrmcChange::Type> last;
            QList<quint32> order;
            for (const IrmcChange &change : log.value().changes) {
                if (!last.contains(change.luid))
                    order.append(change.luid);
                last.insert(change.luid, change.type);
            }
            for (const quint32 luid : std::as_const(order)) {
                if (last.value(luid) == IrmcChange::Type::Deleted)
                    job->delta.deletes.append(luid);
                else
                    job->toFetch.append(luid);
            }
            job->delta.newCounter = job->currentCounter;
            fetchNext(job);
        });
}

void IrmcSync::fetchNext(const std::shared_ptr<Job> &job)
{
    if (job->toFetch.isEmpty()) {
        applyAndFinish(job);
        return;
    }
    const quint32 luid = job->toFetch.takeFirst();
    const QString name =
        path(QStringLiteral("luid/%1.%2").arg(luidText(luid), extension(m_domain)));
    job->obex->get(name, {}, shortTimeout)
        .then(this, [this, job, luid](const Result<ObexResponse> &response) {
            if (job->stopIfCanceled())
                return;
            if (!response.hasValue()) {
                // Gone again since the change log was written.
                if (response.error().code() == ErrorCode::NotFound) {
                    job->delta.deletes.append(luid);
                    fetchNext(job);
                    return;
                }
                job->fail(response.error());
                return;
            }
            const Result<QList<RawObject>> objects = splitObjects(response.value().body, m_domain);
            if (!objects.hasValue() || objects.value().isEmpty()) {
                job->fail(
                    protocolError(QStringLiteral("Object %1 has no content").arg(luidText(luid))));
                return;
            }
            RawObject object = objects.value().first();
            object.luid = luid;
            // An object the store already holds in this form was written by
            // this program or changed back; it carries nothing new.
            if (m_store->objectHash(m_domain, luid) != object.hash)
                job->delta.upserts.append(object);
            fetchNext(job);
        });
}

void IrmcSync::loadAll(const std::shared_ptr<Job> &job)
{
    job->delta.full = true;
    job->delta.deletes.clear();
    job->toFetch.clear();
    const QString name =
        QStringLiteral("telecom/%1.%2").arg(areaName(m_domain), extension(m_domain));
    job->obex->get(name, {}, longTimeout)
        .then(this, [this, job](const Result<ObexResponse> &response) {
            if (job->stopIfCanceled())
                return;
            if (!response.hasValue()) {
                job->fail(response.error());
                return;
            }
            const QByteArray dump = response.value().body;
            const IrmcDomain domain = m_domain;
            if (dump.size() <= backgroundSplitThreshold) {
                const Result<QList<RawObject>> objects = splitObjects(dump, domain);
                if (!objects.hasValue()) {
                    job->fail(objects.error());
                    return;
                }
                finishFullLoad(job, objects.value());
                return;
            }
            QtConcurrent::run([dump, domain]() {
                return splitObjects(dump, domain);
            }).then(this, [this, job](const Result<QList<RawObject>> &objects) {
                if (job->stopIfCanceled())
                    return;
                if (!objects.hasValue()) {
                    job->fail(objects.error());
                    return;
                }
                finishFullLoad(job, objects.value());
            });
        });
}

void IrmcSync::finishFullLoad(const std::shared_ptr<Job> &job, const QList<RawObject> &objects)
{
    for (const RawObject &object : objects) {
        if (object.luid == 0) {
            qCWarning(lcSync) << "Object without LUID skipped in" << areaName(m_domain);
            continue;
        }
        job->delta.upserts.append(object);
    }
    // Reading the dump is what fills the phone's change table, so the counter
    // read before it can be behind.
    job->obex->get(path(QStringLiteral("luid/cc.log")), {}, shortTimeout)
        .then(this, [this, job](const Result<ObexResponse> &response) {
            if (job->stopIfCanceled())
                return;
            if (!response.hasValue()) {
                job->fail(response.error());
                return;
            }
            const Result<quint64> counter = parseCounter(response.value().body);
            if (!counter.hasValue()) {
                job->fail(counter.error());
                return;
            }
            job->delta.newCounter = counter.value();
            applyAndFinish(job);
        });
}

void IrmcSync::applyAndFinish(const std::shared_ptr<Job> &job)
{
    const Result<void> stored = m_store->apply(m_domain, job->delta);
    if (!stored.hasValue()) {
        job->fail(stored.error());
        return;
    }
    qCInfo(lcSync) << "Sync of" << areaName(m_domain) << (job->delta.full ? "full" : "incremental")
                   << "upserts" << job->delta.upserts.size() << "deletes"
                   << job->delta.deletes.size();
    job->promise.addResult(Result<SyncDelta>(job->delta));
    job->promise.finish();
}

} // namespace qpm::core::sync
