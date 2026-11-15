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
#include <utility>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace qpm::core::protocol {
struct ObexPacket;
}

namespace qpm::core::codec {
struct PhoneNotification;
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

/// One row of the fake phone's call log.
struct FakeCall
{
    quint32 id = 0;
    QString number;
    QString name;
    /// "DC" dialled, "RC" received or "MC" missed.
    QString storage = QStringLiteral("DC");
    /// "yy/MM/dd,HH:mm:ss" in local time.
    QString time;
    qint64 duration = 0;
    /// "128", or "SIM<n>" on a phone with two SIM cards.
    QString addressType = QStringLiteral("128");
};

/// A text message the fake phone was asked to send; nothing is ever sent.
struct FakeSentMessage
{
    QString number;
    QString text;
    /// The third field of AT+CMGS: 0 without, 1 with delivery report, 2 ping.
    int mode = 0;
    /// -1 when the command names no SIM card.
    int simSlot = -1;
};

/// A file in the fake phone's storage.
struct FakeFile
{
    QByteArray data;
    QDateTime modified;
};

/// An app installed on the fake phone.
struct FakeApp
{
    QString package;
    QString label;
    /// Empty: the app has no version name, which the phone writes as "null".
    QString version;
    bool userApp = true;
    /// Path of the APK on the phone such as "/data/app/a/base.apk"; empty when the phone gives
    /// none.
    QString sourcePath;
    QDateTime installed;
    /// The APK, kept as a file below "\phone" at sourcePath.
    QByteArray apk;
    /// PNG of the icon; empty when the phone cannot give one.
    QByteArray icon;
    /// The phone has a window to open for the app.
    bool launchable = true;
};

/// A notification shown on the fake phone.
struct FakeNotification
{
    int id = 0;
    QString tag;
    QString package = QStringLiteral("org.example.chat");
    QString appLabel = QStringLiteral("Chat");
    QString title;
    QString text;
    QStringList actions;
    QDateTime posted;
    /// Android notification flags; 0x02 marks an ongoing one.
    int flags = 0;
    /// A text for the status bar; without it the phone reports a new one only in detail mode.
    QString ticker = QStringLiteral("ticker");
    /// JPEG of its picture; empty for none.
    QByteArray picture;

    [[nodiscard]] QString key() const;
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
    /**
     * Number of connections closed right after they are taken, before any
     * answer, as adb does with a forwarded port while the client behind it
     * does not listen yet.
     */
    int closeFirstConnections = 0;
    /// AT command -> chunks written verbatim, each with its own write, echo included.
    QHash<QString, QList<QByteArray>> scriptedReplies;
    /// AT commands that get no answer at all.
    QSet<QString> unanswered;
    /// Period of the unsolicited "*ALIVE" line in AT mode; zero sends none.
    std::chrono::milliseconds alivePeriod{0};
    /// Names of OBEX requests that get no answer at all.
    QSet<QString> unansweredObex;
    /// Response code sent to every OBEX CONNECT instead of accepting it; zero accepts.
    quint8 connectRefusal = 0;
    /// Bytes written right behind the answer to DISCONNECT, in the same write.
    QByteArray afterDisconnect;
    /**
     * A line such as "+CMTI: \"ME\",3", without its line end, that arrives
     * late, inside the next OBEX session, as from a client that queued it in
     * AT mode just before the switch. Written once, with CRLF and a write of
     * its own; the field is emptied then.
     */
    QByteArray lateLine;
    /**
     * Where lateLine goes: -1 right behind CONNECT, otherwise right before the
     * OBEX answer packet with this index in the session, counted from 0 for
     * the answer to the OBEX CONNECT.
     */
    int lateLineBeforeAnswer = -1;
    /// Name of an OBEX GET; the connection is closed right after its last answer packet.
    QString dropAfterGet;
    /// Answer every call log command with ERROR, as a client without the permission does.
    bool callLogDenied = false;
    /// Refuse AT+CPBW and AT+CPBD like a client that may read the call log but not change it.
    bool callLogReadOnly = false;
    /// Answer to AT+CMGS=?; without "TXT" the phone cannot send.
    QString smsCapabilities = QStringLiteral("TXTv2,PING");
    /// Refuse AT+CMGD, AT+CMGE and AT+CMGW like the client 4.15, which lacks WRITE_SMS.
    bool smsWriteDenied = true;
    /// Final line for every AT+CMGS send instead of OK, for example "+CMS ERROR: 38".
    QByteArray smsSendFailure;
    /// Answer every request below "telecom/cal" with "unauthorized", as a client without
    /// the calendar permission does.
    bool calendarDenied = false;
    /**
     * The "+CALA:" lines of the clock app without the prefix, for example
     * `"07:30",1,0,"Wake up","1,2,3,4,5",1`. Nothing means a phone whose clock
     * app the client cannot reach: AT+CALA then fails.
     */
    std::optional<QStringList> alarms;
    /// The EXIF orientation sent with every thumbnail, 0 to 8.
    int thumbnailOrientation = 0;
    /**
     * Delay before the phone reads a socket upload. Data that the desktop
     * sends and then closes its side on before the phone read it is lost,
     * as over adb forwarding, and the phone keeps an empty file.
     */
    std::chrono::milliseconds uploadReadDelay{0};
    /**
     * Number of socket uploads whose channel the phone opens too late. The
     * connection is taken and closed again unread, as adb forwarding does.
     * SOCKET STATUS then answers "request timeout" and keeps the empty file,
     * as the client does after ten seconds.
     */
    int missedUploadChannels = 0;
    /**
     * Number of connections to the channel of a socket download, and to the
     * thumbnail channel, that are taken and closed again unread before one
     * is served. That is how adb forwarding handles a connection that comes
     * before the phone listens; the phone then keeps listening.
     */
    int droppedDownloadConnections = 0;
    int droppedThumbnailConnections = 0;

    /// How the user of the phone answers the question whether to allow the screen capture.
    enum class ScreenConsent { Allow, Decline, NoAnswer };
    ScreenConsent screenConsent = ScreenConsent::Allow;
    /// The accessibility service of the client is on, so the phone takes keys and text.
    bool accessibilityService = true;
    /// Sent on the screen channel in answer to every request there, verbatim; nothing when empty.
    QByteArray screenFrame;
    /// Connections to the screen channel that are taken and closed again unread before one is
    /// served.
    int droppedScreenConnections = 0;

    /// Whether the client may read the notifications, as the user set it on the phone.
    enum class NotificationAccess { Granted, Disabled, NotBound, NotSupported };
    NotificationAccess notificationAccess = NotificationAccess::Granted;
    /// The text on the clipboard of the phone.
    QString clipboard;
    /**
     * AT*CLIPBOARD? is answered only after this delay and without an echo of
     * its own, as the client does when Android lets it read the clipboard
     * only through a window of its app; zero answers at once.
     */
    std::chrono::milliseconds clipboardReadDelay{0};
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
 * Contacts written to "telecom/pb" are treated like the client does: a new
 * contact gets a LUID and keys for its entries, a change touches only the
 * entries it names, and an empty body deletes the contact. The call log
 * answers AT+CPBS, AT+CPBR, AT+CPBW and AT+CPBD like the client, newest entry
 * first. A dial command is answered with OK and only recorded.
 *
 * Messages live in the area "msg". AT+CMGS in text mode is recorded and
 * answered like the client, and the message then appears as a sent one;
 * AT+CMGD, AT+CMGE and AT+CMGW are refused like the client 4.15 unless
 * FakePhoneConfig::smsWriteDenied is false. A PUT below "telecom/msg" takes
 * scheduled messages only, gives them a LUID with bit 0x20000000 and keeps
 * them in the form the client sends them back in.
 *
 * The calendar and the notes take changes only after "initsync" was read in
 * the same OBEX session, as the client does. A new event gets a LUID with bit
 * 0x10000000 and goes to the calendar its X-CALENDAR names, a task or a note
 * a small LUID; both come back in the form the client sends. A change of an
 * event keeps its calendar. Categories named in a task or a note that the
 * phone does not know yet are added to its metadata, and metadata.xml takes
 * the choice of calendars and changes of categories. AT+CALA answers from
 * FakePhoneConfig::alarms.
 *
 * The apps answer AT*EJAVA like the client, the list in blocks of ten lines.
 * Starting an app and asking for its removal are only recorded; the removal
 * and an installation happen when the test confirms them, as the user does on
 * the phone, and are then reported with an unsolicited line. The icons come
 * from "\appicon\<package>\<size>", and an APK written to the root of the
 * file browser is taken as an installation request instead of a file.
 *
 * The file browser lists a folder, and when asked for the subfolders also
 * every folder below it, one block each. Uploads to a name that exists get a free
 * name like "a (1).txt", as on the client. ABORT drops a running socket
 * transfer, so nothing of an aborted upload is kept. A PUT of
 * "\getthumbnail" answers with the side channel and sends there the data of
 * each listed file with a picture or video extension as its thumbnail, in
 * the stream form of the client.
 *
 * The screen capture follows FakePhoneConfig::screenConsent. Once allowed,
 * the screen channel listens on a port of its own, see screenPort(), and
 * answers each request there with FakePhoneConfig::screenFrame. Keys and
 * text need FakePhoneConfig::accessibilityService and an Android version of
 * at least 7, the capture at least 5; touches are always taken, as the client
 * does, and only recorded.
 *
 * Notifications answer AT*NOTIR, AT*NOTIFY, AT*NOTIC and AT*NOTIACTION like
 * the client, with the refusals FakePhoneConfig::notificationAccess picks, and
 * their pictures come from "\notificationimage\<package>\<id>\<w>\<h>". The
 * clipboard answers AT*CLIPBOARD and AT*CLIPWATCH. Calls are reported with
 * "*ECAV" through setCallState(); ATA takes a ringing call, ATH and AT+CHUP
 * end one, and AT+CMUT and AT*SESP are recorded.
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
    /// Name and body of every PUT on IRMC-SYNC, in order; a delete has an empty body.
    [[nodiscard]] QList<std::pair<QString, QByteArray>> syncPuts() const { return m_syncPuts; }
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

    void setCalls(const QList<FakeCall> &calls);
    [[nodiscard]] QList<FakeCall> calls() const { return m_calls; }

    /// Every AT+CMGS send so far, in order.
    [[nodiscard]] QList<FakeSentMessage> sentMessages() const { return m_sentMessages; }

    /// Paths use backslashes and start at a storage root, e.g. "\sdcard\DCIM".
    void addFolder(const QString &path);
    void addFile(const QString &path, const QByteArray &data, const QDateTime &modified);
    [[nodiscard]] std::optional<FakeFile> file(const QString &path) const;
    [[nodiscard]] bool hasFolder(const QString &path) const { return m_folders.contains(path); }
    /// Paths of all files, sorted.
    [[nodiscard]] QStringList filePaths() const { return m_files.keys(); }
    /// The body of every PUT of "\getthumbnail", in order.
    [[nodiscard]] QList<QByteArray> thumbnailRequests() const { return m_thumbnailRequests; }

    /// Replaces the apps; the APK of each is kept as a file at its path below "\phone".
    void setApps(const QList<FakeApp> &apps);
    [[nodiscard]] QList<FakeApp> apps() const { return m_apps; }
    /// The packages of every start request, in order.
    [[nodiscard]] QStringList startRequests() const { return m_startRequests; }
    /// The packages of every removal request, in order.
    [[nodiscard]] QStringList uninstallRequests() const { return m_uninstallRequests; }
    /// Name and content of every APK written to the root of the file browser.
    [[nodiscard]] QList<std::pair<QString, QByteArray>> installRequests() const
    {
        return m_installRequests;
    }
    /// Removes the app as the user's confirmation on the phone does, and reports it.
    void confirmUninstall(const QString &package);
    /// Installs or updates the app as the user's confirmation on the phone does, and reports it.
    void confirmInstall(const FakeApp &app);

    /**
     * Answers the next OBEX request other than CONNECT and DISCONNECT with
     * `code`, and with `appParameters` as App-Parameters when not empty.
     */
    void setFailNext(quint8 code, const QByteArray &appParameters = {})
    {
        m_failNext = code;
        m_failNextAppParameters = appParameters;
    }

    /// Port of the screen channel while it listens or is connected; 0 otherwise.
    [[nodiscard]] quint16 screenPort() const;
    [[nodiscard]] bool isScreenConnected() const;
    /// The requests received on the screen channel, in order.
    [[nodiscard]] QList<QByteArray> screenRequests() const { return m_screenRequests; }
    /// Writes `bytes` to the screen channel as they are.
    void sendScreenData(const QByteArray &bytes);
    /// Closes the screen channel, as the phone does when the capture ends.
    void closeScreen();

    /// Writes an unsolicited line in AT mode.
    void sendUnsolicited(const QByteArray &line);

    /// Replaces the notifications without reporting anything.
    void setNotifications(const QList<FakeNotification> &notifications);
    [[nodiscard]] QList<FakeNotification> notifications() const { return m_notifications; }
    /**
     * Shows a notification, replacing one with the same key, and reports it
     * like the client: when it has a ticker text, or in detail mode.
     */
    void postNotification(const FakeNotification &notification);
    /// Removes the notification, reporting it in detail mode like the client.
    void removeNotification(const QString &key);
    /// The detail mode the desktop asked for with AT*NOTIFY=2.
    [[nodiscard]] bool notificationsDetailed() const { return m_notificationsDetailed; }
    /// Key and action title of every AT*NOTIACTION, in order; an empty title is the tap.
    [[nodiscard]] QList<std::pair<QString, QString>> notificationActions() const
    {
        return m_notificationActions;
    }
    /// Whether AT*CLIPWATCH=1 is in effect.
    [[nodiscard]] bool clipboardWatched() const { return m_clipboardWatched; }
    /// Changes the clipboard as the user of the phone does; reported while watched.
    void copyOnPhone(const QString &text);

    /// The state of the call as the phone tracks it: 0 none, 1 dialling, 3 going on, 6 ringing.
    [[nodiscard]] int callState() const { return m_callState; }
    /// Makes a call ring, go on or end, reported like the client with "*ECAV".
    void setCallState(int state, const QString &number);
    /// The microphone and speaker switches as last set by the desktop.
    [[nodiscard]] bool callMuted() const { return m_callMuted; }
    [[nodiscard]] bool speakerOn() const { return m_speakerOn; }

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
    /// Answers a call log command; returns false for any other command.
    bool handleCallLog(const QString &command, const QString &name);
    /// Answers a text message command; returns false for any other command.
    bool handleMessaging(const QString &command, const QString &name);
    void handleMessagePut(const QString &rest, const QByteArray &body, bool hasBody);
    void writeAt(const QByteArray &bytes);
    void processObex();
    void handleObex(const QByteArray &bytes);
    void handleSyncGet(const QString &name);
    void handleSyncPut(const QString &name, const QByteArray &body, bool hasBody);
    void handleContactPut(const QString &rest, const QByteArray &body);
    void handleCalendarPut(const QString &rest, const QByteArray &body, bool hasBody);
    void handleNotePut(const QString &rest, const QByteArray &body, bool hasBody);
    /// Answers AT+CALA; returns false for any other command.
    bool handleAlarms(const QString &command, const QString &name);
    /// Answers AT*EJAVA; returns false for any other command.
    bool handleApps(const QString &command, const QString &name);
    /// Answers the screen and input commands; returns false for any other command.
    bool handleScreen(const QString &command, const QString &name);
    /// Answers the notification commands; returns false for any other command.
    bool handleNotifications(const QString &command, const QString &name);
    /// Answers the clipboard commands; returns false for any other command.
    bool handleClipboard(const QString &command, const QString &name);
    /// Answers the call commands; returns false for any other command.
    bool handleCalls(const QString &command, const QString &name);
    [[nodiscard]] QByteArray notificationRecord(const FakeNotification &notification) const;
    void openScreenChannel();
    [[nodiscard]] QByteArray appLine(const FakeApp &app) const;
    /// Keeps what an upload delivered: a file, or an installation request at the root.
    void keepUpload(const Transfer &transfer, const QByteArray &data);
    void handleFileRequest(const qpm::core::protocol::ObexPacket &request);
    void sendPacket(const QByteArray &packet);
    void sendFirst(const QByteArray &packet);
    void sendBody(const QByteArray &body);
    void sendNextChunk();
    void endOfStream();
    void recordChange(const QString &area, char type, quint32 luid);
    void finishTransfer();
    /// Ends the socket transfer without an answer, as ABORT does.
    void dropTransfer();
    void handleThumbnailRequest(const QByteArray &body);
    [[nodiscard]] QString resolve(const QString &name) const;
    [[nodiscard]] QByteArray folderListing(bool withSubfolders) const;
    [[nodiscard]] QString folderListingBlock(const QString &folder) const;
    [[nodiscard]] QString uniqueName(const QString &path) const;
    void protocolError(const QString &what);

    FakePhoneConfig m_config;
    QTcpServer *m_server = nullptr;
    QTimer *m_aliveTimer = nullptr;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_buffer;
    QStringList m_received;
    QStringList m_protocolErrors;
    QList<std::pair<QString, QByteArray>> m_syncPuts;
    /// Next key for a contact entry the fake phone creates.
    quint64 m_nextDataId = 1000;
    QHash<QString, FakeSyncArea> m_areas;
    QList<FakeCall> m_calls;
    QString m_callStorage = QStringLiteral("ALL");
    quint32 m_nextCallId = 5000;
    QList<FakeSentMessage> m_sentMessages;
    QList<FakeApp> m_apps;
    QStringList m_startRequests;
    QStringList m_uninstallRequests;
    QList<std::pair<QString, QByteArray>> m_installRequests;
    QMap<QString, FakeFile> m_files;
    QSet<QString> m_folders;
    std::optional<quint8> m_failNext;
    QByteArray m_failNextAppParameters;
    int m_wrongPins = 0;
    bool m_authenticated = false;
    bool m_obexMode = false;
    bool m_dropAfterStream = false;
    QByteArray m_target;
    /// Areas whose "initsync" was read in the running OBEX session.
    QSet<QString> m_loadedAreas;
    QString m_currentFolder;
    quint16 m_maxPacket = 0xFFFF;
    qsizetype m_discard = 0;
    /// OBEX answer packets sent in the running session.
    int m_answersInSession = 0;
    QByteArray m_pendingStream;
    std::unique_ptr<Transfer> m_transfer;
    QList<QByteArray> m_thumbnailRequests;
    QTcpServer *m_thumbnailServer = nullptr;
    QTcpServer *m_screenServer = nullptr;
    QPointer<QTcpSocket> m_screenSocket;
    QList<QByteArray> m_screenRequests;
    QList<FakeNotification> m_notifications;
    QList<std::pair<QString, QString>> m_notificationActions;
    bool m_notificationsDetailed = false;
    bool m_clipboardWatched = false;
    int m_callState = 0;
    QString m_callNumber;
    bool m_callMuted = false;
    bool m_speakerOn = false;
};

} // namespace qpm::test
