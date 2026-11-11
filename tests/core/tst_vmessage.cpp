#include "core/codec/SmsLength.h"
#include "core/codec/VMessage.h"

#include <QTest>
#include <QTimeZone>

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

QByteArray inboxSms()
{
    return "BEGIN:VMESSAGE\r\n"
           "TYP:SMS;IN\r\n"
           "BOX:INBOX\r\n"
           "READ:0\r\n"
           "SIM:1\r\n"
           "DATE:20261110T081530Z\r\n"
           "NUMBER:+4930123456\r\n"
           "BODY;ENCODING=QUOTED-PRINTABLE;CHARSET=UTF-8:Grüße=0D=0A2=3D1, ok; \"ja\"\r\n"
           "X-IRMC-LUID:00000000002A\r\n"
           "END:VMESSAGE\r\n";
}

QByteArray multimediaMessage()
{
    return "BEGIN:VMESSAGE\r\n"
           "TYP:MMS;IN\r\n"
           "BOX:INBOX\r\n"
           "READ:1\r\n"
           "DATE:20261109T120000Z\r\n"
           "NUMBER:01511234567\r\n"
           "SUBJECT;CHARSET=UTF-8:Fotos vom Ausflug\r\n"
           "SMIL:<smil><body><par/></body></smil>\r\n"
           "ATT;NAME=text_0.txt;TYPE=text/plain;CHARSET=UTF-8:Schöne Grüße\r\n"
           "ATT;NAME=Foto=2C 1.jpg;TYPE=image/jpeg;SIZE=4;ENCODING=BASE64:/9j/2Q==\r\n"
           "\r\n"
           "ATT;NAME=film.mp4;TYPE=video/mp4;SIZE=20971520;URI=content=3A//mms/part/7:NULL\r\n"
           "ATT;NAME=kontakt.vcf;TYPE=text/x-vcard:BEGIN=3AVCARD\r\n"
           "X-IRMC-LUID:000010000005\r\n"
           "END:VMESSAGE\r\n";
}

QByteArray scheduledMessage()
{
    return "BEGIN:VMESSAGE\r\n"
           "SIM:0\r\n"
           "TYP:SMS;OUT\r\n"
           "BOX:SHEDULED\r\n"
           "RECIPIENT;CHARSET=UTF-8:Jörg \\; Söhne;+491701234567;SENT;20261111T090000Z;\r\n"
           "RECIPIENT:Erika;030123456;FAILED;;NO_SERVICE\r\n"
           "RECIPIENT:;0401234;SHEDULED;;\r\n"
           "DATE:20261201T080000Z\r\n"
           "BODY:Frohe Weihnachten\r\n"
           "X-IRMC-LUID:000020000003\r\n"
           "END:VMESSAGE\r\n";
}

Message plannedMessage()
{
    Message message;
    message.kind = MessageKind::Scheduled;
    message.box = MessageBox::Scheduled;
    message.date = QDateTime(QDate(2026, 12, 24), QTime(18, 0), QTimeZone::UTC);
    message.simSlot = 1;
    message.body = QStringLiteral("Frohes Fest, a=b\nZeile 2 😀");
    MessageRecipient erika;
    erika.name = QStringLiteral("Erika; Mustermann");
    erika.number = QStringLiteral("+4915112345678");
    MessageRecipient jorg;
    jorg.name = QStringLiteral("Jörg");
    jorg.number = QStringLiteral("030123456");
    message.recipients = {erika, jorg};
    return message;
}

} // namespace

class TestVMessage : public QObject
{
    Q_OBJECT

public:
    explicit TestVMessage(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void receivedTextMessageIsReadWithEscapesAndUtf8();
    void sentTextMessageIsOutgoingAndRead();
    void multimediaMessageKeepsItsPartsAndJoinsItsText();
    void scheduledMessageIsReadWithItsRecipients();
    void storeItemThatIsNotScheduledIsASentTextMessage();
    void softLineBreaksAreJoined();
    void unknownLinesArePassedOver();
    void malformedMessageIsRejected_data();
    void malformedMessageIsRejected();
    void oversizedMessageIsRejected_data();
    void oversizedMessageIsRejected();
    void scheduledMessageIsWrittenForThePhone();
    void writtenScheduledMessageReadsBack();
    void incompleteScheduledMessageIsRefused_data();
    void incompleteScheduledMessageIsRefused();
    void lengthCountsMessagesLikeAPhone_data();
    void lengthCountsMessagesLikeAPhone();
};

void TestVMessage::receivedTextMessageIsReadWithEscapesAndUtf8()
{
    const Result<Message> parsed = parseVMessage(inboxSms());

    QVERIFY(parsed.hasValue());
    const Message &message = parsed.value();
    QCOMPARE(message.luid, 42u);
    QCOMPARE(message.kind, MessageKind::Sms);
    QCOMPARE(message.box, MessageBox::Inbox);
    QVERIFY(message.incoming);
    QVERIFY(!message.read);
    QCOMPARE(message.simSlot, 1);
    QCOMPARE(message.date, QDateTime(QDate(2026, 11, 10), QTime(8, 15, 30), QTimeZone::UTC));
    QCOMPARE(message.addresses, QStringList{QStringLiteral("+4930123456")});
    QCOMPARE(message.body, QStringLiteral("Grüße\r\n2=1, ok; \"ja\""));
    QVERIFY(message.attachments.isEmpty());
}

void TestVMessage::sentTextMessageIsOutgoingAndRead()
{
    const Result<Message> parsed = parseVMessage("BEGIN:VMESSAGE\r\nTYP:SMS;OUT\r\nBOX:SENT\r\n"
                                                 "DATE:20261110T081530Z\r\nNUMBER:0301\r\n"
                                                 "BODY:Hi\r\nX-IRMC-LUID:000000000007\r\n"
                                                 "END:VMESSAGE\r\n");

    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().box, MessageBox::Sent);
    QVERIFY(!parsed.value().incoming);
    QVERIFY(parsed.value().read);
    QCOMPARE(parsed.value().simSlot, -1);
}

void TestVMessage::multimediaMessageKeepsItsPartsAndJoinsItsText()
{
    const Result<Message> parsed = parseVMessage(multimediaMessage());

    QVERIFY(parsed.hasValue());
    const Message &message = parsed.value();
    QCOMPARE(message.luid, 0x10000005u);
    QCOMPARE(message.kind, MessageKind::Mms);
    QCOMPARE(message.subject, QStringLiteral("Fotos vom Ausflug"));
    QCOMPARE(message.body, QStringLiteral("Schöne Grüße"));
    QCOMPARE(message.attachments.size(), 3);
    QCOMPARE(message.attachments.at(0).name, QStringLiteral("Foto, 1.jpg"));
    QCOMPARE(message.attachments.at(0).mimeType, QStringLiteral("image/jpeg"));
    QCOMPARE(message.attachments.at(0).data, QByteArray::fromHex("FFD8FFD9"));
    QCOMPARE(message.attachments.at(0).size, 4);
    QVERIFY(!message.attachments.at(0).onPhoneOnly);
    QCOMPARE(message.attachments.at(1).name, QStringLiteral("film.mp4"));
    QVERIFY(message.attachments.at(1).onPhoneOnly);
    QVERIFY(message.attachments.at(1).data.isEmpty());
    QCOMPARE(message.attachments.at(1).size, 20971520);
    QCOMPARE(message.attachments.at(2).mimeType, QStringLiteral("text/x-vcard"));
    QCOMPARE(message.attachments.at(2).data, QByteArray("BEGIN=3AVCARD"));
}

void TestVMessage::scheduledMessageIsReadWithItsRecipients()
{
    const Result<Message> parsed = parseVMessage(scheduledMessage());

    QVERIFY(parsed.hasValue());
    const Message &message = parsed.value();
    QCOMPARE(message.kind, MessageKind::Scheduled);
    QCOMPARE(message.box, MessageBox::Scheduled);
    QVERIFY(!message.incoming);
    QCOMPARE(message.simSlot, 0);
    QCOMPARE(message.date, QDateTime(QDate(2026, 12, 1), QTime(8, 0), QTimeZone::UTC));
    QCOMPARE(message.body, QStringLiteral("Frohe Weihnachten"));
    QCOMPARE(message.recipients.size(), 3);
    QCOMPARE(message.recipients.at(0).name, QStringLiteral("Jörg ; Söhne"));
    QCOMPARE(message.recipients.at(0).number, QStringLiteral("+491701234567"));
    QCOMPARE(message.recipients.at(0).status, MessageRecipient::Status::Sent);
    QCOMPARE(message.recipients.at(0).sentAt,
             QDateTime(QDate(2026, 11, 11), QTime(9, 0), QTimeZone::UTC));
    QCOMPARE(message.recipients.at(1).status, MessageRecipient::Status::Failed);
    QCOMPARE(message.recipients.at(1).error, QStringLiteral("NO_SERVICE"));
    QCOMPARE(message.recipients.at(2).status, MessageRecipient::Status::Scheduled);
    QCOMPARE(message.addresses,
             (QStringList{QStringLiteral("+491701234567"), QStringLiteral("030123456"),
                          QStringLiteral("0401234")}));
}

void TestVMessage::storeItemThatIsNotScheduledIsASentTextMessage()
{
    const Result<Message> parsed = parseVMessage("BEGIN:VMESSAGE\r\nTYP:SMS;OUT\r\nBOX:SENT\r\n"
                                                 "NUMBER:0301\r\nDATE:20261110T081530Z\r\n"
                                                 "BODY:Hi\r\nX-IRMC-LUID:000020000009\r\n"
                                                 "END:VMESSAGE\r\n");

    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().kind, MessageKind::Sms);
    QCOMPARE(kindOfLuid(0x20000009u), MessageKind::Scheduled);
    QCOMPARE(kindOfLuid(0x10000009u), MessageKind::Mms);
    QCOMPARE(kindOfLuid(9u), MessageKind::Sms);
}

void TestVMessage::softLineBreaksAreJoined()
{
    const Result<Message> parsed = parseVMessage(
        "BEGIN:VMESSAGE\r\nBOX:INBOX\r\nBODY;ENCODING=QUOTED-PRINTABLE:Erste H=\r\nälfte\r\n"
        "END:VMESSAGE\r\n");

    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().body, QStringLiteral("Erste Hälfte"));
}

void TestVMessage::unknownLinesArePassedOver()
{
    const Result<Message> parsed =
        parseVMessage("BEGIN:VMESSAGE\r\nBOX:INBOX\r\nS-DELIVERED:20261109T120000Z\r\n"
                      "garbage without colon\r\nX-UNKNOWN;A=B:C\r\nBODY:Hi\r\nEND:VMESSAGE\r\n");

    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().body, QStringLiteral("Hi"));
}

void TestVMessage::malformedMessageIsRejected_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("no begin") << QByteArray("BOX:INBOX\r\nBODY:Hi\r\nEND:VMESSAGE\r\n");
    QTest::newRow("no end") << QByteArray("BEGIN:VMESSAGE\r\nBOX:INBOX\r\nBODY:Hi\r\n");
    QTest::newRow("wrong object") << QByteArray("BEGIN:VCARD\r\nFN:x\r\nEND:VCARD\r\n");
    QTest::newRow("bad base64") << QByteArray(
        "BEGIN:VMESSAGE\r\nATT;NAME=a;TYPE=image/png;ENCODING=BASE64:@@@@\r\nEND:VMESSAGE\r\n");
    QTest::newRow("bad luid") << QByteArray(
        "BEGIN:VMESSAGE\r\nX-IRMC-LUID:FFFFFFFFFFFFF\r\nEND:VMESSAGE\r\n");
    QTest::newRow("luid not hex") << QByteArray(
        "BEGIN:VMESSAGE\r\nX-IRMC-LUID:zz\r\nEND:VMESSAGE\r\n");
}

void TestVMessage::malformedMessageIsRejected()
{
    QFETCH(QByteArray, data);

    const Result<Message> parsed = parseVMessage(data);

    QVERIFY(!parsed.hasValue());
    QCOMPARE(parsed.error().code(), ErrorCode::InvalidInput);
}

void TestVMessage::oversizedMessageIsRejected_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("whole message") << QByteArray("BEGIN:VMESSAGE\r\nBODY:") +
                                          QByteArray(maxMessageBytes, 'a') +
                                          QByteArray("\r\nEND:VMESSAGE\r\n");
    QByteArray lines = "BEGIN:VMESSAGE\r\n";
    for (qsizetype i = 0; i < maxMessageLines; ++i)
        lines += "X:1\r\n";
    QTest::newRow("lines") << lines + "END:VMESSAGE\r\n";
    QByteArray parts = "BEGIN:VMESSAGE\r\n";
    for (qsizetype i = 0; i <= maxMessageAttachments; ++i)
        parts += "ATT;NAME=a;TYPE=image/png;SIZE=1;ENCODING=BASE64:AA==\r\n";
    QTest::newRow("parts") << parts + "END:VMESSAGE\r\n";
    QTest::newRow("one part")
        << QByteArray("BEGIN:VMESSAGE\r\nATT;NAME=a;TYPE=image/png;ENCODING=BASE64:") +
               QByteArray(maxAttachmentBytes / 3 * 4 + 8, 'A') + QByteArray("\r\nEND:VMESSAGE\r\n");
    QByteArray recipients = "BEGIN:VMESSAGE\r\nBOX:SHEDULED\r\n";
    for (qsizetype i = 0; i <= maxMessageAddresses; ++i)
        recipients += "RECIPIENT:;0301234;SHEDULED;;\r\n";
    QTest::newRow("recipients") << recipients + "END:VMESSAGE\r\n";
    QTest::newRow("address") << QByteArray("BEGIN:VMESSAGE\r\nNUMBER:") +
                                    QByteArray(maxMessageAddressLength + 1, '1') +
                                    QByteArray("\r\nEND:VMESSAGE\r\n");
    QTest::newRow("text") << QByteArray("BEGIN:VMESSAGE\r\nBODY:") +
                                 QByteArray(maxMessageTextLength + 1, 'a') +
                                 QByteArray("\r\nEND:VMESSAGE\r\n");
}

void TestVMessage::oversizedMessageIsRejected()
{
    QFETCH(QByteArray, data);

    const Result<Message> parsed = parseVMessage(data);

    QVERIFY(!parsed.hasValue());
    QCOMPARE(parsed.error().code(), ErrorCode::LimitExceeded);
}

void TestVMessage::scheduledMessageIsWrittenForThePhone()
{
    const Result<QByteArray> written = encodeScheduledMessage(plannedMessage());

    QVERIFY(written.hasValue());
    QCOMPARE(written.value(),
             QByteArray("BEGIN:VMESSAGE\r\n"
                        "TYP:SMS;OUT\r\n"
                        "BOX:SHEDULED\r\n"
                        "SIM:1\r\n"
                        "RECIPIENT;CHARSET=UTF-8:Erika, Mustermann;+4915112345678;;0\r\n"
                        "RECIPIENT;CHARSET=UTF-8:J\xC3\xB6rg;030123456;;0\r\n"
                        "DATE:20261224T180000Z\r\n"
                        "BODY;ENCODING=QUOTED-PRINTABLE;CHARSET=UTF-8:Frohes Fest, a=3Db=0AZeile 2 "
                        "\xF0\x9F\x98\x80\r\n"
                        "END:VMESSAGE\r\n"));
}

void TestVMessage::writtenScheduledMessageReadsBack()
{
    const Message planned = plannedMessage();

    const Result<Message> back = parseVMessage(encodeScheduledMessage(planned).value());

    QVERIFY(back.hasValue());
    QCOMPARE(back.value().kind, MessageKind::Scheduled);
    QCOMPARE(back.value().body, planned.body);
    QCOMPARE(back.value().date, planned.date);
    QCOMPARE(back.value().simSlot, 1);
    QCOMPARE(back.value().recipients.size(), 2);
    QCOMPARE(back.value().recipients.at(1).name, QStringLiteral("Jörg"));
    QCOMPARE(back.value().recipients.at(1).number, QStringLiteral("030123456"));
}

void TestVMessage::incompleteScheduledMessageIsRefused_data()
{
    QTest::addColumn<int>("change");
    QTest::addColumn<int>("code");

    const int invalid = int(ErrorCode::InvalidInput);
    const int limit = int(ErrorCode::LimitExceeded);
    QTest::newRow("no recipient") << 0 << invalid;
    QTest::newRow("recipient without number") << 1 << invalid;
    QTest::newRow("number with letters") << 2 << invalid;
    QTest::newRow("empty text") << 3 << invalid;
    QTest::newRow("no time") << 4 << invalid;
    QTest::newRow("SIM out of range") << 5 << invalid;
    QTest::newRow("text too long") << 6 << limit;
    QTest::newRow("too many recipients") << 7 << limit;
}

void TestVMessage::incompleteScheduledMessageIsRefused()
{
    QFETCH(int, change);
    QFETCH(int, code);
    Message message = plannedMessage();
    switch (change) {
    case 0:
        message.recipients.clear();
        break;
    case 1:
        message.recipients[0].number.clear();
        break;
    case 2:
        message.recipients[0].number = QStringLiteral("0301x");
        break;
    case 3:
        message.body = QStringLiteral("  ");
        break;
    case 4:
        message.date = QDateTime();
        break;
    case 5:
        message.simSlot = 10;
        break;
    case 6:
        message.body = QString(maxScheduledTextLength + 1, u'a');
        break;
    default:
        while (message.recipients.size() <= maxScheduledRecipients)
            message.recipients.append(message.recipients.constFirst());
        break;
    }

    const Result<QByteArray> written = encodeScheduledMessage(message);

    QVERIFY(!written.hasValue());
    QCOMPARE(int(written.error().code()), code);
}

void TestVMessage::lengthCountsMessagesLikeAPhone_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("units");
    QTest::addColumn<int>("parts");
    QTest::addColumn<bool>("unicode");
    QTest::addColumn<int>("remaining");

    QTest::newRow("empty") << QString() << 0 << 0 << false << 160;
    QTest::newRow("one message") << QString(160, u'a') << 160 << 1 << false << 0;
    QTest::newRow("two messages") << QString(161, u'a') << 161 << 2 << false << 145;
    QTest::newRow("umlauts are 7-bit") << QStringLiteral("Grüße, Jörg") << 11 << 1 << false << 149;
    QTest::newRow("euro counts two") << QStringLiteral("5 €") << 4 << 1 << false << 156;
    // 152 letters and an extension character do not fit 153; it moves on.
    QTest::newRow("extension not split")
        << QString(152, u'a') + QStringLiteral("{") + QString(10, u'b') << 164 << 2 << false << 141;
    QTest::newRow("unicode") << QStringLiteral("ê") << 1 << 1 << true << 69;
    QTest::newRow("unicode two messages") << QString(71, u'ê') << 71 << 2 << true << 63;
    // 66 units and a surrogate pair do not fit 67.
    QTest::newRow("pair not split")
        << QString(66, u'ê') + QStringLiteral("😀") + QString(10, u'ê') << 78 << 2 << true << 55;
}

void TestVMessage::lengthCountsMessagesLikeAPhone()
{
    QFETCH(QString, text);
    QFETCH(int, units);
    QFETCH(int, parts);
    QFETCH(bool, unicode);
    QFETCH(int, remaining);

    const SmsLength length = smsLength(text);

    QCOMPARE(length.units, units);
    QCOMPARE(length.parts, parts);
    QCOMPARE(length.unicode, unicode);
    QCOMPARE(length.remaining, remaining);
}

QTEST_GUILESS_MAIN(TestVMessage)

#include "tst_vmessage.moc"
