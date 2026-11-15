#include "core/service/DeviceStatusService.h"

#include "core/Logging.h"
#include "core/codec/AtText.h"

#include <QLatin1StringView>

#include <utility>

namespace qpm::core::service {

namespace {

using protocol::AtReply;
using protocol::PhoneConnection;

constexpr qsizetype maxTextLength = 256;
constexpr qsizetype maxSims = 8;
constexpr qsizetype maxRoots = 8;
constexpr int maxSlot = 16;
constexpr int maxPixels = 100000;
constexpr int maxDpi = 10000;

const QStringList &detailCommands()
{
    static const QStringList commands = {QStringLiteral("AT*SOPS"), QStringLiteral("AT*SIM"),
                                         QStringLiteral("AT*STATFS"), QStringLiteral("AT*DISPLAY"),
                                         QStringLiteral("AT+CGMR")};
    return commands;
}

// The text after "<prefix>" in a reply line, or nothing for another line.
std::optional<QString> afterPrefix(const QString &line, QLatin1StringView prefix)
{
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(prefix))
        return std::nullopt;
    return trimmed.mid(prefix.size()).trimmed();
}

// A field from the phone, decoded and cut to a sane length.
QString textField(const QStringList &fields, qsizetype index)
{
    return codec::decodeQp(fields.value(index).trimmed()).left(maxTextLength);
}

std::optional<int> intField(const QStringList &fields, qsizetype index, int minimum, int maximum)
{
    bool ok = false;
    const int value = fields.value(index).trimmed().toInt(&ok);
    if (!ok || value < minimum || value > maximum)
        return std::nullopt;
    return value;
}

} // namespace

DeviceStatusService::DeviceStatusService(PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
    connect(connection, &PhoneConnection::deviceIdentified, this,
            [this](const protocol::DeviceIdentity &identity) {
                m_overview.identity = identity;
                Q_EMIT changed();
                refresh();
            });
    connect(connection, &PhoneConnection::statusUpdated, this,
            [this](const protocol::DeviceStatus &status) {
                if (!hasData())
                    return;
                m_overview.status = status;
                Q_EMIT changed();
            });
    connect(connection, &PhoneConnection::stateChanged, this, &DeviceStatusService::onStateChanged);
}

void DeviceStatusService::onStateChanged(PhoneConnection::State state)
{
    const PhoneConnection::State previous = std::exchange(m_lastState, state);
    if (state == PhoneConnection::State::Ready && previous == PhoneConnection::State::Busy) {
        query(QStringLiteral("AT*STATFS"));
        return;
    }
    if (state == PhoneConnection::State::Ready || state == PhoneConnection::State::Busy)
        return;
    if (state == PhoneConnection::State::Connecting || state == PhoneConnection::State::Lost ||
        state == PhoneConnection::State::Disconnected ||
        state == PhoneConnection::State::Discovering) {
        ++m_generation;
        if (hasData()) {
            m_overview = {};
            Q_EMIT changed();
        }
    }
}

void DeviceStatusService::refresh()
{
    if (m_connection->state() != PhoneConnection::State::Ready)
        return;
    m_connection->refreshStatus();
    for (const QString &command : detailCommands())
        query(command);
}

void DeviceStatusService::query(const QString &command)
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return;
    const quint64 generation = m_generation;
    session->send(command).then(this, [this, generation](const AtReply &reply) {
        if (generation != m_generation)
            return;
        handleReply(reply);
    });
}

void DeviceStatusService::handleReply(const AtReply &reply)
{
    const QString command = reply.command;
    if (command == QLatin1StringView("AT*SIM")) {
        m_overview.simPermissionMissing = reply.status == AtReply::Status::PermissionDenied;
        m_overview.simsKnown = reply.status == AtReply::Status::Ok;
        m_overview.sims = m_overview.simsKnown ? parseSims(reply.lines) : QList<SimCard>();
        Q_EMIT changed();
        return;
    }
    if (reply.status != AtReply::Status::Ok) {
        // Optional details: an older phone or a missing permission leaves
        // the value unknown, which the overview shows as such. A closed or
        // silent link is reported by the connection itself.
        if (reply.status != AtReply::Status::Closed && reply.status != AtReply::Status::Timeout)
            qCInfo(lcProtocol) << "Phone detail not available:"
                               << protocol::commandWithoutArguments(command) << int(reply.status);
        return;
    }
    if (command == QLatin1StringView("AT*SOPS"))
        m_overview.networkOperator = parseOperator(reply.lines);
    else if (command == QLatin1StringView("AT*STATFS"))
        m_overview.storage = parseStorage(reply.lines);
    else if (command == QLatin1StringView("AT*DISPLAY"))
        m_overview.display = parseDisplay(reply.lines);
    else if (command == QLatin1StringView("AT+CGMR"))
        m_overview.firmware = reply.lines.value(0).trimmed().left(maxTextLength);
    else
        return;
    Q_EMIT changed();
}

std::optional<NetworkOperator> DeviceStatusService::parseOperator(const QStringList &lines)
{
    for (const QString &line : lines) {
        const std::optional<QString> rest = afterPrefix(line, QLatin1StringView("*SOPS:"));
        if (!rest)
            continue;
        const QStringList fields = codec::splitFields(*rest);
        NetworkOperator result{textField(fields, 0), textField(fields, 1),
                               textField(fields, 2).toLower()};
        if (result.name.isEmpty())
            return std::nullopt;
        return result;
    }
    return std::nullopt;
}

QList<SimCard> DeviceStatusService::parseSims(const QStringList &lines)
{
    QList<SimCard> sims;
    for (const QString &line : lines) {
        const std::optional<QString> rest = afterPrefix(line, QLatin1StringView("*SIM:"));
        if (!rest)
            continue;
        const QStringList fields = codec::splitFields(*rest);
        const std::optional<int> slot = intField(fields, 0, 0, maxSlot);
        if (!slot || fields.size() < 4)
            continue;
        const QString defaults = fields.value(4).trimmed();
        sims.append(SimCard{*slot, textField(fields, 1), textField(fields, 2),
                            textField(fields, 3), defaults.contains(u'V'),
                            defaults.contains(u'S')});
        if (sims.size() == maxSims)
            break;
    }
    return sims;
}

QList<StorageSpace> DeviceStatusService::parseStorage(const QStringList &lines)
{
    QList<StorageSpace> roots;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        const qsizetype colon = trimmed.indexOf(u':');
        if (!trimmed.startsWith(u'/') || colon < 2)
            continue;
        const QString root = trimmed.sliced(1, colon - 1);
        if (root != QLatin1StringView("sdcard") && root != QLatin1StringView("external") &&
            root != QLatin1StringView("data")) {
            continue;
        }
        const QStringList sizes = trimmed.sliced(colon + 1).split(u',');
        bool totalOk = false;
        bool freeOk = false;
        const quint64 total = sizes.value(0).trimmed().toULongLong(&totalOk);
        const quint64 free = sizes.value(1).trimmed().toULongLong(&freeOk);
        if (sizes.size() != 2 || !totalOk || !freeOk || total == 0 || free > total)
            continue;
        roots.append(StorageSpace{root, total, free});
        if (roots.size() == maxRoots)
            break;
    }
    return roots;
}

std::optional<DisplayInfo> DeviceStatusService::parseDisplay(const QStringList &lines)
{
    for (const QString &line : lines) {
        const std::optional<QString> rest = afterPrefix(line, QLatin1StringView("*DISPLAY:"));
        if (!rest)
            continue;
        const QStringList fields = rest->split(u',');
        const std::optional<int> width = intField(fields, 0, 1, maxPixels);
        const std::optional<int> height = intField(fields, 1, 1, maxPixels);
        const std::optional<int> dpi = intField(fields, 4, 1, maxDpi);
        if (!width || !height)
            return std::nullopt;
        return DisplayInfo{*width, *height, dpi.value_or(0)};
    }
    return std::nullopt;
}

} // namespace qpm::core::service
