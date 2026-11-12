#pragma once

#include "core/Error.h"
#include "core/codec/SafeXml.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace qpm::core::codec {

/// One calendar of the phone, as metadata.xml lists it.
struct CalendarInfo
{
    /// Lowest Android access level that lets the phone write events.
    static constexpr int writableAccessLevel = 500;

    /// "<account>@<type>/<sync id>"; several calendars of one account share it.
    QString url;
    QString title;
    /// ARGB.
    quint32 color = 0;
    bool hidden = false;
    /// IANA zone such as "Europe/Berlin"; may be empty.
    QString timezone;
    int accessLevel = 0;
    /// Part of what the phone syncs.
    bool selected = false;
    /// Gets new events that name no calendar.
    bool isDefault = false;

    [[nodiscard]] bool isWritable() const { return accessLevel >= writableAccessLevel; }

    bool operator==(const CalendarInfo &other) const = default;
};

/// A category of tasks or of notes.
struct OrganizerCategory
{
    /// Row number on the phone, 1 or more; 0 for a category not created yet.
    int id = 0;
    QString name;
    /// RGB.
    quint32 color = 0;

    bool operator==(const OrganizerCategory &other) const = default;
};

/// A change to a category, sent in metadata.xml.
struct CategoryChange
{
    enum class Kind { Add, Edit, Delete };

    Kind kind = Kind::Add;
    /// For Edit and Delete the id must be set.
    OrganizerCategory category;
};

/// The contents of "telecom/cal/metadata.xml".
struct CalMetadata
{
    /// The phone syncs every calendar, not a chosen set.
    bool filterDisabled = false;
    QList<CalendarInfo> calendars;
    bool taskSupport = false;
    QList<OrganizerCategory> taskCategories;

    bool operator==(const CalMetadata &other) const = default;
};

/**
 * Reads the calendar metadata. The phone writes a closing "</calendars>"
 * without an opening one when it has no calendar; that is read as an empty
 * list. Fails like parseSafeXml() and with ErrorCode::ProtocolError when the
 * root element is not "metadata".
 */
[[nodiscard]] Result<CalMetadata> parseCalMetadata(const QByteArray &xml);

/**
 * The document that makes the calendars with `selected` set the ones the
 * phone syncs, and the one with `isDefault` the one for new events. The
 * phone takes the new choice with its next sync session.
 */
[[nodiscard]] QByteArray encodeCalendarSelection(const QList<CalendarInfo> &calendars);

/**
 * The document that adds, renames, recolours or deletes categories, for
 * tasks under "task_categories" and for notes under "note_categories".
 * Calendars are not touched by it.
 */
[[nodiscard]] QByteArray encodeCategoryChanges(const QList<CategoryChange> &changes,
                                               bool forNotes);

/**
 * The categories below the element `section` of a metadata document, for
 * example "task_categories". Entries without a name or with an id below 1
 * are skipped.
 */
[[nodiscard]] QList<OrganizerCategory> categoriesIn(const XmlElement &root,
                                                   const QString &section);

/// XML text with &, <, > and the double quote escaped, for attribute values.
[[nodiscard]] QString xmlAttributeText(const QString &text);

} // namespace qpm::core::codec
