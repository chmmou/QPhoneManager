#include "core/protocol/PinAuth.h"

#include <QTest>

using namespace qpm::core::protocol;

class TestPinAuth : public QObject
{
    Q_OBJECT

public:
    explicit TestPinAuth(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void hashIsUpperCaseMd5OfSaltAndPin_data();
    void hashIsUpperCaseMd5OfSaltAndPin();
    void pinLinesAreParsed_data();
    void pinLinesAreParsed();
    void otherLinesAreRejected_data();
    void otherLinesAreRejected();
};

void TestPinAuth::hashIsUpperCaseMd5OfSaltAndPin_data()
{
    QTest::addColumn<QString>("pin");
    QTest::addColumn<QString>("hash");

    // Reference values computed with md5sum over "hash" followed by the PIN.
    QTest::newRow("digits") << QStringLiteral("1234")
                            << QStringLiteral("3971CDBA0627DBE96B334F5559DDA276");
    QTest::newRow("utf-8") << QStringLiteral("Pässwort")
                           << QStringLiteral("E0911D1C67E86A189484901C3FD0926A");
}

void TestPinAuth::hashIsUpperCaseMd5OfSaltAndPin()
{
    QFETCH(QString, pin);
    QFETCH(QString, hash);

    QCOMPARE(pinHash(pin), hash);
}

void TestPinAuth::pinLinesAreParsed_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<int>("state");
    QTest::addColumn<int>("tries");

    QTest::newRow("open") << QStringLiteral("*PIN: ACCESS OK,10") << int(PinStatus::State::AccessOk)
                          << 10;
    QTest::newRow("protected") << QStringLiteral("*PIN: ACCESS PROTECTED,7")
                               << int(PinStatus::State::AccessProtected) << 7;
    QTest::newRow("wrong, locked next")
        << QStringLiteral("*PIN: WRONG PIN,0") << int(PinStatus::State::WrongPin) << 0;
}

void TestPinAuth::pinLinesAreParsed()
{
    QFETCH(QString, line);
    QFETCH(int, state);
    QFETCH(int, tries);

    const std::optional<PinStatus> status = parsePinLine(line);
    QVERIFY(status.has_value());
    QCOMPARE(int(status->state), state);
    QCOMPARE(status->remainingTries, tries);
}

void TestPinAuth::otherLinesAreRejected_data()
{
    QTest::addColumn<QString>("line");

    QTest::newRow("ok") << QStringLiteral("OK");
    QTest::newRow("no count") << QStringLiteral("*PIN: ACCESS OK");
    QTest::newRow("unknown word") << QStringLiteral("*PIN: MAYBE,3");
    QTest::newRow("count too high") << QStringLiteral("*PIN: ACCESS OK,11");
    QTest::newRow("count not a number") << QStringLiteral("*PIN: ACCESS OK,x");
}

void TestPinAuth::otherLinesAreRejected()
{
    QFETCH(QString, line);

    QVERIFY(!parsePinLine(line).has_value());
}

QTEST_GUILESS_MAIN(TestPinAuth)

#include "tst_pinauth.moc"
