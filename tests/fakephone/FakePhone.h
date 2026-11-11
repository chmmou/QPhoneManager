#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <chrono>
#include <memory>
#include <optional>

class QTcpServer;
class QTcpSocket;

namespace qpm::core::protocol {
struct ObexPacket;
}

namespace qpm::test {

/// One object of a sync area, with its complete text including X-IRMC-LUID.
struct FakeSyncObject
{
    quint32 luid = 0;
    QByteArray data;
};

/// One row of the change table of a sync area.
struct FakeChange
{
    quint64 id = 0;
    /// 'N' new, 'E' edited, 'D' deleted.
    char type = 'N';
    quint32 luid = 0;
};

/// The data of one sync area ("pb", "cal", "notes" or "msg").
struct FakeSyncArea
{
    QList<FakeSyncObject> objects;
    QByteArray metadata;
    QList<FakeChange> changes;
    quint64 nextChangeId = 1;
};

/// A file in the fake phone's storage.
struct FakeFile
{
    QByteArray data;
    QDateTime modified;
};

/// Behaviour of the fake phone; tests change it before or during a session.
struct FakePhoneConfig
{
    QString deviceId = QStringLiteral("8f3c2a10-0000-4000-8000-000000000001");
    QString model = QStringLiteral("Example Phone X1");
    QString manufacturer = QStringLiteral("Example");
    /// Empty: no PIN required.
    QString pin;
    int androidApi = 34;
    QString clientVersion = QStringLiteral("4.15");
    /// Keep-alive packets 90 00 03 sent before every first OBEX answer.
    int paddingBeforeReply = 0;
    /// Progress packets sent during each initsync.
    int initsyncProgress = 0;
    /// Delay before each AT answer.
    std::chrono::milliseconds replyDelay{0};
    /// Answer every new connection with ALREADY CONNECTED and close it.
    bool rejectAsAlreadyConnected = false;
    /// AT command -> chunks written verbatim, each with its own write, echo included.
    QHash<QString, QList<QByteArray>> scriptedReplies;
    /// AT commands that get no answer at all.
    QSet<QString> unanswered;
};

/**
 * A stand-in for the phone client on a local TCP port, for tests without a
 * device. It answers the AT session with PIN, the OBEX targets IRMC-SYNC and
 * Filebrowser with chunking, keep-alives and the change log, and the side
 * channel used by socket transfers. Like the client it serves one connection
 * at a time; a new one replaces the old with CONNECTION CHANGE.
 *
 * It records every AT command and every OBEX request it receives, and every
 * violation of the protocol rules a client must keep, such as two commands in
 * one write or a wrong answer to a keep-alive.
 *
 * Never sends text messages or places calls: there is nothing behind it.
 */
class FakePhone : public QObject
{
    Q_OBJECT

public:
    explicit FakePhone(QObject *parent = nullptr);
    ~FakePhone() override;

    FakePhoneConfig &config() { return m_config; }

    /// Listens on a free port of the loopback address; returns false on failure.
    [[nodiscard]] bool listen();
    [[nodiscard]] quint16 port() const;

    /// Connects to the desktop as the client does after the QR code is scanned.
    void dialOut(const QHostAddress &address, quint16 port);

    [[nodiscard]] bool isConnected() const;

    /// AT commands as received and OBEX requests as "OBEX <op> <name>".
    [[nodiscard]] QStringList receivedCommands() const { return m_received; }
    [[nodiscard]] QStringList protocolErrors() const { return m_protocolErrors; }
    void clearLog();

    /// Replaces all objects of `area` without recording changes.
    void setObjects(const QString &area, const QList<FakeSyncObject> &objects);
    void setMetadata(const QString &area, const QByteArray &metadata);
    [[nodiscard]] const FakeSyncArea &area(const QString &area) const;

    /// Change made on the phone: recorded as N, E or D in the change table.
    void addObject(const QString &area, quint32 luid, const QByteArray &data);
    void editObject(const QString &area, quint32 luid, const QByteArray &data);
    void removeObject(const QString &area, quint32 luid);
    /// Drops the change table, as the client does with old rows.
    void forgetChanges(const QString &area);
    [[nodiscard]] quint64 changeCounter(const QString &area) const;

    /// Paths use backslashes and start at a storage root, e.g. "\sdcard\DCIM".
    void addFolder(const QString &path);
    void addFile(const QString &path, const QByteArray &data, const QDateTime &modified);
    [[nodiscard]] std::optional<FakeFile> file(const QString &path) const;

    /// Answers the next OBEX request other than CONNECT and DISCONNECT with `code`.
    void setFailNext(quint8 code) { m_failNext = code; }

    /// Writes an unsolicited line in AT mode.
    void sendUnsolicited(const QByteArray &line);

    /// Drops the connection without a word.
    void dropConnection();

Q_SIGNALS:
    void commandReceived(const QString &command);
    void connectionAccepted();

private:
    struct Transfer;

    void attach(QTcpSocket *socket);
    void onReadyRead();
    void processAt();
    void handleAt(const QString &command);
    void writeAt(const QByteArray &bytes);
    void processObex();
    void handleObex(const QByteArray &bytes);
    void handleSyncGet(const QString &name);
    void handleSyncPut(const QString &name, const QByteArray &body, bool hasBody);
    void handleFileRequest(const qpm::core::protocol::ObexPacket &request);
    void sendPacket(const QByteArray &packet);
    void sendFirst(const QByteArray &packet);
    void sendBody(const QByteArray &body);
    void sendNextChunk();
    void recordChange(const QString &area, char type, quint32 luid);
    void finishTransfer();
    [[nodiscard]] QString resolve(const QString &name) const;
    [[nodiscard]] QByteArray folderListing() const;
    [[nodiscard]] QString uniqueName(const QString &path) const;
    void protocolError(const QString &what);

    FakePhoneConfig m_config;
    QTcpServer *m_server = nullptr;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_buffer;
    QStringList m_received;
    QStringList m_protocolErrors;
    QHash<QString, FakeSyncArea> m_areas;
    QMap<QString, FakeFile> m_files;
    QSet<QString> m_folders;
    std::optional<quint8> m_failNext;
    int m_wrongPins = 0;
    bool m_authenticated = false;
    bool m_obexMode = false;
    QByteArray m_target;
    QString m_currentFolder;
    quint16 m_maxPacket = 0xFFFF;
    qsizetype m_discard = 0;
    QByteArray m_pendingStream;
    std::unique_ptr<Transfer> m_transfer;
};

} // namespace qpm::test
