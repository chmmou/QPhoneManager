#include "CommandGuard.h"

#include "core/codec/MessageExport.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/MessagesService.h"
#include "core/store/MessageStore.h"
#include "core/store/ProfileStore.h"

#include "WaitFor.h"

#include <QDir>
#include <QPointer>
#include <QProcess>
#include <QRandomGenerator>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

using namespace qpm::core;
using namespace qpm::core::codec;
using namespace qpm::core::protocol;
using qpm::core::service::MessagesService;
using qpm::core::service::SendReport;
using qpm::core::store::MessageChanges;
using qpm::core::store::MessageStore;
using qpm::core::store::ProfileStore;
using qpm::core::store::StoredMessage;
using qpm::test::CommandGuard;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int syncTimeoutMs = 240000;
constexpr int noticeTimeoutMs = 30000;
constexpr int connectTries = 10;
constexpr int sentLookups = 5;

QString sixDigits()
{
    return QString::number(QRandomGenerator::global()->bounded(100000, 999999));
}

// A store that lives as long as one test function, so that nothing read from
// a phone stays on this computer.
struct ScratchStore
{
    QTemporaryDir dir;
    std::unique_ptr<ProfileStore> profile;
    std::shared_ptr<MessageStore> messages;

    [[nodiscard]] bool open()
    {
        if (!dir.isValid())
            return false;
        profile = std::make_unique<ProfileStore>(dir.path(), QStringLiteral("device"));
        if (!profile->open().hasValue())
            return false;
        messages = std::make_shared<MessageStore>(profile.get());
        return true;
    }
};

QList<StoredMessage> everything(const MessageStore &store)
{
    QList<StoredMessage> all = store.allMessages().value();
    all.append(store.scheduledMessages().value());
    return all;
}

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling. On the emulator ("emulator-..." serials) the test lets the emulator
// receive a text message of its own through its console, then reads, searches,
// archives and exports it and asks the phone app to delete it and change its
// read state, which the app 4.15 refuses. On any other phone, which may be
// someone's own, it only reads the messages and checks their count and form;
// no content, number or name is printed or kept beyond the test. Exactly one
// test sends one real text message, and only on a phone that is not an
// emulator, with QPM_TEST_SEND_SMS=1, to the number in QPM_TEST_SMS_TO.
class TestDeviceMessages : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceMessages(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void messagesAreReadAndWellFormed();
    void receivedMessageIsReadSearchedArchivedAndExported();
    void refusalsOfThePhoneAppAreReported();
    void oneRealMessageIsSentAndFoundAsSent();

private:
    [[nodiscard]] bool connectToClient();
    [[nodiscard]] Result<MessageChanges> syncInto(MessagesService &service);
    [[nodiscard]] bool adb(const QStringList &arguments);
    [[nodiscard]] bool receiveOnTheEmulator(const QString &number, const QString &text);
    [[nodiscard]] bool removeTestMessagesFromTheEmulator();

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    QPointer<CommandGuard> m_guard;
};

void TestDeviceMessages::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    qInfo() << (m_emulator ? "Target: emulator, received messages of its own"
                           : "Target: phone, read only");

    m_adb = std::make_unique<AdbBridge>(m_adbPath);
    const QFuture<Result<void>> server = m_adb->startServer();
    QVERIFY(waitFor(server, readyTimeoutMs));
    QVERIFY2(server.result().hasValue(), "adb start-server failed");
    const QFuture<Result<quint16>> forward = m_adb->forward(m_serial, clientPort);
    QVERIFY(waitFor(forward, readyTimeoutMs));
    // The error text of adb can name the device, so only the code is shown.
    QVERIFY2(forward.result().hasValue(),
             qPrintable(QStringLiteral("adb forward failed with code %1")
                            .arg(int(forward.result().error().code()))));
    m_localPort = forward.result().value();
    const QFuture<Result<void>> client = m_adb->startClient(m_serial);
    QVERIFY(waitFor(client, readyTimeoutMs));
    QVERIFY2(client.result().hasValue(),
             qPrintable(QStringLiteral("Starting the client failed with code %1")
                            .arg(int(client.result().error().code()))));
    if (m_emulator && !removeTestMessagesFromTheEmulator())
        qInfo() << "Messages of an earlier run could not be removed from the emulator";
}

void TestDeviceMessages::cleanupTestCase()
{
    if (m_emulator && m_adb && !removeTestMessagesFromTheEmulator())
        qInfo() << "The test messages could not be removed from the emulator";
    if (!m_adb || m_localPort == 0)
        return;
    const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, m_localPort);
    QVERIFY(waitFor(removed, readyTimeoutMs));
}

void TestDeviceMessages::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    QVERIFY(connectToClient());
}

void TestDeviceMessages::cleanup()
{
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
}

bool TestDeviceMessages::connectToClient()
{
    for (int attempt = 0; attempt < connectTries; ++attempt) {
        auto guard = std::make_unique<CommandGuard>(
            std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_localPort));
        m_guard = guard.get();
        m_connection->connectTransport(std::move(guard));
        const bool settled = QTest::qWaitFor(
            [this]() {
                return m_connection->state() == State::Ready ||
                       m_connection->state() == State::Lost;
            },
            readyTimeoutMs);
        if (settled && m_connection->state() == State::Ready)
            return true;
        // Only spaces out the tries; no result depends on how long it is.
        QTest::qWait(1000);
    }
    return false;
}

Result<MessageChanges> TestDeviceMessages::syncInto(MessagesService &service)
{
    const QFuture<Result<MessageChanges>> synced = service.sync();
    if (!waitFor(synced, syncTimeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("sync did not finish"));
    return synced.result();
}

bool TestDeviceMessages::adb(const QStringList &arguments)
{
    // Changing data behind the client's back is for the emulator only.
    if (!m_emulator)
        return false;
    QProcess process;
    process.start(m_adbPath, QStringList{QStringLiteral("-s"), m_serial} + arguments);
    return process.waitForFinished(readyTimeoutMs) &&
           process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool TestDeviceMessages::receiveOnTheEmulator(const QString &number, const QString &text)
{
    // The console command only makes the emulator receive a message; nothing is sent.
    return adb(
        QStringList{QStringLiteral("emu"), QStringLiteral("sms"), QStringLiteral("send"), number} +
        text.split(u' '));
}

bool TestDeviceMessages::removeTestMessagesFromTheEmulator()
{
    // Android ignores a deletion of the shell in the message store until the
    // shell may write messages; without that the delete succeeds and removes nothing.
    return adb({QStringLiteral("shell"), QStringLiteral("appops"), QStringLiteral("set"),
                QStringLiteral("com.android.shell"), QStringLiteral("WRITE_SMS"),
                QStringLiteral("allow")}) &&
           adb({QStringLiteral("shell"), QStringLiteral("content"), QStringLiteral("delete"),
                QStringLiteral("--uri"), QStringLiteral("content://sms"), QStringLiteral("--where"),
                QStringLiteral("\"body LIKE 'QPM Eingang %'\"")});
}

void TestDeviceMessages::messagesAreReadAndWellFormed()
{
    ScratchStore scratch;
    QVERIFY(scratch.open());
    MessagesService service(m_connection.get());
    service.setStore(scratch.messages);

    const Result<MessageChanges> first = syncInto(service);

    QVERIFY2(first.hasValue(), qPrintable(QStringLiteral("the messages could not be read, code %1")
                                              .arg(int(first.error().code()))));
    const QList<StoredMessage> all = everything(*scratch.messages);
    int sms = 0;
    int mms = 0;
    int scheduled = 0;
    int received = 0;
    int sent = 0;
    int partsHere = 0;
    int partsOnPhone = 0;
    QSet<quint32> luids;
    for (const StoredMessage &message : all) {
        QVERIFY2(message.luid > 0, "a message came without number");
        QVERIFY2(!luids.contains(message.luid), "a message came twice");
        luids.insert(message.luid);
        QVERIFY2(message.isOnPhone(), "a message got a number of this computer");
        switch (message.kind) {
        case MessageKind::Sms:
            ++sms;
            break;
        case MessageKind::Mms:
            ++mms;
            QVERIFY2(kindOfLuid(message.luid) == MessageKind::Mms,
                     "a multimedia message has an SMS number");
            break;
        case MessageKind::Scheduled:
            ++scheduled;
            QVERIFY2(!message.recipients.isEmpty(), "a scheduled message has no recipient");
            break;
        }
        if (message.box == MessageBox::Inbox) {
            ++received;
            QVERIFY2(message.date.isValid(), "a received message came without time");
        } else if (message.box == MessageBox::Sent) {
            ++sent;
            QVERIFY2(message.date.isValid(), "a sent message came without time");
        }
        for (const store::StoredAttachment &attachment : message.attachments) {
            if (attachment.blob.isEmpty())
                ++partsOnPhone;
            else
                ++partsHere;
        }
    }
    qInfo() << "Messages read, text:" << sms << "multimedia:" << mms << "scheduled:" << scheduled
            << "received:" << received << "sent:" << sent;
    qInfo() << "Conversations:" << scratch.messages->conversations().value().size()
            << "parts here:" << partsHere << "parts left on the phone:" << partsOnPhone
            << "unreadable:" << first.value().unreadable;
    QCOMPARE(first.value().unreadable, 0);
    QCOMPARE(first.value().added, int(all.size()));

    const Result<MessageChanges> again = syncInto(service);
    QVERIFY2(again.hasValue(),
             qPrintable(
                 QStringLiteral("the second read failed, code %1").arg(int(again.error().code()))));
    qInfo() << "Second read, new:" << again.value().added << "changed:" << again.value().changed
            << "deleted:" << again.value().removed;
}

void TestDeviceMessages::receivedMessageIsReadSearchedArchivedAndExported()
{
    if (!m_emulator)
        QSKIP("Messages are made to arrive on the emulator only");
    ScratchStore scratch;
    QVERIFY(scratch.open());
    MessagesService service(m_connection.get());
    service.setStore(scratch.messages);
    QVERIFY(syncInto(service).hasValue());
    QSignalSpy arrived(&service, &MessagesService::messagesArrived);
    // The phone app sends no notice for the first message after it started
    // with an empty message store; that message only sets its counter. A
    // message ahead of the one under test takes that place.
    const QString number = QStringLiteral("4930999") + sixDigits();
    QVERIFY2(receiveOnTheEmulator(number, QStringLiteral("QPM Eingang ") + sixDigits() +
                                              QStringLiteral(" Vorlauf")),
             "the emulator console did not take the message");
    // Whether this one gets a notice depends on how the app started, so the result is not checked.
    static_cast<void>(
        QTest::qWaitFor([&arrived]() { return arrived.count() > 0; }, noticeTimeoutMs));
    QVERIFY(syncInto(service).hasValue());
    arrived.clear();
    const QString text = QStringLiteral("QPM Eingang ") + sixDigits() + QStringLiteral(" Grüße");

    QVERIFY2(receiveOnTheEmulator(number, text), "the emulator console did not take the message");

    // The notice makes the service pull the message without being asked.
    QVERIFY2(QTest::qWaitFor([&arrived]() { return arrived.count() > 0; }, noticeTimeoutMs),
             "no new-message notice from the phone app");
    std::optional<StoredMessage> found;
    const QList<StoredMessage> stored = scratch.messages->allMessages().value();
    for (const StoredMessage &message : stored) {
        if (message.body == text)
            found = message;
    }
    QVERIFY2(found, "the received message is not in the store");
    QCOMPARE(found->box, MessageBox::Inbox);
    QVERIFY(found->incoming);
    QVERIFY(!found->read);
    QVERIFY(found->addresses.value(0).endsWith(number.right(6)));
    QVERIFY(found->date.isValid());

    const QSet<QString> matches =
        scratch.messages->searchConversations(text.section(u' ', 2, 2)).value();
    QVERIFY(matches.contains(found->threadKey));
    QVERIFY(service.setArchived({found->luid}, true).hasValue());
    QVERIFY(scratch.messages->message(found->luid).value().archived);

    QTemporaryDir exports;
    QVERIFY(exports.isValid());
    ExportedMessage exported;
    exported.message.box = found->box;
    exported.message.incoming = true;
    exported.message.date = found->date;
    exported.message.addresses = found->addresses;
    exported.message.body = found->body;
    exported.archived = true;
    const QByteArray xml = writeMessagesXml({exported});
    const Result<QList<Message>> back = readMessagesXml(xml);
    QVERIFY(back.hasValue());
    QCOMPARE(back.value().size(), 1);
    QCOMPARE(back.value().constFirst().body, text);
    QVERIFY(QString::fromUtf8(
                writeMessagesHtml({exported}, QStringLiteral("Test"), CsvLanguage::German))
                .contains(text));
    QVERIFY(QString::fromUtf8(writeMessagesCsv({exported}, CsvLanguage::German)).contains(text));
}

void TestDeviceMessages::refusalsOfThePhoneAppAreReported()
{
    if (!m_emulator)
        QSKIP("The read state and deleting are asked for on the emulator only");
    ScratchStore scratch;
    QVERIFY(scratch.open());
    MessagesService service(m_connection.get());
    service.setStore(scratch.messages);
    QVERIFY(syncInto(service).hasValue());
    const QString text = QStringLiteral("QPM Eingang ") + sixDigits();
    QVERIFY(receiveOnTheEmulator(QStringLiteral("4930999") + sixDigits(), text));
    std::optional<StoredMessage> found;
    for (int lookup = 0; lookup < sentLookups && !found; ++lookup) {
        QVERIFY(syncInto(service).hasValue());
        const QList<StoredMessage> stored = scratch.messages->allMessages().value();
        for (const StoredMessage &message : stored) {
            if (message.body == text)
                found = message;
        }
    }
    QVERIFY2(found, "the received message is not in the store");

    const QFuture<Result<void>> read = service.setRead({found->luid}, true);
    QVERIFY(waitFor(read, readyTimeoutMs));
    const QFuture<Result<void>> removed = service.remove({found->luid});
    QVERIFY(waitFor(removed, readyTimeoutMs));

    qInfo() << "Read state:" << (read.result().hasValue() ? "changed" : "refused, code")
            << int(read.result().error().code());
    qInfo() << "Deleting:" << (removed.result().hasValue() ? "done" : "refused, code")
            << int(removed.result().error().code());
    // The app 4.15 lacks the permission to change messages; a later one may have it.
    QVERIFY(read.result().hasValue() ||
            read.result().error().code() == ErrorCode::PermissionDenied);
    QVERIFY(removed.result().hasValue() ||
            removed.result().error().code() == ErrorCode::PermissionDenied);
    QCOMPARE(scratch.messages->message(found->luid).hasValue(), !removed.result().hasValue());
    if (!read.result().hasValue())
        QVERIFY(!scratch.messages->message(found->luid).value().read);
}

void TestDeviceMessages::oneRealMessageIsSentAndFoundAsSent()
{
    if (m_emulator)
        QSKIP("The emulator would hand the message to its radio; sending is tested on the fake "
              "phone");
    if (qEnvironmentVariable("QPM_TEST_SEND_SMS") != QLatin1StringView("1"))
        QSKIP("QPM_TEST_SEND_SMS is not 1, so no message is sent");
    const QString number = qEnvironmentVariable("QPM_TEST_SMS_TO");
    QVERIFY2(!number.isEmpty(), "QPM_TEST_SMS_TO is not set");
    ScratchStore scratch;
    QVERIFY(scratch.open());
    MessagesService service(m_connection.get());
    service.setStore(scratch.messages);
    QVERIFY(syncInto(service).hasValue());
    const QString text = QStringLiteral("QPM Test ") + sixDigits();

    const QFuture<Result<SendReport>> sent = service.send({number}, text, -1);

    QVERIFY(waitFor(sent, syncTimeoutMs));
    QVERIFY2(
        sent.result().hasValue(),
        qPrintable(QStringLiteral("not sent, code %1").arg(int(sent.result().error().code()))));
    QVERIFY2(sent.result().value().sent == 1,
             qPrintable(QStringLiteral("the phone refused, code %1")
                            .arg(int(sent.result().value().firstError.code()))));
    bool found = false;
    for (int lookup = 0; lookup < sentLookups && !found; ++lookup) {
        QVERIFY(syncInto(service).hasValue());
        const QList<StoredMessage> stored = scratch.messages->allMessages().value();
        for (const StoredMessage &message : stored) {
            if (message.body == text && !message.incoming)
                found = true;
        }
    }
    qInfo() << "The test message is among the sent messages:" << (found ? "yes" : "no");
    QVERIFY2(found, "the sent test message is not among the sent messages");
}

QTEST_GUILESS_MAIN(TestDeviceMessages)

#include "tst_device_messages.moc"
