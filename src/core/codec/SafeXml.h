#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QXmlStreamAttributes>

namespace qpm::core::codec {

/// One element of a parsed XML document, with its attributes and children.
struct XmlElement
{
    QString name;
    QXmlStreamAttributes attributes;
    /// Character data directly inside the element, whitespace kept.
    QString text;
    QList<XmlElement> children;

    /// Returns the value of `attribute`, or an empty string when it is absent.
    [[nodiscard]] QString attribute(const QString &attribute) const
    {
        return attributes.value(attribute).toString();
    }
};

/**
 * Parses an XML document that came from the phone into a tree.
 *
 * A document type declaration is accepted only without an internal subset, so
 * no entity can be declared, and entity references other than the five
 * predefined ones are rejected; nothing is ever loaded from outside. The
 * document may have at most limits::maxXmlBytes bytes, limits::maxXmlDepth
 * levels and limits::maxXmlElements elements. Every violation and every
 * syntax error fails with ErrorCode::InvalidInput or ErrorCode::LimitExceeded.
 */
[[nodiscard]] Result<XmlElement> parseSafeXml(const QByteArray &document);

} // namespace qpm::core::codec
