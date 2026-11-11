#include "core/protocol/PhoneConnection.h"
#include "core/service/CallLogService.h"
#include "core/store/CallStore.h"
#include "core/store/ProfileStore.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::CallLogService;
using qpm::core::store::CallChanges;
using qpm::core::store::CallStore;
using qpm::core::store::ProfileStore;
using qpm::test::FakeCall;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;

FakeCall fakeCall(quint32 id, const QString &storage, const QString &time, const QString &number,
                  const QString &name = {}, qint64 duration = 0)
{
    FakeCall call;
    call.id = id;
    call.storage = storage;
    call.time = time;
    call.number = number;
    call.name = name;
    call.duration = duration;
    return call;
}

QList<FakeCall> exampleLog()
{
    return {
        fakeCall(11, QStringLiteral("DC"), QStringLiteral("26/11/10,08:00:00"),
                 QStringLiteral("+49 30 12345671"), QStringLiteral("Erika Mustermann"), 65),
        fakeCall(12, QStringLiteral("RC"), QStringLiteral("26/11/10,09:30:00"),
                 QStringLiteral("030 12345672"), QString(), 300),
        fakeCall(13, QStringLiteral("MC"), QStringLiteral("26/11/09,18:45:10"), QString()),
        fakeCall(14, QStringLiteral("MC"), QStringLiteral("26/11/10,12:00:00"),
                 QStringLiteral("+49 30 12345671")),
    };
}

QList<quint32> ids(const QList<CallEntry> &entries)
{
    QList<quint32> result;
    for (const CallEntry &entry : entries)
        result.append(entry.id);
    return result;
}

} // namespace

class TestCallLogService : public QObject
{
    Q_OBJECT

public:
    explicit TestCallLogService(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void syncReadsTheThreeListsNewestFirst();
    void listIsChosenRightBeforeItIsRead();
    void nextSyncReportsWhatChanged();
    void deletedEntriesAreGoneOnThePhoneAndHere();
    void entryAlreadyGoneOnThePhoneCountsAsDeleted();
    void refusedCallLogLeavesTheStoreAsItWas();
    void phoneThatMayNotChangeTheCallLogKeepsEveryEntry();
    void storeKeepsUtcAndHandsOutLocalTime();
    void callsWithoutStoreOrConnectionFail();

private:
    [[nodiscard]] Result<CallChanges> syncOnce();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<ProfileStore> m_profile;
    std::shared_ptr<CallStore> m_store;
    std::unique_ptr<CallLogService> m_service;
};

void TestCallLogService::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_profile = std::make_unique<ProfileStore>(m_dir->path(), QStringLiteral("dev-1"));
    QVERIFY(m_profile->open().hasValue());
    m_store = std::make_shared<CallStore>(m_profile.get());

    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setCalls(exampleLog());

    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_service = std::make_unique<CallLogService>(m_connection.get());
    m_service->setStore(m_store);
}

void TestCallLogService::cleanup()
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

Result<CallChanges> TestCallLogService::syncOnce()
{
    const QFuture<Result<CallChanges>> future = m_service->sync();
    if (!waitFor(future, timeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("sync did not finish"));
    return future.result();
}

void TestCallLogService::syncReadsTheThreeListsNewestFirst()
{
    QSignalSpy changed(m_service.get(), &CallLogService::callsChanged);

    const Result<CallChanges> synced = syncOnce();

    QVERIFY(synced.hasValue());
    QCOMPARE(synced.value().added, 4);
    QCOMPARE(synced.value().changed, 0);
    QCOMPARE(synced.value().removed, 0);
    QCOMPARE(changed.count(), 1);
    const QList<CallEntry> stored = m_store->calls().value();
    QCOMPARE(ids(stored), (QList<quint32>{14, 12, 11, 13}));
    QCOMPARE(stored.at(0).direction, CallDirection::Missed);
    QCOMPARE(stored.at(1).direction, CallDirection::Incoming);
    QCOMPARE(stored.at(1).durationSeconds, qint64(300));
    QCOMPARE(stored.at(2).direction, CallDirection::Outgoing);
    QCOMPARE(stored.at(2).name, QStringLiteral("Erika Mustermann"));
    QVERIFY(stored.at(3).number.isEmpty());
}

void TestCallLogService::listIsChosenRightBeforeItIsRead()
{
    m_phone->clearLog();

    QVERIFY(syncOnce().hasValue());

    QStringList callLog;
    const QStringList received = m_phone->receivedCommands();
    for (const QString &command : received) {
        if (command.startsWith(QStringLiteral("AT+CPB")))
            callLog.append(command);
    }
    QCOMPARE(callLog, (QStringList{QStringLiteral("AT+CPBS=\"DC\""), QStringLiteral("AT+CPBR=,"),
                                   QStringLiteral("AT+CPBS=\"RC\""), QStringLiteral("AT+CPBR=,"),
                                   QStringLiteral("AT+CPBS=\"MC\""), QStringLiteral("AT+CPBR=,")}));
    QVERIFY(m_phone->protocolErrors().isEmpty());
}

void TestCallLogService::nextSyncReportsWhatChanged()
{
    QVERIFY(syncOnce().hasValue());
    QList<FakeCall> log = exampleLog();
    log.removeAt(2);
    log[0].name = QStringLiteral("Erika M.");
    log.append(fakeCall(15, QStringLiteral("RC"), QStringLiteral("26/11/11,07:00:00"),
                        QStringLiteral("0151 2000001"), QString(), 12));
    m_phone->setCalls(log);

    const Result<CallChanges> synced = syncOnce();

    QVERIFY(synced.hasValue());
    QCOMPARE(synced.value().added, 1);
    QCOMPARE(synced.value().changed, 1);
    QCOMPARE(synced.value().removed, 1);
    QCOMPARE(ids(m_store->calls().value()), (QList<quint32>{15, 14, 12, 11}));
}

void TestCallLogService::deletedEntriesAreGoneOnThePhoneAndHere()
{
    QVERIFY(syncOnce().hasValue());
    QSignalSpy changed(m_service.get(), &CallLogService::callsChanged);

    const QFuture<Result<void>> removed = m_service->remove({12, 14});

    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(removed.result().hasValue());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_phone->calls().size(), 2);
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("AT+CPBD=12")));
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("AT+CPBD=14")));
    QCOMPARE(ids(m_store->calls().value()), (QList<quint32>{11, 13}));
    QCOMPARE(syncOnce().value().removed, 0);
}

void TestCallLogService::entryAlreadyGoneOnThePhoneCountsAsDeleted()
{
    QVERIFY(syncOnce().hasValue());
    QList<FakeCall> log = exampleLog();
    log.removeAt(0);
    m_phone->setCalls(log);

    const QFuture<Result<void>> removed = m_service->remove({11});

    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(removed.result().hasValue());
    QCOMPARE(ids(m_store->calls().value()), (QList<quint32>{14, 12, 13}));
}

void TestCallLogService::refusedCallLogLeavesTheStoreAsItWas()
{
    QVERIFY(syncOnce().hasValue());
    m_phone->config().callLogDenied = true;
    m_phone->setCalls({});

    const Result<CallChanges> synced = syncOnce();
    const QFuture<Result<void>> removed = m_service->remove({11});
    QVERIFY(waitFor(removed, timeoutMs));

    QVERIFY(!synced.hasValue());
    QCOMPARE(synced.error().code(), ErrorCode::Rejected);
    QVERIFY(!removed.result().hasValue());
    QCOMPARE(removed.result().error().code(), ErrorCode::Rejected);
    QCOMPARE(m_store->calls().value().size(), 4);
    QVERIFY(m_connection->session());
}

// Android reports the missing permission to write the call log as a
// SecurityException, which the client passes on after "DUMP:".
void TestCallLogService::phoneThatMayNotChangeTheCallLogKeepsEveryEntry()
{
    QVERIFY(syncOnce().hasValue());
    m_phone->config().callLogReadOnly = true;
    QSignalSpy changed(m_service.get(), &CallLogService::callsChanged);

    const QFuture<Result<void>> removed = m_service->remove({11, 12});

    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(!removed.result().hasValue());
    QCOMPARE(removed.result().error().code(), ErrorCode::PermissionDenied);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(m_phone->calls().size(), 4);
    QCOMPARE(m_store->calls().value().size(), 4);
    QVERIFY(syncOnce().hasValue());
}

void TestCallLogService::storeKeepsUtcAndHandsOutLocalTime()
{
    CallEntry entry;
    entry.id = 7;
    entry.number = QStringLiteral("0301");
    entry.start = QDateTime(QDate(2026, 3, 29), QTime(2, 30, 0), QTimeZone::UTC);
    entry.durationSeconds = 9;
    entry.simSlot = 1;

    QVERIFY(m_store->replaceAll({entry}).hasValue());
    const QList<CallEntry> stored = m_store->calls().value();

    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).start.timeSpec(), Qt::LocalTime);
    QCOMPARE(stored.at(0).start.toUTC(), entry.start);
    QCOMPARE(stored.at(0).simSlot, 1);
    QCOMPARE(m_store->replaceAll({entry}).value().changed, 0);
    QVERIFY(m_store->remove({7, 99}).hasValue());
    QVERIFY(m_store->calls().value().isEmpty());
}

void TestCallLogService::callsWithoutStoreOrConnectionFail()
{
    CallLogService withoutStore(m_connection.get());
    const QFuture<Result<CallChanges>> noStore = withoutStore.sync();
    QVERIFY(waitFor(noStore));
    QCOMPARE(noStore.result().error().code(), ErrorCode::InvalidInput);

    const QFuture<Result<void>> wrongId = m_service->remove({0});
    QVERIFY(waitFor(wrongId));
    QCOMPARE(wrongId.result().error().code(), ErrorCode::InvalidInput);

    m_connection->disconnectFromPhone();
    const QFuture<Result<CallChanges>> offline = m_service->sync();
    QVERIFY(waitFor(offline));
    QCOMPARE(offline.result().error().code(), ErrorCode::ConnectionClosed);
    const QFuture<Result<void>> offlineRemove = m_service->remove({11});
    QVERIFY(waitFor(offlineRemove));
    QCOMPARE(offlineRemove.result().error().code(), ErrorCode::ConnectionClosed);
}

QTEST_GUILESS_MAIN(TestCallLogService)

#include "tst_calllogservice.moc"
