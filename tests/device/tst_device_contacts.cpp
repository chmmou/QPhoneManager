#include "CommandGuard.h"

#include "core/codec/PhoneVCard.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/ContactsService.h"
#include "core/store/ContactStore.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include "WaitFor.h"

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QPointer>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <utility>

using namespace qpm::core;
using namespace qpm::core::codec;
using namespace qpm::core::protocol;
using namespace qpm::core::service;
using namespace qpm::core::sync;
using qpm::core::store::ContactChanges;
using qpm::core::store::ContactStore;
using qpm::core::store::ProfileStore;
using qpm::test::CommandGuard;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int syncTimeoutMs = 180000;
constexpr int connectTries = 10;

// The phone's state as a pull sees it, kept in memory only.
class CountingStore : public SyncStoreAdapter
{
public:
    std::optional<quint64> changeCounter(IrmcDomain) const override { return std::nullopt; }
    std::optional<QByteArray> objectHash(IrmcDomain, quint32) const override
    {
        return std::nullopt;
    }
    Result<void> apply(IrmcDomain, const SyncDelta &) override { return {}; }
};

ContactValue value(int type, const QString &text)
{
    ContactValue entry;
    entry.type = type;
    entry.value = text;
    return entry;
}

QByteArray testPhoto()
{
    QImage image(160, 160, QImage::Format_RGB32);
    image.fill(QColor(0, 106, 106));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 80);
    return bytes;
}

// Only contacts named like this are ever deleted by the test.
const QRegularExpression &testNamePattern()
{
    static const QRegularExpression pattern(QStringLiteral("^QPM Testkontakt \\d{6}$"));
    return pattern;
}

QString testName()
{
    return QStringLiteral("QPM Testkontakt %1")
        .arg(QRandomGenerator::global()->bounded(100000, 999999));
}

const ContactValue *byDataId(const QList<ContactValue> &values, quint64 dataId)
{
    for (const ContactValue &entry : values) {
        if (entry.dataId == dataId)
            return &entry;
    }
    return nullptr;
}

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling. On the emulator ("emulator-..." serials) every write is tried. On
// any other phone, which may be someone's own, the test only reads counts and
// form, and writes nothing but one contact of its own that it deletes again.
// No value read from such a phone is ever printed or stored.
class TestDeviceContacts : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceContacts(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testContactsLeftByAnEarlierRunAreRemoved();
    void pullDeliversWellFormedContacts();
    void ownTestContactIsCreatedChangedAndDeletedAgain();
    void everyFieldComesBackAsWritten();
    void changeOnThePhoneArrivesAloneInTheChangeSync();

private:
    [[nodiscard]] bool connectToClient();
    [[nodiscard]] Result<QList<RawObject>> pullAll();
    [[nodiscard]] Result<ObexResponse>
    request(const std::function<QFuture<Result<ObexResponse>>(ObexClient &)> &call);
    [[nodiscard]] Result<Contact> fetch(quint32 luid);
    [[nodiscard]] Result<quint64> totalRecords();
    [[nodiscard]] bool adbShell(const QStringList &arguments);

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    QPointer<CommandGuard> m_guard;
    /// The contact this test created and has not deleted yet.
    QList<quint32> m_created;
};

void TestDeviceContacts::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    qInfo() << (m_emulator ? "Target: emulator, every write is tried"
                           : "Target: phone, read only and one own test contact");

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

void TestDeviceContacts::cleanupTestCase()
{
    if (!m_adb || m_localPort == 0)
        return;
    const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, m_localPort);
    QVERIFY(waitFor(removed, readyTimeoutMs));
}

void TestDeviceContacts::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    QVERIFY(connectToClient());
}

void TestDeviceContacts::cleanup()
{
    // Whatever failed above, a contact this test created does not stay.
    const QList<quint32> leftOver = std::exchange(m_created, {});
    for (const quint32 luid : leftOver) {
        if (!m_connection || m_connection->state() != State::Ready) {
            m_connection = std::make_unique<PhoneConnection>();
            QVERIFY(connectToClient());
        }
        const Result<ObexResponse> removed = request([luid](ObexClient &obex) {
            return obex.remove(
                QStringLiteral("telecom/pb/luid/%1.vcf").arg(IrmcSync::luidText(luid)));
        });
        QVERIFY2(removed.hasValue() || removed.error().code() == ErrorCode::NotFound,
                 "The test contact could not be deleted");
    }
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
}

bool TestDeviceContacts::connectToClient()
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

Result<QList<RawObject>> TestDeviceContacts::pullAll()
{
    CountingStore store;
    IrmcSync sync(IrmcDomain::Contacts, &store);
    const QFuture<Result<SyncDelta>> pulled = m_connection->withObex<SyncDelta>(
        ObexClient::Target::IrmcSync, [&sync](ObexClient &obex) { return sync.pull(obex); });
    if (!waitFor(pulled, syncTimeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("pull did not finish"));
    if (!pulled.result().hasValue())
        return pulled.result().error();
    if (!pulled.result().value().full)
        return Error(ErrorCode::ProtocolError, QStringLiteral("pull was not full"));
    return pulled.result().value().upserts;
}

Result<ObexResponse>
TestDeviceContacts::request(const std::function<QFuture<Result<ObexResponse>>(ObexClient &)> &call)
{
    const QFuture<Result<ObexResponse>> done =
        m_connection->withObex<ObexResponse>(ObexClient::Target::IrmcSync, call);
    if (!waitFor(done, readyTimeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("request did not finish"));
    return done.result();
}

Result<Contact> TestDeviceContacts::fetch(quint32 luid)
{
    const Result<ObexResponse> read = request([luid](ObexClient &obex) {
        return obex.get(QStringLiteral("telecom/pb/luid/%1.vcf").arg(IrmcSync::luidText(luid)));
    });
    if (!read.hasValue())
        return read.error();
    Result<Contact> parsed = parsePhoneVCard(read.value().body);
    if (!parsed.hasValue())
        return parsed;
    Contact contact = parsed.value();
    contact.luid = luid;
    return contact;
}

Result<quint64> TestDeviceContacts::totalRecords()
{
    const Result<ObexResponse> info =
        request([](ObexClient &obex) { return obex.get(QStringLiteral("telecom/pb/info.log")); });
    if (!info.hasValue())
        return info.error();
    const QByteArray body = info.value().body.trimmed();
    bool ok = false;
    const quint64 total = body.mid(body.indexOf(':') + 1).trimmed().toULongLong(&ok);
    if (!ok)
        return Error(ErrorCode::ProtocolError, QStringLiteral("info.log without a count"));
    return total;
}

bool TestDeviceContacts::adbShell(const QStringList &arguments)
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

// A run that ended without its cleanup can leave test contacts behind; only
// contacts with exactly the test's own name pattern are touched.
void TestDeviceContacts::testContactsLeftByAnEarlierRunAreRemoved()
{
    const Result<QList<RawObject>> objects = pullAll();
    QVERIFY2(objects.hasValue(), "the full pull failed");
    QList<quint32> leftOver;
    for (const RawObject &object : objects.value()) {
        const Result<Contact> parsed = parsePhoneVCard(object.data);
        if (!parsed.hasValue())
            continue;
        if (testNamePattern().match(parsed.value().givenName).hasMatch() ||
            testNamePattern().match(displayNameOf(parsed.value())).hasMatch()) {
            leftOver.append(object.luid);
        }
    }
    for (const quint32 luid : std::as_const(leftOver)) {
        const Result<ObexResponse> removed = request([luid](ObexClient &obex) {
            return obex.remove(
                QStringLiteral("telecom/pb/luid/%1.vcf").arg(IrmcSync::luidText(luid)));
        });
        QVERIFY2(removed.hasValue(), "a left-over test contact could not be deleted");
    }
    const Result<ObexResponse> metadata = request(
        [](ObexClient &obex) { return obex.get(QStringLiteral("telecom/pb/metadata.xml")); });
    QVERIFY(metadata.hasValue());
    qInfo() << "Left-over test contacts removed:" << leftOver.size()
            << "test group present:" << metadata.value().body.contains("title=\"QPM Testgruppe\"");
}

void TestDeviceContacts::pullDeliversWellFormedContacts()
{
    const Result<QList<RawObject>> objects = pullAll();
    QVERIFY2(objects.hasValue(), "the full pull failed");

    int withPhoto = 0;
    for (const RawObject &object : objects.value()) {
        QVERIFY2(object.luid != 0, "a contact came without LUID");
        QVERIFY2(!object.data.contains("\r\nVERSION:"), "a contact carries a VERSION line");
        QVERIFY2(!object.data.contains("\r\n ") || object.data.contains("\r\nPHOTO;"),
                 "a contact is folded");
        const Result<Contact> parsed = parsePhoneVCard(object.data);
        QVERIFY2(parsed.hasValue(), "a contact cannot be read");
        QVERIFY2(parsed.value().luid == object.luid, "the LUID inside differs from the change log");
        if (!parsed.value().photo.isEmpty())
            ++withPhoto;
    }
    qInfo() << "Contacts read:" << objects.value().size() << "with photo:" << withPhoto;
}

void TestDeviceContacts::ownTestContactIsCreatedChangedAndDeletedAgain()
{
    const Result<QList<RawObject>> before = pullAll();
    QVERIFY2(before.hasValue(), "the first pull failed");
    const qsizetype startCount = before.value().size();
    const Result<quint64> startTotal = totalRecords();
    QVERIFY(startTotal.hasValue());

    Contact contact;
    contact.givenName = testName();
    contact.phones = {value(phoneType::mobile, QStringLiteral("+49 30 00000001"))};
    const Result<QByteArray> body = encodeNewContact(contact);
    QVERIFY(body.hasValue());
    const Result<ObexResponse> created = request([&body](ObexClient &obex) {
        return obex.put(QStringLiteral("telecom/pb/luid/.vcf"), body.value());
    });
    QVERIFY2(created.hasValue(), "the phone did not take the test contact");
    const std::optional<quint32> luid = ObexClient::createdLuid(created.value().appParams);
    QVERIFY2(luid && *luid != 0, "the phone did not say which contact it created");
    m_created.append(*luid);

    const Result<Contact> fresh = fetch(*luid);
    QVERIFY(fresh.hasValue());
    QCOMPARE(fresh.value().givenName, contact.givenName);
    QCOMPARE(fresh.value().phones.size(), 1);
    QCOMPARE(fresh.value().phones.at(0).value, contact.phones.at(0).value);

    Contact edited = fresh.value();
    edited.phones[0].value = QStringLiteral("+49 30 00000002");
    edited.emails = {value(emailType::work, QStringLiteral("qpm-test@example.org"))};
    const Result<QByteArray> change = encodeContactUpdate(fresh.value(), edited);
    QVERIFY(change.hasValue());
    const Result<ObexResponse> changed = request([&change, luid](ObexClient &obex) {
        return obex.put(QStringLiteral("telecom/pb/luid/%1.vcf").arg(IrmcSync::luidText(*luid)),
                        change.value());
    });
    QVERIFY2(changed.hasValue(), "the phone did not take the change");

    const Result<Contact> after = fetch(*luid);
    QVERIFY(after.hasValue());
    QCOMPARE(after.value().phones.size(), 1);
    QCOMPARE(after.value().phones.at(0).value, QStringLiteral("+49 30 00000002"));
    QCOMPARE(after.value().phones.at(0).dataId, fresh.value().phones.at(0).dataId);
    QCOMPARE(after.value().emails.size(), 1);
    QCOMPARE(after.value().emails.at(0).value, QStringLiteral("qpm-test@example.org"));

    const Result<ObexResponse> removed = request([luid](ObexClient &obex) {
        return obex.remove(QStringLiteral("telecom/pb/luid/%1.vcf").arg(IrmcSync::luidText(*luid)));
    });
    QVERIFY2(removed.hasValue(), "the phone did not delete the test contact");
    m_created.removeAll(*luid);

    const Result<Contact> gone = fetch(*luid);
    QVERIFY2(!gone.hasValue() && gone.error().code() == ErrorCode::NotFound,
             "the test contact is still on the phone");
    const Result<QList<RawObject>> afterwards = pullAll();
    QVERIFY2(afterwards.hasValue(), "the last pull failed");
    QCOMPARE(afterwards.value().size(), startCount);
    const Result<quint64> endTotal = totalRecords();
    QVERIFY(endTotal.hasValue());
    QCOMPARE(endTotal.value(), startTotal.value());
    for (const RawObject &object : afterwards.value())
        QVERIFY2(object.luid != *luid, "the test contact is still in the phonebook");
}

void TestDeviceContacts::everyFieldComesBackAsWritten()
{
    if (!m_emulator)
        QSKIP("Writes beyond one own test contact run on the emulator only");
    Contact contact;
    contact.prefix = QStringLiteral("Dr.");
    contact.givenName = testName();
    contact.middleName = QStringLiteral("Zoë");
    contact.familyName = QStringLiteral("Größe");
    contact.suffix = QStringLiteral("jr.");
    contact.nickname = QStringLiteral("Testi");
    contact.organization = QStringLiteral("Beispiel GmbH");
    contact.department = QStringLiteral("Prüfung");
    contact.title = QStringLiteral("Leitung");
    contact.note = QStringLiteral("Erste Zeile\r\nZweite = Zeile 😀");
    contact.phones = {value(phoneType::mobile, QStringLiteral("+49 151 00000003")),
                      value(phoneType::faxWork, QStringLiteral("+49 30 00000004"))};
    contact.phones[0].primary = true;
    ContactValue custom = value(phoneType::custom, QStringLiteral("+49 30 00000005"));
    custom.label = QStringLiteral("Werkstatt; hinten");
    contact.phones.append(custom);
    contact.emails = {value(emailType::home, QStringLiteral("qpm-a@example.org")),
                      value(emailType::work, QStringLiteral("qpm-b@example.org"))};
    ContactValue address;
    address.type = addressType::work;
    address.address = {QString(),
                       QString(),
                       QStringLiteral("Prüfweg 1"),
                       QStringLiteral("Köln"),
                       QStringLiteral("NRW"),
                       QStringLiteral("50667"),
                       QStringLiteral("Deutschland")};
    contact.addresses = {address};
    contact.websites = {value(websiteType::work, QStringLiteral("https://example.org/qpm"))};
    contact.ims = {value(imProtocol::jabber, QStringLiteral("qpm@example.org"))};
    contact.events = {value(eventType::other, QStringLiteral("2015-06-01"))};
    contact.birthday = QDate(1980, 2, 29);
    contact.anniversary = QDate(2005, 7, 1);
    contact.groups = {QStringLiteral("QPM Testgruppe")};
    contact.photo = testPhoto();

    const Result<QByteArray> body = encodeNewContact(contact);
    QVERIFY(body.hasValue());
    const Result<ObexResponse> created = request([&body](ObexClient &obex) {
        return obex.put(QStringLiteral("telecom/pb/luid/.vcf"), body.value());
    });
    QVERIFY(created.hasValue());
    const std::optional<quint32> luid = ObexClient::createdLuid(created.value().appParams);
    QVERIFY(luid);
    m_created.append(*luid);

    const Result<Contact> read = fetch(*luid);
    QVERIFY(read.hasValue());
    Contact expected = contact;
    Contact got = read.value();
    // Keys and LUID come from the phone; the photo is scaled by Android.
    for (const ContactValueKind kind : allContactValueKinds) {
        for (ContactValue &entry : got.values(kind))
            entry.dataId = 0;
    }
    got.nicknameId = 0;
    got.luid = 0;
    QVERIFY(!got.photo.isEmpty());
    got.photo = expected.photo;
    got.accountType.clear();
    got.accountName.clear();
    expected.displayName = got.displayName;
    // The phone marks the first mail address as preferred on its own.
    expected.emails[0].primary = got.emails.value(0).primary;
    QCOMPARE(got.familyName, expected.familyName);
    QCOMPARE(got.note, expected.note);
    QCOMPARE(got.phones, expected.phones);
    QCOMPARE(got.emails, expected.emails);
    QCOMPARE(got.addresses, expected.addresses);
    QCOMPARE(got.websites, expected.websites);
    QCOMPARE(got.ims, expected.ims);
    QCOMPARE(got.events, expected.events);
    QCOMPARE(got, expected);

    // Change some entries, delete others, leave the rest: only those move.
    const Contact fresh = read.value();
    Contact edited = fresh;
    edited.phones[1].value = QStringLiteral("+49 30 00000006");
    edited.addresses.clear();
    edited.emails.removeAt(1);
    edited.websites.append(value(websiteType::blog, QStringLiteral("https://example.org/blog")));
    edited.note = QStringLiteral("Geändert");
    edited.organization = QStringLiteral("Andere AG");
    edited.department.clear();
    edited.title = QStringLiteral("Vertrieb");
    edited.birthday = QDate(1981, 3, 1);
    edited.nickname = QStringLiteral("Testo");
    edited.groups.clear();
    edited.photo.clear();
    const Result<QByteArray> change = encodeContactUpdate(fresh, edited);
    QVERIFY(change.hasValue());
    const Result<ObexResponse> changed = request([&change, luid](ObexClient &obex) {
        return obex.put(QStringLiteral("telecom/pb/luid/%1.vcf").arg(IrmcSync::luidText(*luid)),
                        change.value());
    });
    QVERIFY(changed.hasValue());

    const Result<Contact> after = fetch(*luid);
    QVERIFY(after.hasValue());
    const Contact &now = after.value();
    QCOMPARE(now.phones.size(), 3);
    QCOMPARE(byDataId(now.phones, fresh.phones.at(0).dataId)->value, fresh.phones.at(0).value);
    QCOMPARE(byDataId(now.phones, fresh.phones.at(1).dataId)->value,
             QStringLiteral("+49 30 00000006"));
    QCOMPARE(byDataId(now.phones, fresh.phones.at(2).dataId)->label, fresh.phones.at(2).label);
    QCOMPARE(now.emails.size(), 1);
    QCOMPARE(now.emails.at(0).dataId, fresh.emails.at(0).dataId);
    QVERIFY2(now.addresses.isEmpty(), "the address was not deleted");
    QCOMPARE(now.websites.size(), 2);
    QCOMPARE(now.note, QStringLiteral("Geändert"));
    QCOMPARE(now.organization, QStringLiteral("Andere AG"));
    QCOMPARE(now.department, QString());
    QCOMPARE(now.title, QStringLiteral("Vertrieb"));
    QCOMPARE(now.birthday, QDate(1981, 3, 1));
    QCOMPARE(now.nickname, QStringLiteral("Testo"));
    QVERIFY2(now.groups.isEmpty(), "the group was not left");
    QVERIFY2(now.photo.isEmpty(), "the photo was not removed");
}

void TestDeviceContacts::changeOnThePhoneArrivesAloneInTheChangeSync()
{
    if (!m_emulator)
        QSKIP("Writes beyond one own test contact run on the emulator only");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore profile(dir.path(), QStringLiteral("emulator"));
    QVERIFY(profile.open().hasValue());
    const auto store = std::make_shared<ContactStore>(&profile);
    ContactsService service(m_connection.get());
    service.setStore(store);

    const QFuture<Result<ContactChanges>> first = service.sync();
    QVERIFY(waitFor(first, syncTimeoutMs));
    QVERIFY(first.result().hasValue());

    QList<quint32> created;
    for (int i = 0; i < 2; ++i) {
        Contact contact;
        contact.givenName = testName();
        contact.phones = {value(phoneType::mobile, QStringLiteral("+49 30 0000001%1").arg(i))};
        contact.photo = i == 0 ? testPhoto() : QByteArray();
        const QFuture<Result<SaveOutcome>> saved =
            service.save(contact, true, SaveMode::CheckConflict);
        QVERIFY(waitFor(saved, readyTimeoutMs));
        QVERIFY(saved.result().hasValue());
        created.append(saved.result().value().luid);
        m_created.append(saved.result().value().luid);
    }
    QVERIFY(!store->contact(created.at(0)).value().photo.isEmpty());
    const QFuture<Result<ContactChanges>> settled = service.sync();
    QVERIFY(waitFor(settled, syncTimeoutMs));
    QVERIFY(settled.result().hasValue());
    QCOMPARE(settled.result().value().changed, 0);
    QCOMPARE(settled.result().value().added, 0);

    const Contact target = store->contact(created.at(1)).value();
    QVERIFY(
        adbShell({QStringLiteral("content"), QStringLiteral("update"), QStringLiteral("--uri"),
                  QStringLiteral("content://com.android.contacts/data"), QStringLiteral("--bind"),
                  QStringLiteral("data1:s:+4930000000999"), QStringLiteral("--where"),
                  QStringLiteral("_id=%1").arg(target.phones.at(0).dataId)}));

    const QFuture<Result<ContactChanges>> next = service.sync();
    QVERIFY(waitFor(next, syncTimeoutMs));
    QVERIFY(next.result().hasValue());
    QCOMPARE(next.result().value().changed, 1);
    QCOMPARE(next.result().value().added, 0);
    QCOMPARE(next.result().value().removed, 0);
    QCOMPARE(store->contact(created.at(1)).value().phones.at(0).value,
             QStringLiteral("+4930000000999"));

    const QFuture<Result<void>> removed = service.remove(created);
    QVERIFY(waitFor(removed, readyTimeoutMs));
    QVERIFY(removed.result().hasValue());
    for (const quint32 luid : std::as_const(created)) {
        m_created.removeAll(luid);
        QCOMPARE(store->contact(luid).error().code(), ErrorCode::NotFound);
        QCOMPARE(fetch(luid).error().code(), ErrorCode::NotFound);
    }
}

QTEST_MAIN(TestDeviceContacts)

#include "tst_device_contacts.moc"
