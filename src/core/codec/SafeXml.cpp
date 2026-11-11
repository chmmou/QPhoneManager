#include "core/codec/SafeXml.h"

#include "core/codec/Limits.h"

#include <QXmlStreamReader>

#include <optional>

namespace qpm::core::codec {

namespace {

Error invalid(const QString &what)
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Unsafe or invalid XML: %1").arg(what));
}

} // namespace

Result<XmlElement> parseSafeXml(const QByteArray &document)
{
    if (document.size() > limits::maxXmlBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("XML document larger than %1 bytes").arg(limits::maxXmlBytes));
    }

    QXmlStreamReader reader(document);

    QList<XmlElement> stack;
    std::optional<XmlElement> root;
    qsizetype elementCount = 0;

    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::DTD:
            if (!reader.entityDeclarations().isEmpty() ||
                !reader.notationDeclarations().isEmpty() ||
                reader.text().contains(QLatin1StringView("<!ENTITY")) ||
                reader.text().contains(u'[')) {
                return invalid(QStringLiteral("document type declaration with an internal subset"));
            }
            break;
        case QXmlStreamReader::EntityReference:
            return invalid(QStringLiteral("undeclared entity reference"));
        case QXmlStreamReader::StartElement: {
            if (root)
                return invalid(QStringLiteral("more than one root element"));
            if (stack.size() >= limits::maxXmlDepth) {
                return Error(
                    ErrorCode::LimitExceeded,
                    QStringLiteral("XML nested deeper than %1 levels").arg(limits::maxXmlDepth));
            }
            if (++elementCount > limits::maxXmlElements) {
                return Error(
                    ErrorCode::LimitExceeded,
                    QStringLiteral("XML with more than %1 elements").arg(limits::maxXmlElements));
            }
            XmlElement element;
            element.name = reader.name().toString();
            element.attributes = reader.attributes();
            stack.append(element);
            break;
        }
        case QXmlStreamReader::Characters:
            if (!stack.isEmpty())
                stack.last().text += reader.text();
            break;
        case QXmlStreamReader::EndElement: {
            XmlElement finished = stack.takeLast();
            if (stack.isEmpty())
                root = std::move(finished);
            else
                stack.last().children.append(std::move(finished));
            break;
        }
        default:
            break;
        }
    }
    if (reader.hasError())
        return invalid(reader.errorString());
    if (!root)
        return invalid(QStringLiteral("no root element"));
    return *root;
}

} // namespace qpm::core::codec
