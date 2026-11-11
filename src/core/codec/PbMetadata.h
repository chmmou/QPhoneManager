#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace qpm::core::codec {

/// A contact account on the phone, such as the phone itself or a Google account.
struct PbAccount
{
    /// "null" for the phone's own account.
    QString type;
    QString name;
    /// Human-readable kind of account, for example "Google".
    QString title;
    /// Its contacts are part of a sync.
    bool selected = false;
    /// New contacts go here when they name no account.
    bool isDefault = false;

    bool operator==(const PbAccount &other) const = default;
};

/// A contact group of one account.
struct PbGroup
{
    QString title;
    QString sourceId;
    QString accountType;
    QString accountName;

    bool operator==(const PbGroup &other) const = default;
};

/// A ringtone the phone offers for contacts.
struct PbRingtone
{
    QString id;
    QString title;
    bool isDefault = false;

    bool operator==(const PbRingtone &other) const = default;
};

/// The contents of "telecom/pb/metadata.xml".
struct PbMetadata
{
    /// The phone has no saved account filter and syncs every account.
    bool filterDisabled = false;
    QList<PbAccount> accounts;
    QList<PbGroup> groups;
    QList<PbRingtone> ringtones;

    bool operator==(const PbMetadata &other) const = default;
};

/**
 * Reads the metadata of the phonebook. Ringtone entries that report an error
 * instead of a title are skipped. Fails like parseSafeXml(), and with
 * ErrorCode::ProtocolError when the root element is not "metadata".
 */
[[nodiscard]] Result<PbMetadata> parsePbMetadata(const QByteArray &xml);

/**
 * Writes the document the phone takes to change which accounts are synced and
 * which one gets new contacts. Only the accounts are sent; the phone ignores
 * groups and ringtones in it.
 */
[[nodiscard]] QByteArray encodePbMetadata(const PbMetadata &metadata);

} // namespace qpm::core::codec
