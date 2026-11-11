#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QList>

#include <optional>

namespace qpm::core::sync {

/// The four data areas the phone keeps a change log for.
enum class IrmcDomain { Contacts, Calendar, Notes, Messages };

/// One object as the phone sends it: a vCard, vCalendar, vNote or vMessage.
struct RawObject
{
    quint32 luid = 0;
    /// The text from the BEGIN line to the END line, CRLF line ends included.
    QByteArray data;
    /// SHA-1 of `data`.
    QByteArray hash;
};

/// What changed on the phone since the last pull of one area.
struct SyncDelta
{
    /// True when `upserts` holds every object of the area, so that every
    /// object not in it is gone from the phone.
    bool full = false;
    /// New and changed objects. Without `full`, objects whose hash equals the
    /// stored hash are left out.
    QList<RawObject> upserts;
    QList<quint32> deletes;
    /// The change counter to store together with these changes.
    quint64 newCounter = 0;
    /// metadata.xml of the area; empty for messages.
    QByteArray metadata;
};

/**
 * What a sync needs from the local store of one device profile. The store
 * keeps one change counter and one hash per object for each area.
 */
class SyncStoreAdapter
{
public:
    virtual ~SyncStoreAdapter() = default;

    /// The counter stored with the last applied delta, or nothing before the first.
    [[nodiscard]] virtual std::optional<quint64> changeCounter(IrmcDomain domain) const = 0;

    /// The stored hash of an object, or nothing when the object is unknown.
    [[nodiscard]] virtual std::optional<QByteArray> objectHash(IrmcDomain domain,
                                                               quint32 luid) const = 0;

    /**
     * Stores the objects, deletions and the new counter of `delta` together,
     * so that a failure leaves the previous state intact.
     */
    [[nodiscard]] virtual Result<void> apply(IrmcDomain domain, const SyncDelta &delta) = 0;
};

} // namespace qpm::core::sync
