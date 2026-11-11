#include "core/codec/CsvTable.h"
#include "core/codec/VCardInterchange.h"

#include <QStringDecoder>
#include <QTest>

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

QByteArray fakeJpeg()
{
    return QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(200, 'p') + QByteArray("\xFF\xD9", 2);
}

ContactValue entry(int type, const QString &value)
{
    ContactValue result;
    result.type = type;
    result.value = value;
    return result;
}

Contact fullContact()
{
    Contact contact;
    contact.familyName = QStringLiteral("Müller");
    contact.givenName = QStringLiteral("Jörg");
    contact.middleName = QStringLiteral("Peter");
    contact.prefix = QStringLiteral("Dr.");
    contact.suffix = QStringLiteral("jr.");
    contact.displayName = QStringLiteral("Dr. Jörg Peter Müller jr.");
    contact.nickname = QStringLiteral("Jojo");
    contact.organization = QStringLiteral("Beispiel, Söhne & Co");
    contact.department = QStringLiteral("Einkauf");
    contact.title = QStringLiteral("Leiter; Einkauf");
    contact.note = QStringLiteral("Erste Zeile\nZweite Zeile mit \\ und 😀");
    ContactValue custom = entry(phoneType::custom, QStringLiteral("+49 30 1234"));
    custom.label = QStringLiteral("Büro, Platz 2");
    contact.phones = {entry(phoneType::mobile, QStringLiteral("+4915112345678")),
                      entry(phoneType::faxHome, QStringLiteral("+49301111")),
                      entry(phoneType::workMobile, QStringLiteral("+49152")),
                      entry(phoneType::other, QStringLiteral("0800 123")), custom};
    contact.phones[0].primary = true;
    contact.emails = {entry(emailType::work, QStringLiteral("joerg@example.org")),
                      entry(emailType::other, QStringLiteral("jojo@example.net"))};
    ContactValue address;
    address.type = addressType::home;
    address.address = {QString(),
                       QString(),
                       QStringLiteral("Hauptstraße 5; Hinterhaus"),
                       QStringLiteral("Köln"),
                       QStringLiteral("NRW"),
                       QStringLiteral("50667"),
                       QStringLiteral("Deutschland")};
    contact.addresses = {address};
    contact.websites = {entry(websiteType::work, QStringLiteral("https://example.org/a,b"))};
    contact.ims = {entry(imProtocol::skype, QStringLiteral("jojo.mueller"))};
    ContactValue otherIm = entry(imProtocol::custom, QStringLiteral("jojo"));
    otherIm.label = QStringLiteral("Signal");
    contact.ims.append(otherIm);
    contact.events = {entry(eventType::other, QStringLiteral("2015-06-01"))};
    contact.birthday = QDate(1970, 2, 28);
    contact.anniversary = QDate(1999, 9, 9);
    contact.groups = {QStringLiteral("Familie"), QStringLiteral("Freunde, alte")};
    contact.photo = fakeJpeg();
    return contact;
}

} // namespace

class TestVCardInterchange : public QObject
{
    Q_OBJECT

public:
    explicit TestVCardInterchange(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void version21WithQuotedPrintableAndBareTypesIsRead();
    void version30WithFoldingTypeListsAndPhotoIsRead();
    void version40WithQuotedTypesPrefAndDataUriIsRead();
    void groupPrefixesAndOldMessengerPropertiesAreUnderstood();
    void writtenVersion30ReadsBackUnchanged();
    void linesAreFoldedAfter75BytesWithoutSplittingCharacters();
    void fileWithoutVCardIsRejected();
    void cardWithNothingToSaveIsSkipped();
    void contactTableReadsBackInBothLanguages_data();
    void contactTableReadsBackInBothLanguages();
    void tableQuotesSeparatorsQuotesAndLineBreaks();
    void tableNeutralisesSpreadsheetFormulasAndReadsThemBack_data();
    void tableNeutralisesSpreadsheetFormulasAndReadsThemBack();
    void tableWithCommasIsRead();
    void tableWithAnOpenQuoteIsRejected();
    void tableWithoutKnownColumnIsRejected();
    void tableThatIsNoUtf8IsRejected();
};

void TestVCardInterchange::version21WithQuotedPrintableAndBareTypesIsRead()
{
    const QByteArray file = "BEGIN:VCARD\r\n"
                            "VERSION:2.1\r\n"
                            "N;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:M=C3=BCller;J=C3=B6rg\r\n"
                            "TEL;CELL;PREF:+4915112345678\r\n"
                            "TEL;WORK;FAX:+49301111\r\n"
                            "TEL;HOME;VOICE:+49302222\r\n"
                            "NOTE;ENCODING=QUOTED-PRINTABLE:Erste Zeile=0D=0A=\r\n"
                            "Zweite Zeile\r\n"
                            "EMAIL;INTERNET;HOME:joerg@example.org\r\n"
                            "BDAY:19700228\r\n"
                            "END:VCARD\r\n";

    const Result<QList<Contact>> read = readVCards(file);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().size(), 1);
    const Contact &contact = read.value().constFirst();
    QCOMPARE(contact.familyName, QStringLiteral("Müller"));
    QCOMPARE(contact.givenName, QStringLiteral("Jörg"));
    QCOMPARE(contact.phones.size(), 3);
    QCOMPARE(contact.phones.at(0).type, phoneType::mobile);
    QVERIFY(contact.phones.at(0).primary);
    QCOMPARE(contact.phones.at(1).type, phoneType::faxWork);
    QCOMPARE(contact.phones.at(2).type, phoneType::home);
    QCOMPARE(contact.note, QStringLiteral("Erste Zeile\r\nZweite Zeile"));
    QCOMPARE(contact.emails.at(0).type, emailType::home);
    QCOMPARE(contact.birthday, QDate(1970, 2, 28));
}

void TestVCardInterchange::version30WithFoldingTypeListsAndPhotoIsRead()
{
    const QByteArray photo = fakeJpeg().toBase64();
    const QByteArray file = "BEGIN:VCARD\r\n"
                            "VERSION:3.0\r\n"
                            "FN:Erika Muster\r\n"
                            " mann\r\n"
                            "N:Mustermann;Erika;;;\r\n"
                            "TEL;TYPE=WORK,CELL:+4915200000\r\n"
                            "TEL;TYPE=HOME;TYPE=FAX:+49300000\r\n"
                            "ADR;TYPE=WORK:;;Weg 1\\; Hof;Bonn;;53111;Deutschland\r\n"
                            "ORG:Firma\\, groß;Vertrieb\r\n"
                            "NOTE:Zeile\\nZweite\\, mit Komma\r\n"
                            "CATEGORIES:Arbeit,Sport\r\n"
                            "PHOTO;ENCODING=b;TYPE=JPEG:" +
                            photo.left(40) + "\r\n " + photo.mid(40) +
                            "\r\n"
                            "END:VCARD\r\n";

    const Result<QList<Contact>> read = readVCards(file);

    QVERIFY(read.hasValue());
    const Contact &contact = read.value().constFirst();
    QCOMPARE(contact.displayName, QStringLiteral("Erika Mustermann"));
    QCOMPARE(contact.phones.at(0).type, phoneType::workMobile);
    QCOMPARE(contact.phones.at(1).type, phoneType::faxHome);
    QCOMPARE(contact.addresses.at(0).type, addressType::work);
    QCOMPARE(contact.addresses.at(0).address.at(addressPart::street), QStringLiteral("Weg 1; Hof"));
    QCOMPARE(contact.addresses.at(0).address.at(addressPart::postcode), QStringLiteral("53111"));
    QCOMPARE(contact.organization, QStringLiteral("Firma, groß"));
    QCOMPARE(contact.department, QStringLiteral("Vertrieb"));
    QCOMPARE(contact.note, QStringLiteral("Zeile\nZweite, mit Komma"));
    QCOMPARE(contact.groups, QStringList({QStringLiteral("Arbeit"), QStringLiteral("Sport")}));
    QCOMPARE(contact.photo, fakeJpeg());
}

void TestVCardInterchange::version40WithQuotedTypesPrefAndDataUriIsRead()
{
    const QByteArray file = "BEGIN:VCARD\r\n"
                            "VERSION:4.0\r\n"
                            "FN:Max Beispiel\r\n"
                            "TEL;VALUE=uri;TYPE=\"cell,voice\";PREF=1:tel:+491701234567\r\n"
                            "EMAIL;TYPE=work:max@example.org\r\n"
                            "IMPP;PREF=1:xmpp:max@example.org\r\n"
                            "ANNIVERSARY:20090808\r\n"
                            "PHOTO:data:image/jpeg;base64," +
                            fakeJpeg().toBase64() +
                            "\r\n"
                            "END:VCARD\r\n";

    const Result<QList<Contact>> read = readVCards(file);

    QVERIFY(read.hasValue());
    const Contact &contact = read.value().constFirst();
    QCOMPARE(contact.phones.at(0).type, phoneType::mobile);
    QCOMPARE(contact.phones.at(0).value, QStringLiteral("+491701234567"));
    QVERIFY(contact.phones.at(0).primary);
    QCOMPARE(contact.emails.at(0).type, emailType::work);
    QCOMPARE(contact.ims.at(0).type, imProtocol::jabber);
    QCOMPARE(contact.ims.at(0).value, QStringLiteral("max@example.org"));
    QCOMPARE(contact.anniversary, QDate(2009, 8, 8));
    QCOMPARE(contact.photo, fakeJpeg());
}

void TestVCardInterchange::groupPrefixesAndOldMessengerPropertiesAreUnderstood()
{
    const QByteArray file = "BEGIN:VCARD\r\n"
                            "VERSION:3.0\r\n"
                            "N:Apfel;Anna;;;\r\n"
                            "item1.TEL;type=pref:+49301\r\n"
                            "item1.X-ABLabel:Werkstatt\r\n"
                            "X-JABBER;TYPE=HOME:anna@example.org\r\n"
                            "X-SKYPE:anna.apfel\r\n"
                            "PHOTO;VALUE=uri:https://example.org/anna.jpg\r\n"
                            "END:VCARD\r\n";

    const Result<QList<Contact>> read = readVCards(file);

    QVERIFY(read.hasValue());
    const Contact &contact = read.value().constFirst();
    QCOMPARE(contact.phones.size(), 1);
    QCOMPARE(contact.phones.at(0).value, QStringLiteral("+49301"));
    QCOMPARE(contact.ims.size(), 2);
    QCOMPARE(contact.ims.at(0).type, imProtocol::jabber);
    QCOMPARE(contact.ims.at(1).type, imProtocol::skype);
    QVERIFY(contact.photo.isEmpty());
}

void TestVCardInterchange::writtenVersion30ReadsBackUnchanged()
{
    const Contact original = fullContact();

    const QByteArray written = writeVCards({original, original});
    const Result<QList<Contact>> read = readVCards(written);

    QVERIFY(written.startsWith("BEGIN:VCARD\r\nVERSION:3.0\r\n"));
    QVERIFY(read.hasValue());
    QCOMPARE(read.value().size(), 2);
    QCOMPARE(read.value().constFirst(), original);
}

void TestVCardInterchange::linesAreFoldedAfter75BytesWithoutSplittingCharacters()
{
    Contact contact;
    contact.givenName = QStringLiteral("Lang");
    contact.note = QString(200, QChar(u'ä')) + QStringLiteral("😀😀😀");
    contact.photo = QByteArray(3000, 'x');

    const QByteArray written = writeVCards({contact});

    const QList<QByteArray> lines = written.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        QVERIFY2(line.size() <= 75, line.constData());
        QStringDecoder decoder(QStringDecoder::Utf8);
        static_cast<void>(decoder.decode(line));
        QVERIFY2(!decoder.hasError(), line.constData());
    }
    QCOMPARE(readVCards(written).value().constFirst().note, contact.note);
    QCOMPARE(readVCards(written).value().constFirst().photo, contact.photo);
}

void TestVCardInterchange::fileWithoutVCardIsRejected()
{
    const Result<QList<Contact>> read = readVCards("Name;Nummer\r\nErika;0151\r\n");

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::InvalidInput);
}

void TestVCardInterchange::cardWithNothingToSaveIsSkipped()
{
    const Result<QList<Contact>> read =
        readVCards("BEGIN:VCARD\r\nVERSION:3.0\r\nNOTE:nur Notiz\r\nEND:VCARD\r\n"
                   "BEGIN:VCARD\r\nVERSION:3.0\r\nTEL:+49301\r\nEND:VCARD\r\n");

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().size(), 1);
    QCOMPARE(read.value().constFirst().phones.at(0).value, QStringLiteral("+49301"));
}

void TestVCardInterchange::contactTableReadsBackInBothLanguages_data()
{
    QTest::addColumn<int>("language");
    QTest::addColumn<QByteArray>("firstTitle");

    QTest::newRow("English") << int(CsvLanguage::English) << QByteArray("First name");
    QTest::newRow("German") << int(CsvLanguage::German) << QByteArray("Vorname");
}

void TestVCardInterchange::contactTableReadsBackInBothLanguages()
{
    QFETCH(int, language);
    QFETCH(QByteArray, firstTitle);
    Contact contact = fullContact();

    const QByteArray table = writeContactsCsv({contact}, static_cast<CsvLanguage>(language));
    const Result<QList<Contact>> read = readContactsCsv(table);

    QVERIFY(table.startsWith("\xEF\xBB\xBF" + firstTitle + ';'));
    QVERIFY(read.hasValue());
    QCOMPARE(read.value().size(), 1);
    const Contact &back = read.value().constFirst();
    QCOMPARE(back.givenName, contact.givenName);
    QCOMPARE(back.familyName, contact.familyName);
    QCOMPARE(back.organization, contact.organization);
    QCOMPARE(back.title, contact.title);
    QCOMPARE(back.note, contact.note);
    QCOMPARE(back.birthday, contact.birthday);
    const QStringList groups = {QStringLiteral("Familie"), QStringLiteral("Freunde"),
                                QStringLiteral("alte")};
    QCOMPARE(back.groups, groups);
    QStringList numbers;
    for (const ContactValue &phone : back.phones)
        numbers.append(phone.value);
    QCOMPARE(numbers.size(), 5);
    QVERIFY(numbers.contains(QStringLiteral("+4915112345678")));
    QVERIFY(numbers.contains(QStringLiteral("+49 30 1234")));
    QCOMPARE(back.emails.size(), 2);
    QCOMPARE(back.addresses.size(), 1);
    QCOMPARE(back.addresses.at(0).address.at(addressPart::street),
             QStringLiteral("Hauptstraße 5; Hinterhaus"));
}

void TestVCardInterchange::tableQuotesSeparatorsQuotesAndLineBreaks()
{
    const CsvRows rows = {{QStringLiteral("a;b"), QStringLiteral("say \"hi\""),
                           QStringLiteral("line\nbreak"), QStringLiteral(" padded"),
                           QStringLiteral("plain")}};

    const QByteArray written = writeCsv(rows);
    const Result<CsvRows> read = readCsv(written);

    QCOMPARE(written,
             QByteArray("\xEF\xBB\xBF\"a;b\";\"say \"\"hi\"\"\";\"line\nbreak\";\" padded\";"
                        "plain\r\n"));
    QVERIFY(read.hasValue());
    QCOMPARE(read.value(), rows);
}

void TestVCardInterchange::tableNeutralisesSpreadsheetFormulasAndReadsThemBack_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QByteArray>("written");

    QTest::newRow("formula") << QStringLiteral("=HYPERLINK(\"x\")")
                             << QByteArray("\"'=HYPERLINK(\"\"x\"\")\"");
    QTest::newRow("phone number") << QStringLiteral("+49 30 1234567")
                                  << QByteArray("\"'+49 30 1234567\"");
    QTest::newRow("minus") << QStringLiteral("-2+3") << QByteArray("\"'-2+3\"");
    QTest::newRow("at sign") << QStringLiteral("@SUM(A1)") << QByteArray("\"'@SUM(A1)\"");
    QTest::newRow("tab") << QStringLiteral("\t=1") << QByteArray("\"'\t=1\"");
    QTest::newRow("carriage return") << QStringLiteral("\r=1") << QByteArray("\"'\r=1\"");
    // An apostrophe the field already starts with survives the round trip.
    QTest::newRow("apostrophe before formula") << QStringLiteral("'=1")
                                               << QByteArray("\"''=1\"");
    QTest::newRow("apostrophe before text") << QStringLiteral("'Hallo") << QByteArray("'Hallo");
    QTest::newRow("plain") << QStringLiteral("Hallo") << QByteArray("Hallo");
    QTest::newRow("minus inside") << QStringLiteral("Müller-Lüdenscheidt")
                                  << QStringLiteral("Müller-Lüdenscheidt").toUtf8();
}

// A spreadsheet runs a cell that starts with one of = + - @ tab or carriage
// return as a formula. Following OWASP "CSV Injection", such a cell gets a
// leading apostrophe and quotes, and readCsv() takes the apostrophe off again.
void TestVCardInterchange::tableNeutralisesSpreadsheetFormulasAndReadsThemBack()
{
    QFETCH(QString, field);
    QFETCH(QByteArray, written);

    const QByteArray table = writeCsv({{field, QStringLiteral("x")}});
    const Result<CsvRows> read = readCsv(table);

    QCOMPARE(table, QByteArray("\xEF\xBB\xBF") + written + QByteArray(";x\r\n"));
    QVERIFY(read.hasValue());
    QCOMPARE(read.value(), CsvRows({{field, QStringLiteral("x")}}));
}

void TestVCardInterchange::tableWithCommasIsRead()
{
    const Result<CsvRows> read =
        readCsv("First name,Last name,Mobile\r\nErika,\"Muster, mann\",0151\r\n");

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().at(1),
             QStringList({QStringLiteral("Erika"), QStringLiteral("Muster, mann"),
                          QStringLiteral("0151")}));
    const Result<QList<Contact>> contacts =
        readContactsCsv("First name,Last name,Mobile\r\nErika,Mustermann,0151\r\n");
    QVERIFY(contacts.hasValue());
    QCOMPARE(contacts.value().constFirst().phones.at(0).type, phoneType::mobile);
}

void TestVCardInterchange::tableWithAnOpenQuoteIsRejected()
{
    const Result<CsvRows> read = readCsv("a;\"b\r\nc;d\r\n");

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::InvalidInput);
}

void TestVCardInterchange::tableWithoutKnownColumnIsRejected()
{
    const Result<QList<Contact>> read = readContactsCsv("Spalte A;Spalte B\r\n1;2\r\n");

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::InvalidInput);
}

void TestVCardInterchange::tableThatIsNoUtf8IsRejected()
{
    const Result<CsvRows> read = readCsv("Vorname;Nachname\r\nJ\xF6rg;M\xFCller\r\n");

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::InvalidInput);
}

QTEST_GUILESS_MAIN(TestVCardInterchange)

#include "tst_vcardinterchange.moc"
