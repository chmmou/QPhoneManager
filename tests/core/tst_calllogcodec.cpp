#include "core/codec/CallLogCodec.h"
#include "core/codec/CsvTable.h"

#include <QTest>
#include <QTimeZone>

using namespace qpm::core;
using namespace qpm::core::codec;

Q_DECLARE_METATYPE(qpm::core::codec::CallDirection)

namespace {

QDateTime local(int year, int month, int day, int hour, int minute, int second)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second), QTimeZone::LocalTime);
}

CallEntry sample()
{
    CallEntry entry;
    entry.id = 0;
    entry.number = QStringLiteral("+49 30 12345678");
    entry.name = QStringLiteral("Erika Mustermann");
    entry.direction = CallDirection::Incoming;
    entry.start = local(2026, 11, 3, 9, 15, 0);
    entry.durationSeconds = 125;
    return entry;
}

} // namespace

class TestCallLogCodec : public QObject
{
    Q_OBJECT

public:
    explicit TestCallLogCodec(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void listIsChosenAndReadWithTheClientCommands();
    void lineIsReadWithAllFields();
    void quotesAndCommasInNameAndNumberAreKept();
    void hiddenNumberAndSecondSimAreRead();
    void malformedLineIsRejected_data();
    void malformedLineIsRejected();
    void listSkipsAndCountsMalformedLines();
    void tooLongListIsRefused();
    void writeCommandCarriesEveryField_data();
    void writeCommandCarriesEveryField();
    void writtenEntryReadsBackTheSame();
    void writeRefusesWhatThePhoneCannotTake_data();
    void writeRefusesWhatThePhoneCannotTake();
    void deleteCommandNeedsAValidId();
    void tableHasTitlesAndOneRowPerCall();
};

void TestCallLogCodec::listIsChosenAndReadWithTheClientCommands()
{
    QCOMPARE(selectCallListCommand(CallDirection::Outgoing), QStringLiteral("AT+CPBS=\"DC\""));
    QCOMPARE(selectCallListCommand(CallDirection::Incoming), QStringLiteral("AT+CPBS=\"RC\""));
    QCOMPARE(selectCallListCommand(CallDirection::Missed), QStringLiteral("AT+CPBS=\"MC\""));
    QCOMPARE(readCallListCommand(), QStringLiteral("AT+CPBR=,"));
}

void TestCallLogCodec::lineIsReadWithAllFields()
{
    const Result<CallEntry> entry = parseCallLine(
        QStringLiteral("+CPBR: 412,\"+4930123456\",128,\"Zoë Örtel\",\"26/11/03,09:15:00\",125"),
        CallDirection::Incoming);

    QVERIFY(entry.hasValue());
    QCOMPARE(entry.value().id, 412u);
    QCOMPARE(entry.value().number, QStringLiteral("+4930123456"));
    QCOMPARE(entry.value().name, QStringLiteral("Zoë Örtel"));
    QCOMPARE(entry.value().direction, CallDirection::Incoming);
    QCOMPARE(entry.value().start, local(2026, 11, 3, 9, 15, 0));
    QCOMPARE(entry.value().durationSeconds, qint64(125));
    QCOMPARE(entry.value().simSlot, -1);
}

// The phone writes number and name without escaping, so a quote and a comma
// inside them must not end the field.
void TestCallLogCodec::quotesAndCommasInNameAndNumberAreKept()
{
    const Result<CallEntry> entry = parseCallLine(
        QStringLiteral(
            "+CPBR: 7,\"030 1234\",128,\"Firma \"Nord\", Empfang\",\"25/01/31,23:59:59\",0"),
        CallDirection::Outgoing);

    QVERIFY(entry.hasValue());
    QCOMPARE(entry.value().number, QStringLiteral("030 1234"));
    QCOMPARE(entry.value().name, QStringLiteral("Firma \"Nord\", Empfang"));
    QCOMPARE(entry.value().start, local(2025, 1, 31, 23, 59, 59));
}

void TestCallLogCodec::hiddenNumberAndSecondSimAreRead()
{
    const Result<CallEntry> entry = parseCallLine(
        QStringLiteral("+CPBR: 9,\"\",SIM1,\"\",\"24/02/29,00:00:01\",0"), CallDirection::Missed);

    QVERIFY(entry.hasValue());
    QVERIFY(entry.value().number.isEmpty());
    QVERIFY(entry.value().name.isEmpty());
    QCOMPARE(entry.value().simSlot, 1);
    QCOMPARE(entry.value().direction, CallDirection::Missed);
}

void TestCallLogCodec::malformedLineIsRejected_data()
{
    QTest::addColumn<QString>("line");

    QTest::newRow("no prefix") << QStringLiteral("7,\"1\",128,\"\",\"25/01/01,10:00:00\",0");
    QTest::newRow("other reply") << QStringLiteral("+CPBS: \"DC\"");
    QTest::newRow("empty") << QString();
    QTest::newRow("id zero") << QStringLiteral("+CPBR: 0,\"1\",128,\"\",\"25/01/01,10:00:00\",0");
    QTest::newRow("id beyond int")
        << QStringLiteral("+CPBR: 2147483648,\"1\",128,\"\",\"25/01/01,10:00:00\",0");
    QTest::newRow("negative duration")
        << QStringLiteral("+CPBR: 1,\"1\",128,\"\",\"25/01/01,10:00:00\",-5");
    QTest::newRow("huge duration")
        << QStringLiteral("+CPBR: 1,\"1\",128,\"\",\"25/01/01,10:00:00\",999999999999");
    QTest::newRow("impossible date")
        << QStringLiteral("+CPBR: 1,\"1\",128,\"\",\"25/02/30,10:00:00\",0");
    QTest::newRow("four-digit year")
        << QStringLiteral("+CPBR: 1,\"1\",128,\"\",\"2025/01/01,10:00:00\",0");
    QTest::newRow("missing field") << QStringLiteral("+CPBR: 1,\"1\",128,\"25/01/01,10:00:00\",0");
    QTest::newRow("unknown address type")
        << QStringLiteral("+CPBR: 1,\"1\",SIMX,\"\",\"25/01/01,10:00:00\",0");
    QTest::newRow("control character")
        << QStringLiteral("+CPBR: 1,\"1\",128,\"A%1B\",\"25/01/01,10:00:00\",0").arg(QChar(0x07));
    QTest::newRow("number too long")
        << QStringLiteral("+CPBR: 1,\"%1\",128,\"\",\"25/01/01,10:00:00\",0")
               .arg(QString(maxCallNumberLength + 1, u'1'));
    QTest::newRow("name too long")
        << QStringLiteral("+CPBR: 1,\"1\",128,\"%1\",\"25/01/01,10:00:00\",0")
               .arg(QString(maxCallNameLength + 1, u'n'));
    QTest::newRow("trailing text")
        << QStringLiteral("+CPBR: 1,\"1\",128,\"\",\"25/01/01,10:00:00\",0,extra");
}

void TestCallLogCodec::malformedLineIsRejected()
{
    QFETCH(QString, line);

    const Result<CallEntry> entry = parseCallLine(line, CallDirection::Outgoing);

    QVERIFY(!entry.hasValue());
    QCOMPARE(entry.error().code(), ErrorCode::InvalidInput);
}

void TestCallLogCodec::listSkipsAndCountsMalformedLines()
{
    const QStringList lines = {
        QStringLiteral("+CPBR: 3,\"0301\",128,\"\",\"26/11/10,12:00:00\",5"),
        QStringLiteral("+CPBR: broken"),
        QStringLiteral("+CPBR: 2,\"0302\",128,\"Max\",\"26/11/09,12:00:00\",0"),
    };

    const Result<CallList> list = parseCallList(lines, CallDirection::Outgoing);

    QVERIFY(list.hasValue());
    QCOMPARE(list.value().entries.size(), 2);
    QCOMPARE(list.value().malformed, 1);
    QCOMPARE(list.value().entries.at(0).id, 3u);
    QCOMPARE(list.value().entries.at(1).name, QStringLiteral("Max"));
}

void TestCallLogCodec::tooLongListIsRefused()
{
    const QStringList lines(maxCallEntries + 1,
                            QStringLiteral("+CPBR: 1,\"1\",128,\"\",\"25/01/01,10:00:00\",0"));

    const Result<CallList> list = parseCallList(lines, CallDirection::Missed);

    QVERIFY(!list.hasValue());
    QCOMPARE(list.error().code(), ErrorCode::LimitExceeded);
}

void TestCallLogCodec::writeCommandCarriesEveryField_data()
{
    QTest::addColumn<CallDirection>("direction");
    QTest::addColumn<QString>("storage");

    QTest::newRow("outgoing") << CallDirection::Outgoing << QStringLiteral("DC");
    QTest::newRow("incoming") << CallDirection::Incoming << QStringLiteral("RC");
    QTest::newRow("missed") << CallDirection::Missed << QStringLiteral("MC");
}

void TestCallLogCodec::writeCommandCarriesEveryField()
{
    QFETCH(CallDirection, direction);
    QFETCH(QString, storage);
    CallEntry entry = sample();
    entry.direction = direction;

    const Result<QString> command = encodeCallWrite(entry);

    QVERIFY(command.hasValue());
    QCOMPARE(command.value(),
             QStringLiteral("AT+CPBW=0,\"+49 30 12345678\",128,\"Erika Mustermann\","
                            "\"26/11/03,09:15:00\",125,\"%1\"")
                 .arg(storage));
}

void TestCallLogCodec::writtenEntryReadsBackTheSame()
{
    CallEntry entry = sample();
    entry.id = 77;
    entry.name = QStringLiteral("Müller, Hans");
    const Result<QString> command = encodeCallWrite(entry);
    QVERIFY(command.hasValue());

    // A read answers with the fields of the write, without the list name.
    const QString suffix = QStringLiteral(",\"RC\"");
    QVERIFY(command.value().endsWith(suffix));
    const QString line =
        QStringLiteral("+CPBR: ") +
        command.value().mid(QStringLiteral("AT+CPBW=").size()).chopped(suffix.size());
    const Result<CallEntry> read = parseCallLine(line, CallDirection::Incoming);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value(), entry);
}

void TestCallLogCodec::writeRefusesWhatThePhoneCannotTake_data()
{
    QTest::addColumn<QString>("number");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QDateTime>("start");
    QTest::addColumn<qint64>("duration");

    const QDateTime valid = local(2026, 1, 1, 10, 0, 0);
    QTest::newRow("quote in name")
        << QStringLiteral("0301") << QStringLiteral("A \"B\"") << valid << qint64(0);
    QTest::newRow("quote in number") << QStringLiteral("030\"1") << QString() << valid << qint64(0);
    QTest::newRow("line break in name")
        << QStringLiteral("0301") << QStringLiteral("A\r\nATD0") << valid << qint64(0);
    QTest::newRow("year 1999") << QStringLiteral("0301") << QString()
                               << local(1999, 12, 31, 23, 0, 0) << qint64(0);
    QTest::newRow("year 2100") << QStringLiteral("0301") << QString() << local(2100, 1, 1, 0, 0, 0)
                               << qint64(0);
    QTest::newRow("invalid time") << QStringLiteral("0301") << QString() << QDateTime()
                                  << qint64(0);
    QTest::newRow("negative duration")
        << QStringLiteral("0301") << QString() << valid << qint64(-1);
    QTest::newRow("too long duration")
        << QStringLiteral("0301") << QString() << valid << maxCallDurationSeconds + 1;
    QTest::newRow("number too long")
        << QString(maxCallNumberLength + 1, u'1') << QString() << valid << qint64(0);
}

void TestCallLogCodec::writeRefusesWhatThePhoneCannotTake()
{
    QFETCH(QString, number);
    QFETCH(QString, name);
    QFETCH(QDateTime, start);
    QFETCH(qint64, duration);
    CallEntry entry;
    entry.number = number;
    entry.name = name;
    entry.start = start;
    entry.durationSeconds = duration;

    const Result<QString> command = encodeCallWrite(entry);

    QVERIFY(!command.hasValue());
    QCOMPARE(command.error().code(), ErrorCode::InvalidInput);
}

void TestCallLogCodec::deleteCommandNeedsAValidId()
{
    QCOMPARE(encodeCallDelete(412).value(), QStringLiteral("AT+CPBD=412"));
    QCOMPARE(encodeCallDelete(0).error().code(), ErrorCode::InvalidInput);
    QCOMPARE(encodeCallDelete(maxCallId + 1).error().code(), ErrorCode::InvalidInput);
}

void TestCallLogCodec::tableHasTitlesAndOneRowPerCall()
{
    CallEntry missed = sample();
    missed.direction = CallDirection::Missed;
    missed.durationSeconds = 0;
    missed.simSlot = 0;
    missed.name = QStringLiteral("Weiß; Jörg");
    const QList<CallEntry> entries = {sample(), missed};

    const Result<CsvRows> german = readCsv(writeCallsCsv(entries, CsvLanguage::German));
    const Result<CsvRows> english = readCsv(writeCallsCsv(entries, CsvLanguage::English));

    QVERIFY(german.hasValue());
    QVERIFY(english.hasValue());
    QCOMPARE(german.value().size(), 3);
    QCOMPARE(german.value().at(0),
             (QStringList{QStringLiteral("Art"), QStringLiteral("Name"), QStringLiteral("Nummer"),
                          QStringLiteral("Zeit"), QStringLiteral("Dauer (Sekunden)"),
                          QStringLiteral("SIM")}));
    QCOMPARE(german.value().at(1),
             (QStringList{QStringLiteral("Eingehend"), QStringLiteral("Erika Mustermann"),
                          QStringLiteral("+49 30 12345678"), QStringLiteral("2026-11-03 09:15:00"),
                          QStringLiteral("125"), QString()}));
    QCOMPARE(german.value().at(2).at(0), QStringLiteral("Verpasst"));
    QCOMPARE(german.value().at(2).at(1), QStringLiteral("Weiß; Jörg"));
    QCOMPARE(german.value().at(2).at(5), QStringLiteral("1"));
    QCOMPARE(english.value().at(0).at(0), QStringLiteral("Type"));
    QCOMPARE(english.value().at(1).at(0), QStringLiteral("Incoming"));
}

QTEST_GUILESS_MAIN(TestCallLogCodec)

#include "tst_calllogcodec.moc"
