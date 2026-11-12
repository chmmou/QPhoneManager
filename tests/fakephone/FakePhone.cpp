#include "FakePhone.h"

#include "core/codec/AtText.h"
#include "core/codec/CalMetadata.h"
#include "core/codec/ContentLines.h"
#include "core/codec/PhoneTime.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/PhoneVCard.h"
#include "core/codec/SafeXml.h"
#include "core/codec/VNote.h"
#include "core/protocol/ObexPacket.h"

#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <array>

namespace qpm::test {

using core::protocol::ObexPacket;
namespace obex = core::protocol::obex;

namespace {

constexpr int maxPinTries = 10;
constexpr qsizetype chunkOverhead = 6;
constexpr int luidDigits = 12;

constexpr std::array longTextPrefixes = {
    QLatin1StringView("AT*CLIPBOARD="),
    QLatin1StringView("AT*KBRD=2,"),
    QLatin1StringView("AT*T="),
    QLatin1StringView("AT*SELTEXT="),
};

QByteArray fileBrowserUuid()
{
    return QByteArray::fromHex("F9EC7BC4953C11D2984E525400DC9E09");
}

QByteArray codeOnly(quint8 code)
{
    return QByteArray(1, char(code)) + QByteArray::fromHex("0003");
}

QByteArray encoded(const ObexPacket &packet)
{
    const auto result = packet.encode();
    Q_ASSERT(result.hasValue());
    return result.value();
}

QString luidText(quint32 luid)
{
    return QStringLiteral("%1").arg(luid, luidDigits, 16, QLatin1Char('0')).toUpper();
}

QString extensionOf(const QString &area)
{
    if (area == QLatin1StringView("pb"))
        return QStringLiteral("vcf");
    if (area == QLatin1StringView("cal"))
        return QStringLiteral("vcs");
    if (area == QLatin1StringView("notes"))
        return QStringLiteral("vnt");
    return QStringLiteral("vmg");
}

bool isKnownArea(const QString &area)
{
    return area == QLatin1StringView("pb") || area == QLatin1StringView("cal") ||
           area == QLatin1StringView("notes") || area == QLatin1StringView("msg");
}

// True when `body` holds a line of the property `name`.
bool hasProperty(const QByteArray &body, const QByteArray &name)
{
    const QList<QByteArray> lines = body.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith(name + ':') || line.startsWith(name + ';'))
            return true;
    }
    return false;
}

void assignDataIds(core::codec::Contact &contact, quint64 &nextDataId)
{
    for (const core::codec::ContactValueKind kind : core::codec::allContactValueKinds) {
        for (core::codec::ContactValue &entry : contact.values(kind))
            entry.dataId = nextDataId++;
    }
    if (!contact.nickname.isEmpty())
        contact.nicknameId = nextDataId++;
}

void dropEmptyEntries(core::codec::Contact &contact)
{
    for (const core::codec::ContactValueKind kind : core::codec::allContactValueKinds) {
        contact.values(kind).removeIf(
            [](const core::codec::ContactValue &entry) { return entry.isEmpty(); });
    }
}

// Applies a change the way the client does: name lines always, other entries
// only where the body names them, by their key.
core::codec::Contact appliedChange(core::codec::Contact current, const core::codec::Contact &change,
                                   const QByteArray &body, quint64 &nextDataId)
{
    current.familyName = change.familyName;
    current.givenName = change.givenName;
    current.middleName = change.middleName;
    current.prefix = change.prefix;
    current.suffix = change.suffix;
    current.displayName = change.displayName;
    if (hasProperty(body, "NICKNAME")) {
        current.nickname = change.nickname;
        current.nicknameId = change.nickname.isEmpty() ? 0 : nextDataId++;
    }
    if (hasProperty(body, "ORG")) {
        current.organization = change.organization;
        current.department = change.department;
    }
    if (hasProperty(body, "TITLE"))
        current.title = change.title;
    if (hasProperty(body, "NOTE"))
        current.note = change.note;
    if (change.birthday.isValid())
        current.birthday = change.birthday;
    if (change.anniversary.isValid())
        current.anniversary = change.anniversary;
    if (hasProperty(body, "CATEGORIES"))
        current.groups = change.groups;
    if (hasProperty(body, "X-RINGTONE"))
        current.ringtone = change.ringtone;
    if (body.contains("PHOTO;ACTION=DELETE"))
        current.photo.clear();
    else if (hasProperty(body, "PHOTO"))
        current.photo = change.photo;

    for (const core::codec::ContactValueKind kind : core::codec::allContactValueKinds) {
        QList<core::codec::ContactValue> &own = current.values(kind);
        for (const core::codec::ContactValue &entry : change.values(kind)) {
            if (entry.dataId == 0) {
                if (entry.isEmpty())
                    continue;
                core::codec::ContactValue added = entry;
                added.dataId = nextDataId++;
                own.append(added);
                continue;
            }
            for (qsizetype i = 0; i < own.size(); ++i) {
                if (own.at(i).dataId != entry.dataId)
                    continue;
                if (entry.isEmpty()) {
                    own.removeAt(i);
                } else {
                    own[i] = entry;
                }
                break;
            }
        }
    }
    return current;
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'\\');
    return slash <= 0 ? QString() : path.left(slash);
}

QString leafOf(const QString &path)
{
    return path.mid(path.lastIndexOf(u'\\') + 1);
}

QString xmlEscaped(const QString &text)
{
    QString escaped = text;
    escaped.replace(u'&', QLatin1StringView("&amp;"));
    escaped.replace(u'"', QLatin1StringView("&quot;"));
    return escaped;
}

QString opName(quint8 code)
{
    switch (code) {
    case obex::opConnect:
        return QStringLiteral("CONNECT");
    case obex::opDisconnect:
        return QStringLiteral("DISCONNECT");
    case obex::opPut:
    case obex::opPutFinal:
        return QStringLiteral("PUT");
    case obex::opGetFinal:
        return QStringLiteral("GET");
    case obex::opMove:
        return QStringLiteral("MOVE");
    case obex::opSetPath:
        return QStringLiteral("SETPATH");
    case obex::opSocketPut:
        return QStringLiteral("SOCKETPUT");
    case obex::opSocketGet:
        return QStringLiteral("SOCKETGET");
    case obex::opSocketStatus:
        return QStringLiteral("SOCKETSTATUS");
    case obex::opAbort:
        return QStringLiteral("ABORT");
    default:
        break;
    }
    return QStringLiteral("0x%1").arg(uint(code), 2, 16, QLatin1Char('0'));
}

// A vMessage property line as the client writes it: bytes below 0x20 and "="
// as =XX, everything else as it is.
QByteArray messageLine(const QByteArray &name, const QString &value)
{
    QByteArray out;
    bool escaped = false;
    bool high = false;
    const QByteArray bytes = value.toUtf8();
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || c == '=') {
            out += '=' + QByteArray::number(byte, 16).rightJustified(2, '0').toUpper();
            escaped = true;
        } else {
            out += c;
        }
        high = high || byte >= 0x80;
    }
    QByteArray line = name;
    if (escaped)
        line += ";ENCODING=QUOTED-PRINTABLE";
    if (high)
        line += ";CHARSET=UTF-8";
    return line + ':' + out + "\r\n";
}

QList<QByteArray> messageLines(const QByteArray &body)
{
    QList<QByteArray> lines = body.split('\n');
    for (QByteArray &line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
    }
    return lines;
}

// The client reads "<name>;<number>;<sent>;<state digit>" and sends back
// "<name>;<number>;<SHEDULED|SENT|FAILED>;<sent>;<error>", with SIM first.
QByteArray scheduledAsPhoneSendsIt(const QByteArray &body, quint32 luid)
{
    QByteArray sim;
    QByteArray date;
    QByteArray text;
    QList<QByteArray> recipients;
    const QList<QByteArray> lines = messageLines(body);
    for (const QByteArray &line : lines) {
        if (line.startsWith("SIM:")) {
            sim = line;
        } else if (line.startsWith("DATE:")) {
            date = line;
        } else if (line.startsWith("BODY")) {
            text = line;
        } else if (line.startsWith("RECIPIENT")) {
            const qsizetype colon = line.indexOf(':');
            const QList<QByteArray> fields = line.mid(colon + 1).split(';');
            const QByteArray state = fields.value(3) == "1"   ? QByteArray("SENT")
                                     : fields.value(3) == "2" ? QByteArray("FAILED")
                                                              : QByteArray("SHEDULED");
            recipients.append(line.left(colon) + ':' + fields.value(0) + ';' + fields.value(1) +
                              ';' + state + ';' + fields.value(2) + ';');
        }
    }
    QByteArray out = "BEGIN:VMESSAGE\r\n";
    if (!sim.isEmpty())
        out += sim + "\r\n";
    out += "TYP:SMS;OUT\r\nBOX:SHEDULED\r\n";
    for (const QByteArray &recipient : std::as_const(recipients))
        out += recipient + "\r\n";
    if (!date.isEmpty())
        out += date + "\r\n";
    if (!text.isEmpty())
        out += text + "\r\n";
    return out + "X-IRMC-LUID:" + luidText(luid).toLatin1() + "\r\nEND:VMESSAGE\r\n";
}

bool hasMessageLine(const QByteArray &body, QByteArrayView expected)
{
    const QList<QByteArray> lines = messageLines(body);
    return std::any_of(lines.cbegin(), lines.cend(),
                       [expected](const QByteArray &line) { return line == expected; });
}

constexpr quint32 messageKindMask = 0x30000000;
constexpr quint32 scheduledBit = 0x20000000;
constexpr quint32 eventBit = 0x10000000;
constexpr quint32 rgbMask = 0x00FFFFFF;

QString hex6(quint32 color)
{
    return QStringLiteral("%1").arg(color & rgbMask, 6, 16, QLatin1Char('0')).toUpper();
}

QString categoryLines(const QList<core::codec::OrganizerCategory> &categories)
{
    QString out;
    for (const core::codec::OrganizerCategory &category : categories) {
        out += QStringLiteral("<category name=\"%1\" color=\"%2\" id=\"%3\"/>\r\n")
                   .arg(xmlEscaped(category.name), hex6(category.color))
                   .arg(category.id);
    }
    return out;
}

// metadata.xml of the calendar in the form the client writes it.
QByteArray phoneCalMetadata(const core::codec::CalMetadata &metadata)
{
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                                 "<metadata version=\"1.0\">\r\n");
    if (!metadata.calendars.isEmpty())
        out += QStringLiteral("<calendars filter_disabled=\"%1\">\r\n")
                   .arg(metadata.filterDisabled ? 1 : 0);
    for (const core::codec::CalendarInfo &calendar : metadata.calendars) {
        out += QStringLiteral("<calendar selected=\"%1\"").arg(calendar.selected ? 1 : 0);
        if (calendar.isDefault)
            out += QStringLiteral(" default=\"1\"");
        out += QStringLiteral(" url=\"%1\" title=\"%2\" color=\"%3\" hidden=\"%4\" "
                              "timezone=\"%5\" access_level=\"%6\"/>\r\n")
                   .arg(xmlEscaped(calendar.url), xmlEscaped(calendar.title),
                        QStringLiteral("%1").arg(calendar.color, 8, 16, QLatin1Char('0')).toUpper())
                   .arg(calendar.hidden ? 1 : 0)
                   .arg(calendar.timezone)
                   .arg(calendar.accessLevel);
    }
    out += QStringLiteral("</calendars>\r\n<tasksupport>1</tasksupport>\r\n<task_categories>\r\n");
    out += categoryLines(metadata.taskCategories);
    out += QStringLiteral("</task_categories>\r\n</metadata>");
    return out.toUtf8();
}

QByteArray phoneNoteMetadata(const QList<core::codec::OrganizerCategory> &categories)
{
    return (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata "
                           "version=\"1.0\"><note_categories>\r\n") +
            categoryLines(categories) + QStringLiteral("</note_categories>\r\n</metadata>"))
        .toUtf8();
}

// Changes of categories in a metadata PUT, applied like the client does.
void applyCategoryChanges(QList<core::codec::OrganizerCategory> &categories,
                          const core::codec::XmlElement &section)
{
    for (const core::codec::XmlElement &entry : section.children) {
        if (entry.name != QLatin1StringView("category"))
            continue;
        const QString flag = entry.attribute(QStringLiteral("flag"));
        const QString name = entry.attribute(QStringLiteral("name"));
        const quint32 color = entry.attribute(QStringLiteral("color")).toUInt(nullptr, 16);
        const int id = entry.attribute(QStringLiteral("id")).toInt();
        if (flag == QLatin1StringView("add")) {
            const bool known = std::any_of(
                categories.cbegin(), categories.cend(),
                [&name](const core::codec::OrganizerCategory &category) { return category.name == name; });
            if (known || name.isEmpty())
                continue;
            int next = 1;
            for (const core::codec::OrganizerCategory &category : std::as_const(categories))
                next = std::max(next, category.id + 1);
            categories.append({next, name, color});
        } else if (flag == QLatin1StringView("edit")) {
            for (core::codec::OrganizerCategory &category : categories) {
                if (category.id == id) {
                    category.name = name;
                    category.color = color;
                }
            }
        } else if (flag == QLatin1StringView("delete")) {
            categories.removeIf(
                [id](const core::codec::OrganizerCategory &category) { return category.id == id; });
        }
    }
}

// Adds the categories a task or a note names that the phone does not know,
// with the colours of the COLOR parameter where the body has one.
void addNamedCategories(QList<core::codec::OrganizerCategory> &categories,
                        const QStringList &names, const QByteArray &body)
{
    QStringList colors;
    const QList<core::codec::ContentLine> lines = core::codec::readContentLines(body);
    for (const core::codec::ContentLine &line : lines) {
        if (line.name == QLatin1StringView("CATEGORIES"))
            colors = line.parameter(QStringLiteral("COLOR")).split(u',');
    }
    for (qsizetype i = 0; i < names.size(); ++i) {
        const QString &name = names.at(i);
        const bool known = std::any_of(categories.cbegin(), categories.cend(),
                                       [&name](const core::codec::OrganizerCategory &category) {
                                           return category.name.compare(name, Qt::CaseInsensitive) == 0;
                                       });
        if (known)
            continue;
        int next = 1;
        for (const core::codec::OrganizerCategory &category : std::as_const(categories))
            next = std::max(next, category.id + 1);
        categories.append({next, name, colors.value(i).toUInt(nullptr, 16)});
    }
}

QByteArray createdAnswer(quint32 luid)
{
    ObexPacket response;
    response.code = obex::rspOk;
    response.appParameters = "  " + luidText(luid).toLatin1() + " ";
    return encoded(response);
}

QDateTime nowToTheSecond()
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    now.setTime(QTime(now.time().hour(), now.time().minute(), now.time().second()));
    return now;
}

} // namespace

struct FakePhone::Transfer
{
    bool upload = false;
    QString path;
    QByteArray data;
    quint64 length = 0;
    QDateTime modified;
    QTcpServer *server = nullptr;
    QPointer<QTcpSocket> side;
    bool done = false;
    bool statusPending = false;
};

FakePhone::FakePhone(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_aliveTimer(new QTimer(this))
{
    connect(m_aliveTimer, &QTimer::timeout, this, [this]() { sendUnsolicited("*ALIVE: 0,72,0"); });
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server->hasPendingConnections()) {
            QTcpSocket *socket = m_server->nextPendingConnection();
            if (m_config.rejectAsAlreadyConnected) {
                socket->write("ALREADY CONNECTED\r\nERROR\r\n");
                socket->flush();
                socket->disconnectFromHost();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                continue;
            }
            attach(socket);
            Q_EMIT connectionAccepted();
        }
    });
}

FakePhone::~FakePhone() = default;

bool FakePhone::listen()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

quint16 FakePhone::port() const
{
    return m_server->serverPort();
}

void FakePhone::dialOut(const QHostAddress &address, quint16 port)
{
    auto *socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket]() {
        attach(socket);
        Q_EMIT connectionAccepted();
    });
    socket->connectToHost(address, port);
}

bool FakePhone::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void FakePhone::clearLog()
{
    m_received.clear();
    m_protocolErrors.clear();
    m_syncPuts.clear();
}

void FakePhone::setObjects(const QString &area, const QList<FakeSyncObject> &objects)
{
    m_areas[area].objects = objects;
}

void FakePhone::setMetadata(const QString &area, const QByteArray &metadata)
{
    m_areas[area].metadata = metadata;
}

const FakeSyncArea &FakePhone::area(const QString &area) const
{
    static const FakeSyncArea empty;
    const auto it = m_areas.constFind(area);
    return it == m_areas.cend() ? empty : it.value();
}

void FakePhone::addObject(const QString &area, quint32 luid, const QByteArray &data)
{
    m_areas[area].objects.append({luid, data});
    recordChange(area, 'N', luid);
}

void FakePhone::editObject(const QString &area, quint32 luid, const QByteArray &data)
{
    for (FakeSyncObject &object : m_areas[area].objects) {
        if (object.luid == luid)
            object.data = data;
    }
    recordChange(area, 'E', luid);
}

void FakePhone::removeObject(const QString &area, quint32 luid)
{
    QList<FakeSyncObject> &objects = m_areas[area].objects;
    objects.removeIf([luid](const FakeSyncObject &object) { return object.luid == luid; });
    recordChange(area, 'D', luid);
}

void FakePhone::forgetChanges(const QString &area)
{
    m_areas[area].changes.clear();
}

quint64 FakePhone::changeCounter(const QString &area) const
{
    const FakeSyncArea &data = this->area(area);
    return data.changes.isEmpty() ? 0 : data.changes.last().id;
}

void FakePhone::setCalls(const QList<FakeCall> &calls)
{
    m_calls = calls;
}

void FakePhone::addFolder(const QString &path)
{
    QString folder = path;
    while (!folder.isEmpty()) {
        m_folders.insert(folder);
        folder = parentOf(folder);
    }
}

void FakePhone::addFile(const QString &path, const QByteArray &data, const QDateTime &modified)
{
    addFolder(parentOf(path));
    m_files.insert(path, {data, modified});
}

std::optional<FakeFile> FakePhone::file(const QString &path) const
{
    const auto it = m_files.constFind(path);
    if (it == m_files.cend())
        return std::nullopt;
    return it.value();
}

void FakePhone::sendUnsolicited(const QByteArray &line)
{
    if (isConnected() && !m_obexMode) {
        m_socket->write(line + "\r\n");
        m_socket->flush();
    }
}

void FakePhone::dropConnection()
{
    if (m_socket)
        m_socket->abort();
}

void FakePhone::attach(QTcpSocket *socket)
{
    if (m_socket) {
        QTcpSocket *old = m_socket;
        old->disconnect(this);
        old->write("CONNECTION CHANGE\r\n");
        old->flush();
        old->disconnectFromHost();
        connect(old, &QTcpSocket::disconnected, old, &QObject::deleteLater);
    }
    m_socket = socket;
    m_buffer.clear();
    m_authenticated = false;
    m_obexMode = false;
    m_target.clear();
    m_currentFolder.clear();
    m_discard = 0;
    m_pendingStream.clear();
    m_dropAfterStream = false;
    if (m_config.alivePeriod.count() > 0)
        m_aliveTimer->start(m_config.alivePeriod);
    connect(socket, &QTcpSocket::readyRead, this, &FakePhone::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        if (m_socket == socket)
            m_obexMode = false;
    });
}

void FakePhone::onReadyRead()
{
    if (!m_socket)
        return;
    const QByteArray data = m_socket->readAll();
    if (!m_obexMode && data.count('\n') > 1)
        protocolError(QStringLiteral("two AT commands in one write"));
    m_buffer += data;
    if (m_obexMode)
        processObex();
    else
        processAt();
}

void FakePhone::processAt()
{
    while (!m_obexMode && m_socket) {
        const qsizetype end = m_buffer.indexOf('\n');
        if (end < 0)
            return;
        const QByteArray line = m_buffer.left(end).trimmed();
        m_buffer.remove(0, end + 1);
        if (!line.isEmpty())
            handleAt(QString::fromUtf8(line));
    }
    if (m_obexMode && !m_buffer.isEmpty())
        processObex();
}

void FakePhone::writeAt(const QByteArray &bytes)
{
    QPointer<QTcpSocket> socket = m_socket;
    const auto write = [socket, bytes]() {
        if (socket) {
            socket->write(bytes);
            socket->flush();
        }
    };
    if (m_config.replyDelay.count() > 0)
        QTimer::singleShot(m_config.replyDelay, this, write);
    else
        write();
}

void FakePhone::handleAt(const QString &command)
{
    m_received.append(command);
    Q_EMIT commandReceived(command);

    for (const QLatin1StringView prefix : longTextPrefixes) {
        if (command.startsWith(prefix) && !command.endsWith(u','))
            protocolError(QStringLiteral("long text command without a trailing comma"));
    }

    const QString name = command.section(u'=', 0, 0).section(u'?', 0, 0).toUpper();
    const bool locked = !m_config.pin.isEmpty() && !m_authenticated;
    if (locked && name != QLatin1StringView("AT+CGSN") && name != QLatin1StringView("AT+CIMI") &&
        name != QLatin1StringView("AT*APPVERSION") && name != QLatin1StringView("AT*PIN")) {
        m_socket->write("ACCESS DENIED\r\nERROR\r\n");
        m_socket->flush();
        m_socket->disconnectFromHost();
        return;
    }
    if (m_config.unanswered.contains(command))
        return;
    const auto scripted = m_config.scriptedReplies.constFind(command);
    if (scripted != m_config.scriptedReplies.cend()) {
        for (const QByteArray &chunk : scripted.value()) {
            m_socket->write(chunk);
            m_socket->flush();
        }
        return;
    }

    if (handleCallLog(command, name))
        return;
    if (handleMessaging(command, name))
        return;
    if (handleAlarms(command, name))
        return;

    if (command == QLatin1StringView("AT+CPROT=0")) {
        writeAt("AT+CPROT=0\r\nCONNECT\r\n");
        m_obexMode = true;
        m_target.clear();
        return;
    }

    QStringList lines;
    bool ok = true;
    if (name == QLatin1StringView("AT*PIN") && command.contains(u'=')) {
        if (m_wrongPins >= maxPinTries) {
            // The client stops its service without an answer.
            m_socket->disconnectFromHost();
            return;
        }
        const QByteArray expected =
            QCryptographicHash::hash(QByteArrayLiteral("hash") + m_config.pin.toUtf8(),
                                     QCryptographicHash::Md5)
                .toHex()
                .toUpper();
        const QByteArray sent = command.section(u'=', 1).trimmed().toUpper().toLatin1();
        if (m_config.pin.isEmpty() || sent == expected) {
            m_authenticated = true;
            m_wrongPins = 0;
            lines << QStringLiteral("*PIN: ACCESS OK,%1").arg(maxPinTries);
        } else {
            ++m_wrongPins;
            lines << QStringLiteral("*PIN: WRONG PIN,%1").arg(maxPinTries - m_wrongPins);
        }
    } else if (command == QLatin1StringView("AT*PIN?")) {
        const bool open = m_config.pin.isEmpty() || m_authenticated;
        lines << QStringLiteral("*PIN: %1,%2")
                     .arg(open ? QStringLiteral("ACCESS OK") : QStringLiteral("ACCESS PROTECTED"))
                     .arg(maxPinTries - m_wrongPins);
    } else if (command == QLatin1StringView("AT+CGSN")) {
        lines << m_config.deviceId;
    } else if (command == QLatin1StringView("ATI")) {
        lines << m_config.model;
    } else if (command == QLatin1StringView("AT+CGMI")) {
        lines << m_config.manufacturer;
    } else if (name == QLatin1StringView("AT*APPVERSION")) {
        lines << QStringLiteral("%1,%2").arg(m_config.androidApi).arg(m_config.clientVersion);
    } else if (command == QLatin1StringView("AT+CBC")) {
        lines << QStringLiteral("+CBC: 0,72");
    } else if (command == QLatin1StringView("AT+CBCEX")) {
        lines << QStringLiteral("+CBCEX: -1,72,24,3915");
    } else if (command == QLatin1StringView("AT+CSQ")) {
        lines << QStringLiteral("+CSQ: 17,99");
    } else if (command == QLatin1StringView("AT*SOPS")) {
        lines << QStringLiteral("*SOPS: ,,");
    } else if (command == QLatin1StringView("AT+CMGS=?")) {
        lines << QStringLiteral("+CMGS: ") + m_config.smsCapabilities;
    } else if (command == QLatin1StringView("AT*ANDROIDVERSION")) {
        lines << QStringLiteral("*ANDROIDVERSION: Android 14,Upside Down Cake");
    } else if (command == QLatin1StringView("AT*STATFS")) {
        lines << QStringLiteral("/sdcard:64000000000,32000000000")
              << QStringLiteral("/data:64000000000,32000000000");
    } else if (command == QLatin1StringView("AT*ECPU")) {
        lines << QStringLiteral("*ECPU: 12");
    } else if (command == QLatin1StringView("AT*DISPLAY")) {
        lines << QStringLiteral("*DISPLAY: 1080,2400,409.432,411.891,420");
    } else if (command == QLatin1StringView("AT+CGMR")) {
        lines << QStringLiteral("EX1A.260101.001");
    } else if ((name.startsWith(QLatin1StringView("ATD")) && command.endsWith(u';')) ||
               name == QLatin1StringView("ATH") || name == QLatin1StringView("AT+CHUP")) {
        // Recorded above; there is no phone line behind the fake.
    } else if (command == QLatin1StringView("AT*SIM") ||
               command == QLatin1StringView("AT*ECAM=1") ||
               command == QLatin1StringView("AT*NOTIFY=2") ||
               command == QLatin1StringView("AT*CLIPWATCH=1") ||
               command == QLatin1StringView("AT*NOTIR") ||
               command.endsWith(QLatin1StringView("=?")) ||
               name == QLatin1StringView("AT*CLIPBOARD")) {
        // Accepted without a body.
    } else {
        ok = false;
    }

    QString echo = command;
    if (name == QLatin1StringView("AT*CLIPBOARD") && command.contains(u'='))
        echo = QStringLiteral("AT*CLIPBOARD");
    QByteArray reply = echo.toUtf8() + "\r\n";
    for (const QString &line : std::as_const(lines))
        reply += line.toUtf8() + "\r\n";
    reply += ok ? "OK\r\n" : "ERROR\r\n";
    writeAt(reply);
}

bool FakePhone::handleCallLog(const QString &command, const QString &name)
{
    const bool callLog =
        name == QLatin1StringView("AT+CPBS") || name == QLatin1StringView("AT+CPBR") ||
        name == QLatin1StringView("AT+CPBW") || name == QLatin1StringView("AT+CPBD");
    if (!callLog)
        return false;

    QByteArray reply = command.toUtf8() + "\r\n";
    const auto finish = [this, &reply](const QByteArray &finalLine) {
        writeAt(reply + finalLine + "\r\n");
    };
    const auto line = [](const FakeCall &call) {
        return QStringLiteral("+CPBR: %1,\"%2\",%3,\"%4\",\"%5\",%6\r\n")
            .arg(QString::number(call.id), call.number, call.addressType, call.name, call.time,
                 QString::number(call.duration))
            .toUtf8();
    };
    const auto unquote = [](const QString &field) {
        const QString trimmed = field.trimmed();
        if (trimmed.size() >= 2 && trimmed.startsWith(u'"') && trimmed.endsWith(u'"'))
            return trimmed.mid(1, trimmed.size() - 2);
        return trimmed;
    };
    if (m_config.callLogDenied) {
        finish("ERROR");
        return true;
    }
    const QString argument = command.section(u'=', 1);
    const bool changes =
        name == QLatin1StringView("AT+CPBW") || name == QLatin1StringView("AT+CPBD");
    if (m_config.callLogReadOnly && changes) {
        reply += "DUMP: java.lang.SecurityException: Permission Denial: writing "
                 "com.android.providers.contacts.CallLogProvider uri content://call_log/calls "
                 "requires android.permission.WRITE_CALL_LOG, or grantUriPermission()\r\n";
        finish("ERROR");
        return true;
    }

    if (name == QLatin1StringView("AT+CPBS")) {
        static const QStringList storages = {QStringLiteral("DC"), QStringLiteral("RC"),
                                             QStringLiteral("MC"), QStringLiteral("ALL")};
        const QString storage = unquote(argument);
        if (!command.contains(u'=') || !storages.contains(storage)) {
            finish("ERROR");
            return true;
        }
        m_callStorage = storage;
        finish("OK");
        return true;
    }

    if (name == QLatin1StringView("AT+CPBR")) {
        if (argument.isEmpty() || argument.startsWith(u',')) {
            const int limit = argument.size() > 1 ? argument.mid(1).toInt() : -1;
            QList<FakeCall> shown;
            for (const FakeCall &call : std::as_const(m_calls)) {
                if (m_callStorage == QLatin1StringView("ALL") || call.storage == m_callStorage)
                    shown.append(call);
            }
            // The two-digit years of this century sort like the dates.
            std::stable_sort(shown.begin(), shown.end(),
                             [](const FakeCall &a, const FakeCall &b) { return a.time > b.time; });
            for (qsizetype i = 0; i < shown.size() && (limit < 0 || i < limit); ++i)
                reply += line(shown.at(i));
            finish("OK");
            return true;
        }
        bool ok = false;
        const quint32 id = argument.toUInt(&ok);
        for (const FakeCall &call : std::as_const(m_calls)) {
            if (ok && call.id == id) {
                reply += line(call);
                finish("OK");
                return true;
            }
        }
        finish("+CMS ERROR: 321");
        return true;
    }

    if (name == QLatin1StringView("AT+CPBD")) {
        bool ok = false;
        const quint32 id = argument.toUInt(&ok);
        if (!ok) {
            finish("ERROR");
            return true;
        }
        const qsizetype removed =
            m_calls.removeIf([id](const FakeCall &call) { return call.id == id; });
        finish(removed > 0 ? "OK" : "+CMS ERROR: 500");
        return true;
    }

    const QStringList fields = core::codec::splitFields(argument);
    if (fields.size() < 6 || !core::codec::parseCallLogTime(unquote(fields.at(4)))) {
        finish("ERROR");
        return true;
    }
    FakeCall call;
    call.number = unquote(fields.at(1));
    call.name = unquote(fields.at(3));
    call.time = unquote(fields.at(4));
    call.duration = fields.at(5).trimmed().toLongLong();
    const QString storage = fields.size() > 6 ? unquote(fields.at(6)) : QString();
    call.storage = storage == QLatin1StringView("RC") || storage == QLatin1StringView("MC")
                       ? storage
                       : QStringLiteral("DC");
    bool ok = false;
    const quint32 id = fields.at(0).trimmed().toUInt(&ok);
    if (!ok || id == 0) {
        call.id = m_nextCallId++;
        m_calls.append(call);
    } else {
        for (FakeCall &stored : m_calls) {
            if (stored.id == id) {
                call.id = id;
                stored = call;
            }
        }
    }
    finish("OK");
    return true;
}

bool FakePhone::handleMessaging(const QString &command, const QString &name)
{
    const bool send = name == QLatin1StringView("AT+CMGS") && command.contains(u'=') &&
                      !command.endsWith(QLatin1StringView("=?"));
    const bool write = name == QLatin1StringView("AT+CMGD") ||
                       name == QLatin1StringView("AT+CMGE") || name == QLatin1StringView("AT+CMGW");
    if (!send && !write)
        return false;

    const QByteArray echo = command.toUtf8() + "\r\n";
    const QString argument = command.section(u'=', 1);
    const QStringList fields = core::codec::splitFields(argument);
    QList<FakeSyncObject> &objects = m_areas[QStringLiteral("msg")].objects;
    if (write) {
        if (m_config.smsWriteDenied) {
            writeAt(echo + name.mid(2).toUtf8() + ": PERMISSION DENIED\r\nERROR\r\n");
            return true;
        }
        bool ok = false;
        const quint32 id = fields.value(0).trimmed().toUInt(&ok);
        const auto found =
            std::find_if(objects.begin(), objects.end(),
                         [ok, id](const auto &object) { return ok && object.luid == id; });
        if (name == QLatin1StringView("AT+CMGD")) {
            if (found == objects.end()) {
                writeAt(echo + "+CMS ERROR: 500\r\n");
                return true;
            }
            removeObject(QStringLiteral("msg"), id);
            writeAt(echo + "OK\r\n");
            return true;
        }
        if (name == QLatin1StringView("AT+CMGE") && found != objects.end()) {
            QByteArray data = found->data;
            const QByteArray read =
                fields.value(1).trimmed() == QLatin1StringView("1") ? "READ:1" : "READ:0";
            data.replace("READ:0", read);
            data.replace("READ:1", read);
            editObject(QStringLiteral("msg"), id, data);
            writeAt(echo + "OK\r\n");
            return true;
        }
        writeAt(echo + "+CMS ERROR: 401\r\n");
        return true;
    }

    if (fields.size() < 3) {
        writeAt(echo + "ERROR\r\n");
        return true;
    }
    FakeSentMessage sent;
    sent.number = core::codec::decodeQp(fields.at(0));
    sent.text = core::codec::decodeQp(fields.at(1));
    sent.mode = fields.at(2).trimmed().toInt();
    sent.simSlot = fields.size() > 4 ? fields.at(4).trimmed().toInt() : -1;
    m_sentMessages.append(sent);
    if (!m_config.smsSendFailure.isEmpty()) {
        writeAt(echo + m_config.smsSendFailure + "\r\n");
        return true;
    }
    // Android files a message an app sent as a sent message of its own.
    quint32 id = 1;
    for (const FakeSyncObject &object : std::as_const(objects)) {
        if ((object.luid & messageKindMask) == 0)
            id = std::max(id, object.luid + 1);
    }
    QByteArray message = "BEGIN:VMESSAGE\r\nTYP:SMS;OUT\r\nBOX:SENT\r\n";
    if (sent.simSlot >= 0)
        message += "SIM:" + QByteArray::number(sent.simSlot) + "\r\n";
    message +=
        "DATE:" + core::codec::formatUtcStamp(QDateTime::currentDateTimeUtc()).toLatin1() + "\r\n";
    message += messageLine("NUMBER", sent.number) + messageLine("BODY", sent.text);
    message += "X-IRMC-LUID:" + luidText(id).toLatin1() + "\r\nEND:VMESSAGE\r\n";
    addObject(QStringLiteral("msg"), id, message);
    writeAt(echo + "+CMGS: 0\r\n+CMGW: " + QByteArray::number(id) + "\r\nOK\r\n");
    return true;
}

void FakePhone::processObex()
{
    while (m_obexMode && m_socket) {
        if (m_discard > 0) {
            if (m_buffer.size() < m_discard)
                return;
            const QByteArray answers = m_buffer.left(m_discard);
            m_buffer.remove(0, m_discard);
            if (answers != QByteArray::fromHex("830003").repeated(m_discard / 3))
                protocolError(QStringLiteral("keep-alive not answered with 83 00 03"));
            m_discard = 0;
        }
        const std::optional<quint16> length = ObexPacket::declaredLength(m_buffer);
        if (!length || m_buffer.size() < *length)
            return;
        if (*length < 3) {
            protocolError(QStringLiteral("OBEX packet with an invalid length"));
            m_buffer.clear();
            return;
        }
        const QByteArray packet = m_buffer.left(*length);
        m_buffer.remove(0, *length);
        handleObex(packet);
    }
    if (!m_obexMode && !m_buffer.isEmpty())
        processAt();
}

void FakePhone::handleObex(const QByteArray &bytes)
{
    const quint8 code = quint8(bytes.at(0));
    if (code == obex::opGetFinal && bytes.size() == 3) {
        if (m_pendingStream.isEmpty())
            sendPacket(codeOnly(obex::rspNotAcceptable));
        else
            sendNextChunk();
        return;
    }
    const auto decoded = ObexPacket::decode(bytes, ObexPacket::requestLayout(code));
    if (!decoded.hasValue()) {
        protocolError(decoded.error().message());
        sendPacket(codeOnly(obex::rspBadRequest));
        return;
    }
    const ObexPacket &request = decoded.value();

    QString label = QStringLiteral("OBEX ") + opName(code);
    if (code == obex::opConnect)
        label += request.target == fileBrowserUuid() ? QStringLiteral(" FILEBROWSER")
                                                     : u' ' + QString::fromLatin1(request.target);
    else if (request.name)
        label += u' ' + *request.name;
    else if (!request.type.isEmpty())
        label += u' ' + QString::fromLatin1(request.type);
    if (request.targetName)
        label += QStringLiteral(" -> ") + *request.targetName;
    m_received.append(label);
    Q_EMIT commandReceived(label);
    if (request.name && m_config.unansweredObex.contains(*request.name))
        return;
    m_dropAfterStream = code == obex::opGetFinal && request.name &&
                        !m_config.dropAfterGet.isEmpty() && *request.name == m_config.dropAfterGet;

    if (code != obex::opConnect && code != obex::opAbort && request.connectionId != 1u)
        protocolError(QStringLiteral("%1 without Connection-ID 1").arg(label));
    if (request.bodyKind == ObexPacket::BodyKind::Body)
        protocolError(QStringLiteral("%1 split over several packets").arg(label));

    if (m_failNext && code != obex::opConnect && code != obex::opDisconnect) {
        ObexPacket failure;
        failure.code = *m_failNext;
        if (!m_failNextAppParameters.isEmpty())
            failure.appParameters = m_failNextAppParameters;
        m_failNext.reset();
        m_failNextAppParameters.clear();
        sendFirst(encoded(failure));
        return;
    }

    switch (code) {
    case obex::opConnect: {
        if (m_config.connectRefusal != 0) {
            sendPacket(codeOnly(m_config.connectRefusal));
            return;
        }
        if (request.target != QByteArrayLiteral("IRMC-SYNC") &&
            request.target != fileBrowserUuid()) {
            sendPacket(codeOnly(obex::rspNotImplemented));
            return;
        }
        m_target = request.target;
        m_loadedAreas.clear();
        m_currentFolder.clear();
        m_maxPacket = request.maxPacketLength == 0 ? 0xFFFF : request.maxPacketLength;
        ObexPacket response;
        response.code = obex::rspOk;
        response.layout = ObexPacket::Layout::Connect;
        response.connectionId = 1;
        response.who = request.target;
        sendPacket(encoded(response));
        return;
    }
    case obex::opDisconnect:
        sendPacket(codeOnly(obex::rspOk) + m_config.afterDisconnect);
        m_obexMode = false;
        m_target.clear();
        m_loadedAreas.clear();
        m_pendingStream.clear();
        return;
    case obex::opAbort:
        m_pendingStream.clear();
        sendPacket(codeOnly(obex::rspOk));
        return;
    default:
        break;
    }

    if (m_target == QByteArrayLiteral("IRMC-SYNC")) {
        if (code == obex::opGetFinal && request.name)
            handleSyncGet(*request.name);
        else if (code == obex::opPutFinal && request.name)
            handleSyncPut(*request.name, request.body,
                          request.bodyKind != ObexPacket::BodyKind::None);
        else
            sendFirst(codeOnly(obex::rspNotAcceptable));
        return;
    }
    if (m_target == fileBrowserUuid()) {
        handleFileRequest(request);
        return;
    }
    sendPacket(codeOnly(obex::rspNotAcceptable));
}

void FakePhone::handleSyncGet(const QString &name)
{
    const QString lower = name.toLower();
    static constexpr QLatin1StringView root("telecom/");
    if (!lower.startsWith(root)) {
        sendFirst(codeOnly(obex::rspNotFound));
        return;
    }
    QString areaName = lower.mid(root.size());
    areaName = areaName.left(
        std::min(areaName.indexOf(u'/') < 0 ? areaName.size() : areaName.indexOf(u'/'),
                 areaName.indexOf(u'.') < 0 ? areaName.size() : areaName.indexOf(u'.')));
    if (!isKnownArea(areaName)) {
        sendFirst(codeOnly(obex::rspNotFound));
        return;
    }
    if (areaName == QLatin1StringView("cal") && m_config.calendarDenied) {
        sendFirst(codeOnly(obex::rspUnauthorized));
        return;
    }
    FakeSyncArea &data = m_areas[areaName];
    const QString rest = lower.mid(root.size() + areaName.size());
    const QString extension = extensionOf(areaName);
    const QByteArray total = "Total-Records:" + QByteArray::number(data.objects.size());

    if (rest == QLatin1StringView("/info.log")) {
        sendBody(total);
    } else if (rest == QLatin1StringView("/initsync")) {
        m_loadedAreas.insert(areaName);
        for (int i = 0; i < m_config.paddingBeforeReply; ++i) {
            sendPacket(QByteArray::fromHex("900003"));
            m_discard += 3;
        }
        for (int i = 1; i <= m_config.initsyncProgress; ++i) {
            ObexPacket progress;
            progress.code = obex::rspContinue;
            progress.bodyKind = ObexPacket::BodyKind::Body;
            progress.body = "\r\n" + QByteArray::number(i) + '/' +
                            QByteArray::number(m_config.initsyncProgress);
            sendPacket(encoded(progress));
            m_discard += 3;
        }
        sendPacket(codeOnly(obex::rspOk));
    } else if (rest == QLatin1StringView("/metadata.xml") && areaName != QLatin1StringView("msg")) {
        sendBody(data.metadata);
    } else if (rest == QLatin1StringView("/luid/cc.log")) {
        sendBody(QByteArray::number(changeCounter(areaName)));
    } else if (rest.startsWith(QLatin1StringView("/luid/")) &&
               rest.endsWith(QLatin1StringView(".log"))) {
        bool ok = false;
        const quint64 since = rest.mid(6, rest.size() - 10).toULongLong(&ok);
        if (!ok) {
            sendFirst(codeOnly(obex::rspNotFound));
            return;
        }
        QByteArray body = total + "\r\n";
        const auto hasRow = [&data](quint64 id) {
            return std::any_of(data.changes.cbegin(), data.changes.cend(),
                               [id](const FakeChange &change) { return change.id == id; });
        };
        if (hasRow(since + 1)) {
            QList<FakeChange> latest;
            for (const FakeChange &change : std::as_const(data.changes)) {
                if (change.id <= since)
                    continue;
                latest.removeIf(
                    [&change](const FakeChange &earlier) { return earlier.luid == change.luid; });
                latest.append(change);
            }
            for (const FakeChange &change : std::as_const(latest)) {
                body += change.type + QByteArray(":") + QByteArray::number(change.id) +
                        "::" + luidText(change.luid).toLatin1() + "\r\n";
            }
        } else if (!hasRow(since)) {
            body += "*\r\n";
        }
        sendBody(body);
    } else if (rest == u'.' + extension) {
        QByteArray dump;
        for (const FakeSyncObject &object : std::as_const(data.objects))
            dump += object.data + "\r\n";
        sendBody(dump);
    } else if (rest.startsWith(QLatin1StringView("/luid/")) && rest.endsWith(u'.' + extension) &&
               rest.size() == 6 + luidDigits + 4) {
        bool ok = false;
        const quint32 luid = rest.mid(6, luidDigits).toUInt(&ok, 16);
        for (const FakeSyncObject &object : std::as_const(data.objects)) {
            if (ok && object.luid == luid) {
                sendBody(object.data);
                return;
            }
        }
        sendFirst(codeOnly(areaName == QLatin1StringView("msg") ? obex::rspNotFound
                                                                : obex::rspDatabaseLocked));
    } else {
        sendFirst(codeOnly(obex::rspNotFound));
    }
}

void FakePhone::handleSyncPut(const QString &name, const QByteArray &body, bool hasBody)
{
    const QString lower = name.toLower();
    if (lower == QLatin1StringView("telecom/msg/setflags")) {
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    static constexpr QLatin1StringView root("telecom/");
    const qsizetype slash = lower.indexOf(u'/', root.size());
    const QString areaName = slash < 0 ? QString() : lower.mid(root.size(), slash - root.size());
    if (!lower.startsWith(root) || !isKnownArea(areaName)) {
        sendFirst(codeOnly(obex::rspNotFound));
        return;
    }
    FakeSyncArea &data = m_areas[areaName];
    const QString rest = lower.mid(slash);
    const QString extension = extensionOf(areaName);
    m_syncPuts.append({name, hasBody ? body : QByteArray()});

    if (areaName == QLatin1StringView("msg")) {
        handleMessagePut(rest, body, hasBody);
        return;
    }
    if (areaName == QLatin1StringView("cal")) {
        handleCalendarPut(rest, body, hasBody);
        return;
    }
    if (areaName == QLatin1StringView("notes")) {
        handleNotePut(rest, body, hasBody);
        return;
    }
    if (rest == QLatin1StringView("/metadata.xml")) {
        data.metadata = body;
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    if (areaName == QLatin1StringView("pb")) {
        handleContactPut(rest, hasBody ? body : QByteArray());
        return;
    }
    if (rest == QLatin1StringView("/luid/.") + extension) {
        if (!hasBody || body.isEmpty()) {
            sendFirst(codeOnly(obex::rspBadRequest));
            return;
        }
        quint32 luid = 1;
        for (const FakeSyncObject &object : std::as_const(data.objects))
            luid = std::max(luid, object.luid + 1);
        data.objects.append({luid, body});
        recordChange(areaName, 'N', luid);
        ObexPacket response;
        response.code = obex::rspOk;
        response.appParameters = "  " + luidText(luid).toLatin1() + " ";
        sendFirst(encoded(response));
        return;
    }
    if (rest.startsWith(QLatin1StringView("/luid/")) && rest.endsWith(u'.' + extension)) {
        bool ok = false;
        const quint32 luid = rest.mid(6, luidDigits).toUInt(&ok, 16);
        for (FakeSyncObject &object : data.objects) {
            if (!ok || object.luid != luid)
                continue;
            if (!hasBody || body.isEmpty()) {
                removeObject(areaName, luid);
            } else {
                object.data = body;
                recordChange(areaName, 'E', luid);
            }
            sendFirst(codeOnly(obex::rspOk));
            return;
        }
    }
    sendFirst(codeOnly(obex::rspNotFound));
}

void FakePhone::handleMessagePut(const QString &rest, const QByteArray &body, bool hasBody)
{
    const QString area = QStringLiteral("msg");
    const bool scheduled =
        hasMessageLine(body, "BEGIN:VMESSAGE") && hasMessageLine(body, "BOX:SHEDULED");
    if (rest == QLatin1StringView("/luid/.vmg")) {
        if (!hasBody || !scheduled) {
            sendFirst(codeOnly(obex::rspBadRequest));
            return;
        }
        quint32 luid = scheduledBit | 1;
        for (const FakeSyncObject &object : std::as_const(m_areas[area].objects)) {
            if ((object.luid & messageKindMask) == scheduledBit)
                luid = std::max(luid, object.luid + 1);
        }
        addObject(area, luid, scheduledAsPhoneSendsIt(body, luid));
        ObexPacket response;
        response.code = obex::rspOk;
        response.appParameters = "  " + luidText(luid).toLatin1() + " ";
        sendFirst(encoded(response));
        return;
    }
    bool ok = rest.startsWith(QLatin1StringView("/luid/")) &&
              rest.endsWith(QLatin1StringView(".vmg")) && rest.size() == 6 + luidDigits + 4;
    const quint32 luid = ok ? rest.mid(6, luidDigits).toUInt(&ok, 16) : 0;
    if (!ok) {
        sendFirst(codeOnly(obex::rspNotFound));
        return;
    }
    const QList<FakeSyncObject> &objects = m_areas[area].objects;
    const bool known =
        std::any_of(objects.cbegin(), objects.cend(),
                    [luid](const FakeSyncObject &object) { return object.luid == luid; });
    // The client answers OK for a message it does not keep itself and changes nothing.
    if ((luid & messageKindMask) != scheduledBit || !known) {
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    if (!hasBody || body.isEmpty()) {
        removeObject(area, luid);
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    if (!scheduled) {
        sendFirst(codeOnly(obex::rspBadRequest));
        return;
    }
    editObject(area, luid, scheduledAsPhoneSendsIt(body, luid));
    sendFirst(codeOnly(obex::rspOk));
}

void FakePhone::handleContactPut(const QString &rest, const QByteArray &body)
{
    FakeSyncArea &data = m_areas[QStringLiteral("pb")];
    if (rest == QLatin1StringView("/luid/.vcf")) {
        const auto parsed = core::codec::parsePhoneVCard(body);
        if (!parsed.hasValue()) {
            sendFirst(codeOnly(obex::rspBadRequest));
            return;
        }
        quint32 luid = 1;
        for (const FakeSyncObject &object : std::as_const(data.objects))
            luid = std::max(luid, object.luid + 1);
        core::codec::Contact contact = parsed.value();
        dropEmptyEntries(contact);
        assignDataIds(contact, m_nextDataId);
        contact.luid = luid;
        if (contact.displayName.isEmpty())
            contact.displayName = core::codec::displayNameOf(contact);
        data.objects.append({luid, core::codec::encodeAsPhoneSendsIt(contact)});
        recordChange(QStringLiteral("pb"), 'N', luid);
        ObexPacket response;
        response.code = obex::rspOk;
        response.appParameters = "  " + luidText(luid).toLatin1() + " ";
        sendFirst(encoded(response));
        return;
    }
    bool ok = rest.startsWith(QLatin1StringView("/luid/")) &&
              rest.endsWith(QLatin1StringView(".vcf")) && rest.size() == 6 + luidDigits + 4;
    const quint32 luid = ok ? rest.mid(6, luidDigits).toUInt(&ok, 16) : 0;
    for (FakeSyncObject &object : data.objects) {
        if (!ok || object.luid != luid)
            continue;
        if (body.isEmpty()) {
            removeObject(QStringLiteral("pb"), luid);
            sendFirst(codeOnly(obex::rspOk));
            return;
        }
        const auto current = core::codec::parsePhoneVCard(object.data);
        const auto change = core::codec::parsePhoneVCard(body);
        if (!current.hasValue() || !change.hasValue()) {
            sendFirst(codeOnly(obex::rspBadRequest));
            return;
        }
        core::codec::Contact updated =
            appliedChange(current.value(), change.value(), body, m_nextDataId);
        updated.luid = luid;
        object.data = core::codec::encodeAsPhoneSendsIt(updated);
        recordChange(QStringLiteral("pb"), 'E', luid);
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    sendFirst(codeOnly(obex::rspNotFound));
}

void FakePhone::handleCalendarPut(const QString &rest, const QByteArray &body, bool hasBody)
{
    const QString area = QStringLiteral("cal");
    FakeSyncArea &data = m_areas[area];
    if (m_config.calendarDenied) {
        sendFirst(codeOnly(obex::rspUnauthorized));
        return;
    }
    const auto parsedMetadata = core::codec::parseCalMetadata(data.metadata);
    core::codec::CalMetadata metadata =
        parsedMetadata.hasValue() ? parsedMetadata.value() : core::codec::CalMetadata();
    if (rest == QLatin1StringView("/metadata.xml")) {
        // The client answers OK even to a document it cannot read.
        const auto document = core::codec::parseSafeXml(body);
        if (document.hasValue()) {
            for (const core::codec::XmlElement &section : document.value().children) {
                if (section.name == QLatin1StringView("calendars")) {
                    metadata.filterDisabled =
                        section.attribute(QStringLiteral("filter_disabled")) == QLatin1StringView("1");
                    bool defaultSeen = false;
                    for (core::codec::CalendarInfo &calendar : metadata.calendars) {
                        calendar.selected = metadata.filterDisabled;
                        for (const core::codec::XmlElement &entry : section.children) {
                            if (entry.attribute(QStringLiteral("url")) != calendar.url ||
                                entry.attribute(QStringLiteral("title")) != calendar.title) {
                                continue;
                            }
                            if (entry.attribute(QStringLiteral("selected")) == QLatin1StringView("1"))
                                calendar.selected = true;
                            if (entry.attribute(QStringLiteral("default")) == QLatin1StringView("1"))
                                defaultSeen = true;
                        }
                    }
                    if (defaultSeen) {
                        for (core::codec::CalendarInfo &calendar : metadata.calendars) {
                            calendar.isDefault = false;
                            for (const core::codec::XmlElement &entry : section.children) {
                                if (entry.attribute(QStringLiteral("url")) == calendar.url &&
                                    entry.attribute(QStringLiteral("title")) == calendar.title &&
                                    entry.attribute(QStringLiteral("default")) ==
                                        QLatin1StringView("1")) {
                                    calendar.isDefault = true;
                                }
                            }
                        }
                    }
                } else if (section.name == QLatin1StringView("task_categories")) {
                    applyCategoryChanges(metadata.taskCategories, section);
                }
            }
            data.metadata = phoneCalMetadata(metadata);
        }
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    if (!m_loadedAreas.contains(area)) {
        sendFirst(codeOnly(obex::rspPreconditionFailed));
        return;
    }
    const bool isTask = body.contains("\r\nBEGIN:VTODO\r\n") || body.startsWith("BEGIN:VTODO\r\n");
    const bool isEvent = body.contains("BEGIN:VEVENT\r\n");
    if (rest == QLatin1StringView("/luid/.vcs")) {
        if (!hasBody || (!isTask && !isEvent)) {
            sendFirst(codeOnly(obex::rspBadRequest));
            return;
        }
        quint32 luid = 0;
        QByteArray stored;
        if (isTask) {
            const auto task = core::codec::parsePhoneTask(body);
            if (!task.hasValue()) {
                sendFirst(codeOnly(obex::rspBadRequest));
                return;
            }
            luid = 1;
            for (const FakeSyncObject &object : std::as_const(data.objects)) {
                if ((object.luid & eventBit) == 0)
                    luid = std::max(luid, object.luid + 1);
            }
            core::codec::CalendarTask created = task.value();
            created.luid = luid;
            created.lastModified = nowToTheSecond();
            addNamedCategories(metadata.taskCategories, created.categories, body);
            data.metadata = phoneCalMetadata(metadata);
            stored = core::codec::encodeTaskAsPhoneSendsIt(created);
        } else {
            const auto event = core::codec::parsePhoneEvent(body);
            if (!event.hasValue()) {
                sendFirst(codeOnly(obex::rspBadRequest));
                return;
            }
            core::codec::CalendarEvent created = event.value();
            const core::codec::CalendarInfo *target = nullptr;
            for (const core::codec::CalendarInfo &calendar : std::as_const(metadata.calendars)) {
                const bool usable = calendar.selected || metadata.filterDisabled;
                if (usable && calendar.url == created.calendarUrl &&
                    calendar.title == created.calendarTitle) {
                    target = &calendar;
                }
            }
            for (const core::codec::CalendarInfo &calendar : std::as_const(metadata.calendars)) {
                if (!target && calendar.isDefault)
                    target = &calendar;
            }
            if (!target && !metadata.calendars.isEmpty())
                target = &metadata.calendars.constFirst();
            created.calendarUrl = target ? target->url : QString();
            created.calendarTitle = target ? target->title : QString();
            quint32 next = 1;
            for (const FakeSyncObject &object : std::as_const(data.objects)) {
                if ((object.luid & eventBit) != 0)
                    next = std::max(next, (object.luid & ~eventBit) + 1);
            }
            luid = eventBit | next;
            created.luid = luid;
            stored = core::codec::encodeEventAsPhoneSendsIt(created);
        }
        addObject(area, luid, stored);
        sendFirst(createdAnswer(luid));
        return;
    }
    bool ok = rest.startsWith(QLatin1StringView("/luid/")) &&
              rest.endsWith(QLatin1StringView(".vcs")) && rest.size() == 6 + luidDigits + 4;
    const quint32 luid = ok ? rest.mid(6, luidDigits).toUInt(&ok, 16) : 0;
    FakeSyncObject *existing = nullptr;
    for (FakeSyncObject &object : data.objects) {
        if (ok && object.luid == luid)
            existing = &object;
    }
    if (!existing) {
        sendFirst(codeOnly(obex::rspNotFound));
        return;
    }
    if (!hasBody || body.isEmpty()) {
        removeObject(area, luid);
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    if (core::codec::isTaskObject(existing->data)) {
        const auto task = core::codec::parsePhoneTask(body);
        if (!isTask || !task.hasValue()) {
            sendFirst(codeOnly(obex::rspBadRequest));
            return;
        }
        core::codec::CalendarTask changed = task.value();
        changed.luid = luid;
        changed.lastModified = nowToTheSecond();
        addNamedCategories(metadata.taskCategories, changed.categories, body);
        data.metadata = phoneCalMetadata(metadata);
        editObject(area, luid, core::codec::encodeTaskAsPhoneSendsIt(changed));
    } else {
        const auto before = core::codec::parsePhoneEvent(existing->data);
        const auto event = core::codec::parsePhoneEvent(body);
        if (!isEvent || !event.hasValue() || !before.hasValue()) {
            sendFirst(codeOnly(obex::rspBadRequest));
            return;
        }
        core::codec::CalendarEvent changed = event.value();
        changed.luid = luid;
        // The client never moves an event to another calendar on a change.
        changed.calendarUrl = before.value().calendarUrl;
        changed.calendarTitle = before.value().calendarTitle;
        if (changed.color && *changed.color == 0)
            changed.color.reset();
        editObject(area, luid, core::codec::encodeEventAsPhoneSendsIt(changed));
    }
    sendFirst(codeOnly(obex::rspOk));
}

void FakePhone::handleNotePut(const QString &rest, const QByteArray &body, bool hasBody)
{
    const QString area = QStringLiteral("notes");
    FakeSyncArea &data = m_areas[area];
    const auto parsedCategories = core::codec::parseNoteMetadata(data.metadata);
    QList<core::codec::OrganizerCategory> categories =
        parsedCategories.hasValue() ? parsedCategories.value()
                                    : QList<core::codec::OrganizerCategory>();
    if (rest == QLatin1StringView("/metadata.xml")) {
        const auto document = core::codec::parseSafeXml(body);
        if (document.hasValue()) {
            for (const core::codec::XmlElement &section : document.value().children) {
                if (section.name == QLatin1StringView("note_categories"))
                    applyCategoryChanges(categories, section);
            }
            data.metadata = phoneNoteMetadata(categories);
        }
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    if (!m_loadedAreas.contains(area)) {
        sendFirst(codeOnly(obex::rspPreconditionFailed));
        return;
    }
    const bool isNote = body.contains("BEGIN:VNOTE\r\n");
    if (rest == QLatin1StringView("/luid/.vnt")) {
        const auto note = core::codec::parsePhoneNote(body);
        if (!hasBody || !isNote || !note.hasValue()) {
            sendFirst(codeOnly(obex::rspBadRequest));
            return;
        }
        quint32 luid = 1;
        for (const FakeSyncObject &object : std::as_const(data.objects))
            luid = std::max(luid, object.luid + 1);
        core::codec::Note created = note.value();
        created.luid = luid;
        created.lastModified = nowToTheSecond();
        addNamedCategories(categories, created.categories, body);
        data.metadata = phoneNoteMetadata(categories);
        addObject(area, luid, core::codec::encodeNoteAsPhoneSendsIt(created));
        sendFirst(createdAnswer(luid));
        return;
    }
    bool ok = rest.startsWith(QLatin1StringView("/luid/")) &&
              rest.endsWith(QLatin1StringView(".vnt")) && rest.size() == 6 + luidDigits + 4;
    const quint32 luid = ok ? rest.mid(6, luidDigits).toUInt(&ok, 16) : 0;
    const bool known = ok && std::any_of(data.objects.cbegin(), data.objects.cend(),
                                         [luid](const FakeSyncObject &object) {
                                             return object.luid == luid;
                                         });
    if (!known) {
        sendFirst(codeOnly(obex::rspNotFound));
        return;
    }
    if (!hasBody || body.isEmpty()) {
        removeObject(area, luid);
        sendFirst(codeOnly(obex::rspOk));
        return;
    }
    const auto note = core::codec::parsePhoneNote(body);
    if (!isNote || !note.hasValue()) {
        sendFirst(codeOnly(obex::rspBadRequest));
        return;
    }
    core::codec::Note changed = note.value();
    changed.luid = luid;
    changed.lastModified = nowToTheSecond();
    addNamedCategories(categories, changed.categories, body);
    data.metadata = phoneNoteMetadata(categories);
    editObject(area, luid, core::codec::encodeNoteAsPhoneSendsIt(changed));
    sendFirst(codeOnly(obex::rspOk));
}

bool FakePhone::handleAlarms(const QString &command, const QString &name)
{
    if (name != QLatin1StringView("AT+CALA"))
        return false;
    QByteArray reply = command.toUtf8() + "\r\n";
    if (!m_config.alarms) {
        writeAt(reply + "DUMP: java.lang.Exception: Alarm Provider not found\r\nERROR\r\n");
        return true;
    }
    QStringList &alarms = *m_config.alarms;
    if (command == QLatin1StringView("AT+CALA?")) {
        for (const QString &line : std::as_const(alarms))
            reply += "+CALA: " + line.toUtf8() + "\r\n";
        writeAt(reply + (alarms.isEmpty() ? "ERROR\r\n" : "OK\r\n"));
        return true;
    }
    const QStringList fields = core::codec::splitFields(command.mid(8));
    bool ok = fields.size() >= 5;
    int id = ok ? fields.at(1).trimmed().toInt(&ok) : 0;
    if (!ok) {
        writeAt(reply + "ERROR\r\n");
        return true;
    }
    if (id == 0) {
        for (const QString &line : std::as_const(alarms))
            id = std::max(id, core::codec::splitFields(line).value(1).toInt() + 1);
        id = std::max(id, 1);
    }
    const QString days = fields.size() > 5 ? fields.mid(4, fields.size() - 5).join(u',') : QString();
    const QString line = QStringLiteral("%1,%2,%3,%4,%5,%6")
                             .arg(fields.at(0).trimmed())
                             .arg(id)
                             .arg(fields.at(2).trimmed(), fields.at(3).trimmed(), days,
                                  fields.constLast().trimmed());
    bool replaced = false;
    for (QString &existing : alarms) {
        if (core::codec::splitFields(existing).value(1).toInt() == id) {
            existing = line;
            replaced = true;
        }
    }
    if (!replaced)
        alarms.append(line);
    writeAt(reply + "OK\r\n");
    return true;
}

void FakePhone::handleFileRequest(const ObexPacket &request)
{
    const QString name = request.name.value_or(QString());
    switch (request.code) {
    case obex::opSetPath: {
        if (request.setPathFlags & 0x01) {
            m_currentFolder = parentOf(m_currentFolder);
            sendFirst(codeOnly(obex::rspOk));
        } else if (name.isEmpty()) {
            m_currentFolder.clear();
            sendFirst(codeOnly(obex::rspOk));
        } else if (const QString path = resolve(name); m_folders.contains(path)) {
            m_currentFolder = path;
            sendFirst(codeOnly(obex::rspOk));
        } else if (request.setPathFlags & 0x02) {
            sendFirst(codeOnly(obex::rspNotFound));
        } else {
            addFolder(path);
            m_currentFolder = path;
            sendFirst(codeOnly(obex::rspCreated));
        }
        return;
    }
    case obex::opGetFinal:
        if (request.type == "x-obex/folder-listing") {
            sendBody(folderListing());
        } else if (const auto file = this->file(resolve(name))) {
            sendBody(file->data);
        } else {
            sendFirst(codeOnly(obex::rspNotFound));
        }
        return;
    case obex::opPutFinal: {
        const QString path = resolve(name);
        if (request.bodyKind != ObexPacket::BodyKind::None) {
            QDateTime modified = QDateTime::currentDateTimeUtc();
            if (const auto time = core::codec::parseUtcStamp(QString::fromLatin1(request.time)))
                modified = *time;
            addFile(uniqueName(path), request.body, modified);
            sendFirst(codeOnly(obex::rspOk));
        } else if (m_files.remove(path) > 0) {
            sendFirst(codeOnly(obex::rspOk));
        } else if (m_folders.contains(path)) {
            const QString below = path + u'\\';
            m_folders.removeIf(
                [&](const QString &folder) { return folder == path || folder.startsWith(below); });
            m_files.removeIf([&](const auto &entry) { return entry.key().startsWith(below); });
            sendFirst(codeOnly(obex::rspOk));
        } else {
            sendFirst(codeOnly(obex::rspNotFound));
        }
        return;
    }
    case obex::opMove: {
        const QString from = resolve(name);
        const QString to = resolve(request.targetName.value_or(QString()));
        if (!m_files.contains(from) && !m_folders.contains(from)) {
            sendFirst(codeOnly(obex::rspNotFound));
        } else if (m_files.contains(to) || m_folders.contains(to)) {
            sendFirst(codeOnly(obex::rspConflict));
        } else {
            if (m_files.contains(from)) {
                m_files.insert(to, m_files.take(from));
            } else {
                m_folders.remove(from);
                addFolder(to);
            }
            sendFirst(codeOnly(obex::rspOk));
        }
        return;
    }
    case obex::opSocketGet:
    case obex::opSocketPut: {
        auto transfer = std::make_unique<Transfer>();
        transfer->upload = request.code == obex::opSocketPut;
        if (transfer->upload) {
            if (!request.length && !request.lengthBig) {
                protocolError(QStringLiteral("socket upload without a length"));
                sendFirst(codeOnly(obex::rspBadRequest));
                return;
            }
            transfer->length =
                request.lengthBig.value_or(0) > 0 ? *request.lengthBig : request.length.value_or(0);
            transfer->path = uniqueName(resolve(name));
            transfer->modified = core::codec::parseUtcStamp(QString::fromLatin1(request.time))
                                     .value_or(QDateTime::currentDateTimeUtc());
        } else {
            const auto file = this->file(resolve(name));
            if (!file) {
                sendFirst(codeOnly(obex::rspNotFound));
                return;
            }
            transfer->path = resolve(name);
            transfer->data = file->data;
            transfer->length = quint64(file->data.size());
        }
        transfer->server = new QTcpServer(this);
        if (!transfer->server->listen(QHostAddress::LocalHost, 0)) {
            sendFirst(codeOnly(obex::rspInternalServerError));
            delete transfer->server;
            return;
        }
        Transfer *raw = transfer.get();
        connect(transfer->server, &QTcpServer::newConnection, this, [this, raw]() {
            QTcpSocket *side = raw->server->nextPendingConnection();
            raw->side = side;
            raw->server->close();
            if (!raw->upload) {
                side->write(raw->data);
                side->disconnectFromHost();
                raw->done = true;
                if (raw->statusPending)
                    finishTransfer();
                return;
            }
            connect(side, &QTcpSocket::readyRead, this, [this, raw, side]() {
                raw->data += side->readAll();
                if (quint64(raw->data.size()) >= raw->length && !raw->done) {
                    raw->done = true;
                    addFile(raw->path, raw->data.left(qsizetype(raw->length)), raw->modified);
                    side->disconnectFromHost();
                    if (raw->statusPending)
                        finishTransfer();
                }
            });
        });
        ObexPacket response;
        response.code = obex::rspOk;
        response.length = quint32(transfer->length);
        response.lengthBig = transfer->length;
        response.name = leafOf(transfer->path);
        response.appParameters = "TCP:" + QByteArray::number(transfer->server->serverPort());
        m_transfer = std::move(transfer);
        sendFirst(encoded(response));
        return;
    }
    case obex::opSocketStatus:
        if (!m_transfer) {
            sendFirst(codeOnly(obex::rspPreconditionFailed));
        } else if (m_transfer->done) {
            finishTransfer();
        } else {
            m_transfer->statusPending = true;
        }
        return;
    default:
        break;
    }
    sendFirst(codeOnly(obex::rspNotAcceptable));
}

void FakePhone::finishTransfer()
{
    if (m_transfer && m_transfer->side)
        m_transfer->side->disconnect(this);
    if (m_transfer && m_transfer->server) {
        m_transfer->server->disconnect(this);
        m_transfer->server->deleteLater();
    }
    m_transfer.reset();
    sendPacket(codeOnly(obex::rspOk));
}

QString FakePhone::resolve(const QString &name) const
{
    QString path;
    if (name.startsWith(u'\\'))
        path = name;
    else if (m_currentFolder.isEmpty())
        path = u'\\' + name;
    else
        path = m_currentFolder + u'\\' + name;
    while (path.size() > 1 && path.endsWith(u'\\'))
        path.chop(1);
    return path;
}

QByteArray FakePhone::folderListing() const
{
    QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                                 "<!DOCTYPE folder-listing SYSTEM \"obex-folder-listing.dtd\">\r\n"
                                 "<folder-listing version=\"1.0\">\r\n");
    xml += QStringLiteral("<parent-folder path=\"%1\"/>")
               .arg(m_currentFolder.isEmpty() ? QString() : xmlEscaped(m_currentFolder + u'\\'));
    QStringList folders;
    for (const QString &folder : m_folders) {
        if (parentOf(folder) == m_currentFolder)
            folders.append(leafOf(folder));
    }
    folders.sort();
    for (const QString &folder : std::as_const(folders))
        xml += QStringLiteral("\r\n<folder name=\"%1\"/>").arg(xmlEscaped(folder));
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        if (parentOf(it.key()) != m_currentFolder)
            continue;
        xml += QStringLiteral("\r\n<file name=\"%1\" size=\"%2\" modified=\"%3\"/>")
                   .arg(xmlEscaped(leafOf(it.key())))
                   .arg(it.value().data.size())
                   .arg(core::codec::formatUtcStamp(it.value().modified));
    }
    xml += QStringLiteral("\r\n</folder-listing>");
    return xml.toUtf8();
}

QString FakePhone::uniqueName(const QString &path) const
{
    if (!m_files.contains(path))
        return path;
    const QString folder = parentOf(path);
    const QString leaf = leafOf(path);
    const qsizetype dot = leaf.lastIndexOf(u'.');
    const QString stem = dot <= 0 ? leaf : leaf.left(dot);
    const QString suffix = dot <= 0 ? QString() : leaf.mid(dot);
    for (int i = 1;; ++i) {
        const QString candidate =
            folder + u'\\' + QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(suffix);
        if (!m_files.contains(candidate))
            return candidate;
    }
}

void FakePhone::sendPacket(const QByteArray &packet)
{
    if (m_socket) {
        m_socket->write(packet);
        m_socket->flush();
    }
}

void FakePhone::sendFirst(const QByteArray &packet)
{
    for (int i = 0; i < m_config.paddingBeforeReply; ++i) {
        sendPacket(QByteArray::fromHex("900003"));
        m_discard += 3;
    }
    sendPacket(packet);
}

void FakePhone::sendBody(const QByteArray &body)
{
    const qsizetype chunk = m_maxPacket - chunkOverhead;
    ObexPacket packet;
    if (body.size() <= chunk) {
        packet.code = obex::rspOk;
        packet.bodyKind = ObexPacket::BodyKind::EndOfBody;
        packet.body = body;
        sendFirst(encoded(packet));
        endOfStream();
        return;
    }
    packet.code = obex::rspContinue;
    packet.bodyKind = ObexPacket::BodyKind::Body;
    packet.body = body.left(chunk);
    m_pendingStream = body.mid(chunk);
    sendFirst(encoded(packet));
}

void FakePhone::sendNextChunk()
{
    const qsizetype chunk = m_maxPacket - chunkOverhead;
    ObexPacket packet;
    if (m_pendingStream.size() <= chunk) {
        packet.code = obex::rspOk;
        packet.bodyKind = ObexPacket::BodyKind::EndOfBody;
        packet.body = m_pendingStream;
        m_pendingStream.clear();
    } else {
        packet.code = obex::rspContinue;
        packet.bodyKind = ObexPacket::BodyKind::Body;
        packet.body = m_pendingStream.left(chunk);
        m_pendingStream.remove(0, chunk);
    }
    sendPacket(encoded(packet));
    if (packet.code == obex::rspOk)
        endOfStream();
}

void FakePhone::endOfStream()
{
    if (!m_dropAfterStream || !m_socket)
        return;
    m_dropAfterStream = false;
    m_socket->disconnectFromHost();
}

void FakePhone::recordChange(const QString &area, char type, quint32 luid)
{
    FakeSyncArea &data = m_areas[area];
    data.changes.append({data.nextChangeId++, type, luid});
}

void FakePhone::protocolError(const QString &what)
{
    m_protocolErrors.append(what);
}

} // namespace qpm::test
