#pragma once

#include "core/Error.h"
#include "core/codec/CallLogCodec.h"

#include <QList>

namespace qpm::core::store {

class ProfileStore;

/// What a replace changed in the stored call log.
struct CallChanges
{
    int added = 0;
    int changed = 0;
    int removed = 0;
};

/**
 * The call log of one phone on this computer: the table `calls` of the
 * profile database. Times are kept in UTC and handed out in local time.
 *
 * The profile store must be open and must outlive this object. Use from the
 * main thread only.
 */
class CallStore
{
public:
    explicit CallStore(ProfileStore *profile);

    /**
     * Replaces every stored entry with `entries` in one transaction and
     * returns what changed against the entries stored before. An entry counts
     * as changed when its id was stored with other values. Nothing changes
     * when the write fails.
     */
    [[nodiscard]] Result<CallChanges> replaceAll(const QList<codec::CallEntry> &entries);

    /// Every stored entry, newest first.
    [[nodiscard]] Result<QList<codec::CallEntry>> calls() const;

    /// Removes the entries `ids`; ids that are not stored are passed over.
    [[nodiscard]] Result<void> remove(const QList<quint32> &ids);

private:
    ProfileStore *m_profile = nullptr;
};

} // namespace qpm::core::store
