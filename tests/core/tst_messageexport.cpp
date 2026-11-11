#include "core/codec/CsvTable.h"
#include "core/codec/MessageExport.h"

#include <QTest>
#include <QTimeZone>

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

ExportedMessage received()
{
    ExportedMessage exported;
    exported.message.luid = 7;
    exported.message.box = MessageBox::Inbox;
    exported.message.incoming = true;
    exported.message.read = false;
    exported.message.date = QDateTime(QDate(2026, 11, 10), QTime(8, 15), QTimeZone::UTC);
    exported.message.addresses = {QStringLiteral("+49 30 1234567")};
    exported.message.body = QStringLiteral("=HYPERLINK(\"http://example.org\")\nZweite Zeile");
    exported.message.simSlot = 1;
    exported.contactName = QStringLiteral("Erika Mustermann");
    return exported;
}

ExportedMessage multimedia()
{
    ExportedMessage exported;
    exported.message.kind = MessageKind::Mms;
    exported.message.box = MessageBox::Sent;
    exported.message.incoming = false;
    exported.message.date = QDateTime(QDate(2026, 11, 9), QTime(12, 0), QTimeZone::UTC);
    exported.message.addresses = {QStringLiteral("0151 2000001")};
    exported.message.subject = QStringLiteral("Fotos");
    exported.message.body = QStringLiteral("Grüße aus Köln 😀");
    MessageAttachment photo;
    photo.name = QStringLiteral("Foto 1.jpg");
    photo.mimeType = QStringLiteral("image/jpeg");
    photo.data = QByteArray::fromHex("FFD8FFD9");
    photo.size = 4;
    MessageAttachment film;
    film.name = QStringLiteral("film.mp4");
    film.mimeType = QStringLiteral("video/mp4");
    film.size = 20971520;
    film.onPhoneOnly = true;
    exported.message.attachments = {photo, film};
    exported.archived = true;
    return exported;
}

} // namespace

class TestMessageExport : public QObject
{
    Q_OBJECT

public:
    explicit TestMessageExport(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void tableHasTheColumnsInBothLanguages_data();
    void tableHasTheColumnsInBothLanguages();
    void tableKeepsAFormulaFromRunning();
    void pageEscapesEveryTextFromTheMessages();
    void pageForbidsScriptsAndRequests();
    void archiveReadsBackWhatWasWritten();
    void archiveReplacesCharactersXmlDoesNotAllow();
    void archiveLeavesScheduledMessagesOut();
    void foreignOrBrokenArchiveIsRejected_data();
    void foreignOrBrokenArchiveIsRejected();
    void oversizedArchiveIsRejected_data();
    void oversizedArchiveIsRejected();
};

void TestMessageExport::tableHasTheColumnsInBothLanguages_data()
{
    QTest::addColumn<bool>("german");
    QTest::addColumn<QStringList>("header");
    QTest::addColumn<QStringList>("second");

    QTest::newRow("German") << true
                            << QStringList{QStringLiteral("Zeit"),   QStringLiteral("Richtung"),
                                           QStringLiteral("Ordner"), QStringLiteral("Kontakt"),
                                           QStringLiteral("Nummer"), QStringLiteral("Betreff"),
                                           QStringLiteral("Text"),   QStringLiteral("Anhänge"),
                                           QStringLiteral("SIM"),    QStringLiteral("Archiviert")}
                            << QStringList{QStringLiteral("Gesendet"), QStringLiteral("Gesendet"),
                                           QStringLiteral("ja")};
    QTest::newRow("English") << false
                             << QStringList{QStringLiteral("Time"),   QStringLiteral("Direction"),
                                            QStringLiteral("Folder"), QStringLiteral("Contact"),
                                            QStringLiteral("Number"), QStringLiteral("Subject"),
                                            QStringLiteral("Text"),   QStringLiteral("Attachments"),
                                            QStringLiteral("SIM"),    QStringLiteral("Archived")}
                             << QStringList{QStringLiteral("Sent"), QStringLiteral("Sent"),
                                            QStringLiteral("yes")};
}

void TestMessageExport::tableHasTheColumnsInBothLanguages()
{
    QFETCH(bool, german);
    QFETCH(QStringList, header);
    QFETCH(QStringList, second);

    const Result<CsvRows> rows = readCsv(writeMessagesCsv(
        {received(), multimedia()}, german ? CsvLanguage::German : CsvLanguage::English));

    QVERIFY(rows.hasValue());
    QCOMPARE(rows.value().size(), 3);
    QCOMPARE(rows.value().at(0), header);
    const QStringList &first = rows.value().at(1);
    QCOMPARE(first.at(3), QStringLiteral("Erika Mustermann"));
    QCOMPARE(first.at(4), QStringLiteral("+49 30 1234567"));
    QCOMPARE(first.at(8), QStringLiteral("2"));
    const QStringList &mms = rows.value().at(2);
    QCOMPARE(mms.at(1), second.at(0));
    QCOMPARE(mms.at(2), second.at(1));
    QCOMPARE(mms.at(5), QStringLiteral("Fotos"));
    QCOMPARE(mms.at(6), QStringLiteral("Grüße aus Köln 😀"));
    QCOMPARE(mms.at(7), QStringLiteral("Foto 1.jpg, film.mp4"));
    QCOMPARE(mms.at(8), QString());
    QCOMPARE(mms.at(9), second.at(2));
}

void TestMessageExport::tableKeepsAFormulaFromRunning()
{
    const QByteArray table = writeMessagesCsv({received()}, CsvLanguage::German);

    QVERIFY(table.contains("\"'=HYPERLINK("));
    QVERIFY(table.contains("\"'+49 30 1234567\""));
    const Result<CsvRows> rows = readCsv(table);
    QVERIFY(rows.hasValue());
    QCOMPARE(rows.value().at(1).at(6), received().message.body);
    QCOMPARE(rows.value().at(1).at(4), QStringLiteral("+49 30 1234567"));
}

void TestMessageExport::pageEscapesEveryTextFromTheMessages()
{
    ExportedMessage hostile = received();
    hostile.contactName = QStringLiteral("<b onmouseover=\"x()\">Eve</b>");
    hostile.message.subject = QStringLiteral("<img src=x onerror=alert(1)>");
    hostile.message.body = QStringLiteral("<script>alert('x')</script> & \"q\"\nneu");
    MessageAttachment part;
    part.name = QStringLiteral("<iframe src='//example.org'>.jpg");
    part.mimeType = QStringLiteral("image/jpeg");
    hostile.message.attachments = {part};

    const QString page = QString::fromUtf8(
        writeMessagesHtml({hostile}, QStringLiteral("Chat <mit> Eve"), CsvLanguage::German));

    QVERIFY(!page.contains(QLatin1StringView("<script")));
    QVERIFY(!page.contains(QLatin1StringView("<img")));
    QVERIFY(!page.contains(QLatin1StringView("<iframe")));
    QVERIFY(!page.contains(QLatin1StringView("<b ")));
    QVERIFY(!page.contains(QLatin1StringView("<mit>")));
    QVERIFY(page.contains(QLatin1StringView("&lt;script&gt;alert(&#39;x&#39;)&lt;/script&gt; "
                                            "&amp; &quot;q&quot;<br>neu")));
    QVERIFY(page.contains(QLatin1StringView("<title>Chat &lt;mit&gt; Eve</title>")));
    QVERIFY(page.contains(QLatin1StringView("&lt;b onmouseover=&quot;x()&quot;&gt;Eve&lt;/b&gt;")));
}

void TestMessageExport::pageForbidsScriptsAndRequests()
{
    const QString page = QString::fromUtf8(writeMessagesHtml(
        {received(), multimedia()}, QStringLiteral("Alle"), CsvLanguage::English));

    QVERIFY(page.startsWith(QLatin1StringView("<!DOCTYPE html>")));
    QVERIFY(page.contains(QLatin1StringView(
        "http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src "
        "'unsafe-inline'\"")));
    QVERIFY(page.contains(QLatin1StringView("<html lang=\"en\">")));
    QVERIFY(page.contains(QLatin1StringView("Attachments: Foto 1.jpg, film.mp4")));
    QVERIFY(!page.contains(QLatin1StringView("http://example.org\"")));
}

void TestMessageExport::archiveReadsBackWhatWasWritten()
{
    const QList<ExportedMessage> written = {received(), multimedia()};

    const Result<QList<Message>> read = readMessagesXml(writeMessagesXml(written));

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().size(), 2);
    Message first = written.at(0).message;
    first.luid = 0;
    QCOMPARE(read.value().at(0), first);
    const Message &second = read.value().at(1);
    QCOMPARE(second.kind, MessageKind::Mms);
    QCOMPARE(second.box, MessageBox::Sent);
    QVERIFY(!second.incoming);
    QCOMPARE(second.subject, QStringLiteral("Fotos"));
    QCOMPARE(second.body, QStringLiteral("Grüße aus Köln 😀"));
    QCOMPARE(second.attachments.size(), 2);
    QCOMPARE(second.attachments.at(0), written.at(1).message.attachments.at(0));
    QVERIFY(second.attachments.at(1).onPhoneOnly);
    QCOMPARE(second.attachments.at(1).size, 20971520);
}

void TestMessageExport::archiveReplacesCharactersXmlDoesNotAllow()
{
    ExportedMessage exported = received();
    exported.message.body = QStringLiteral("a") + QChar(0x01) + QStringLiteral("b") +
                            QChar(0xFFFF) + QStringLiteral("\tc\nd");

    const Result<QList<Message>> read = readMessagesXml(writeMessagesXml({exported}));

    QVERIFY(read.hasValue());
    const QChar replaced(QChar::ReplacementCharacter);
    QCOMPARE(read.value().at(0).body, QStringLiteral("a") + replaced + QStringLiteral("b") +
                                          replaced + QStringLiteral("\tc\nd"));
}

void TestMessageExport::archiveLeavesScheduledMessagesOut()
{
    ExportedMessage planned = received();
    planned.message.kind = MessageKind::Scheduled;
    planned.message.box = MessageBox::Scheduled;

    const Result<QList<Message>> read = readMessagesXml(writeMessagesXml({planned, received()}));

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().size(), 1);
}

void TestMessageExport::foreignOrBrokenArchiveIsRejected_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("other root") << QByteArray("<mpe_messages><sms/></mpe_messages>");
    QTest::newRow("other version") << QByteArray("<messages version=\"2\"/>");
    QTest::newRow("not XML") << QByteArray("Zeit;Text\r\n");
    QTest::newRow("unclosed") << QByteArray("<messages version=\"1\"><message kind=\"sms\" "
                                            "box=\"inbox\">");
    QTest::newRow("internal entity")
        << QByteArray("<!DOCTYPE messages [<!ENTITY x \"y\">]><messages version=\"1\">"
                      "<message kind=\"sms\" box=\"inbox\"><body>&x;</body></message></messages>");
    QTest::newRow("external entity")
        << QByteArray("<!DOCTYPE messages [<!ENTITY x SYSTEM \"file:///etc/passwd\">]>"
                      "<messages version=\"1\"><message kind=\"sms\" box=\"inbox\"><body>&x;"
                      "</body></message></messages>");
    QTest::newRow("undeclared entity")
        << QByteArray("<messages version=\"1\"><message kind=\"sms\" box=\"inbox\"><body>&x;"
                      "</body></message></messages>");
    QTest::newRow("unknown kind") << QByteArray(
        "<messages version=\"1\"><message kind=\"fax\" box=\"inbox\"/></messages>");
    QTest::newRow("unknown folder")
        << QByteArray("<messages version=\"1\"><message kind=\"sms\" box=\"trash\"/></messages>");
    QTest::newRow("bad time") << QByteArray("<messages version=\"1\"><message kind=\"sms\" "
                                            "box=\"inbox\" date=\"gestern\"/></messages>");
    QTest::newRow("unknown element") << QByteArray(
        "<messages version=\"1\"><message kind=\"sms\" box=\"inbox\"><x/></message></messages>");
    QTest::newRow("nested element") << QByteArray(
        "<messages version=\"1\"><message kind=\"sms\" box=\"inbox\"><body><b>x</b></body>"
        "</message></messages>");
    QTest::newRow("bad Base64") << QByteArray(
        "<messages version=\"1\"><message kind=\"mms\" box=\"inbox\"><attachment name=\"a\" "
        "mime=\"image/png\">@@@</attachment></message></messages>");
}

void TestMessageExport::foreignOrBrokenArchiveIsRejected()
{
    QFETCH(QByteArray, data);

    const Result<QList<Message>> read = readMessagesXml(data);

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::InvalidInput);
}

void TestMessageExport::oversizedArchiveIsRejected_data()
{
    QTest::addColumn<QByteArray>("data");

    QByteArray many = "<messages version=\"1\">";
    many.reserve(qsizetype(40) * (maxArchiveMessages + 1));
    for (qsizetype i = 0; i <= maxArchiveMessages; ++i)
        many += "<message kind=\"sms\" box=\"inbox\"/>";
    QTest::newRow("messages") << many + "</messages>";
    QTest::newRow("part") << QByteArray("<messages version=\"1\"><message kind=\"mms\" "
                                        "box=\"inbox\"><attachment name=\"a\" mime=\"b\">") +
                                 QByteArray(maxAttachmentBytes / 3 * 4 + 8, 'A') +
                                 QByteArray("</attachment></message></messages>");
    QTest::newRow("address") << QByteArray("<messages version=\"1\"><message kind=\"sms\" "
                                           "box=\"inbox\"><address>") +
                                    QByteArray(maxMessageAddressLength + 1, '1') +
                                    QByteArray("</address></message></messages>");
}

void TestMessageExport::oversizedArchiveIsRejected()
{
    QFETCH(QByteArray, data);

    const Result<QList<Message>> read = readMessagesXml(data);

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::LimitExceeded);
}

QTEST_GUILESS_MAIN(TestMessageExport)

#include "tst_messageexport.moc"
