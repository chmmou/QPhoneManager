#include "CommandGuard.h"

#include "core/codec/CallLogCodec.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/CallLogService.h"
#include "core/store/CallStore.h"
#include "core/store/ProfileStore.h"

#include "WaitFor.h"

#include <QDir>
#include <QPointer>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <memory>
#include <utility>

using namespace qpm::core;
using namespace qpm::core::codec;
using namespace qpm::core::protocol;
using qpm::core::service::CallLogService;
using qpm::core::store::CallChanges;
using qpm::core::store::CallStore;
using qpm::core::store::ProfileStore;
using qpm::test::CommandGuard;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int listTimeoutMs = 120000;
constexpr int connectTries = 10;
constexpr std::array allDirections = {CallDirection::Outgoing, CallDirection::Incoming,
                                      CallDirection::Missed};

// Only entries with both a name and a number of this form are ever deleted by
// the test. The numbers lie in a block no line uses.
const QRegularExpression &testNamePattern()
{
    static const QRegularExpression pattern(QStringLiteral("^QPM Testanruf \\d{6}$"));
    return pattern;
}

const QRegularExpression &testNumberPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^\\+4930999\\d{6}$"));
    return pattern;
}

// Android refuses a change of the call log to an app without WRITE_CALL_LOG,
// and the client passes the SecurityException on after "DUMP:".
bool isPermissionRefusal(const AtReply &reply)
{
    return reply.status == AtReply::Status::Dump &&
           reply.detail.contains(QLatin1StringView("SecurityException"));
}

int androidCallType(CallDirection direction)
{
    switch (direction) {
    case CallDirection::Incoming:
        return 1;
    case CallDirection::Missed:
        return 3;
    case CallDirection::Outgoing:
        break;
    }
    return 2;
}

bool isTestEntry(const CallEntry &entry)
{
    return testNamePattern().match(entry.name).hasMatch() &&
           testNumberPattern().match(entry.number).hasMatch();
}

CallEntry testEntry(CallDirection direction)
{
    QRandomGenerator *random = QRandomGenerator::global();
    CallEntry entry;
    entry.number = QStringLiteral("+4930999%1").arg(random->bounded(100000, 999999));
    entry.name = QStringLiteral("QPM Testanruf %1").arg(random->bounded(100000, 999999));
    entry.direction = direction;
    // Far in the past, so that the entry sits at the end of every list.
    entry.start = QDateTime(QDate(2001, 1, 1), QTime(12, 0, random->bounded(0, 59)));
    entry.durationSeconds = direction == CallDirection::Missed ? 0 : random->bounded(1, 600);
    return entry;
}

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling. On the emulator ("emulator-..." serials) the test writes, reads,
// filters and deletes entries of its own. Where the client may not change the
// call log, the emulator gets its entries through adb and loses them the same
// way. On any other phone, which may be someone's own, it only reads counts
// and form, and writes nothing but one entry of its own with a made-up number,
// which it deletes again, and only when the client may write; with
// QPM_TEST_READ_ONLY set it writes nothing at all. No value read from such a
// phone is ever printed or stored.
class TestDeviceCalls : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceCalls(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testEntriesLeftByAnEarlierRunAreRemoved();
    void callListsAreWellFormed();
    void ownEntryIsWrittenReadAndDeletedAgain();
    void serviceReadsFiltersAndDeletesOnTheEmulator();

private:
    [[nodiscard]] bool connectToClient();
    [[nodiscard]] AtReply send(const QString &command, int timeoutMs = readyTimeoutMs);
    [[nodiscard]] Result<CallList> readList(CallDirection direction);
    [[nodiscard]] Result<QList<CallEntry>> readAll();
    [[nodiscard]] bool remove(quint32 id);
    [[nodiscard]] bool adbShell(const QStringList &arguments);
    [[nodiscard]] bool adbInsert(const CallEntry &entry);
    [[nodiscard]] std::optional<quint32> findId(const CallEntry &entry);
    [[nodiscard]] std::optional<CallEntry> findWritten(const QList<CallEntry> &entries,
                                                       const CallEntry &written) const;

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    bool m_readOnly = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    QPointer<CommandGuard> m_guard;
    /// Entries this test wrote and has not deleted yet.
    QList<quint32> m_created;
};

void TestDeviceCalls::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    m_readOnly = qEnvironmentVariableIsSet("QPM_TEST_READ_ONLY");
    if (m_emulator)
        qInfo() << "Target: emulator, every write is tried";
    else if (m_readOnly)
        qInfo() << "Target: phone, read only";
    else
        qInfo() << "Target: phone, read only and one own test entry";

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
}

void TestDeviceCalls::cleanupTestCase()
{
    if (!m_adb || m_localPort == 0)
        return;
    const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, m_localPort);
    QVERIFY(waitFor(removed, readyTimeoutMs));
}

void TestDeviceCalls::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    QVERIFY(connectToClient());
}

void TestDeviceCalls::cleanup()
{
    // Whatever failed above, an entry this test wrote does not stay.
    const QList<quint32> leftOver = std::exchange(m_created, {});
    for (const quint32 id : leftOver) {
        if (!m_connection || m_connection->state() != State::Ready) {
            m_connection = std::make_unique<PhoneConnection>();
            QVERIFY(connectToClient());
        }
        QVERIFY2(remove(id), "The test entry could not be deleted");
    }
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
}

bool TestDeviceCalls::connectToClient()
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

AtReply TestDeviceCalls::send(const QString &command, int timeoutMs)
{
    AtSession *session = m_connection ? m_connection->session() : nullptr;
    if (!session) {
        AtReply closed;
        closed.status = AtReply::Status::Closed;
        return closed;
    }
    const QFuture<AtReply> reply = session->send(command, std::chrono::milliseconds(timeoutMs));
    if (!waitFor(reply, timeoutMs + readyTimeoutMs)) {
        AtReply timedOut;
        timedOut.status = AtReply::Status::Timeout;
        return timedOut;
    }
    return reply.result();
}

Result<CallList> TestDeviceCalls::readList(CallDirection direction)
{
    const AtReply selected = send(selectCallListCommand(direction));
    if (selected.status != AtReply::Status::Ok)
        return Error(ErrorCode::Rejected, QStringLiteral("list not chosen"));
    const AtReply read = send(readCallListCommand(), listTimeoutMs);
    if (read.status != AtReply::Status::Ok)
        return Error(ErrorCode::Rejected, QStringLiteral("list not read"));
    return parseCallList(read.lines, direction);
}

Result<QList<CallEntry>> TestDeviceCalls::readAll()
{
    QList<CallEntry> all;
    for (const CallDirection direction : allDirections) {
        const Result<CallList> list = readList(direction);
        if (!list.hasValue())
            return list.error();
        if (list.value().malformed > 0)
            return Error(ErrorCode::ProtocolError, QStringLiteral("unreadable lines"));
        all.append(list.value().entries);
    }
    return all;
}

bool TestDeviceCalls::remove(quint32 id)
{
    const Result<QString> command = encodeCallDelete(id);
    if (!command.hasValue())
        return false;
    const AtReply reply = send(command.value());
    // "+CMS ERROR: 500": the entry is gone already.
    if (reply.status == AtReply::Status::Ok ||
        (reply.status == AtReply::Status::CmsError && reply.detail == QLatin1StringView("500"))) {
        return true;
    }
    if (!isPermissionRefusal(reply))
        return false;
    return adbShell({QStringLiteral("content"), QStringLiteral("delete"), QStringLiteral("--uri"),
                     QStringLiteral("content://call_log/calls"), QStringLiteral("--where"),
                     QStringLiteral("_id=%1").arg(id)});
}

bool TestDeviceCalls::adbShell(const QStringList &arguments)
{
    // Changing data behind the client's back is for the emulator only.
    if (!m_emulator)
        return false;
    QProcess process;
    process.start(m_adbPath,
                  QStringList{QStringLiteral("-s"), m_serial, QStringLiteral("shell")} + arguments);
    return process.waitForFinished(readyTimeoutMs) &&
           process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool TestDeviceCalls::adbInsert(const CallEntry &entry)
{
    // adb hands the arguments to a shell on the device, which would split the
    // name at its spaces without the quotes.
    return adbShell(
        {QStringLiteral("content"), QStringLiteral("insert"), QStringLiteral("--uri"),
         QStringLiteral("content://call_log/calls"), QStringLiteral("--bind"),
         QStringLiteral("number:s:") + entry.number, QStringLiteral("--bind"),
         QStringLiteral("name:s:'%1'").arg(entry.name), QStringLiteral("--bind"),
         QStringLiteral("type:i:%1").arg(androidCallType(entry.direction)),
         QStringLiteral("--bind"), QStringLiteral("date:l:%1").arg(entry.start.toMSecsSinceEpoch()),
         QStringLiteral("--bind"), QStringLiteral("duration:l:%1").arg(entry.durationSeconds),
         QStringLiteral("--bind"), QStringLiteral("new:i:0")});
}

std::optional<quint32> TestDeviceCalls::findId(const CallEntry &entry)
{
    const Result<QList<CallEntry>> all = readAll();
    if (!all.hasValue())
        return std::nullopt;
    const std::optional<CallEntry> found = findWritten(all.value(), entry);
    return found ? std::optional<quint32>(found->id) : std::nullopt;
}

std::optional<CallEntry> TestDeviceCalls::findWritten(const QList<CallEntry> &entries,
                                                      const CallEntry &written) const
{
    for (const CallEntry &entry : entries) {
        if (entry.number == written.number && entry.name == written.name)
            return entry;
    }
    return std::nullopt;
}

// A run that ended without its cleanup can leave test entries behind; only
// entries with the test's own name and number pattern are touched.
void TestDeviceCalls::testEntriesLeftByAnEarlierRunAreRemoved()
{
    const Result<QList<CallEntry>> all = readAll();
    QVERIFY2(all.hasValue(), "the call lists could not be read");
    int removed = 0;
    for (const CallEntry &entry : all.value()) {
        if (!isTestEntry(entry))
            continue;
        if (m_readOnly) {
            ++removed;
            continue;
        }
        QVERIFY2(remove(entry.id), "a left-over test entry could not be deleted");
        ++removed;
    }
    qInfo() << (m_readOnly ? "Left-over test entries found:" : "Left-over test entries removed:")
            << removed;
}

void TestDeviceCalls::callListsAreWellFormed()
{
    QSet<quint32> ids;
    QList<int> counts;
    for (const CallDirection direction : allDirections) {
        const Result<CallList> list = readList(direction);
        QVERIFY2(list.hasValue(), "a call list could not be read");
        QCOMPARE(list.value().malformed, 0);
        for (const CallEntry &entry : list.value().entries) {
            QVERIFY2(entry.id > 0, "an entry came without id");
            QVERIFY2(entry.start.isValid(), "an entry came without time");
            QVERIFY2(entry.durationSeconds >= 0, "an entry came with a negative duration");
            QVERIFY2(!ids.contains(entry.id), "an id came twice");
            ids.insert(entry.id);
        }
        for (qsizetype i = 1; i < list.value().entries.size(); ++i) {
            QVERIFY2(list.value().entries.at(i - 1).start >= list.value().entries.at(i).start,
                     "a list is not newest first");
        }
        counts.append(int(list.value().entries.size()));
    }
    qInfo() << "Calls read, outgoing:" << counts.at(0) << "incoming:" << counts.at(1)
            << "missed:" << counts.at(2);
}

void TestDeviceCalls::ownEntryIsWrittenReadAndDeletedAgain()
{
    if (m_readOnly)
        QSKIP("QPM_TEST_READ_ONLY is set");
    const Result<CallList> before = readList(CallDirection::Outgoing);
    QVERIFY2(before.hasValue(), "the outgoing calls could not be read");
    const qsizetype startCount = before.value().entries.size();

    const CallEntry written = testEntry(CallDirection::Outgoing);
    const Result<QString> command = encodeCallWrite(written);
    QVERIFY(command.hasValue());
    const AtReply reply = send(command.value());
    if (reply.status != AtReply::Status::Ok) {
        QVERIFY2(isPermissionRefusal(reply) || !m_emulator,
                 "the emulator refused the test entry for another reason than the permission");
        QSKIP("The app on the phone may not write the call log, so the phone stays read only");
    }

    const Result<CallList> after = readList(CallDirection::Outgoing);
    QVERIFY2(after.hasValue(), "the outgoing calls could not be read again");
    const std::optional<CallEntry> found = findWritten(after.value().entries, written);
    QVERIFY2(found, "the test entry is not in the outgoing calls");
    m_created.append(found->id);
    QCOMPARE(after.value().entries.size(), startCount + 1);
    QCOMPARE(found->start, written.start);
    QCOMPARE(found->durationSeconds, written.durationSeconds);
    QCOMPARE(found->direction, CallDirection::Outgoing);

    QVERIFY2(remove(found->id), "the phone did not delete the test entry");
    m_created.removeAll(found->id);

    const Result<CallList> end = readList(CallDirection::Outgoing);
    QVERIFY2(end.hasValue(), "the outgoing calls could not be read at the end");
    QCOMPARE(end.value().entries.size(), startCount);
    QVERIFY2(!findWritten(end.value().entries, written), "the test entry is still on the phone");
    const AtReply again = send(encodeCallDelete(found->id).value());
    QCOMPARE(again.status, AtReply::Status::CmsError);
}

void TestDeviceCalls::serviceReadsFiltersAndDeletesOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Writes beyond one own test entry run on the emulator only");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore profile(dir.path(), QStringLiteral("emulator"));
    QVERIFY(profile.open().hasValue());
    const auto store = std::make_shared<CallStore>(&profile);
    CallLogService service(m_connection.get());
    service.setStore(store);

    const QFuture<Result<CallChanges>> first = service.sync();
    QVERIFY(waitFor(first, listTimeoutMs));
    QVERIFY(first.result().hasValue());
    const qsizetype startCount = store->calls().value().size();

    QList<CallEntry> written;
    bool clientWrites = true;
    for (const CallDirection direction : allDirections) {
        const CallEntry entry = testEntry(direction);
        const AtReply reply = send(encodeCallWrite(entry).value());
        if (reply.status != AtReply::Status::Ok) {
            QVERIFY2(isPermissionRefusal(reply), "the client refused the entry for another reason");
            clientWrites = false;
            QVERIFY2(adbInsert(entry), "adb could not write the test entry");
        }
        written.append(entry);
        if (const std::optional<quint32> id = findId(entry))
            m_created.append(*id);
    }
    qInfo() << (clientWrites ? "The client writes the call log"
                             : "The client may not write the call log; entries came through adb");

    const QFuture<Result<CallChanges>> synced = service.sync();
    QVERIFY(waitFor(synced, listTimeoutMs));
    QVERIFY(synced.result().hasValue());
    QCOMPARE(synced.result().value().added, 3);
    QCOMPARE(synced.result().value().removed, 0);
    const QList<CallEntry> stored = store->calls().value();
    QCOMPARE(stored.size(), startCount + 3);
    QList<quint32> ids;
    for (const CallEntry &entry : written) {
        const std::optional<CallEntry> found = findWritten(stored, entry);
        QVERIFY2(found, qPrintable(entry.name));
        if (!m_created.contains(found->id))
            m_created.append(found->id);
        ids.append(found->id);
        // The list an entry comes from decides its kind.
        QCOMPARE(found->direction, entry.direction);
        QCOMPARE(found->start, entry.start);
        QCOMPARE(found->durationSeconds, entry.durationSeconds);
    }
    for (const CallDirection direction : allDirections) {
        const Result<CallList> list = readList(direction);
        QVERIFY(list.hasValue());
        for (const CallEntry &entry : written) {
            QCOMPARE(bool(findWritten(list.value().entries, entry)), entry.direction == direction);
        }
    }

    const QFuture<Result<void>> removed = service.remove(ids);
    QVERIFY(waitFor(removed, listTimeoutMs));
    if (clientWrites) {
        QVERIFY(removed.result().hasValue());
        QCOMPARE(store->calls().value().size(), startCount);
    } else {
        // The refusal reaches the caller, and nothing is taken off the list.
        QVERIFY(!removed.result().hasValue());
        QCOMPARE(removed.result().error().code(), ErrorCode::PermissionDenied);
        QCOMPARE(store->calls().value().size(), startCount + 3);
        for (const quint32 id : std::as_const(ids))
            QVERIFY2(remove(id), "adb could not delete the test entry");
    }
    for (const quint32 id : std::as_const(ids))
        m_created.removeAll(id);

    const QFuture<Result<CallChanges>> last = service.sync();
    QVERIFY(waitFor(last, listTimeoutMs));
    QVERIFY(last.result().hasValue());
    QCOMPARE(last.result().value().added, 0);
    QCOMPARE(last.result().value().removed, clientWrites ? 0 : 3);
    QCOMPARE(store->calls().value().size(), startCount);
}

QTEST_GUILESS_MAIN(TestDeviceCalls)

#include "tst_device_calls.moc"
