#pragma once

#include "core/Error.h"
#include "core/store/CallStore.h"

#include <QFuture>
#include <QList>
#include <QObject>

#include <memory>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {

/**
 * The call log of the connected phone: reading its three lists into the call
 * store and deleting entries on the phone. A deletion goes to the phone first;
 * the store follows only for the entries the phone confirmed.
 *
 * Every call that needs the phone fails with ErrorCode::ConnectionClosed when
 * it is not connected, and every call that needs the store with
 * ErrorCode::InvalidInput when none is set. Lives in the main thread; no call
 * blocks, and the reply is read on a worker thread. Does not own the
 * connection, which must outlive it.
 */
class CallLogService : public QObject
{
    Q_OBJECT

public:
    explicit CallLogService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /**
     * The store of the current phone; null when none is chosen. The service
     * keeps only a weak reference: a call that finishes after the store is
     * gone fails with ErrorCode::Cancelled and changes nothing.
     */
    void setStore(const std::shared_ptr<store::CallStore> &store);

    /**
     * Reads the outgoing, incoming and missed calls from the phone and
     * replaces the stored call log with them. Reply lines that cannot be read
     * are left out and logged without their content. A phone that refuses a
     * list fails the whole read with ErrorCode::PermissionDenied or
     * ErrorCode::Rejected, and the store stays as it was.
     */
    QFuture<Result<store::CallChanges>> sync();

    /**
     * Deletes the entries `ids` on the phone and then here. An entry the phone
     * no longer has counts as deleted. When the phone refuses one, the others
     * are still deleted and the first refusal is returned, among them
     * ErrorCode::PermissionDenied when the app on the phone may not change
     * the call log.
     */
    QFuture<Result<void>> remove(const QList<quint32> &ids);

Q_SIGNALS:
    /// The stored call log changed.
    void callsChanged();

private:
    protocol::PhoneConnection *m_connection = nullptr;
    std::weak_ptr<store::CallStore> m_store;
};

} // namespace qpm::core::service
