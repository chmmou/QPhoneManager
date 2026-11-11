#include "core/codec/PhoneTime.h"

#include <QTest>
#include <QTimeZone>

using namespace qpm::core::codec;

class TestPhoneTime : public QObject
{
    Q_OBJECT

public:
    explicit TestPhoneTime(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void callLogTimeIsReadAsLocalTimeInThisCentury();
    void callLogTimeRoundTrips();
    void malformedCallLogTimeIsRejected_data();
    void malformedCallLogTimeIsRejected();
    void localStampIsReadWithoutZone();
    void localStampRoundTrips();
    void utcStampRequiresTheZoneSuffix();
    void utcStampIsWrittenInUtc();
    void dateIsReadAndWritten();
    void impossibleDateIsRejected();
};

void TestPhoneTime::callLogTimeIsReadAsLocalTimeInThisCentury()
{
    const std::optional<QDateTime> time = parseCallLogTime(u"24/03/05,14:07:09");

    QVERIFY(time.has_value());
    QCOMPARE(time->date(), QDate(2024, 3, 5));
    QCOMPARE(time->time(), QTime(14, 7, 9));
    QCOMPARE(time->timeSpec(), Qt::LocalTime);
}

void TestPhoneTime::callLogTimeRoundTrips()
{
    const QDateTime time(QDate(2031, 12, 31), QTime(23, 59, 58), QTimeZone::LocalTime);

    QCOMPARE(formatCallLogTime(time), QStringLiteral("31/12/31,23:59:58"));
    QCOMPARE(parseCallLogTime(formatCallLogTime(time)).value_or(QDateTime()), time);
}

void TestPhoneTime::malformedCallLogTimeIsRejected_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("month 13") << QStringLiteral("24/13/05,14:07:09");
    QTest::newRow("four-digit year") << QStringLiteral("2024/03/05,14:07:09");
    QTest::newRow("dashes") << QStringLiteral("24-03-05,14:07:09");
    QTest::newRow("letters") << QStringLiteral("2a/03/05,14:07:09");
    QTest::newRow("hour 24") << QStringLiteral("24/03/05,24:00:00");
    QTest::newRow("empty") << QString();
}

void TestPhoneTime::malformedCallLogTimeIsRejected()
{
    QFETCH(QString, text);

    QVERIFY(!parseCallLogTime(text).has_value());
}

void TestPhoneTime::localStampIsReadWithoutZone()
{
    const std::optional<QDateTime> time = parseLocalStamp(u"20240305T140709");

    QVERIFY(time.has_value());
    QCOMPARE(time->date(), QDate(2024, 3, 5));
    QCOMPARE(time->time(), QTime(14, 7, 9));
    QCOMPARE(time->timeSpec(), Qt::LocalTime);
    QVERIFY(!parseLocalStamp(u"20240305T140709Z").has_value());
}

void TestPhoneTime::localStampRoundTrips()
{
    const QDateTime time(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::LocalTime);

    QCOMPARE(formatLocalStamp(time), QStringLiteral("20260102T030405"));
    QCOMPARE(parseLocalStamp(formatLocalStamp(time)).value_or(QDateTime()), time);
}

void TestPhoneTime::utcStampRequiresTheZoneSuffix()
{
    const std::optional<QDateTime> time = parseUtcStamp(u"20240305T140709Z");

    QVERIFY(time.has_value());
    QCOMPARE(*time, QDateTime(QDate(2024, 3, 5), QTime(14, 7, 9), QTimeZone::UTC));
    QVERIFY(!parseUtcStamp(u"20240305T140709").has_value());
    QVERIFY(!parseUtcStamp(u"20240305 140709Z").has_value());
}

void TestPhoneTime::utcStampIsWrittenInUtc()
{
    const QDateTime utc(QDate(2024, 3, 5), QTime(14, 7, 9), QTimeZone::UTC);

    QCOMPARE(formatUtcStamp(utc.toLocalTime()), QStringLiteral("20240305T140709Z"));
}

void TestPhoneTime::dateIsReadAndWritten()
{
    QCOMPARE(parseDate(u"20240229").value_or(QDate()), QDate(2024, 2, 29));
    QCOMPARE(formatDate(QDate(2024, 2, 29)), QStringLiteral("20240229"));
}

void TestPhoneTime::impossibleDateIsRejected()
{
    QVERIFY(!parseDate(u"20230229").has_value());
    QVERIFY(!parseDate(u"2023022").has_value());
}

QTEST_GUILESS_MAIN(TestPhoneTime)

#include "tst_phonetime.moc"
