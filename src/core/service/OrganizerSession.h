#pragma once

#include "core/Error.h"
#include "core/store/OrganizerStore.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QPromise>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QObject;

namespace qpm::core::protocol {
class ObexClient;
}

namespace qpm::core::service::organizer {

// The OBEX steps that the calendar and the notes services share. Each step
// reports through `done` exactly once, in the thread of `context`, and not
// at all once `context` is gone.

template <typename T> using Done = std::function<void(const Result<T> &)>;

/// "telecom/<area>/luid/<LUID12>.<extension>"
[[nodiscard]] QString objectPath(sync::IrmcDomain domain, quint32 luid);

/**
 * Makes the phone load the area, which it needs before it takes a change:
 * without it the phone answers a create with "precondition failed".
 */
void loadArea(protocol::ObexClient &obex, sync::IrmcDomain domain, QObject *context,
              Done<void> done);

/// Reads one object; ErrorCode::NotFound when the phone does not know it.
void readObject(protocol::ObexClient &obex, sync::IrmcDomain domain, quint32 luid,
                QObject *context, Done<sync::RawObject> done);

/// Creates an object from `body` and reads back what the phone made of it.
void createObject(protocol::ObexClient &obex, sync::IrmcDomain domain, const QByteArray &body,
                  QObject *context, Done<sync::RawObject> done);

/// Replaces the object `luid` with `body` and reads back what the phone made of it.
void replaceObject(protocol::ObexClient &obex, sync::IrmcDomain domain, quint32 luid,
                   const QByteArray &body, QObject *context, Done<sync::RawObject> done);

/**
 * Deletes the objects one after another and takes each out of `store`; an
 * object already gone counts as deleted. `removedOne` runs after each.
 */
void removeObjects(protocol::ObexClient &obex, sync::IrmcDomain domain, QList<quint32> luids,
                   std::weak_ptr<store::OrganizerStore> store, QObject *context,
                   std::function<void()> removedOne, Done<void> done);

/// Reads metadata.xml of the area and keeps it in `store`.
void readMetadata(protocol::ObexClient &obex, sync::IrmcDomain domain,
                  std::weak_ptr<store::OrganizerStore> store, QObject *context, Done<void> done);

/// Writes `document` to metadata.xml, reads it back and keeps it in `store`.
void writeMetadata(protocol::ObexClient &obex, sync::IrmcDomain domain, const QByteArray &document,
                   std::weak_ptr<store::OrganizerStore> store, QObject *context, Done<void> done);

/**
 * Pulls the area into `store` within the session of `obex`. The store is
 * held weakly, so that a pull that ends after the phone was switched
 * changes nothing.
 */
void pull(protocol::ObexClient &obex, sync::IrmcDomain domain,
          std::weak_ptr<store::OrganizerStore> store, QObject *context,
          Done<store::OrganizerChanges> done);

/// A future and the callback that settles it once.
template <typename T> std::pair<QFuture<Result<T>>, Done<T>> promised()
{
    auto promise = std::make_shared<QPromise<Result<T>>>();
    promise->start();
    QFuture<Result<T>> future = promise->future();
    return {future, [promise](const Result<T> &result) {
                if (promise->future().isFinished())
                    return;
                promise->addResult(result);
                promise->finish();
            }};
}

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

[[nodiscard]] Error noStore();
[[nodiscard]] Error storeGone();
[[nodiscard]] Error notConnected();
[[nodiscard]] Error clientGone();

/// A colour for a category that does not exist yet, the same for the same name.
[[nodiscard]] quint32 newCategoryColor(const QString &name);

/**
 * An RGB colour for each of `names`: that of the known category of the same
 * name, else newCategoryColor().
 */
[[nodiscard]] QList<std::optional<quint32>>
categoryColors(const QStringList &names, const QList<codec::OrganizerCategory> &known);

} // namespace qpm::core::service::organizer
