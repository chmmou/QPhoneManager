#include "core/codec/PhoneVCard.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/ContactsService.h"
#include "core/store/ContactStore.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using namespace qpm::core::service;
using qpm::core::protocol::PhoneConnection;
using qpm::core::store::ContactChanges;
using qpm::core::store::ContactStore;
using qpm::core::store::ContactSummary;
using qpm::core::store::ProfileStore;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;

ContactValue value(quint64 dataId, int type, const QString &text)
{
    ContactValue entry;
    entry.dataId = dataId;
    entry.type = type;
    entry.value = text;
    return entry;
}

Contact erika()
{
    Contact contact;
    contact.luid = 1;
    contact.familyName = QStringLiteral("Mustermann");
    contact.givenName = QStringLiteral("Erika");
    contact.displayName = QStringLiteral("Erika Mustermann");
    contact.phones = {value(11, phoneType::mobile, QStringLiteral("+49 151 1234567"))};
    contact.emails = {value(12, emailType::home, QStringLiteral("erika@example.org"))};
    contact.groups = {QStringLiteral("Familie")};
    contact.accountType = QStringLiteral("com.google");
    contact.accountName = QStringLiteral("erika@example.org");
    return contact;
}

Contact maxMueller()
{
    Contact contact;
    contact.luid = 2;
    contact.familyName = QStringLiteral("Müller");
    contact.givenName = QStringLiteral("Max");
    contact.displayName = QStringLiteral("Max Müller");
    contact.organization = QStringLiteral("Beispiel GmbH");
    contact.phones = {value(21, phoneType::work, QStringLiteral("030 12-34"))};
    return contact;
}

Contact erikaTwice()
{
    Contact contact;
    contact.luid = 3;
    contact.familyName = QStringLiteral("Musterman");
    contact.givenName = QStringLiteral("Erika");
    contact.displayName = QStringLiteral("Erika Musterman");
    contact.phones = {value(31, phoneType::home, QStringLiteral("0151 1234567")),
                      value(32, phoneType::work, QStringLiteral("+49 40 999999"))};
    contact.note = QStringLiteral("Zweiter Eintrag");
    return contact;
}

QByteArray metadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<accounts filter_disabled=\"0\">\r\n"
           "<account selected=\"1\" type=\"null\" name=\"null\"  default=\"1\" "
           "title=\"Phone\"/>\r\n"
           "<account selected=\"1\" type=\"com.google\" name=\"erika@example.org\"  "
           "title=\"Google\"/>\r\n</accounts>\r\n<groups>\r\n"
           "<group title=\"Familie\" sourceid=\"1\" account_name=\"erika@example.org\" "
           "account_type=\"com.google\"/>\r\n</groups>\r\n</metadata>";
}

QStringList names(const QList<ContactSummary> &summaries)
{
    QStringList result;
    for (const ContactSummary &summary : summaries)
        result.append(summary.displayName);
    return result;
}

} // namespace

class TestContactsService : public QObject
{
    Q_OBJECT

public:
    explicit TestContactsService(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void firstSyncLoadsEveryContactWithAccountsAndGroups();
    void phoneChangeArrivesAloneInTheNextSync();
    void newContactIsCreatedWithTheLuidOfThePhone();
    void changeSendsOnlyTheChangedEntriesAndKeepsThePhoneVersion();
    void ownChangeIsNotReportedAgainByTheNextSync();
    void unchangedPhotoIsNotSentAgain();
    void contactChangedOnThePhoneIsReportedAsConflict();
    void deletedContactIsGoneOnThePhoneAndHere();
    void searchFindsNameNumberMailAndCompany();
    void duplicatesAreFoundByNumberMailAndSimilarName();
    void mergeJoinsTheEntriesAndDeletesTheSecondContact();
    void importCreatesEveryContactAndSkipsWhatThePhoneCannotTake();
    void defaultAccountIsSentToThePhone();
    void dialSendsTheNumberAndRefusesWhatIsNoNumber_data();
    void dialSendsTheNumberAndRefusesWhatIsNoNumber();
    void callsWithoutStoreOrConnectionFail();
    void replacedOrDeletedPhotoLeavesNoFileBehind();

private:
    [[nodiscard]] bool syncOnce(ContactChanges *changes = nullptr);
    [[nodiscard]] Result<SaveOutcome> save(const Contact &contact, bool photoChanged,
                                           SaveMode mode = SaveMode::CheckConflict);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<ProfileStore> m_profile;
    std::shared_ptr<ContactStore> m_store;
    std::unique_ptr<ContactsService> m_service;
};

void TestContactsService::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_profile = std::make_unique<ProfileStore>(m_dir->path(), QStringLiteral("dev-1"));
    QVERIFY(m_profile->open().hasValue());
    m_store = std::make_shared<ContactStore>(m_profile.get());

    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("pb"), {{1, encodeAsPhoneSendsIt(erika())},
                                               {2, encodeAsPhoneSendsIt(maxMueller())},
                                               {3, encodeAsPhoneSendsIt(erikaTwice())}});
    m_phone->setMetadata(QStringLiteral("pb"), metadata());

    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_service = std::make_unique<ContactsService>(m_connection.get());
    m_service->setStore(m_store);
}

void TestContactsService::cleanup()
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

bool TestContactsService::syncOnce(ContactChanges *changes)
{
    const QFuture<Result<ContactChanges>> future = m_service->sync();
    if (!waitFor(future, timeoutMs) || !future.result().hasValue())
        return false;
    if (changes)
        *changes = future.result().value();
    return true;
}

Result<SaveOutcome> TestContactsService::save(const Contact &contact, bool photoChanged,
                                              SaveMode mode)
{
    const QFuture<Result<SaveOutcome>> future = m_service->save(contact, photoChanged, mode);
    if (!waitFor(future, timeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("save did not finish"));
    return future.result();
}

void TestContactsService::firstSyncLoadsEveryContactWithAccountsAndGroups()
{
    QSignalSpy changed(m_service.get(), &ContactsService::contactsChanged);
    ContactChanges changes;

    QVERIFY(syncOnce(&changes));

    QCOMPARE(changes.added, 3);
    QCOMPARE(changes.changed, 0);
    QCOMPARE(changes.removed, 0);
    QCOMPARE(changed.count(), 1);
    const QList<ContactSummary> summaries = m_store->summaries().value();
    QCOMPARE(names(summaries),
             QStringList({QStringLiteral("Erika Musterman"), QStringLiteral("Erika Mustermann"),
                          QStringLiteral("Max Müller")}));
    QCOMPARE(summaries.at(1).primaryNumber, QStringLiteral("+49 151 1234567"));
    QCOMPARE(summaries.at(1).groups, QStringList({QStringLiteral("Familie")}));
    QCOMPARE(summaries.at(2).organization, QStringLiteral("Beispiel GmbH"));
    const PbMetadata stored = m_store->metadata().value();
    QCOMPARE(stored.accounts.size(), 2);
    QCOMPARE(stored.groups.size(), 1);
    const Contact read = m_store->contact(1).value();
    QCOMPARE(read.emails.at(0).value, QStringLiteral("erika@example.org"));
}

void TestContactsService::phoneChangeArrivesAloneInTheNextSync()
{
    QVERIFY(syncOnce());
    Contact changed = maxMueller();
    changed.title = QStringLiteral("Einkauf");
    m_phone->editObject(QStringLiteral("pb"), 2, encodeAsPhoneSendsIt(changed));
    m_phone->clearLog();
    ContactChanges changes;

    QVERIFY(syncOnce(&changes));

    QCOMPARE(changes.changed, 1);
    QCOMPARE(changes.added, 0);
    QVERIFY(m_phone->receivedCommands().contains(
        QStringLiteral("OBEX GET telecom/pb/luid/000000000002.vcf")));
    QVERIFY(!m_phone->receivedCommands().contains(QStringLiteral("OBEX GET telecom/pb.vcf")));
    QCOMPARE(m_store->contact(2).value().title, QStringLiteral("Einkauf"));
}

void TestContactsService::newContactIsCreatedWithTheLuidOfThePhone()
{
    QVERIFY(syncOnce());
    Contact contact;
    contact.givenName = QStringLiteral("Neu");
    contact.familyName = QStringLiteral("Kontakt");
    contact.phones = {value(0, phoneType::mobile, QStringLiteral("+4917011112222"))};
    contact.accountType = QStringLiteral("com.google");
    contact.accountName = QStringLiteral("erika@example.org");

    const Result<SaveOutcome> saved = save(contact, true);

    QVERIFY(saved.hasValue());
    QCOMPARE(saved.value().luid, 4u);
    QCOMPARE(m_phone->syncPuts().constLast().first, QStringLiteral("telecom/pb/luid/.vcf"));
    QVERIFY(
        m_phone->syncPuts().constLast().second.contains("X-ACCOUNT:com.google;erika@example.org"));
    const Contact stored = m_store->contact(4).value();
    QCOMPARE(stored.givenName, QStringLiteral("Neu"));
    QVERIFY(stored.phones.at(0).dataId != 0);
    QCOMPARE(m_store->summaries().value().size(), 4);
}

void TestContactsService::changeSendsOnlyTheChangedEntriesAndKeepsThePhoneVersion()
{
    QVERIFY(syncOnce());
    Contact edited = m_store->contact(3).value();
    edited.phones[0].value = QStringLiteral("0151 7654321");
    edited.emails.append(value(0, emailType::work, QStringLiteral("e.m@example.org")));

    const Result<SaveOutcome> saved = save(edited, false);

    QVERIFY(saved.hasValue());
    QVERIFY(!saved.value().conflict);
    const QByteArray body = m_phone->syncPuts().constLast().second;
    QVERIFY(body.contains("TEL;HOME;X-SYNCMLREF31:0151 7654321\r\n"));
    QVERIFY(body.contains("EMAIL;WORK:e.m@example.org\r\n"));
    QVERIFY(!body.contains("SYNCMLREF32"));
    QVERIFY(!body.contains("NOTE"));
    QVERIFY(!body.contains("PHOTO"));
    const Contact stored = m_store->contact(3).value();
    QCOMPARE(stored.phones.at(0).value, QStringLiteral("0151 7654321"));
    QCOMPARE(stored.phones.at(1).value, QStringLiteral("+49 40 999999"));
    QCOMPARE(stored.emails.size(), 1);
    QVERIFY(stored.emails.at(0).dataId != 0);
}

void TestContactsService::ownChangeIsNotReportedAgainByTheNextSync()
{
    QVERIFY(syncOnce());
    Contact edited = m_store->contact(2).value();
    edited.organization = QStringLiteral("Neue Firma");
    QVERIFY(save(edited, false).hasValue());
    ContactChanges changes;

    QVERIFY(syncOnce(&changes));

    QCOMPARE(changes.changed, 0);
    QCOMPARE(changes.added, 0);
    QCOMPARE(m_store->contact(2).value().organization, QStringLiteral("Neue Firma"));
}

void TestContactsService::unchangedPhotoIsNotSentAgain()
{
    Contact withPhoto = maxMueller();
    withPhoto.photo = QByteArray("\xFF\xD8\xFF", 3) + QByteArray(300000, 'p');
    m_phone->setObjects(QStringLiteral("pb"), {{2, encodeAsPhoneSendsIt(withPhoto)}});
    QVERIFY(syncOnce());
    Contact edited = m_store->contact(2).value();
    edited.photo.clear();
    edited.nickname = QStringLiteral("Maxi");

    const Result<SaveOutcome> saved = save(edited, false);

    QVERIFY(saved.hasValue());
    QVERIFY(!m_phone->syncPuts().constLast().second.contains("PHOTO"));
    QCOMPARE(m_store->contact(2).value().photo, withPhoto.photo);
}

void TestContactsService::contactChangedOnThePhoneIsReportedAsConflict()
{
    QVERIFY(syncOnce());
    Contact onPhone = maxMueller();
    onPhone.note = QStringLiteral("Auf dem Handy geändert");
    m_phone->editObject(QStringLiteral("pb"), 2, encodeAsPhoneSendsIt(onPhone));
    Contact edited = m_store->contact(2).value();
    edited.title = QStringLiteral("Chef");
    const qsizetype putsBefore = m_phone->syncPuts().size();

    const Result<SaveOutcome> checked = save(edited, false);

    QVERIFY(checked.hasValue());
    QVERIFY(checked.value().conflict);
    QCOMPARE(checked.value().phoneVersion.note, onPhone.note);
    QCOMPARE(m_phone->syncPuts().size(), putsBefore);

    const Result<SaveOutcome> forced = save(edited, false, SaveMode::Overwrite);
    QVERIFY(forced.hasValue());
    QVERIFY(!forced.value().conflict);
    const Contact stored = m_store->contact(2).value();
    QCOMPARE(stored.title, QStringLiteral("Chef"));

    onPhone.title = QStringLiteral("Vorstand");
    m_phone->editObject(QStringLiteral("pb"), 2, encodeAsPhoneSendsIt(onPhone));
    const Result<SaveOutcome> again = save(stored, false);
    QVERIFY(again.value().conflict);
    QVERIFY(m_service->keepPhoneVersion(again.value().phoneObject).hasValue());
    QCOMPARE(m_store->contact(2).value().title, QStringLiteral("Vorstand"));
}

void TestContactsService::deletedContactIsGoneOnThePhoneAndHere()
{
    QVERIFY(syncOnce());

    const QFuture<Result<void>> removed = m_service->remove({1, 3});

    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(removed.result().hasValue());
    QCOMPARE(m_phone->area(QStringLiteral("pb")).objects.size(), 1);
    QCOMPARE(names(m_store->summaries().value()), QStringList({QStringLiteral("Max Müller")}));
    QCOMPARE(m_store->contact(1).error().code(), ErrorCode::NotFound);
    ContactChanges changes;
    QVERIFY(syncOnce(&changes));
    QCOMPARE(changes.removed, 0);
}

void TestContactsService::searchFindsNameNumberMailAndCompany()
{
    QVERIFY(syncOnce());

    QCOMPARE(m_store->search(QStringLiteral("muller")).value(), QSet<quint32>({2}));
    QCOMPARE(m_store->search(QStringLiteral("MUSTER")).value(), QSet<quint32>({1, 3}));
    QCOMPARE(m_store->search(QStringLiteral("1234567")).value(), QSet<quint32>({1, 3}));
    QCOMPARE(m_store->search(QStringLiteral("030 1234")).value(), QSet<quint32>({2}));
    QCOMPARE(m_store->search(QStringLiteral("example.org")).value(), QSet<quint32>({1}));
    QCOMPARE(m_store->search(QStringLiteral("beispiel")).value(), QSet<quint32>({2}));
    QVERIFY(m_store->search(QStringLiteral("100%_")).value().isEmpty());
}

void TestContactsService::duplicatesAreFoundByNumberMailAndSimilarName()
{
    Contact mail = maxMueller();
    mail.luid = 7;
    mail.familyName = QStringLiteral("Anders");
    mail.givenName = QStringLiteral("Paula");
    mail.displayName = QStringLiteral("Paula Anders");
    mail.phones.clear();
    mail.emails = {value(71, emailType::work, QStringLiteral("ERIKA@example.org "))};
    Contact name = maxMueller();
    name.luid = 8;
    name.phones.clear();
    name.organization.clear();
    name.displayName = QStringLiteral("Müller Max");

    const QList<DuplicatePair> pairs =
        ContactsService::findDuplicates({erika(), maxMueller(), erikaTwice(), mail, name});

    QCOMPARE(pairs.size(), 3);
    QCOMPARE(pairs.at(0), (DuplicatePair{1, 3, DuplicatePair::Reason::Number}));
    QCOMPARE(pairs.at(1), (DuplicatePair{1, 7, DuplicatePair::Reason::Email}));
    QCOMPARE(pairs.at(2), (DuplicatePair{2, 8, DuplicatePair::Reason::Name}));
    QVERIFY(ContactsService::findDuplicates({maxMueller()}).isEmpty());
}

void TestContactsService::mergeJoinsTheEntriesAndDeletesTheSecondContact()
{
    QVERIFY(syncOnce());

    const QFuture<Result<quint32>> merged = m_service->merge(1, 3);

    QVERIFY(waitFor(merged, timeoutMs));
    QVERIFY(merged.result().hasValue());
    QCOMPARE(merged.result().value(), 1u);
    const Contact joined = m_store->contact(1).value();
    QCOMPARE(joined.phones.size(), 2);
    QCOMPARE(joined.phones.at(1).value, QStringLiteral("+49 40 999999"));
    QCOMPARE(joined.note, QStringLiteral("Zweiter Eintrag"));
    QCOMPARE(m_store->contact(3).error().code(), ErrorCode::NotFound);
    QCOMPARE(m_phone->area(QStringLiteral("pb")).objects.size(), 2);
}

void TestContactsService::importCreatesEveryContactAndSkipsWhatThePhoneCannotTake()
{
    QVERIFY(syncOnce());
    Contact first;
    first.givenName = QStringLiteral("Import");
    first.accountType = QStringLiteral("com.google");
    Contact second;
    second.organization = QStringLiteral("Firma ohne Namen");
    Contact tooMany;
    tooMany.givenName = QStringLiteral("Viele");
    for (int i = 0; i < 21; ++i)
        tooMany.phones.append(value(0, phoneType::mobile, QStringLiteral("+49301%1").arg(i)));

    const QFuture<Result<ImportOutcome>> imported =
        m_service->importContacts({first, second, tooMany});

    QVERIFY(waitFor(imported, timeoutMs));
    QVERIFY(imported.result().hasValue());
    QCOMPARE(imported.result().value().created, 2);
    QCOMPARE(imported.result().value().skipped, 1);
    QCOMPARE(m_store->summaries().value().size(), 5);
    const QList<std::pair<QString, QByteArray>> puts = m_phone->syncPuts();
    for (const auto &put : puts)
        QVERIFY(!put.second.contains("X-ACCOUNT"));
}

void TestContactsService::defaultAccountIsSentToThePhone()
{
    QVERIFY(syncOnce());

    const QFuture<Result<void>> done = m_service->setDefaultAccount(
        QStringLiteral("com.google"), QStringLiteral("erika@example.org"));

    QVERIFY(waitFor(done, timeoutMs));
    QVERIFY(done.result().hasValue());
    const QByteArray sent = m_phone->syncPuts().constLast().second;
    QCOMPARE(m_phone->syncPuts().constLast().first, QStringLiteral("telecom/pb/metadata.xml"));
    QVERIFY(sent.contains("name=\"erika@example.org\"  default=\"1\""));
    QVERIFY(!sent.contains("name=\"null\"  default"));
    const PbMetadata stored = m_store->metadata().value();
    QCOMPARE(stored.accounts.at(0).name, QStringLiteral("erika@example.org"));
    QVERIFY(stored.accounts.at(0).isDefault);

    const QFuture<Result<void>> unknown =
        m_service->setDefaultAccount(QStringLiteral("x"), QStringLiteral("y"));
    QVERIFY(waitFor(unknown, timeoutMs));
    QCOMPARE(unknown.result().error().code(), ErrorCode::NotFound);
}

void TestContactsService::dialSendsTheNumberAndRefusesWhatIsNoNumber_data()
{
    QTest::addColumn<QString>("number");
    QTest::addColumn<QString>("sent");

    QTest::newRow("international")
        << QStringLiteral("+49 (151) 123-456/7") << QStringLiteral("ATD+491511234567;");
    QTest::newRow("service code") << QStringLiteral("*100#") << QStringLiteral("ATD*100#;");
    QTest::newRow("letters") << QStringLiteral("0151 abc") << QString();
    QTest::newRow("command injection") << QStringLiteral("0151;ATA") << QString();
    QTest::newRow("plus inside") << QStringLiteral("01+51") << QString();
    QTest::newRow("too short") << QStringLiteral("12") << QString();
}

void TestContactsService::dialSendsTheNumberAndRefusesWhatIsNoNumber()
{
    QFETCH(QString, number);
    QFETCH(QString, sent);
    m_phone->clearLog();

    const QFuture<Result<void>> dialled = m_service->dial(number);

    QVERIFY(waitFor(dialled, timeoutMs));
    if (sent.isEmpty()) {
        QCOMPARE(dialled.result().error().code(), ErrorCode::InvalidInput);
        const QStringList commands = m_phone->receivedCommands();
        for (const QString &command : commands)
            QVERIFY(!command.startsWith(QStringLiteral("ATD")));
    } else {
        QVERIFY(dialled.result().hasValue());
        QVERIFY(m_phone->receivedCommands().contains(sent));
    }
}

void TestContactsService::callsWithoutStoreOrConnectionFail()
{
    ContactsService withoutStore(m_connection.get());
    const QFuture<Result<ContactChanges>> noStore = withoutStore.sync();
    QVERIFY(waitFor(noStore));
    QCOMPARE(noStore.result().error().code(), ErrorCode::InvalidInput);

    QVERIFY(syncOnce());
    m_connection->disconnectFromPhone();
    const QFuture<Result<SaveOutcome>> offline =
        m_service->save(maxMueller(), false, SaveMode::CheckConflict);
    QVERIFY(waitFor(offline));
    QCOMPARE(offline.result().error().code(), ErrorCode::ConnectionClosed);
    const QFuture<Result<void>> dialled = m_service->dial(QStringLiteral("+4930123456"));
    QVERIFY(waitFor(dialled));
    QCOMPARE(dialled.result().error().code(), ErrorCode::ConnectionClosed);
}

// The photo files of a profile grew with every photo change and deletion:
// the store wrote the new photo and kept the old file, which nothing pointed
// at any more. A photo two contacts share stays while one still uses it.
void TestContactsService::replacedOrDeletedPhotoLeavesNoFileBehind()
{
    const auto photo = [](char fill) {
        return QByteArray("\xFF\xD8\xFF", 3) + QByteArray(2000, fill);
    };
    const auto file = [this](const QByteArray &data) {
        return m_profile->directory() + QStringLiteral("/blobs/") +
               QString::fromLatin1(
                   QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    };
    Contact first = erika();
    first.photo = photo('a');
    Contact second = maxMueller();
    second.photo = photo('b');
    Contact third = erikaTwice();
    third.photo = photo('b');
    m_phone->setObjects(QStringLiteral("pb"), {{1, encodeAsPhoneSendsIt(first)},
                                               {2, encodeAsPhoneSendsIt(second)},
                                               {3, encodeAsPhoneSendsIt(third)}});
    QVERIFY(syncOnce());
    QVERIFY(QFile::exists(file(photo('a'))));
    QVERIFY(QFile::exists(file(photo('b'))));

    first.photo = photo('c');
    m_phone->editObject(QStringLiteral("pb"), 1, encodeAsPhoneSendsIt(first));
    QVERIFY(syncOnce());
    QVERIFY(!QFile::exists(file(photo('a'))));
    QVERIFY(QFile::exists(file(photo('c'))));

    m_phone->removeObject(QStringLiteral("pb"), 2);
    QVERIFY(syncOnce());
    QVERIFY(QFile::exists(file(photo('b'))));

    const QFuture<Result<void>> removed = m_service->remove({3});
    QVERIFY(waitFor(removed, timeoutMs));
    QVERIFY(removed.result().hasValue());
    QVERIFY(!QFile::exists(file(photo('b'))));

    first.photo.clear();
    m_phone->editObject(QStringLiteral("pb"), 1, encodeAsPhoneSendsIt(first));
    m_phone->forgetChanges(QStringLiteral("pb"));
    QVERIFY(syncOnce());
    QVERIFY(!QFile::exists(file(photo('c'))));
    QCOMPARE(m_store->contact(1).value().photo, QByteArray());
}

QTEST_GUILESS_MAIN(TestContactsService)

#include "tst_contactsservice.moc"
