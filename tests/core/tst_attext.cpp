#include "core/codec/AtText.h"

#include <QTest>

using namespace qpm::core::codec;

class TestAtText : public QObject
{
    Q_OBJECT

public:
    explicit TestAtText(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void encodeQpEscapesFieldSeparatorsAndControlCharacters_data();
    void encodeQpEscapesFieldSeparatorsAndControlCharacters();
    void decodeQpRestoresTheEncodedText_data();
    void decodeQpRestoresTheEncodedText();
    void decodeQpKeepsAnEqualsSignWithoutTwoHexDigits_data();
    void decodeQpKeepsAnEqualsSignWithoutTwoHexDigits();
    void decodeQpAcceptsLowerCaseHex();
    void splitFieldsIgnoresCommasInsideQuotes_data();
    void splitFieldsIgnoresCommasInsideQuotes();
};

void TestAtText::encodeQpEscapesFieldSeparatorsAndControlCharacters_data()
{
    QTest::addColumn<QString>("plain");
    QTest::addColumn<QString>("encoded");

    QTest::newRow("comma") << QStringLiteral("a,b") << QStringLiteral("a=2Cb");
    QTest::newRow("equals") << QStringLiteral("x=y") << QStringLiteral("x=3Dy");
    QTest::newRow("colon semicolon quote")
        << QStringLiteral("a:b;c\"d") << QStringLiteral("a=3Ab=3Bc=22d");
    QTest::newRow("line break") << QStringLiteral("line\r\nnext")
                                << QStringLiteral("line=0D=0Anext");
    QTest::newRow("non-ascii stays")
        << QStringLiteral("Grüße, 東京") << QStringLiteral("Grüße=2C 東京");
    QTest::newRow("empty") << QString() << QString();
}

void TestAtText::encodeQpEscapesFieldSeparatorsAndControlCharacters()
{
    QFETCH(QString, plain);
    QFETCH(QString, encoded);

    QCOMPARE(encodeQp(plain), encoded);
}

void TestAtText::decodeQpRestoresTheEncodedText_data()
{
    encodeQpEscapesFieldSeparatorsAndControlCharacters_data();
}

void TestAtText::decodeQpRestoresTheEncodedText()
{
    QFETCH(QString, plain);
    QFETCH(QString, encoded);

    QCOMPARE(decodeQp(encoded), plain);
}

void TestAtText::decodeQpKeepsAnEqualsSignWithoutTwoHexDigits_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("letters after") << QStringLiteral("a=xy");
    QTest::newRow("at the end") << QStringLiteral("a=");
    QTest::newRow("one digit at the end") << QStringLiteral("a=4");
    QTest::newRow("invalid digit") << QStringLiteral("=G1");
}

void TestAtText::decodeQpKeepsAnEqualsSignWithoutTwoHexDigits()
{
    QFETCH(QString, text);

    QCOMPARE(decodeQp(text), text);
}

void TestAtText::decodeQpAcceptsLowerCaseHex()
{
    QCOMPARE(decodeQp(QStringLiteral("a=2cb")), QStringLiteral("a,b"));
}

void TestAtText::splitFieldsIgnoresCommasInsideQuotes_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("fields");

    QTest::newRow("plain") << QStringLiteral("0,72")
                           << QStringList{QStringLiteral("0"), QStringLiteral("72")};
    QTest::newRow("quoted comma") << QStringLiteral("1,\"a,b\",3")
                                  << QStringList{QStringLiteral("1"), QStringLiteral("\"a,b\""),
                                                 QStringLiteral("3")};
    QTest::newRow("empty fields") << QStringLiteral("a,,b")
                                  << QStringList{QStringLiteral("a"), QString(),
                                                 QStringLiteral("b")};
    QTest::newRow("empty text") << QString() << QStringList{QString()};
}

void TestAtText::splitFieldsIgnoresCommasInsideQuotes()
{
    QFETCH(QString, text);
    QFETCH(QStringList, fields);

    QCOMPARE(splitFields(text), fields);
}

QTEST_GUILESS_MAIN(TestAtText)

#include "tst_attext.moc"
