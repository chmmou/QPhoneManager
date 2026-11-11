#include "core/protocol/AtSession.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/TcpTransport.h"
#include "core/sync/IrmcSync.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QCryptographicHash>
#include <QHash>
#include <QMap>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::protocol;
using namespace qpm::core::sync;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::test::waitFor;

namespace {

QByteArray luidHex(quint32 luid)
{
    return IrmcSync::luidText(luid).toLatin1();
}

QByteArray note(quint32 luid, const QByteArray &body)
{
    return "BEGIN:VNOTE\r\nBODY:" + body + "\r\nX-IRMC-LUID:" + luidHex(luid) + "\r\nEND:VNOTE\r\n";
}

QByteArray card(quint32 luid, const QByteArray &name)
{
    return "BEGIN:VCARD\r\nN:" + name + ";Example;;;\r\nFN:Example " + name +
           "\r\nTEL;CELL;X-SYNCMLREF1:+49301234567\r\nNOTE:" + QByteArray(400, 'n') +
           "\r\nX-IRMC-LUID:" + luidHex(luid) + "\r\nEND:VCARD\r\n";
}

QByteArray message(quint32 luid, const QByteArray &body)
{
    return "BEGIN:VMESSAGE\r\nTYP:SMS;IN\r\nBOX:INBOX\r\nREAD:1\r\nDATE:20260101T120000Z\r\n"
           "NUMBER:+49301234560\r\nBODY:" +
           body + "\r\nX-IRMC-LUID:" + luidHex(luid) + "\r\nEND:VMESSAGE\r\n";
}

QByteArray sha1(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1);
}

// The local store as the sync sees it, kept in memory.
class MemoryStore : public SyncStoreAdapter
{
public:
    std::optional<quint64> changeCounter(IrmcDomain domain) const override
    {
        const auto it = counters.constFind(domain);
        return it == counters.cend() ? std::nullopt : std::optional<quint64>(it.value());
    }

    std::optional<QByteArray> objectHash(IrmcDomain domain, quint32 luid) const override
    {
        const QByteArray data = objects.value(domain).value(luid);
        if (hashOverrides.contains(luid))
            return hashOverrides.value(luid);
        return data.isEmpty() ? std::nullopt : std::optional<QByteArray>(sha1(data));
    }

    Result<void> apply(IrmcDomain domain, const SyncDelta &delta) override
    {
        if (failApply)
            return Error(ErrorCode::IoFailure, QStringLiteral("Store not writable"));
        QMap<quint32, QByteArray> &stored = objects[domain];
        if (delta.full)
            stored.clear();
        for (const RawObject &object : delta.upserts)
            stored.insert(object.luid, object.data);
        for (const quint32 luid : delta.deletes)
            stored.remove(luid);
        counters.insert(domain, delta.newCounter);
        ++applied;
        return {};
    }

    QHash<IrmcDomain, quint64> counters;
    QHash<IrmcDomain, QMap<quint32, QByteArray>> objects;
    QHash<quint32, QByteArray> hashOverrides;
    bool failApply = false;
    int applied = 0;
};

QStringList obexRequests(const QStringList &commands)
{
    QStringList requests;
    for (const QString &command : commands) {
        if (command.startsWith(QLatin1StringView("OBEX ")))
            requests.append(command.mid(5));
    }
    return requests;
}

} // namespace

class TestIrmcSync : public QObject
{
    Q_OBJECT

public:
    explicit TestIrmcSync(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void firstPullLoadsEverythingAndKeepsTheCounter();
    void unchangedCounterFetchesNothing();
    void changesOnThePhoneAreFetchedOneByOne();
    void unknownCounterForcesAFullReload();
    void ownWritesAreRecognisedByTheirHash();
    void objectGoneBeforeTheFetchCountsAsDeleted();
    void messagesSwitchOnScheduledMessagesFirst();
    void largeDumpIsSplitOffTheMainThread();
    void storeFailureLeavesTheCounterAlone();
    void changeLogLinesAreParsed();
    void changeLogAskingForAFullReloadIsRecognised();
    void malformedChangeLogIsRejected_data();
    void malformedChangeLogIsRejected();
    void calendarDumpIsSplitAtTheCalendarEnd();

private:
    [[nodiscard]] Result<SyncDelta> pull(IrmcDomain domain);

    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<TcpTransport> m_transport;
    std::unique_ptr<AtSession> m_session;
    std::unique_ptr<ObexClient> m_obex;
    std::unique_ptr<MemoryStore> m_store;
};

void TestIrmcSync::init()
{
    m_store = std::make_unique<MemoryStore>();
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setMetadata(QStringLiteral("notes"),
                         "<metadata version=\"1.0\"><note_categories>\r\n</note_categories>\r\n"
                         "</metadata>");
    m_phone->setObjects(QStringLiteral("notes"), {{1, note(1, "First")}, {2, note(2, "Second")}});
    m_phone->addObject(QStringLiteral("notes"), 3, note(3, "Third"));

    m_transport = std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_phone->port());
    m_session = std::make_unique<AtSession>(m_transport.get());
    m_obex = std::make_unique<ObexClient>(m_session.get());
    QSignalSpy opened(m_transport.get(), &Transport::opened);
    m_transport->open();
    QVERIFY(opened.wait());
    const QFuture<Result<void>> connected = m_obex->connectTarget(ObexClient::Target::IrmcSync);
    QVERIFY(waitFor(connected));
    QVERIFY(connected.result().hasValue());
}

void TestIrmcSync::cleanup()
{
    QVERIFY2(m_phone->protocolErrors().isEmpty(), qPrintable(m_phone->protocolErrors().join(u';')));
    m_obex.reset();
    m_session.reset();
    m_transport.reset();
    m_phone.reset();
}

Result<SyncDelta> TestIrmcSync::pull(IrmcDomain domain)
{
    IrmcSync sync(domain, m_store.get());
    const QFuture<Result<SyncDelta>> pulled = sync.pull(*m_obex);
    if (!waitFor(pulled, 20000))
        return Error(ErrorCode::Timeout, QStringLiteral("pull did not finish"));
    return pulled.result();
}

void TestIrmcSync::firstPullLoadsEverythingAndKeepsTheCounter()
{
    const Result<SyncDelta> delta = pull(IrmcDomain::Notes);

    QVERIFY(delta.hasValue());
    QVERIFY(delta.value().full);
    QCOMPARE(delta.value().upserts.size(), 3);
    QCOMPARE(delta.value().upserts.at(2).luid, 3u);
    QCOMPARE(delta.value().upserts.at(2).data, note(3, "Third"));
    QCOMPARE(delta.value().upserts.at(2).hash, sha1(note(3, "Third")));
    QCOMPARE(delta.value().newCounter, 1ULL);
    QVERIFY(delta.value().metadata.startsWith("<metadata"));
    QCOMPARE(m_store->counters.value(IrmcDomain::Notes), 1ULL);
    QCOMPARE(m_store->objects.value(IrmcDomain::Notes).size(), 3);
    QCOMPARE(obexRequests(m_phone->receivedCommands()),
             (QStringList{QStringLiteral("CONNECT IRMC-SYNC"),
                          QStringLiteral("GET telecom/notes/info.log"),
                          QStringLiteral("GET telecom/notes/initsync"),
                          QStringLiteral("GET telecom/notes/metadata.xml"),
                          QStringLiteral("GET telecom/notes/luid/cc.log"),
                          QStringLiteral("GET telecom/notes.vnt"),
                          QStringLiteral("GET telecom/notes/luid/cc.log")}));
}

void TestIrmcSync::unchangedCounterFetchesNothing()
{
    QVERIFY(pull(IrmcDomain::Notes).hasValue());
    m_phone->clearLog();

    const Result<SyncDelta> delta = pull(IrmcDomain::Notes);

    QVERIFY(delta.hasValue());
    QVERIFY(!delta.value().full);
    QVERIFY(delta.value().upserts.isEmpty());
    QVERIFY(delta.value().deletes.isEmpty());
    QCOMPARE(obexRequests(m_phone->receivedCommands()).constLast(),
             QStringLiteral("GET telecom/notes/luid/cc.log"));
    QVERIFY(!m_phone->receivedCommands().contains(QStringLiteral("OBEX GET telecom/notes.vnt")));
}

void TestIrmcSync::changesOnThePhoneAreFetchedOneByOne()
{
    QVERIFY(pull(IrmcDomain::Notes).hasValue());
    m_phone->editObject(QStringLiteral("notes"), 2, note(2, "Second, edited"));
    m_phone->addObject(QStringLiteral("notes"), 4, note(4, "Fourth"));
    m_phone->removeObject(QStringLiteral("notes"), 1);
    m_phone->clearLog();

    const Result<SyncDelta> delta = pull(IrmcDomain::Notes);

    QVERIFY(delta.hasValue());
    QVERIFY(!delta.value().full);
    QCOMPARE(delta.value().upserts.size(), 2);
    QCOMPARE(delta.value().upserts.at(0).luid, 2u);
    QCOMPARE(delta.value().upserts.at(0).data, note(2, "Second, edited"));
    QCOMPARE(delta.value().upserts.at(1).luid, 4u);
    QCOMPARE(delta.value().deletes, QList<quint32>{1});
    QCOMPARE(delta.value().newCounter, 4ULL);
    const QStringList requests = obexRequests(m_phone->receivedCommands());
    QVERIFY(requests.contains(QStringLiteral("GET telecom/notes/luid/1.log")));
    QVERIFY(requests.contains(QStringLiteral("GET telecom/notes/luid/000000000002.vnt")));
    QVERIFY(requests.contains(QStringLiteral("GET telecom/notes/luid/000000000004.vnt")));
    QVERIFY(!requests.contains(QStringLiteral("GET telecom/notes.vnt")));
    QCOMPARE(m_store->objects.value(IrmcDomain::Notes).keys(), (QList<quint32>{2, 3, 4}));
}

void TestIrmcSync::unknownCounterForcesAFullReload()
{
    QVERIFY(pull(IrmcDomain::Notes).hasValue());
    m_phone->addObject(QStringLiteral("notes"), 4, note(4, "Fourth"));
    m_phone->forgetChanges(QStringLiteral("notes"));
    m_phone->clearLog();

    const Result<SyncDelta> delta = pull(IrmcDomain::Notes);

    QVERIFY(delta.hasValue());
    QVERIFY(delta.value().full);
    QCOMPARE(delta.value().upserts.size(), 4);
    QVERIFY(obexRequests(m_phone->receivedCommands())
                .contains(QStringLiteral("GET telecom/notes.vnt")));
}

void TestIrmcSync::ownWritesAreRecognisedByTheirHash()
{
    QVERIFY(pull(IrmcDomain::Notes).hasValue());
    const QByteArray written = note(2, "Written here");
    const QFuture<Result<ObexResponse>> put =
        m_obex->put(QStringLiteral("telecom/notes/luid/000000000002.vnt"), written);
    QVERIFY(waitFor(put));
    QVERIFY(put.result().hasValue());
    // The store keeps the hash of what this program wrote.
    m_store->hashOverrides.insert(2, sha1(written));

    const Result<SyncDelta> delta = pull(IrmcDomain::Notes);

    QVERIFY(delta.hasValue());
    QVERIFY(!delta.value().full);
    QVERIFY(delta.value().upserts.isEmpty());
    QCOMPARE(delta.value().newCounter, 2ULL);
    QVERIFY(m_phone->receivedCommands().contains(
        QStringLiteral("OBEX GET telecom/notes/luid/000000000002.vnt")));
}

void TestIrmcSync::objectGoneBeforeTheFetchCountsAsDeleted()
{
    QVERIFY(pull(IrmcDomain::Notes).hasValue());
    m_phone->editObject(QStringLiteral("notes"), 2, note(2, "Edited"));
    // Removed again without a change row, so only the edit is in the log.
    m_phone->setObjects(QStringLiteral("notes"), {{1, note(1, "First")}, {3, note(3, "Third")}});

    const Result<SyncDelta> delta = pull(IrmcDomain::Notes);

    QVERIFY(delta.hasValue());
    QVERIFY(delta.value().upserts.isEmpty());
    QCOMPARE(delta.value().deletes, QList<quint32>{2});
}

void TestIrmcSync::messagesSwitchOnScheduledMessagesFirst()
{
    m_phone->setObjects(QStringLiteral("msg"),
                        {{0x11, message(0x11, "Hello")}, {0x12, message(0x12, "See you")}});
    m_phone->clearLog();

    const Result<SyncDelta> delta = pull(IrmcDomain::Messages);

    QVERIFY(delta.hasValue());
    QCOMPARE(delta.value().upserts.size(), 2);
    QVERIFY(delta.value().metadata.isEmpty());
    const QStringList requests = obexRequests(m_phone->receivedCommands());
    QCOMPARE(requests.first(), QStringLiteral("PUT telecom/msg/setflags"));
    QVERIFY(requests.contains(QStringLiteral("GET telecom/msg.vmg")));
    QVERIFY(!requests.contains(QStringLiteral("GET telecom/msg/metadata.xml")));
}

void TestIrmcSync::largeDumpIsSplitOffTheMainThread()
{
    QList<FakeSyncObject> cards;
    for (quint32 luid = 1; luid <= 400; ++luid)
        cards.append({luid, card(luid, "Person" + QByteArray::number(luid))});
    m_phone->setObjects(QStringLiteral("pb"), cards);

    const Result<SyncDelta> delta = pull(IrmcDomain::Contacts);

    QVERIFY(delta.hasValue());
    QCOMPARE(delta.value().upserts.size(), 400);
    QCOMPARE(delta.value().upserts.at(399).luid, 400u);
    QCOMPARE(delta.value().upserts.at(399).data, cards.at(399).data);
}

void TestIrmcSync::storeFailureLeavesTheCounterAlone()
{
    m_store->failApply = true;

    const Result<SyncDelta> delta = pull(IrmcDomain::Notes);

    QVERIFY(!delta.hasValue());
    QCOMPARE(delta.error().code(), ErrorCode::IoFailure);
    QVERIFY(!m_store->changeCounter(IrmcDomain::Notes).has_value());
}

void TestIrmcSync::changeLogLinesAreParsed()
{
    const Result<IrmcChangeLog> log = IrmcSync::parseChangeLog(
        "Total-Records:17\r\nN:5::00000000004D\r\nE:6::000000000002\r\nD:7::00000000000A\r\n");

    QVERIFY(log.hasValue());
    QCOMPARE(log.value().totalRecords, 17ULL);
    QVERIFY(!log.value().fullReloadRequired);
    QCOMPARE(log.value().changes.size(), 3);
    QVERIFY(log.value().changes.at(0).type == IrmcChange::Type::New);
    QCOMPARE(log.value().changes.at(0).luid, 0x4Du);
    QCOMPARE(log.value().changes.at(0).row, 5ULL);
    QVERIFY(log.value().changes.at(1).type == IrmcChange::Type::Edited);
    QVERIFY(log.value().changes.at(2).type == IrmcChange::Type::Deleted);
}

void TestIrmcSync::changeLogAskingForAFullReloadIsRecognised()
{
    const Result<IrmcChangeLog> log = IrmcSync::parseChangeLog("Total-Records:0\r\n*\r\n");

    QVERIFY(log.hasValue());
    QVERIFY(log.value().fullReloadRequired);
    QVERIFY(log.value().changes.isEmpty());
}

void TestIrmcSync::malformedChangeLogIsRejected_data()
{
    QTest::addColumn<QByteArray>("body");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("no total") << QByteArrayLiteral("Hello\r\n");
    QTest::newRow("bad total") << QByteArrayLiteral("Total-Records:x\r\n");
    QTest::newRow("unknown type") << QByteArrayLiteral("Total-Records:1\r\nX:1::000000000001\r\n");
    QTest::newRow("single colon") << QByteArrayLiteral("Total-Records:1\r\nN:1:000000000001\r\n");
    QTest::newRow("row not a number")
        << QByteArrayLiteral("Total-Records:1\r\nN:a::000000000001\r\n");
    QTest::newRow("luid not hex") << QByteArrayLiteral("Total-Records:1\r\nN:1::GGGG\r\n");
    QTest::newRow("luid too long")
        << QByteArrayLiteral("Total-Records:1\r\nN:1::0000000000000001\r\n");
}

void TestIrmcSync::malformedChangeLogIsRejected()
{
    QFETCH(QByteArray, body);

    const Result<IrmcChangeLog> log = IrmcSync::parseChangeLog(body);
    QVERIFY(!log.hasValue());
    QCOMPARE(log.error().code(), ErrorCode::ProtocolError);
}

void TestIrmcSync::calendarDumpIsSplitAtTheCalendarEnd()
{
    const QByteArray event =
        "BEGIN:VCALENDAR\r\nVERSION:1.0\r\nBEGIN:VEVENT\r\nSUMMARY:Meeting\r\n"
        "DTSTART:20260310T090000Z\r\nX-IRMC-LUID:000010000001\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    const QByteArray task = "BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nSUMMARY:Task\r\n"
                            "X-IRMC-LUID:000000000007\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";

    const Result<QList<RawObject>> objects =
        IrmcSync::splitObjects(task + "\r\n" + event + "\r\n", IrmcDomain::Calendar);

    QVERIFY(objects.hasValue());
    QCOMPARE(objects.value().size(), 2);
    QCOMPARE(objects.value().at(0).luid, 7u);
    QCOMPARE(objects.value().at(0).data, task);
    QCOMPARE(objects.value().at(1).luid, 0x10000001u);
    QCOMPARE(objects.value().at(1).data, event);
}

QTEST_GUILESS_MAIN(TestIrmcSync)

#include "tst_irmcsync.moc"
