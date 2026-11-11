#include "core/codec/PhoneVCard.h"

#include <QTest>

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

QByteArray fakeJpeg(qsizetype size = 64)
{
    QByteArray data("\xFF\xD8\xFF\xE0", 4);
    data += QByteArray(size - 6, 'j');
    data += QByteArray("\xFF\xD9", 2);
    return data;
}

// The client's own example from the protocol notes, with placeholder values.
QByteArray clientCard()
{
    return "BEGIN:VCARD\r\n"
           "N;CHARSET=UTF-8:Family;Given;Middle;Prefix;Suffix\r\n"
           "FN;CHARSET=UTF-8:Prefix Given Middle Family Suffix\r\n"
           "NICKNAME;X-SYNCMLREF101:Nick\r\n"
           "ORG:Example Corp;Sales\r\n"
           "TITLE:Engineer\r\n"
           "NOTE;ENCODING=QUOTED-PRINTABLE:Line one=0D=0ALine two\r\n"
           "TEL;CELL;PREF;X-SYNCMLREF102:+10000000001\r\n"
           "TEL;HOME;X-SYNCMLREF103:+10000000002\r\n"
           "TEL;LABEL=Desk=3B1;X-SYNCMLREF104:+10000000003\r\n"
           "EMAIL;WORK;PREF;X-SYNCMLREF105:user@example.org\r\n"
           "EMAIL;HOME;X-SYNCMLREF106:user2@example.org\r\n"
           "IMPP;X-SYNCMLREF107:xmpp:user@example.org\r\n"
           "URL;HOMEPAGE;X-SYNCMLREF108:https://example.org/\r\n"
           "ADR;HOME;X-SYNCMLREF109:;;1 Example Street;Example City;Example Region;00000;"
           "Example Country\r\n"
           "BDAY:19900131\r\n"
           "X-ANNIVERSARY:20100214\r\n"
           "X-EVENT;LABEL=First day;X-SYNCMLREF110:20200101\r\n"
           "CATEGORIES:Coworkers;My Contacts\r\n"
           "PHOTO;ENCODING=BASE64;TYPE=JPEG:" +
           fakeJpeg().toBase64() +
           "\r\n"
           "\r\n"
           "X-IRMC-LUID:00000000003E\r\n"
           "X-ACCOUNT:com.google;user@example.org\r\n"
           "END:VCARD\r\n";
}

ContactValue entry(quint64 dataId, int type, const QString &value, bool primary = false)
{
    ContactValue result;
    result.dataId = dataId;
    result.type = type;
    result.value = value;
    result.primary = primary;
    return result;
}

Contact phoneContact()
{
    Contact contact;
    contact.luid = 0x4D;
    contact.familyName = QStringLiteral("Family");
    contact.givenName = QStringLiteral("Given");
    contact.displayName = QStringLiteral("Given Family");
    contact.phones = {entry(102, phoneType::mobile, QStringLiteral("+10000000001")),
                      entry(103, phoneType::home, QStringLiteral("+10000000002")),
                      entry(104, phoneType::work, QStringLiteral("+10000000004"))};
    contact.emails = {entry(105, emailType::home, QStringLiteral("user@example.org"), true)};
    contact.groups = {QStringLiteral("Friends")};
    contact.photo = fakeJpeg();
    return contact;
}

QList<QByteArray> lines(const QByteArray &body)
{
    QList<QByteArray> result = body.split('\n');
    for (QByteArray &line : result) {
        if (line.endsWith('\r'))
            line.chop(1);
    }
    if (!result.isEmpty() && result.last().isEmpty())
        result.removeLast();
    return result;
}

} // namespace

class TestPhoneVCard : public QObject
{
    Q_OBJECT

public:
    explicit TestPhoneVCard(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void clientCardIsReadCompletely();
    void phoneTypeTokensSurviveARoundTrip_data();
    void phoneTypeTokensSurviveARoundTrip();
    void otherKindsKeepTheirTypes_data();
    void otherKindsKeepTheirTypes();
    void newContactIsWrittenInTheDialectThePhoneAccepts();
    void quotedPrintableOnlyForControlCharactersAndEquals();
    void customLabelIsEncodedLikeTheClient();
    void updateSendsOnlyChangedEntries();
    void entryLeftOutOrEmptiedIsDeletedByItsKey();
    void deletedAddressCarriesSevenEmptyParts();
    void unchangedPhotoAndGroupsAreNotSent();
    void photoChangeAndRemovalAreSent();
    void moreThanTwentyEntriesOfAKindAreRefused();
    void photoAboveTheLimitIsRefused();
    void contactWithoutNameNumberMailOrCompanyIsRefused();
    void emojiAndUmlautsSurviveARoundTrip();
    void incompleteCardIsRejected();
    void cardWithTooManyEntriesIsRejected();
    void displayNameFallsBackToCompanyAndNumber();
};

void TestPhoneVCard::clientCardIsReadCompletely()
{
    const Result<Contact> parsed = parsePhoneVCard(clientCard());

    QVERIFY(parsed.hasValue());
    const Contact &contact = parsed.value();
    QCOMPARE(contact.luid, 0x3Eu);
    QCOMPARE(contact.familyName, QStringLiteral("Family"));
    QCOMPARE(contact.givenName, QStringLiteral("Given"));
    QCOMPARE(contact.middleName, QStringLiteral("Middle"));
    QCOMPARE(contact.prefix, QStringLiteral("Prefix"));
    QCOMPARE(contact.suffix, QStringLiteral("Suffix"));
    QCOMPARE(contact.displayName, QStringLiteral("Prefix Given Middle Family Suffix"));
    QCOMPARE(contact.nickname, QStringLiteral("Nick"));
    QCOMPARE(contact.nicknameId, 101u);
    QCOMPARE(contact.organization, QStringLiteral("Example Corp"));
    QCOMPARE(contact.department, QStringLiteral("Sales"));
    QCOMPARE(contact.title, QStringLiteral("Engineer"));
    QCOMPARE(contact.note, QStringLiteral("Line one\r\nLine two"));

    QCOMPARE(contact.phones.size(), 3);
    QCOMPARE(contact.phones.at(0),
             entry(102, phoneType::mobile, QStringLiteral("+10000000001"), true));
    QCOMPARE(contact.phones.at(1), entry(103, phoneType::home, QStringLiteral("+10000000002")));
    QCOMPARE(contact.phones.at(2).type, phoneType::custom);
    QCOMPARE(contact.phones.at(2).label, QStringLiteral("Desk;1"));
    QCOMPARE(contact.phones.at(2).dataId, 104u);
    QCOMPARE(contact.emails.size(), 2);
    QCOMPARE(contact.emails.at(0),
             entry(105, emailType::work, QStringLiteral("user@example.org"), true));
    QCOMPARE(contact.emails.at(1),
             entry(106, emailType::home, QStringLiteral("user2@example.org")));
    QCOMPARE(contact.ims.size(), 1);
    QCOMPARE(contact.ims.at(0), entry(107, imProtocol::jabber, QStringLiteral("user@example.org")));
    QCOMPARE(contact.websites.size(), 1);
    QCOMPARE(contact.websites.at(0),
             entry(108, websiteType::homepage, QStringLiteral("https://example.org/")));
    QCOMPARE(contact.addresses.size(), 1);
    QCOMPARE(contact.addresses.at(0).dataId, 109u);
    QCOMPARE(contact.addresses.at(0).type, addressType::home);
    QCOMPARE(contact.addresses.at(0).address,
             QStringList({QString(), QString(), QStringLiteral("1 Example Street"),
                          QStringLiteral("Example City"), QStringLiteral("Example Region"),
                          QStringLiteral("00000"), QStringLiteral("Example Country")}));
    QCOMPARE(contact.birthday, QDate(1990, 1, 31));
    QCOMPARE(contact.anniversary, QDate(2010, 2, 14));
    QCOMPARE(contact.events.size(), 1);
    QCOMPARE(contact.events.at(0).label, QStringLiteral("First day"));
    QCOMPARE(contact.events.at(0).value, QStringLiteral("2020-01-01"));
    QCOMPARE(contact.groups,
             QStringList({QStringLiteral("Coworkers"), QStringLiteral("My Contacts")}));
    QCOMPARE(contact.photo, fakeJpeg());
    QCOMPARE(contact.accountType, QStringLiteral("com.google"));
    QCOMPARE(contact.accountName, QStringLiteral("user@example.org"));
}

void TestPhoneVCard::phoneTypeTokensSurviveARoundTrip_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<QByteArray>("header");

    QTest::newRow("home") << phoneType::home << QByteArray("TEL;HOME;");
    QTest::newRow("mobile") << phoneType::mobile << QByteArray("TEL;CELL;");
    QTest::newRow("work") << phoneType::work << QByteArray("TEL;WORK;");
    QTest::newRow("fax work") << phoneType::faxWork << QByteArray("TEL;FAX;WORK;");
    QTest::newRow("fax home") << phoneType::faxHome << QByteArray("TEL;FAX;HOME;");
    QTest::newRow("pager") << phoneType::pager << QByteArray("TEL;PAGER;");
    QTest::newRow("other") << phoneType::other << QByteArray("TEL;X-SYNCMLREF");
    QTest::newRow("callback") << phoneType::callback << QByteArray("TEL;CALLBACK;");
    QTest::newRow("car") << phoneType::car << QByteArray("TEL;CAR;");
    QTest::newRow("company") << phoneType::companyMain << QByteArray("TEL;COMPANY;");
    QTest::newRow("isdn") << phoneType::isdn << QByteArray("TEL;ISDN;");
    QTest::newRow("main") << phoneType::main << QByteArray("TEL;MAIN;");
    QTest::newRow("other fax") << phoneType::otherFax << QByteArray("TEL;FAX;OTHER;");
    QTest::newRow("radio") << phoneType::radio << QByteArray("TEL;RADIO;");
    QTest::newRow("telex") << phoneType::telex << QByteArray("TEL;TELEX;");
    QTest::newRow("tty") << phoneType::tty << QByteArray("TEL;TTY;");
    QTest::newRow("work mobile") << phoneType::workMobile << QByteArray("TEL;CELL;WORK;");
    QTest::newRow("work pager") << phoneType::workPager << QByteArray("TEL;PAGER;WORK;");
    QTest::newRow("assistant") << phoneType::assistant << QByteArray("TEL;ASSISTANT;");
    QTest::newRow("mms") << phoneType::mms << QByteArray("TEL;MMS;");
}

// The phone matches the type by prefix, so the token has to follow "TEL;"
// directly, in the order the phone writes it.
void TestPhoneVCard::phoneTypeTokensSurviveARoundTrip()
{
    QFETCH(int, type);
    QFETCH(QByteArray, header);
    Contact contact;
    contact.luid = 7;
    contact.givenName = QStringLiteral("Test");
    contact.phones = {entry(5, type, QStringLiteral("+4930123456"))};

    const QByteArray written = encodeAsPhoneSendsIt(contact);
    const Result<QByteArray> created = encodeNewContact(contact);
    const Result<Contact> read = parsePhoneVCard(written);

    QVERIFY(written.contains(header));
    QVERIFY(created.hasValue());
    QVERIFY(created.value().contains(header.left(header.size() - 1) + ':') ||
            created.value().contains("TEL:"));
    QVERIFY(read.hasValue());
    QCOMPARE(read.value().phones.size(), 1);
    QCOMPARE(read.value().phones.at(0).type, type);
}

void TestPhoneVCard::otherKindsKeepTheirTypes_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("type");

    for (const int type : {emailType::home, emailType::work, emailType::other, emailType::mobile})
        QTest::addRow("email %d", type) << int(ContactValueKind::Email) << type;
    for (const int type : {addressType::home, addressType::work, addressType::other})
        QTest::addRow("address %d", type) << int(ContactValueKind::Address) << type;
    for (int type = websiteType::homepage; type <= websiteType::other; ++type)
        QTest::addRow("website %d", type) << int(ContactValueKind::Website) << type;
    for (int type = imProtocol::aim; type <= imProtocol::netMeeting; ++type)
        QTest::addRow("messenger %d", type) << int(ContactValueKind::Im) << type;
    for (const int type : {eventType::anniversary, eventType::other, eventType::birthday})
        QTest::addRow("date %d", type) << int(ContactValueKind::Event) << type;
}

void TestPhoneVCard::otherKindsKeepTheirTypes()
{
    QFETCH(int, kind);
    QFETCH(int, type);
    const auto valueKind = static_cast<ContactValueKind>(kind);
    Contact contact;
    contact.luid = 3;
    contact.givenName = QStringLiteral("Test");
    ContactValue value = entry(9, type, QStringLiteral("2021-05-06"));
    if (valueKind == ContactValueKind::Address) {
        value.value.clear();
        value.address = {QString(),
                         QString(),
                         QStringLiteral("Hauptstraße 1"),
                         QStringLiteral("Köln"),
                         QString(),
                         QStringLiteral("50667"),
                         QStringLiteral("Deutschland")};
    }
    contact.values(valueKind) = {value};

    const Result<Contact> read = parsePhoneVCard(encodeAsPhoneSendsIt(contact));

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().values(valueKind).size(), 1);
    QCOMPARE(read.value().values(valueKind).at(0).type, type);
    QCOMPARE(read.value().values(valueKind).at(0).dataId, 9u);
    if (valueKind == ContactValueKind::Address)
        QCOMPARE(read.value().values(valueKind).at(0).address, value.address);
    else
        QCOMPARE(read.value().values(valueKind).at(0).value, value.value);
}

void TestPhoneVCard::newContactIsWrittenInTheDialectThePhoneAccepts()
{
    Contact contact;
    contact.familyName = QStringLiteral("Mustermann");
    contact.givenName = QStringLiteral("Erika");
    contact.organization = QStringLiteral("Beispiel GmbH");
    contact.note = QString(300, u'n');
    contact.phones = {entry(0, phoneType::mobile, QStringLiteral("+4915112345678"), true)};
    contact.emails = {entry(0, emailType::home, QStringLiteral("erika@example.org"))};
    contact.birthday = QDate(1964, 8, 12);
    contact.groups = {QStringLiteral("Familie"), QStringLiteral("A;B")};
    contact.accountType = QStringLiteral("com.google");
    contact.accountName = QStringLiteral("erika@example.org");
    contact.photo = fakeJpeg();

    const Result<QByteArray> written = encodeNewContact(contact);

    QVERIFY(written.hasValue());
    const QList<QByteArray> got = lines(written.value());
    const QList<QByteArray> expected = {
        "BEGIN:VCARD",
        "N:Mustermann;Erika;;;",
        "FN:Erika Mustermann",
        "ORG:Beispiel GmbH;",
        "NOTE:" + QByteArray(300, 'n'),
        "TEL;CELL;PREF:+4915112345678",
        "EMAIL;HOME:erika@example.org",
        "BDAY:19640812",
        "CATEGORIES:Familie;A\\;B",
        "X-ACCOUNT:com.google;erika@example.org",
        "PHOTO;ENCODING=BASE64;TYPE=JPEG:" + fakeJpeg().toBase64(),
        "",
        "END:VCARD",
    };
    QCOMPARE(got, expected);
    QVERIFY(!written.value().contains("VERSION"));
    QVERIFY(!written.value().contains("X-SYNCMLREF"));
    QVERIFY(!written.value().contains("X-IRMC-LUID"));
}

void TestPhoneVCard::quotedPrintableOnlyForControlCharactersAndEquals()
{
    Contact contact;
    contact.givenName = QStringLiteral("Jürgen");
    contact.note = QStringLiteral("a=b\nGrüße");

    const Result<QByteArray> written = encodeNewContact(contact);

    QVERIFY(written.hasValue());
    QVERIFY(written.value().contains("N;CHARSET=UTF-8:;Jürgen;;;\r\n"));
    QVERIFY(
        written.value().contains("NOTE;ENCODING=QUOTED-PRINTABLE;CHARSET=UTF-8:a=3Db=0AGrüße\r\n"));
    const Result<Contact> read = parsePhoneVCard(written.value());
    QVERIFY(read.hasValue());
    QCOMPARE(read.value().note, contact.note);
    QCOMPARE(read.value().givenName, contact.givenName);
}

void TestPhoneVCard::customLabelIsEncodedLikeTheClient()
{
    Contact contact;
    contact.givenName = QStringLiteral("Test");
    ContactValue custom = entry(0, phoneType::custom, QStringLiteral("+4930123"));
    custom.label = QStringLiteral("Desk;1: \"A\"");
    contact.phones = {custom};

    const Result<QByteArray> written = encodeNewContact(contact);

    QVERIFY(written.hasValue());
    QVERIFY(written.value().contains("TEL;LABEL=Desk=3B1=3A =22A=22:+4930123\r\n"));
    QCOMPARE(parsePhoneVCard(written.value()).value().phones.at(0).label, custom.label);
}

void TestPhoneVCard::updateSendsOnlyChangedEntries()
{
    const Contact phone = phoneContact();
    Contact edited = phone;
    edited.phones[0].value = QStringLiteral("+10000000009");
    edited.phones.removeAt(1);
    edited.phones.append(entry(0, phoneType::work, QStringLiteral("+10000000010")));

    const Result<QByteArray> written = encodeContactUpdate(phone, edited);

    QVERIFY(written.hasValue());
    const QList<QByteArray> expected = {
        "BEGIN:VCARD",
        "N:Family;Given;;;",
        "FN:Given Family",
        "TEL;CELL;X-SYNCMLREF102:+10000000009",
        "TEL;WORK:+10000000010",
        "TEL;X-SYNCMLREF103:",
        "END:VCARD",
    };
    QCOMPARE(lines(written.value()), expected);
}

void TestPhoneVCard::entryLeftOutOrEmptiedIsDeletedByItsKey()
{
    const Contact phone = phoneContact();
    Contact edited = phone;
    edited.emails[0].value.clear();
    edited.phones.removeLast();

    const Result<QByteArray> written = encodeContactUpdate(phone, edited);

    QVERIFY(written.hasValue());
    QVERIFY(written.value().contains("\r\nEMAIL;X-SYNCMLREF105:\r\n"));
    QVERIFY(written.value().contains("\r\nTEL;X-SYNCMLREF104:\r\n"));
    QVERIFY(!written.value().contains("SYNCMLREF102"));
}

void TestPhoneVCard::deletedAddressCarriesSevenEmptyParts()
{
    Contact phone = phoneContact();
    ContactValue address;
    address.dataId = 120;
    address.type = addressType::home;
    address.address = {QString(),
                       QString(),
                       QStringLiteral("Weg 1"),
                       QStringLiteral("Bonn"),
                       QString(),
                       QStringLiteral("53111"),
                       QString()};
    phone.addresses = {address};
    Contact edited = phone;
    edited.addresses.clear();

    const Result<QByteArray> written = encodeContactUpdate(phone, edited);

    QVERIFY(written.hasValue());
    QVERIFY(written.value().contains("\r\nADR;X-SYNCMLREF120:;;;;;;\r\n"));
}

void TestPhoneVCard::unchangedPhotoAndGroupsAreNotSent()
{
    const Contact phone = phoneContact();
    Contact edited = phone;
    edited.groups = {QStringLiteral("Friends")};

    const Result<QByteArray> written = encodeContactUpdate(phone, edited);

    QVERIFY(written.hasValue());
    QVERIFY(!written.value().contains("PHOTO"));
    QVERIFY(!written.value().contains("CATEGORIES"));
    QVERIFY(!written.value().contains("TEL"));
    QVERIFY(!written.value().contains("EMAIL"));
}

void TestPhoneVCard::photoChangeAndRemovalAreSent()
{
    const Contact phone = phoneContact();
    Contact changed = phone;
    changed.photo = fakeJpeg(128);
    Contact removed = phone;
    removed.photo.clear();

    const Result<QByteArray> withPhoto = encodeContactUpdate(phone, changed);
    const Result<QByteArray> withoutPhoto = encodeContactUpdate(phone, removed);

    QVERIFY(withPhoto.hasValue());
    QVERIFY(withPhoto.value().contains(
        "PHOTO;ENCODING=BASE64;TYPE=JPEG:" + fakeJpeg(128).toBase64() + "\r\n\r\nEND:VCARD"));
    QVERIFY(withoutPhoto.hasValue());
    QVERIFY(withoutPhoto.value().contains("\r\nPHOTO;ACTION=DELETE:\r\n"));
}

void TestPhoneVCard::moreThanTwentyEntriesOfAKindAreRefused()
{
    Contact contact;
    contact.givenName = QStringLiteral("Many");
    for (int i = 0; i < 20; ++i)
        contact.phones.append(entry(0, phoneType::mobile, QStringLiteral("+49301000%1").arg(i)));
    QVERIFY(encodeNewContact(contact).hasValue());

    contact.phones.append(entry(0, phoneType::mobile, QStringLiteral("+4930100099")));
    const Result<QByteArray> tooMany = encodeNewContact(contact);
    const Result<QByteArray> tooManyChanges = encodeContactUpdate(Contact(), contact);

    QVERIFY(!tooMany.hasValue());
    QCOMPARE(tooMany.error().code(), ErrorCode::LimitExceeded);
    QVERIFY(!tooManyChanges.hasValue());
    QCOMPARE(tooManyChanges.error().code(), ErrorCode::LimitExceeded);
}

void TestPhoneVCard::photoAboveTheLimitIsRefused()
{
    Contact contact;
    contact.givenName = QStringLiteral("Photo");
    contact.photo = fakeJpeg(maxPhotoBytesToSend);
    QVERIFY(encodeNewContact(contact).hasValue());

    contact.photo = fakeJpeg(maxPhotoBytesToSend + 1);
    const Result<QByteArray> created = encodeNewContact(contact);
    const Result<QByteArray> updated = encodeContactUpdate(phoneContact(), contact);

    QVERIFY(!created.hasValue());
    QCOMPARE(created.error().code(), ErrorCode::LimitExceeded);
    QVERIFY(!updated.hasValue());
    QCOMPARE(updated.error().code(), ErrorCode::LimitExceeded);
}

void TestPhoneVCard::contactWithoutNameNumberMailOrCompanyIsRefused()
{
    Contact contact;
    contact.note = QStringLiteral("only a note");
    contact.phones = {entry(0, phoneType::mobile, QStringLiteral("   "))};

    const Result<QByteArray> created = encodeNewContact(contact);

    QVERIFY(!isSavable(contact));
    QVERIFY(!created.hasValue());
    QCOMPARE(created.error().code(), ErrorCode::InvalidInput);
    contact.organization = QStringLiteral("Firma");
    QVERIFY(isSavable(contact));
}

void TestPhoneVCard::emojiAndUmlautsSurviveARoundTrip()
{
    Contact contact;
    contact.luid = 12;
    contact.givenName = QStringLiteral("Zoë 😀");
    contact.familyName = QStringLiteral("Größe;Weiß");
    contact.displayName = QStringLiteral("Zoë 😀 Größe;Weiß");
    contact.note = QStringLiteral("Zeile 1\r\nZeile 2 = Ende 😀");
    contact.groups = {QStringLiteral("Familie ❤")};

    const Result<Contact> read = parsePhoneVCard(encodeAsPhoneSendsIt(contact));

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().givenName, contact.givenName);
    QCOMPARE(read.value().familyName, contact.familyName);
    QCOMPARE(read.value().displayName, contact.displayName);
    QCOMPARE(read.value().note, contact.note);
    QCOMPARE(read.value().groups, contact.groups);
}

void TestPhoneVCard::incompleteCardIsRejected()
{
    const Result<Contact> withoutEnd =
        parsePhoneVCard("BEGIN:VCARD\r\nN:A;B;;;\r\nX-IRMC-LUID:000000000001\r\n");
    const Result<Contact> withoutBegin = parsePhoneVCard("N:A;B;;;\r\nEND:VCARD\r\n");

    QVERIFY(!withoutEnd.hasValue());
    QCOMPARE(withoutEnd.error().code(), ErrorCode::InvalidInput);
    QVERIFY(!withoutBegin.hasValue());
    QCOMPARE(withoutBegin.error().code(), ErrorCode::InvalidInput);
}

void TestPhoneVCard::cardWithTooManyEntriesIsRejected()
{
    QByteArray card = "BEGIN:VCARD\r\nN:A;B;;;\r\n";
    for (qsizetype i = 0; i <= maxValuesPerKind; ++i)
        card += "TEL;CELL;X-SYNCMLREF" + QByteArray::number(i) + ":+49301\r\n";
    card += "END:VCARD\r\n";

    const Result<Contact> read = parsePhoneVCard(card);

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::LimitExceeded);
}

void TestPhoneVCard::displayNameFallsBackToCompanyAndNumber()
{
    Contact contact;
    contact.phones = {entry(1, phoneType::mobile, QStringLiteral("+4930555"))};
    QCOMPARE(displayNameOf(contact), QStringLiteral("+4930555"));
    contact.organization = QStringLiteral("Ärzte GmbH");
    QCOMPARE(displayNameOf(contact), QStringLiteral("Ärzte GmbH"));
    QCOMPARE(sortKeyOf(contact), QStringLiteral("arzte gmbh"));
    contact.givenName = QStringLiteral("Erika");
    QCOMPARE(displayNameOf(contact), QStringLiteral("Erika"));
}

QTEST_GUILESS_MAIN(TestPhoneVCard)

#include "tst_phonevcard.moc"
