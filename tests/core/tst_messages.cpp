#include "core/codec/VMessage.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/MessagesService.h"
#include "core/store/MessageStore.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QDir>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::MessagesService;
using qpm::core::service::SendReport;
using qpm::core::store::localMessageLuidBase;
using qpm::core::store::MessageChanges;
using qpm::core::store::MessageStore;
using qpm::core::store::ProfileStore;
using qpm::core::store::StoredMessage;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;
constexpr quint32 erikaInbox = 11;
constexpr quint32 erikaSent = 12;
constexpr quint32 photoMessage = 0x10000004;
constexpr quint32 plannedMessage = 0x20000001;

QByteArray sms(quint32 luid, bool incoming, const QString &number, const QString &text,
               const QString &stamp, bool read = true)
{
    QByteArray data = "BEGIN:VMESSAGE\r\nTYP:SMS;" + QByteArray(incoming ? "IN" : "OUT") + "\r\n";
    data += incoming ? "BOX:INBOX\r\nREAD:" + QByteArray(read ? "1" : "0") + "\r\n"
                     : QByteArray("BOX:SENT\r\n");
    data += "DATE:" + stamp.toLatin1() + "\r\n";
    data += "NUMBER:" + number.toUtf8() + "\r\n";
    data += "BODY;CHARSET=UTF-8:" + text.toUtf8() + "\r\n";
    data += "X-IRMC-LUID:" + sync::IrmcSync::luidText(luid).toLatin1() + "\r\nEND:VMESSAGE\r\n";
    return data;
}

QByteArray photoMms()
{
    return "BEGIN:VMESSAGE\r\nTYP:MMS;IN\r\nBOX:INBOX\r\nREAD:1\r\nDATE:20261108T100000Z\r\n"
           "NUMBER:+49 170 2000003\r\nSUBJECT:Ausflug\r\n"
           "ATT;NAME=text_0.txt;TYPE=text/plain:Schau mal\r\n"
           "ATT;NAME=..=2F..=2Fbild.jpg;TYPE=image/jpeg;SIZE=4;ENCODING=BASE64:/9j/2Q==\r\n\r\n"
           "X-IRMC-LUID:000010000004\r\nEND:VMESSAGE\r\n";
}

QByteArray plannedSms()
{
    return "BEGIN:VMESSAGE\r\nSIM:0\r\nTYP:SMS;OUT\r\nBOX:SHEDULED\r\n"
           "RECIPIENT:Bernd;+49302000002;SHEDULED;;\r\nDATE:20301224T170000Z\r\n"
           "BODY:Frohes Fest\r\nX-IRMC-LUID:000020000001\r\nEND:VMESSAGE\r\n";
}

QList<FakeSyncObject> exampleMessages()
{
    return {
        {erikaInbox,
         sms(erikaInbox, true, QStringLiteral("+4915112345678"), QStringLiteral("Grüße aus Köln"),
             QStringLiteral("20261110T080000Z"), false)},
        {erikaSent, sms(erikaSent, false, QStringLiteral("015112345678"),
                        QStringLiteral("Danke, Müller"), QStringLiteral("20261110T090000Z"))},
        {photoMessage, photoMms()},
        {plannedMessage, plannedSms()},
    };
}

Message planned(const QString &text)
{
    Message message;
    message.kind = MessageKind::Scheduled;
    message.box = MessageBox::Scheduled;
    message.date = QDateTime::currentDateTimeUtc().addDays(2);
    message.body = text;
    MessageRecipient recipient;
    recipient.name = QStringLiteral("Clara");
    recipient.number = QStringLiteral("+491702000003");
    message.recipients = {recipient};
    return message;
}

} // namespace

class TestMessages : public QObject
{
    Q_OBJECT

public:
    explicit TestMessages(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void syncLoadsMessagesIntoConversations();
    void partsAreStoredAndTheirNamesKeptAsData();
    void nextSyncBringsOnlyWhatChanged();
    void newMessageNoticeReloadsAndAnnouncesIt();
    void unreadableMessageIsLeftOutAndCounted();
    void tablesBehindTheSyncStateLoadEverythingAgain();
    void sendingUsesTextModeAndShowsTheSentMessage();
    void refusedSendIsCountedAndTheOthersStillGo();
    void invalidSendReachesNoPhone_data();
    void invalidSendReachesNoPhone();
    void phoneThatCannotSendIsNotAsked();
    void refusedDeletionIsReportedAndKeepsTheMessage();
    void deletionWorksWhereThePhoneAllowsIt();
    void multimediaMessageCannotBeDeleted();
    void readStateFollowsThePhone();
    void archivedMessageStaysWhenThePhoneDeletesIt();
    void scheduledMessageIsCreatedChangedAndDeleted();
    void scheduledTimeMustLieAhead();
    void searchMatchesWordStartsWithoutAccents();
    void archiveFileMessagesStayOnThisComputer();
    void callsWithoutStoreOrConnectionFail();

private:
    [[nodiscard]] Result<MessageChanges> syncOnce();
    [[nodiscard]] QStringList sentCommands() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<ProfileStore> m_profile;
    std::shared_ptr<MessageStore> m_store;
    std::unique_ptr<MessagesService> m_service;
};

void TestMessages::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_profile = std::make_unique<ProfileStore>(m_dir->path(), QStringLiteral("dev-1"));
    QVERIFY(m_profile->open().hasValue());
    m_store = std::make_shared<MessageStore>(m_profile.get());

    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("msg"), exampleMessages());

    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_service = std::make_unique<MessagesService>(m_connection.get());
    m_service->setStore(m_store);
}

void TestMessages::cleanup()
{
    m_service.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
    m_store.reset();
    m_profile.reset();
    m_phone.reset();
    m_dir.reset();
}

Result<MessageChanges> TestMessages::syncOnce()
{
    const QFuture<Result<MessageChanges>> synced = m_service->sync();
    if (!waitFor(synced, timeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("sync did not finish"));
    return synced.result();
}

QStringList TestMessages::sentCommands() const
{
    QStringList commands;
    const QStringList received = m_phone->receivedCommands();
    for (const QString &command : received) {
        if (command.startsWith(QLatin1StringView("AT+CMG")) &&
            command != QLatin1StringView("AT+CMGS=?")) {
            commands.append(command);
        }
    }
    return commands;
}

void TestMessages::syncLoadsMessagesIntoConversations()
{
    QSignalSpy arrived(m_service.get(), &MessagesService::messagesArrived);

    const Result<MessageChanges> changes = syncOnce();

    QVERIFY(changes.hasValue());
    QCOMPARE(changes.value().added, 4);
    // The first load brings what the phone keeps; nothing of it counts as just arrived.
    QVERIFY(changes.value().arrived.isEmpty());
    QCOMPARE(arrived.count(), 0);
    const QList<store::ConversationSummary> conversations = m_store->conversations().value();
    // The two messages with Erika share a conversation although one number has
    // the country code; the scheduled message belongs to none.
    QCOMPARE(conversations.size(), 2);
    QCOMPARE(conversations.at(0).count, 2);
    QCOMPARE(conversations.at(0).unread, 1);
    QCOMPARE(conversations.at(0).snippet, QStringLiteral("Danke, Müller"));
    QCOMPARE(conversations.at(0).threadKey,
             MessageStore::threadKeyOf(QStringLiteral("0151 12345678")));
    QCOMPARE(conversations.at(1).snippet, QStringLiteral("Schau mal"));
    QVERIFY(conversations.at(1).lastHasAttachments);

    const QList<StoredMessage> chat = m_store->conversation(conversations.at(0).threadKey).value();
    QCOMPARE(chat.size(), 2);
    QCOMPARE(chat.at(0).luid, erikaInbox);
    QVERIFY(chat.at(0).incoming);
    QVERIFY(!chat.at(1).incoming);
    QCOMPARE(chat.at(0).date, QDateTime(QDate(2026, 11, 10), QTime(8, 0), QTimeZone::UTC));
    const QList<StoredMessage> plannedList = m_store->scheduledMessages().value();
    QCOMPARE(plannedList.size(), 1);
    QCOMPARE(plannedList.at(0).recipients.size(), 1);
    QCOMPARE(plannedList.at(0).recipients.at(0).name, QStringLiteral("Bernd"));
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("OBEX PUT telecom/msg/setflags")));
}

void TestMessages::partsAreStoredAndTheirNamesKeptAsData()
{
    QVERIFY(syncOnce().hasValue());

    const Result<StoredMessage> photo = m_store->message(photoMessage);

    QVERIFY(photo.hasValue());
    QCOMPARE(photo.value().kind, MessageKind::Mms);
    QCOMPARE(photo.value().subject, QStringLiteral("Ausflug"));
    QCOMPARE(photo.value().attachments.size(), 1);
    QCOMPARE(photo.value().attachments.at(0).name, QStringLiteral("../../bild.jpg"));
    QCOMPARE(m_store->attachmentData(photoMessage, 0).value(), QByteArray::fromHex("FFD8FFD9"));
    // The part is a blob named by its hash; the name from the phone is never a path.
    const QString blob = photo.value().attachments.at(0).blob;
    QVERIFY(QFile::exists(QDir(m_profile->directory()).filePath(QStringLiteral("blobs/") + blob)));
    QVERIFY(!QFile::exists(QDir(m_profile->directory()).filePath(QStringLiteral("bild.jpg"))));
}

void TestMessages::nextSyncBringsOnlyWhatChanged()
{
    QVERIFY(syncOnce().hasValue());
    m_phone->addObject(QStringLiteral("msg"), 13,
                       sms(13, true, QStringLiteral("+4915112345678"),
                           QStringLiteral("Bist du da?"), QStringLiteral("20261110T100000Z"),
                           false));
    m_phone->removeObject(QStringLiteral("msg"), erikaSent);
    m_phone->clearLog();

    const Result<MessageChanges> changes = syncOnce();

    QVERIFY(changes.hasValue());
    QCOMPARE(changes.value().added, 1);
    QCOMPARE(changes.value().removed, 1);
    QCOMPARE(changes.value().arrived, QList<quint32>{13});
    QVERIFY(!m_phone->receivedCommands().contains(QStringLiteral("OBEX GET telecom/msg.vmg")));
    QVERIFY(!m_store->message(erikaSent).hasValue());
    QCOMPARE(m_store->conversations().value().at(0).snippet, QStringLiteral("Bist du da?"));
}

void TestMessages::newMessageNoticeReloadsAndAnnouncesIt()
{
    QVERIFY(syncOnce().hasValue());
    QSignalSpy arrived(m_service.get(), &MessagesService::messagesArrived);
    m_phone->addObject(QStringLiteral("msg"), 14,
                       sms(14, true, QStringLiteral("+49302000002"), QStringLiteral("Neu"),
                           QStringLiteral("20261110T110000Z"), false));

    m_phone->sendUnsolicited("+CMTI: \"ME\",14");

    QTRY_COMPARE_WITH_TIMEOUT(arrived.count(), 1, timeoutMs);
    QCOMPARE(arrived.first().first().value<QList<quint32>>(), QList<quint32>{14});
    QVERIFY(m_store->message(14).hasValue());
}

void TestMessages::unreadableMessageIsLeftOutAndCounted()
{
    QList<FakeSyncObject> objects = exampleMessages();
    objects.append({15, QByteArray("BEGIN:VMESSAGE\r\nATT;NAME=a;TYPE=b;ENCODING=BASE64:@@\r\n"
                                   "X-IRMC-LUID:00000000000F\r\nEND:VMESSAGE\r\n")});
    m_phone->setObjects(QStringLiteral("msg"), objects);

    const Result<MessageChanges> changes = syncOnce();

    QVERIFY(changes.hasValue());
    QCOMPARE(changes.value().added, 4);
    QCOMPARE(changes.value().unreadable, 1);
    QVERIFY(!m_store->message(15).hasValue());
}

void TestMessages::tablesBehindTheSyncStateLoadEverythingAgain()
{
    QVERIFY(syncOnce().hasValue());
    QVERIFY(m_store->changeCounter(sync::IrmcDomain::Messages).has_value());
    sync::SyncDelta delta;
    delta.newCounter = 99;

    QVERIFY(m_store->apply(sync::IrmcDomain::Messages, delta).hasValue());

    // Until the prepared messages are written, the next pull loads everything.
    QVERIFY(!m_store->changeCounter(sync::IrmcDomain::Messages).has_value());
    QVERIFY(m_store->applyPrepared({}).hasValue());
    QCOMPARE(m_store->changeCounter(sync::IrmcDomain::Messages), std::optional<quint64>(99));
}

void TestMessages::sendingUsesTextModeAndShowsTheSentMessage()
{
    QVERIFY(syncOnce().hasValue());
    QSignalSpy changed(m_service.get(), &MessagesService::messagesChanged);
    const QString text = QStringLiteral("Hallo, Welt: 1=1; \"ok\"");

    const QFuture<Result<SendReport>> sent = m_service->send(
        {QStringLiteral("+49 (30) 2000002"), QStringLiteral("0170 2000003")}, text, 1);

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY(sent.result().hasValue());
    QCOMPARE(sent.result().value().sent, 2);
    QCOMPARE(sent.result().value().failed, 0);
    QCOMPARE(sentCommands(),
             (QStringList{
                 QStringLiteral("AT+CMGS=+49302000002,Hallo=2C Welt=3A 1=3D1=3B =22ok=22,1,1,1"),
                 QStringLiteral("AT+CMGS=01702000003,Hallo=2C Welt=3A 1=3D1=3B =22ok=22,1,1,1")}));
    QCOMPARE(m_phone->sentMessages().size(), 2);
    QCOMPARE(m_phone->sentMessages().at(0).text, text);
    QCOMPARE(m_phone->sentMessages().at(0).simSlot, 1);
    // The phone files the sent messages; the reload after sending brings them.
    // One of them goes to the number of an existing conversation.
    QTRY_VERIFY_WITH_TIMEOUT(m_store->conversations().value().size() == 3, timeoutMs);
    QVERIFY(changed.count() >= 1);
}

void TestMessages::refusedSendIsCountedAndTheOthersStillGo()
{
    m_phone->config().smsSendFailure = "+CMS ERROR: 38";

    const QFuture<Result<SendReport>> sent = m_service->send(
        {QStringLiteral("0301234567"), QStringLiteral("0401234567")}, QStringLiteral("Test"), -1);

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY(sent.result().hasValue());
    QCOMPARE(sent.result().value().sent, 0);
    QCOMPARE(sent.result().value().failed, 2);
    QCOMPARE(sent.result().value().firstError.code(), ErrorCode::Rejected);
    QCOMPARE(sentCommands().size(), 2);
    QVERIFY(sentCommands().at(0).endsWith(QLatin1StringView(",1,1")));
}

void TestMessages::invalidSendReachesNoPhone_data()
{
    QTest::addColumn<QStringList>("numbers");
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("sim");
    QTest::addColumn<int>("code");

    const int invalid = int(ErrorCode::InvalidInput);
    QTest::newRow("no recipient") << QStringList() << QStringLiteral("Hi") << -1 << invalid;
    QTest::newRow("empty text") << QStringList{QStringLiteral("0301234")} << QStringLiteral(" ")
                                << -1 << invalid;
    QTest::newRow("no number") << QStringList{QStringLiteral("0301234"), QStringLiteral("Erika")}
                               << QStringLiteral("Hi") << -1 << invalid;
    QTest::newRow("SIM out of range")
        << QStringList{QStringLiteral("0301234")} << QStringLiteral("Hi") << 3 << invalid;
    QStringList many;
    for (qsizetype i = 0; i <= MessagesService::maxRecipients; ++i)
        many.append(QStringLiteral("030123456%1").arg(i % 10));
    QTest::newRow("too many recipients")
        << many << QStringLiteral("Hi") << -1 << int(ErrorCode::LimitExceeded);
    QTest::newRow("too long") << QStringList{QStringLiteral("0301234")} << QString(1400, u'"') << -1
                              << int(ErrorCode::LimitExceeded);
}

void TestMessages::invalidSendReachesNoPhone()
{
    QFETCH(QStringList, numbers);
    QFETCH(QString, text);
    QFETCH(int, sim);
    QFETCH(int, code);

    const QFuture<Result<SendReport>> sent = m_service->send(numbers, text, sim);

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY(!sent.result().hasValue());
    QCOMPARE(int(sent.result().error().code()), code);
    QVERIFY(sentCommands().isEmpty());
    QVERIFY(m_phone->sentMessages().isEmpty());
}

void TestMessages::phoneThatCannotSendIsNotAsked()
{
    m_connection->disconnectFromPhone();
    m_phone->config().smsCapabilities = QStringLiteral("PING");
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);

    const QFuture<Result<SendReport>> sent =
        m_service->send({QStringLiteral("0301234567")}, QStringLiteral("Hi"), -1);

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY(!sent.result().hasValue());
    QCOMPARE(sent.result().error().code(), ErrorCode::NotSupported);
    QVERIFY(sentCommands().isEmpty());
}

void TestMessages::refusedDeletionIsReportedAndKeepsTheMessage()
{
    QVERIFY(syncOnce().hasValue());

    const QFuture<Result<void>> removed = m_service->remove({erikaInbox, erikaSent});

    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(!removed.result().hasValue());
    QCOMPARE(removed.result().error().code(), ErrorCode::PermissionDenied);
    // After the first refusal the phone is not asked again for the same thing.
    QCOMPARE(sentCommands(), QStringList{QStringLiteral("AT+CMGD=11")});
    QVERIFY(m_store->message(erikaInbox).hasValue());
    QVERIFY(m_store->message(erikaSent).hasValue());
    QCOMPARE(m_phone->area(QStringLiteral("msg")).objects.size(), 4);
}

void TestMessages::deletionWorksWhereThePhoneAllowsIt()
{
    m_phone->config().smsWriteDenied = false;
    QVERIFY(syncOnce().hasValue());

    const QFuture<Result<void>> removed = m_service->remove({erikaInbox, erikaSent});

    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(removed.result().hasValue());
    QVERIFY(!m_store->message(erikaInbox).hasValue());
    QVERIFY(!m_store->message(erikaSent).hasValue());
    QCOMPARE(m_phone->area(QStringLiteral("msg")).objects.size(), 2);
    QCOMPARE(syncOnce().value().removed, 0);
}

void TestMessages::multimediaMessageCannotBeDeleted()
{
    QVERIFY(syncOnce().hasValue());

    const QFuture<Result<void>> removed = m_service->remove({photoMessage});

    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(!removed.result().hasValue());
    QCOMPARE(removed.result().error().code(), ErrorCode::NotSupported);
    QVERIFY(sentCommands().isEmpty());
    QVERIFY(m_store->message(photoMessage).hasValue());
}

void TestMessages::readStateFollowsThePhone()
{
    QVERIFY(syncOnce().hasValue());

    const QFuture<Result<void>> refused = m_service->setRead({erikaInbox}, true);
    QVERIFY(waitFor(refused, timeoutMs));
    QCOMPARE(refused.result().error().code(), ErrorCode::PermissionDenied);
    QVERIFY(!m_store->message(erikaInbox).value().read);

    m_phone->config().smsWriteDenied = false;
    const QFuture<Result<void>> marked = m_service->setRead({erikaInbox, erikaSent}, true);
    QVERIFY(waitFor(marked, timeoutMs));
    QVERIFY(marked.result().hasValue());
    QVERIFY(m_store->message(erikaInbox).value().read);
    QCOMPARE(sentCommands().constLast(), QStringLiteral("AT+CMGE=11,1"));
    QCOMPARE(sentCommands().count(QStringLiteral("AT+CMGE=11,1")), 2);
}

void TestMessages::archivedMessageStaysWhenThePhoneDeletesIt()
{
    QVERIFY(syncOnce().hasValue());
    QVERIFY(m_service->setArchived({erikaInbox, photoMessage}, true).hasValue());
    m_phone->removeObject(QStringLiteral("msg"), erikaInbox);
    m_phone->removeObject(QStringLiteral("msg"), erikaSent);

    const Result<MessageChanges> changes = syncOnce();

    QVERIFY(changes.hasValue());
    QCOMPARE(changes.value().removed, 2);
    QVERIFY(!m_store->message(erikaInbox).hasValue());
    QVERIFY(!m_store->message(erikaSent).hasValue());
    const QString key = MessageStore::threadKeyOf(QStringLiteral("+4915112345678"));
    const QList<StoredMessage> kept = m_store->conversation(key).value();
    QCOMPARE(kept.size(), 1);
    QVERIFY(kept.at(0).archived);
    QVERIFY(!kept.at(0).isOnPhone());
    QVERIFY(kept.at(0).luid >= localMessageLuidBase);
    QCOMPARE(kept.at(0).body, QStringLiteral("Grüße aus Köln"));

    // A full load after the phone forgot its change log keeps it as well.
    m_phone->removeObject(QStringLiteral("msg"), photoMessage);
    m_phone->forgetChanges(QStringLiteral("msg"));
    const Result<MessageChanges> full = syncOnce();
    QVERIFY(full.hasValue());
    const Result<QSet<QString>> all = m_store->searchConversations(QStringLiteral("Schau"));
    QCOMPARE(all.value().size(), 1);
    QCOMPARE(m_store->conversations().value().size(), 2);
    QVERIFY(!m_store->setArchived({kept.at(0).luid}, false).hasValue());
    QVERIFY(m_store->removeLocal({kept.at(0).luid}).hasValue());
    QCOMPARE(m_store->conversations().value().size(), 1);
}

void TestMessages::scheduledMessageIsCreatedChangedAndDeleted()
{
    QVERIFY(syncOnce().hasValue());

    const QFuture<Result<quint32>> created =
        m_service->saveScheduled(planned(QStringLiteral("Bis morgen")));

    QVERIFY(waitFor(created, timeoutMs));
    QVERIFY(created.result().hasValue());
    const quint32 luid = created.result().value();
    QCOMPARE(kindOfLuid(luid), MessageKind::Scheduled);
    QCOMPARE(m_phone->syncPuts().constLast().first, QStringLiteral("telecom/msg/luid/.vmg"));
    QVERIFY(m_phone->syncPuts().constLast().second.contains("BOX:SHEDULED\r\n"));
    const QList<StoredMessage> list = m_store->scheduledMessages().value();
    QCOMPARE(list.size(), 2);
    // Earliest first: the new one goes out before the one from the phone.
    QCOMPARE(list.at(0).luid, luid);
    QCOMPARE(list.at(0).body, QStringLiteral("Bis morgen"));
    QCOMPARE(list.at(0).recipients.at(0).name, QStringLiteral("Clara"));
    QCOMPARE(list.at(1).luid, plannedMessage);

    Message changed = planned(QStringLiteral("Bis übermorgen"));
    changed.luid = luid;
    const QFuture<Result<quint32>> saved = m_service->saveScheduled(changed);
    QVERIFY(waitFor(saved, timeoutMs));
    QVERIFY(saved.result().hasValue());
    QCOMPARE(m_store->message(luid).value().body, QStringLiteral("Bis übermorgen"));

    const QFuture<Result<void>> removed = m_service->remove({luid});
    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(removed.result().hasValue());
    QVERIFY(!m_store->message(luid).hasValue());
    QCOMPARE(m_phone->syncPuts().constLast().second, QByteArray());
    QCOMPARE(m_store->scheduledMessages().value().size(), 1);
    QCOMPARE(syncOnce().value().added, 0);
}

void TestMessages::scheduledTimeMustLieAhead()
{
    QVERIFY(syncOnce().hasValue());
    Message soon = planned(QStringLiteral("Gleich"));
    soon.date = QDateTime::currentDateTimeUtc().addSecs(10);
    m_phone->clearLog();

    const QFuture<Result<quint32>> created = m_service->saveScheduled(soon);

    QVERIFY(waitFor(created, timeoutMs));
    QCOMPARE(created.result().error().code(), ErrorCode::InvalidInput);
    QVERIFY(m_phone->syncPuts().isEmpty());
}

void TestMessages::searchMatchesWordStartsWithoutAccents()
{
    QVERIFY(syncOnce().hasValue());
    QVERIFY(m_store->hasFullTextSearch());
    const QString erika = MessageStore::threadKeyOf(QStringLiteral("+4915112345678"));

    QCOMPARE(m_store->searchConversations(QStringLiteral("muller")).value(), QSet<QString>{erika});
    QCOMPARE(m_store->searchConversations(QStringLiteral("GRU kol")).value(), QSet<QString>{erika});
    QVERIFY(m_store->searchConversations(QStringLiteral("Frohes")).value().isEmpty());
    // Operators of the search language are taken as text.
    const Result<QSet<QString>> odd = m_store->searchConversations(QStringLiteral("\"OR* NEAR("));
    QVERIFY(odd.hasValue());
    QVERIFY(odd.value().isEmpty());
}

void TestMessages::archiveFileMessagesStayOnThisComputer()
{
    QVERIFY(syncOnce().hasValue());
    Message old;
    old.box = MessageBox::Inbox;
    old.incoming = true;
    old.date = QDateTime(QDate(2020, 1, 1), QTime(12, 0), QTimeZone::UTC);
    old.addresses = {QStringLiteral("+4915112345678")};
    old.body = QStringLiteral("Altes Archiv");
    MessageAttachment part;
    part.name = QStringLiteral("a.png");
    part.mimeType = QStringLiteral("image/png");
    part.data = QByteArray("PNG");
    part.size = 3;
    old.attachments = {part};

    const Result<QList<StoredMessage>> prepared =
        MessageStore::prepareImport({old, old}, m_store->profileDirectory());
    QVERIFY(prepared.hasValue());
    const Result<int> imported = m_store->importPrepared(prepared.value());
    const Result<int> again = m_store->importPrepared(prepared.value());

    QCOMPARE(imported.value(), 1);
    QCOMPARE(again.value(), 0);
    const QList<StoredMessage> chat =
        m_store->conversation(MessageStore::threadKeyOf(QStringLiteral("015112345678"))).value();
    QCOMPARE(chat.size(), 3);
    QCOMPARE(chat.at(0).body, QStringLiteral("Altes Archiv"));
    QVERIFY(chat.at(0).archived);
    QVERIFY(!chat.at(0).isOnPhone());
    QCOMPARE(m_store->attachmentData(chat.at(0).luid, 0).value(), QByteArray("PNG"));
    // The phone never hears of a message only on this computer.
    const QFuture<Result<void>> removed = m_service->remove({chat.at(0).luid});
    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(removed.result().hasValue());
    QVERIFY(sentCommands().isEmpty());
    QCOMPARE(m_store->conversation(chat.at(0).threadKey).value().size(), 2);
}

void TestMessages::callsWithoutStoreOrConnectionFail()
{
    m_service->setStore(nullptr);
    QCOMPARE(m_service->sync().result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(m_service->remove({erikaInbox}).result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(m_service->setArchived({erikaInbox}, true).error().code(), ErrorCode::InvalidInput);

    m_service->setStore(m_store);
    m_connection->disconnectFromPhone();
    QCOMPARE(m_service->sync().result().error().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(m_service->send({QStringLiteral("0301234")}, QStringLiteral("Hi"), -1)
                 .result()
                 .error()
                 .code(),
             ErrorCode::ConnectionClosed);
}

QTEST_GUILESS_MAIN(TestMessages)

#include "tst_messages.moc"
