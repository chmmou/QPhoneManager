#include "core/codec/ContentLines.h"

namespace qpm::core::codec {

namespace {

bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

QByteArray decodeQuotedPrintable(const QByteArray &value)
{
    QByteArray out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const char c = value.at(i);
        if (c == '=' && i + 2 < value.size() && isHexDigit(value.at(i + 1)) &&
            isHexDigit(value.at(i + 2))) {
            out.append(char(value.mid(i + 1, 2).toInt(nullptr, 16)));
            i += 2;
            continue;
        }
        out.append(c);
    }
    return out;
}

// The physical lines of `data` without their line ends.
QList<QByteArray> physicalLines(const QByteArray &data)
{
    QList<QByteArray> lines = data.split('\n');
    for (QByteArray &line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
    }
    return lines;
}

bool isQuotedPrintable(const QByteArray &header)
{
    return header.toUpper().contains("ENCODING=QUOTED-PRINTABLE");
}

} // namespace

QString ContentLine::parameter(const QString &key) const
{
    const QString prefix = key + u'=';
    for (const QString &parameter : parameters) {
        if (parameter.startsWith(prefix, Qt::CaseInsensitive)) {
            QString value = parameter.mid(prefix.size());
            if (value.size() >= 2 && value.startsWith(u'"') && value.endsWith(u'"'))
                value = value.mid(1, value.size() - 2);
            return value;
        }
    }
    return {};
}

QList<ContentLine> readContentLines(const QByteArray &data)
{
    const QList<QByteArray> raw = physicalLines(data);
    QList<QByteArray> logical;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const QByteArray &line = raw.at(i);
        if (!logical.isEmpty() && (line.startsWith(' ') || line.startsWith('\t'))) {
            logical.last().append(line.mid(1));
            continue;
        }
        QByteArray current = line;
        // A quoted-printable value ending in "=" goes on in the next line.
        const qsizetype colon = current.indexOf(':');
        if (colon > 0 && isQuotedPrintable(current.left(colon))) {
            while (current.endsWith('=') && i + 1 < raw.size()) {
                current.chop(1);
                current.append(raw.at(++i));
            }
        }
        logical.append(current);
    }

    QList<ContentLine> lines;
    for (const QByteArray &line : std::as_const(logical)) {
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray header = line.left(colon);
        QByteArray value = line.mid(colon + 1);
        if (isQuotedPrintable(header))
            value = decodeQuotedPrintable(value);
        ContentLine content;
        QStringList segments = QString::fromUtf8(header).split(u';');
        content.name = segments.takeFirst().trimmed().toUpper();
        content.parameters = segments;
        content.value = QString::fromUtf8(value);
        lines.append(content);
    }
    return lines;
}

QByteArray encodePhoneLine(const QString &header, const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    QByteArray out;
    out.reserve(bytes.size());
    bool quoted = false;
    bool utf8 = false;
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == '=') {
            out.append('=');
            out.append(QByteArray::number(byte, 16).rightJustified(2, '0').toUpper());
            quoted = true;
        } else {
            out.append(c);
            utf8 = utf8 || byte > 0x7F;
        }
    }
    QByteArray line = header.toUtf8();
    if (quoted)
        line += ";ENCODING=QUOTED-PRINTABLE";
    if (utf8)
        line += ";CHARSET=UTF-8";
    return line + ':' + out + "\r\n";
}

QStringList splitEscapedList(const QString &value)
{
    QStringList parts;
    QString current;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == u'\\' && i + 1 < value.size() && value.at(i + 1) == u';') {
            current.append(u';');
            ++i;
        } else if (c == u';') {
            parts.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    parts.append(current);
    return parts;
}

QString joinEscapedList(const QStringList &parts)
{
    QStringList escaped;
    for (QString part : parts)
        escaped.append(part.replace(u';', QLatin1StringView("\\;")));
    return escaped.join(u';');
}

} // namespace qpm::core::codec
