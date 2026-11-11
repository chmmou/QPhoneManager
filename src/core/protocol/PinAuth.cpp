#include "core/protocol/PinAuth.h"

#include <QCryptographicHash>

namespace qpm::core::protocol {

namespace {
constexpr int maxTries = 10;
}

QString pinHash(const QString &pin)
{
    // The client compares against this fixed construction; the salt "hash"
    // is part of the protocol, not a choice of this program.
    const QByteArray digest =
        QCryptographicHash::hash(QByteArrayLiteral("hash") + pin.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(digest.toHex().toUpper());
}

std::optional<PinStatus> parsePinLine(const QString &line)
{
    static constexpr QLatin1StringView prefix("*PIN:");
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(prefix))
        return std::nullopt;
    const QString rest = trimmed.mid(prefix.size()).trimmed();
    const qsizetype comma = rest.lastIndexOf(u',');
    if (comma < 0)
        return std::nullopt;

    const QString word = rest.left(comma).trimmed();
    bool ok = false;
    const int tries = rest.mid(comma + 1).trimmed().toInt(&ok);
    if (!ok || tries < 0 || tries > maxTries)
        return std::nullopt;

    PinStatus status;
    status.remainingTries = tries;
    if (word == QLatin1StringView("ACCESS OK"))
        status.state = PinStatus::State::AccessOk;
    else if (word == QLatin1StringView("ACCESS PROTECTED"))
        status.state = PinStatus::State::AccessProtected;
    else if (word == QLatin1StringView("WRONG PIN"))
        status.state = PinStatus::State::WrongPin;
    else
        return std::nullopt;
    return status;
}

} // namespace qpm::core::protocol
