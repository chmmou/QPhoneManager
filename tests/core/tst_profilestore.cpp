#include "core/protocol/AtSession.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/TcpTransport.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::store;
using namespace qpm::core::sync;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

QByteArray sha1(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1);
}

RawObject object(quint32 luid, const QByteArray &data)
{
    return {luid, data, sha1(data)};
}

QByteArray note(quint32 luid, const QByteArray &body)
{
    return "BEGIN:VNOTE\r\nBODY:" + body + "\r\nX-IRMC-LUID:" + IrmcSync::luidText(luid).toLatin1() +
           "\r\nEND:VNOTE\r\n";
}

// Runs statements against an SQLite file outside the store and returns
// whether all of them succeeded.
bool runSql(const QString &path, const QStringList &statements)
{
    const QString name = QStringLiteral("tst_profilestore.raw");
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        ok = db.open();
        QSqlQuery query(db);
        for (const QString &statement : statements)
            ok = ok && query.exec(statement);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

int userVersion(const QString &path)
{
    const QString name = QStringLiteral("tst_profilestore.version");
    int version = -1;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
                version = query.value(0).toInt();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return version;
}

bool hasTable(const QString &path, const QString &table)
{
    const QString name = QStringLiteral("tst_profilestore.table");
    bool found = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open())
            found = db.tables().contains(table);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return found;
}

} // namespace

class TestProfileStore : public QObject
{
    Q_OBJECT

public:
    explicit TestProfileStore(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void directoryNameKeepsPlainIds_data();
    void directoryNameKeepsPlainIds();
    void directoryNameHashesEverythingElse_data();
    void directoryNameHashesEverythingElse();
    void openCreatesTheProfileAtTheCurrentVersion();
    void openRejectsAnEmptyDeviceId();
    void reopeningTheCurrentVersionMakesNoBackup();
    void olderDatabaseIsCopiedBeforeTheMigration();
    void newerDatabaseIsLeftUntouched();
    void profileFieldsSurviveAReopen();
    void profileValuesAreBoundNotInterpolated();
    void invalidDisplayNameIsRejected_data();
    void invalidDisplayNameIsRejected();
    void profilesAreListed();
    void blobIsStoredUnderItsHash();
    void blobNameOutsideTheHashFormIsRejected_data();
    void blobNameOutsideTheHashFormIsRejected();
    void missingBlobIsReported();
    void appliedDeltaKeepsCounterHashesAndTime();
    void fullDeltaReplacesTheArea();
    void callsBeforeOpenFail();
    void syncCounterPersistsAcrossStoreInstances();

private:
    [[nodiscard]] std::unique_ptr<ProfileStore> openStore(const QString &deviceId) const;
    [[nodiscard]] QString databasePath(const QString &deviceId) const;

    std::unique_ptr<QTemporaryDir> m_root;
};

void TestProfileStore::init()
{
    m_root = std::make_unique<QTemporaryDir>();
    QVERIFY(m_root->isValid());
}

void TestProfileStore::cleanup()
{
    m_root.reset();
}

std::unique_ptr<ProfileStore> TestProfileStore::openStore(const QString &deviceId) const
{
    auto store = std::make_unique<ProfileStore>(m_root->path(), deviceId);
    const Result<void> opened = store->open();
    if (!opened.hasValue())
        qWarning() << "open failed:" << opened.error().message();
    return opened.hasValue() ? std::move(store) : nullptr;
}

QString TestProfileStore::databasePath(const QString &deviceId) const
{
    return m_root->filePath(ProfileStore::directoryName(deviceId) + QStringLiteral("/data.sqlite"));
}

void TestProfileStore::directoryNameKeepsPlainIds_data()
{
    QTest::addColumn<QString>("deviceId");

    QTest::newRow("imei") << QStringLiteral("358240051111110");
    QTest::newRow("letters and hyphens") << QStringLiteral("emulator-5554-ABC");
    QTest::newRow("64 characters") << QString(64, u'a');
}

void TestProfileStore::directoryNameKeepsPlainIds()
{
    QFETCH(QString, deviceId);

    QCOMPARE(ProfileStore::directoryName(deviceId), deviceId);
}

void TestProfileStore::directoryNameHashesEverythingElse_data()
{
    QTest::addColumn<QString>("deviceId");

    QTest::newRow("parent folder") << QStringLiteral("../../etc");
    QTest::newRow("slash") << QStringLiteral("a/b");
    QTest::newRow("dot") << QStringLiteral(".");
    QTest::newRow("space") << QStringLiteral("my phone");
    QTest::newRow("umlaut") << QStringLiteral("Gerät-1");
    QTest::newRow("empty") << QString();
    QTest::newRow("65 characters") << QString(65, u'a');
}

void TestProfileStore::directoryNameHashesEverythingElse()
{
    QFETCH(QString, deviceId);

    const QString name = ProfileStore::directoryName(deviceId);

    QCOMPARE(name, QString::fromLatin1(sha1(deviceId.toUtf8()).toHex()));
    QCOMPARE(QDir(m_root->path()).filePath(name), m_root->path() + u'/' + name);
}

void TestProfileStore::openCreatesTheProfileAtTheCurrentVersion()
{
    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("358240051111110"));

    QVERIFY(store);
    QCOMPARE(store->directory(), m_root->filePath(QStringLiteral("358240051111110")));
    QVERIFY(QFileInfo(store->directory() + QStringLiteral("/blobs")).isDir());
    QCOMPARE(userVersion(databasePath(QStringLiteral("358240051111110"))),
             ProfileStore::schemaVersion);
    QVERIFY(hasTable(databasePath(QStringLiteral("358240051111110")), QStringLiteral("meta")));
    QVERIFY(!QFile::exists(store->directory() + QStringLiteral("/data.sqlite.bak")));
}

void TestProfileStore::openRejectsAnEmptyDeviceId()
{
    ProfileStore store(m_root->path(), QString());

    const Result<void> opened = store.open();

    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::InvalidInput);
    QVERIFY(QDir(m_root->path()).isEmpty());
}

void TestProfileStore::reopeningTheCurrentVersionMakesNoBackup()
{
    QVERIFY(openStore(QStringLiteral("dev-1")));

    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));

    QVERIFY(store);
    QVERIFY(!QFile::exists(store->directory() + QStringLiteral("/data.sqlite.bak")));
}

void TestProfileStore::olderDatabaseIsCopiedBeforeTheMigration()
{
    const QString path = databasePath(QStringLiteral("dev-1"));
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QVERIFY(runSql(path, {QStringLiteral("CREATE TABLE earlier (value TEXT)"),
                          QStringLiteral("INSERT INTO earlier VALUES ('kept')")}));

    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));

    QVERIFY(store);
    const QString backup = path + QStringLiteral(".bak");
    QVERIFY(QFile::exists(backup));
    QCOMPARE(userVersion(backup), 0);
    QVERIFY(hasTable(backup, QStringLiteral("earlier")));
    QVERIFY(!hasTable(backup, QStringLiteral("meta")));
    QCOMPARE(userVersion(path), ProfileStore::schemaVersion);
    QVERIFY(hasTable(path, QStringLiteral("meta")));
}

void TestProfileStore::newerDatabaseIsLeftUntouched()
{
    const QString path = databasePath(QStringLiteral("dev-1"));
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QVERIFY(runSql(path, {QStringLiteral("CREATE TABLE later (value TEXT)"),
                          QStringLiteral("PRAGMA user_version = 7")}));

    ProfileStore store(m_root->path(), QStringLiteral("dev-1"));
    const Result<void> opened = store.open();

    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::NotSupported);
    QCOMPARE(userVersion(path), 7);
    QVERIFY(!hasTable(path, QStringLiteral("meta")));
    QVERIFY(!QFile::exists(path + QStringLiteral(".bak")));
}

void TestProfileStore::profileFieldsSurviveAReopen()
{
    DeviceProfile profile;
    profile.deviceId = QStringLiteral("358240051111110");
    profile.displayName = QStringLiteral("Jürgens Pixel");
    profile.model = QStringLiteral("Pixel 8");
    profile.manufacturer = QStringLiteral("Google");
    profile.androidApi = 36;
    profile.clientVersion = QStringLiteral("4.15");
    profile.lastConnection = QDateTime(QDate(2026, 11, 11), QTime(10, 30, 15, 250), QTimeZone::UTC);
    profile.transport = TransportKind::Adb;
    profile.fixedAddress = QStringLiteral("192.0.2.10");
    profile.btAddress = QStringLiteral("00:11:22:33:44:55");
    profile.adbSerial = QStringLiteral("emulator-5554");
    profile.pinStored = true;
    {
        const std::unique_ptr<ProfileStore> store = openStore(profile.deviceId);
        QVERIFY(store);
        QVERIFY(store->saveProfile(profile).hasValue());
    }

    const std::unique_ptr<ProfileStore> store = openStore(profile.deviceId);
    QVERIFY(store);
    const Result<DeviceProfile> loaded = store->loadProfile();

    QVERIFY(loaded.hasValue());
    QCOMPARE(loaded.value().deviceId, profile.deviceId);
    QCOMPARE(loaded.value().displayName, profile.displayName);
    QCOMPARE(loaded.value().model, profile.model);
    QCOMPARE(loaded.value().manufacturer, profile.manufacturer);
    QCOMPARE(loaded.value().androidApi, 36);
    QCOMPARE(loaded.value().clientVersion, profile.clientVersion);
    QCOMPARE(loaded.value().lastConnection, profile.lastConnection);
    QVERIFY(loaded.value().transport == TransportKind::Adb);
    QCOMPARE(loaded.value().fixedAddress, profile.fixedAddress);
    QCOMPARE(loaded.value().btAddress, profile.btAddress);
    QCOMPARE(loaded.value().adbSerial, profile.adbSerial);
    QVERIFY(loaded.value().pinStored);
}

void TestProfileStore::profileValuesAreBoundNotInterpolated()
{
    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
    QVERIFY(store);
    DeviceProfile profile;
    profile.deviceId = QStringLiteral("dev-1");
    profile.displayName = QStringLiteral("O'Brien\"); DROP TABLE meta; --");

    QVERIFY(store->saveProfile(profile).hasValue());

    QVERIFY(hasTable(databasePath(QStringLiteral("dev-1")), QStringLiteral("meta")));
    QCOMPARE(store->loadProfile().value().displayName, profile.displayName);
}

void TestProfileStore::invalidDisplayNameIsRejected_data()
{
    QTest::addColumn<QString>("displayName");

    QTest::newRow("empty") << QString();
    QTest::newRow("65 characters") << QString(65, u'x');
}

void TestProfileStore::invalidDisplayNameIsRejected()
{
    QFETCH(QString, displayName);
    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
    QVERIFY(store);
    DeviceProfile profile;
    profile.deviceId = QStringLiteral("dev-1");
    profile.displayName = displayName;

    const Result<void> saved = store->saveProfile(profile);

    QVERIFY(!saved.hasValue());
    QCOMPARE(saved.error().code(), ErrorCode::InvalidInput);
    QVERIFY(store->loadProfile().value().displayName.isEmpty());
}

void TestProfileStore::profilesAreListed()
{
    for (const QString &id : {QStringLiteral("dev-1"), QStringLiteral("phone/2")}) {
        const std::unique_ptr<ProfileStore> store = openStore(id);
        QVERIFY(store);
        DeviceProfile profile;
        profile.deviceId = id;
        profile.displayName = QStringLiteral("Phone ") + id;
        QVERIFY(store->saveProfile(profile).hasValue());
    }
    QVERIFY(QDir(m_root->path()).mkpath(QStringLiteral("not-a-profile")));

    const Result<QList<DeviceProfile>> profiles = ProfileStore::listProfiles(m_root->path());

    QVERIFY(profiles.hasValue());
    QStringList ids;
    for (const DeviceProfile &profile : profiles.value())
        ids.append(profile.deviceId);
    ids.sort();
    QCOMPARE(ids, (QStringList{QStringLiteral("dev-1"), QStringLiteral("phone/2")}));
}

void TestProfileStore::blobIsStoredUnderItsHash()
{
    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
    QVERIFY(store);
    const QByteArray data("\x89PNG\r\n\x1a\n-image-", 15);

    const Result<QString> name = store->putBlob(data);
    const Result<QString> again = store->putBlob(data);

    QVERIFY(name.hasValue());
    QCOMPARE(name.value(), QString::fromLatin1(sha1(data).toHex()));
    QVERIFY(again.hasValue());
    QCOMPARE(again.value(), name.value());
    QVERIFY(QFile::exists(store->directory() + QStringLiteral("/blobs/") + name.value()));
    QCOMPARE(store->blob(name.value()).value(), data);
}

void TestProfileStore::blobNameOutsideTheHashFormIsRejected_data()
{
    QTest::addColumn<QString>("name");

    QTest::newRow("parent folder") << QStringLiteral("../data.sqlite");
    QTest::newRow("upper case") << QString(40, u'A');
    QTest::newRow("too short") << QString(39, u'a');
    QTest::newRow("empty") << QString();
}

void TestProfileStore::blobNameOutsideTheHashFormIsRejected()
{
    QFETCH(QString, name);
    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
    QVERIFY(store);

    const Result<QByteArray> read = store->blob(name);

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::InvalidInput);
}

void TestProfileStore::missingBlobIsReported()
{
    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
    QVERIFY(store);

    const Result<QByteArray> read = store->blob(QString(40, u'0'));

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::NotFound);
}

void TestProfileStore::appliedDeltaKeepsCounterHashesAndTime()
{
    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
    QVERIFY(store);
    QVERIFY(!store->changeCounter(IrmcDomain::Notes).has_value());
    QVERIFY(!store->lastSync(IrmcDomain::Notes).has_value());
    SyncDelta first;
    first.full = true;
    first.upserts = {object(1, "one"), object(2, "two")};
    first.newCounter = 5;
    QVERIFY(store->apply(IrmcDomain::Notes, first).hasValue());
    SyncDelta second;
    second.upserts = {object(2, "two, edited")};
    second.deletes = {1};
    second.newCounter = 7;

    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-1);
    QVERIFY(store->apply(IrmcDomain::Notes, second).hasValue());

    QCOMPARE(store->changeCounter(IrmcDomain::Notes).value_or(0), 7ULL);
    QVERIFY(!store->changeCounter(IrmcDomain::Contacts).has_value());
    QVERIFY(!store->objectHash(IrmcDomain::Notes, 1).has_value());
    QCOMPARE(store->objectHash(IrmcDomain::Notes, 2).value_or(QByteArray()), sha1("two, edited"));
    QVERIFY(!store->objectHash(IrmcDomain::Contacts, 2).has_value());
    QVERIFY(store->lastSync(IrmcDomain::Notes).has_value());
    QVERIFY(*store->lastSync(IrmcDomain::Notes) >= before);
}

void TestProfileStore::fullDeltaReplacesTheArea()
{
    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
    QVERIFY(store);
    SyncDelta first;
    first.full = true;
    first.upserts = {object(1, "one"), object(2, "two")};
    first.newCounter = 1;
    QVERIFY(store->apply(IrmcDomain::Contacts, first).hasValue());
    QVERIFY(store->apply(IrmcDomain::Notes, first).hasValue());
    SyncDelta full;
    full.full = true;
    full.upserts = {object(3, "three")};
    full.newCounter = 2;

    QVERIFY(store->apply(IrmcDomain::Contacts, full).hasValue());

    QVERIFY(!store->objectHash(IrmcDomain::Contacts, 1).has_value());
    QVERIFY(!store->objectHash(IrmcDomain::Contacts, 2).has_value());
    QVERIFY(store->objectHash(IrmcDomain::Contacts, 3).has_value());
    QVERIFY(store->objectHash(IrmcDomain::Notes, 1).has_value());
}

void TestProfileStore::callsBeforeOpenFail()
{
    ProfileStore store(m_root->path(), QStringLiteral("dev-1"));
    SyncDelta delta;
    delta.newCounter = 1;

    QVERIFY(!store.apply(IrmcDomain::Notes, delta).hasValue());
    QVERIFY(!store.loadProfile().hasValue());
    QVERIFY(!store.putBlob("data").hasValue());
    QVERIFY(!store.changeCounter(IrmcDomain::Notes).has_value());
}

void TestProfileStore::syncCounterPersistsAcrossStoreInstances()
{
    FakePhone phone;
    QVERIFY(phone.listen());
    phone.setMetadata(QStringLiteral("notes"), "<metadata version=\"1.0\"></metadata>");
    phone.setObjects(QStringLiteral("notes"), {{1, note(1, "First")}, {2, note(2, "Second")}});
    protocol::TcpTransport transport(QStringLiteral("127.0.0.1"), phone.port());
    protocol::AtSession session(&transport);
    protocol::ObexClient obex(&session);
    QSignalSpy opened(&transport, &protocol::Transport::opened);
    transport.open();
    QVERIFY(opened.wait());
    const QFuture<Result<void>> connected = obex.connectTarget(protocol::ObexClient::Target::IrmcSync);
    QVERIFY(waitFor(connected));
    QVERIFY(connected.result().hasValue());
    {
        const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
        QVERIFY(store);
        IrmcSync sync(IrmcDomain::Notes, store.get());
        const QFuture<Result<SyncDelta>> pulled = sync.pull(obex);
        QVERIFY(waitFor(pulled, 20000));
        QVERIFY(pulled.result().hasValue());
        QVERIFY(pulled.result().value().full);
    }
    phone.editObject(QStringLiteral("notes"), 2, note(2, "Second, edited"));
    phone.clearLog();

    const std::unique_ptr<ProfileStore> store = openStore(QStringLiteral("dev-1"));
    QVERIFY(store);
    IrmcSync sync(IrmcDomain::Notes, store.get());
    const QFuture<Result<SyncDelta>> pulled = sync.pull(obex);
    QVERIFY(waitFor(pulled, 20000));

    QVERIFY(pulled.result().hasValue());
    QVERIFY(!pulled.result().value().full);
    QCOMPARE(pulled.result().value().upserts.size(), 1);
    QVERIFY(!phone.receivedCommands().contains(QStringLiteral("OBEX GET telecom/notes.vnt")));
    QCOMPARE(store->objectHash(IrmcDomain::Notes, 2).value_or(QByteArray()),
             sha1(note(2, "Second, edited")));
    QVERIFY(phone.protocolErrors().isEmpty());
}

QTEST_GUILESS_MAIN(TestProfileStore)

#include "tst_profilestore.moc"
