#pragma once

#include "core/Error.h"
#include "core/codec/PhoneVCard.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

namespace qpm::ui {

/**
 * The type choices the program offers for each kind of contact entry, with
 * their translated names. The custom type always comes last; an entry of a
 * type the list does not hold is shown with the name of "other".
 */
class ContactTypes
{
    Q_DECLARE_TR_FUNCTIONS(ContactTypes)

public:
    /// The type codes offered for `kind`, in the order of names().
    [[nodiscard]] static QList<int> codes(core::codec::ContactValueKind kind);
    /// The translated names of codes().
    [[nodiscard]] static QStringList names(core::codec::ContactValueKind kind);
    /// Position of `type` in codes(); the position of "other" for an unknown type.
    [[nodiscard]] static int indexOf(core::codec::ContactValueKind kind, int type);
    /// The custom type of `kind`.
    [[nodiscard]] static int customCode(core::codec::ContactValueKind kind);
    /// The name to show for an entry: its own label for the custom type, else the type's name.
    [[nodiscard]] static QString label(core::codec::ContactValueKind kind, int type,
                                       const QString &customLabel);
    /// Heading of the entries of `kind` in the contact view, for example "Phone".
    [[nodiscard]] static QString title(core::codec::ContactValueKind kind);

    /**
     * The text for a failed contact operation: what went wrong and what to
     * do. Empty for a cancelled operation, which needs no message.
     */
    [[nodiscard]] static QString errorText(const core::Error &error);

    /// The contact in a few lines, one per entry, for comparing two versions.
    [[nodiscard]] static QString summary(const core::codec::Contact &contact);
};

} // namespace qpm::ui
