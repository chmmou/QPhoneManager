#pragma once

#include <QString>
#include <QStringList>

namespace qpm::helpgen {

/// One chapter of the handbook as an HTML page.
struct Chapter
{
    QString topic;
    /// The text of the one top-level heading.
    QString title;
    /// A complete HTML page; the top-level heading carries the id `topic`.
    QString html;
    /// The images the chapter shows, as written in the Markdown ("images/<name>").
    QStringList images;
    /// One line per problem; the chapter must not be used unless this is empty.
    QStringList errors;
};

/**
 * Converts the Markdown of the chapter `topic` into an HTML page in
 * `language`. Raw HTML in the Markdown is shown as text, not interpreted.
 *
 * The chapter must have exactly one top-level heading. A link must point to
 * an https address or to another chapter as "<topic>.md", with `topic` one of
 * `topics`; it becomes "<topic>.html". An image must be
 * "images/<name>.png", ".jpg" or ".jpeg". Every breach is reported in
 * Chapter::errors; whether an image file exists is left to the caller.
 */
[[nodiscard]] Chapter convertChapter(const QString &markdown, const QString &topic,
                                     const QString &language, const QStringList &topics);

} // namespace qpm::helpgen
