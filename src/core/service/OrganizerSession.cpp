#include "core/service/OrganizerSession.h"

#include "core/protocol/ObexClient.h"
#include "core/sync/IrmcSync.h"

#include <QCryptographicHash>
#include <QObject>
#include <QPointer>

#include <array>
#include <chrono>

namespace qpm::core::service::organizer {

namespace {

using protocol::ObexClient;
using protocol::ObexResponse;
using sync::IrmcDomain;
using sync::RawObject;

constexpr std::chrono::seconds objectTimeout{20};
constexpr std::chrono::seconds loadTimeout{90};

// Calm colours that read well in both schemes, for new categories.
constexpr std::array<quint32, 8> categoryPalette = {
    0x1E88E5, 0x43A047, 0xF4511E, 0x8E24AA, 0x00897B, 0xFDD835, 0x6D4C41, 0xD81B60,
};

QString extension(IrmcDomain domain)
{
    return domain == IrmcDomain::Notes ? QStringLiteral("vnt") : QStringLiteral("vcs");
}

Result<RawObject> objectFrom(const Result<ObexResponse> &response, IrmcDomain domain, quint32 luid)
{
    if (!response.hasValue())
        return response.error();
    const Result<QList<RawObject>> objects =
        sync::IrmcSync::splitObjects(response.value().body, domain);
    if (!objects.hasValue())
        return objects.error();
    if (objects.value().isEmpty()) {
        return Error(ErrorCode::ProtocolError, QStringLiteral("Object %1 came without content")
                                                   .arg(sync::IrmcSync::luidText(luid)));
    }
    RawObject object = objects.value().constFirst();
    object.luid = luid;
    return object;
}

// Forwards to the store while it exists; IrmcSync may outlive a switch of
// the phone, and then must not touch the store of another phone.
class GuardedStore : public sync::SyncStoreAdapter
{
public:
    explicit GuardedStore(std::weak_ptr<store::OrganizerStore> store) : m_store(std::move(store)) {}

    [[nodiscard]] std::optional<quint64> changeCounter(IrmcDomain domain) const override
    {
        const std::shared_ptr<store::OrganizerStore> store = m_store.lock();
        return store ? store->changeCounter(domain) : std::nullopt;
    }

    [[nodiscard]] std::optional<QByteArray> objectHash(IrmcDomain domain,
                                                       quint32 luid) const override
    {
        const std::shared_ptr<store::OrganizerStore> store = m_store.lock();
        return store ? store->objectHash(domain, luid) : std::nullopt;
    }

    [[nodiscard]] Result<void> apply(IrmcDomain domain, const sync::SyncDelta &delta) override
    {
        const std::shared_ptr<store::OrganizerStore> store = m_store.lock();
        if (!store)
            return storeGone();
        return store->apply(domain, delta);
    }

    [[nodiscard]] store::OrganizerChanges lastChanges() const
    {
        const std::shared_ptr<store::OrganizerStore> store = m_store.lock();
        return store ? store->lastChanges() : store::OrganizerChanges();
    }

private:
    std::weak_ptr<store::OrganizerStore> m_store;
};

} // namespace

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

QString objectPath(IrmcDomain domain, quint32 luid)
{
    return QStringLiteral("telecom/%1/luid/%2.%3")
        .arg(sync::IrmcSync::areaName(domain), sync::IrmcSync::luidText(luid), extension(domain));
}

void loadArea(ObexClient &obex, IrmcDomain domain, QObject *context, Done<void> done)
{
    obex.get(QStringLiteral("telecom/%1/initsync").arg(sync::IrmcSync::areaName(domain)), {},
             loadTimeout)
        .then(context, [done](const Result<ObexResponse> &loaded) {
            done(loaded.hasValue() ? Result<void>() : Result<void>(loaded.error()));
        });
}

void readObject(ObexClient &obex, IrmcDomain domain, quint32 luid, QObject *context,
                Done<RawObject> done)
{
    obex.get(objectPath(domain, luid), {}, objectTimeout)
        .then(context, [done, domain, luid](const Result<ObexResponse> &read) {
            done(objectFrom(read, domain, luid));
        });
}

void createObject(ObexClient &obex, IrmcDomain domain, const QByteArray &body, QObject *context,
                  Done<RawObject> done)
{
    QPointer<ObexClient> client = &obex;
    obex.put(QStringLiteral("telecom/%1/luid/.%2")
                 .arg(sync::IrmcSync::areaName(domain), extension(domain)),
             body)
        .then(context, [client, domain, context, done](const Result<ObexResponse> &created) {
            if (!created.hasValue()) {
                done(created.error());
                return;
            }
            const std::optional<quint32> luid =
                ObexClient::createdLuid(created.value().appParams);
            if (!luid || *luid == 0) {
                done(Error(ErrorCode::ProtocolError,
                           QStringLiteral("The phone did not say which entry it created")));
                return;
            }
            if (!client) {
                done(clientGone());
                return;
            }
            readObject(*client, domain, *luid, context, done);
        });
}

void replaceObject(ObexClient &obex, IrmcDomain domain, quint32 luid, const QByteArray &body,
                   QObject *context, Done<RawObject> done)
{
    QPointer<ObexClient> client = &obex;
    obex.put(objectPath(domain, luid), body)
        .then(context, [client, domain, luid, context, done](const Result<ObexResponse> &put) {
            if (!put.hasValue()) {
                done(put.error());
                return;
            }
            if (!client) {
                done(clientGone());
                return;
            }
            readObject(*client, domain, luid, context, done);
        });
}

void removeObjects(ObexClient &obex, IrmcDomain domain, QList<quint32> luids,
                   std::weak_ptr<store::OrganizerStore> store, QObject *context,
                   std::function<void()> removedOne, Done<void> done)
{
    if (luids.isEmpty()) {
        done({});
        return;
    }
    QPointer<ObexClient> client = &obex;
    const quint32 luid = luids.takeFirst();
    obex.remove(objectPath(domain, luid))
        .then(context, [client, domain, luid, luids, store, context, removedOne,
                        done](const Result<ObexResponse> &removed) {
            if (!removed.hasValue() && removed.error().code() != ErrorCode::NotFound) {
                done(removed.error());
                return;
            }
            const std::shared_ptr<store::OrganizerStore> current = store.lock();
            if (!current) {
                done(storeGone());
                return;
            }
            if (const Result<void> gone = current->removeObject(domain, luid); !gone.hasValue()) {
                done(gone);
                return;
            }
            if (removedOne)
                removedOne();
            if (!client) {
                done(luids.isEmpty() ? Result<void>() : Result<void>(clientGone()));
                return;
            }
            removeObjects(*client, domain, luids, store, context, removedOne, done);
        });
}

void readMetadata(ObexClient &obex, IrmcDomain domain, std::weak_ptr<store::OrganizerStore> store,
                  QObject *context, Done<void> done)
{
    obex.get(QStringLiteral("telecom/%1/metadata.xml").arg(sync::IrmcSync::areaName(domain)), {},
             objectTimeout)
        .then(context, [domain, store, done](const Result<ObexResponse> &read) {
            if (!read.hasValue()) {
                done(read.error());
                return;
            }
            const std::shared_ptr<store::OrganizerStore> current = store.lock();
            if (!current) {
                done(storeGone());
                return;
            }
            done(current->storeMetadata(domain, read.value().body));
        });
}

void writeMetadata(ObexClient &obex, IrmcDomain domain, const QByteArray &document,
                   std::weak_ptr<store::OrganizerStore> store, QObject *context, Done<void> done)
{
    QPointer<ObexClient> client = &obex;
    obex.put(QStringLiteral("telecom/%1/metadata.xml").arg(sync::IrmcSync::areaName(domain)),
             document)
        .then(context, [client, domain, store, context, done](const Result<ObexResponse> &put) {
            if (!put.hasValue()) {
                done(put.error());
                return;
            }
            if (!client) {
                done(clientGone());
                return;
            }
            readMetadata(*client, domain, store, context, done);
        });
}

void pull(ObexClient &obex, IrmcDomain domain, std::weak_ptr<store::OrganizerStore> store,
          QObject *context, Done<store::OrganizerChanges> done)
{
    auto adapter = std::make_shared<GuardedStore>(std::move(store));
    auto *sync = new sync::IrmcSync(domain, adapter.get(), context);
    sync->pull(obex).then(context, [sync, adapter, done](const Result<sync::SyncDelta> &delta) {
        sync->deleteLater();
        if (!delta.hasValue()) {
            done(delta.error());
            return;
        }
        done(adapter->lastChanges());
    });
}

quint32 newCategoryColor(const QString &name)
{
    const QByteArray digest = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1);
    return categoryPalette.at(static_cast<unsigned char>(digest.at(0)) % categoryPalette.size());
}

QList<std::optional<quint32>> categoryColors(const QStringList &names,
                                             const QList<codec::OrganizerCategory> &known)
{
    QList<std::optional<quint32>> colors;
    for (const QString &name : names) {
        std::optional<quint32> color;
        for (const codec::OrganizerCategory &category : known) {
            if (category.name.compare(name, Qt::CaseInsensitive) == 0)
                color = category.color;
        }
        colors.append(color ? color : newCategoryColor(name));
    }
    return colors;
}

} // namespace qpm::core::service::organizer
