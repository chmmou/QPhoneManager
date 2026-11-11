#include "core/protocol/AdbBridge.h"

#include "WaitFor.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace qpm::core;
using namespace qpm::core::protocol;
using qpm::test::waitFor;

namespace {

// Writes an executable shell script that stands in for adb.
QString writeFakeAdb(const QTemporaryDir &directory, const QByteArray &body)
{
    const QString path = directory.filePath(QStringLiteral("adb"));
    QFile script(path);
    if (!script.open(QIODevice::WriteOnly))
        return {};
    script.write("#!/bin/sh\n" + body);
    script.close();
    script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestAdbBridge : public QObject
{
    Q_OBJECT

public:
    explicit TestAdbBridge(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void deviceListIsParsed();
    void unexpectedDeviceListIsRejected();
    void forwardPortIsParsed_data();
    void forwardPortIsParsed();
    void serialNumbersAreChecked_data();
    void serialNumbersAreChecked();
    void relativeAdbPathIsRefused();
    void invalidSerialIsRefusedBeforeAdbRuns();
    void forwardPassesEachArgumentSeparately();
    void failingAdbIsReported();
    void missingClientIsReported();
    void floodedErrorOutputStopsAdb();
};

void TestAdbBridge::deviceListIsParsed()
{
    const QByteArray output =
        "* daemon not running; starting now at tcp:5037\n"
        "* daemon started successfully\n"
        "List of devices attached\n"
        "emulator-5554          device product:sdk_phone64 model:sdk_gphone64_x86_64 "
        "device:emu64xa transport_id:1\n"
        "R58M12ABCDE            unauthorized usb:1-1 transport_id:2\n"
        "\n";

    const Result<QList<AdbDevice>> devices = AdbBridge::parseDevices(output);
    QVERIFY(devices.hasValue());
    QCOMPARE(devices.value().size(), 2);
    QCOMPARE(devices.value().at(0).serial, QStringLiteral("emulator-5554"));
    QCOMPARE(devices.value().at(0).state, QStringLiteral("device"));
    QCOMPARE(devices.value().at(0).model, QStringLiteral("sdk_gphone64_x86_64"));
    QCOMPARE(devices.value().at(0).product, QStringLiteral("sdk_phone64"));
    QCOMPARE(devices.value().at(1).state, QStringLiteral("unauthorized"));
}

void TestAdbBridge::unexpectedDeviceListIsRejected()
{
    const Result<QList<AdbDevice>> devices = AdbBridge::parseDevices("adb: unknown command\n");
    QVERIFY(!devices.hasValue());
    QCOMPARE(devices.error().code(), ErrorCode::ProtocolError);
}

void TestAdbBridge::forwardPortIsParsed_data()
{
    QTest::addColumn<QByteArray>("output");
    QTest::addColumn<int>("port");

    QTest::newRow("port") << QByteArrayLiteral("41234\n") << 41234;
    QTest::newRow("empty") << QByteArray() << 0;
    QTest::newRow("zero") << QByteArrayLiteral("0\n") << 0;
    QTest::newRow("too large") << QByteArrayLiteral("70000\n") << 0;
    QTest::newRow("text") << QByteArrayLiteral("error: closed\n") << 0;
}

void TestAdbBridge::forwardPortIsParsed()
{
    QFETCH(QByteArray, output);
    QFETCH(int, port);

    QCOMPARE(int(AdbBridge::parseForwardPort(output).value_or(0)), port);
}

void TestAdbBridge::serialNumbersAreChecked_data()
{
    QTest::addColumn<QString>("serial");
    QTest::addColumn<bool>("valid");

    QTest::newRow("emulator") << QStringLiteral("emulator-5554") << true;
    QTest::newRow("network") << QStringLiteral("192.168.1.2:5555") << true;
    QTest::newRow("usb") << QStringLiteral("R58M12ABCDE") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("option") << QStringLiteral("-s") << false;
    QTest::newRow("space") << QStringLiteral("a b") << false;
    QTest::newRow("shell") << QStringLiteral("x;rm") << false;
}

void TestAdbBridge::serialNumbersAreChecked()
{
    QFETCH(QString, serial);
    QFETCH(bool, valid);

    QCOMPARE(AdbBridge::isValidSerial(serial), valid);
}

void TestAdbBridge::relativeAdbPathIsRefused()
{
    AdbBridge bridge(QStringLiteral("adb"));

    const QFuture<Result<QList<AdbDevice>>> devices = bridge.devices();
    QVERIFY(waitFor(devices));
    QVERIFY(!devices.result().hasValue());
    QCOMPARE(devices.result().error().code(), ErrorCode::InvalidInput);
}

void TestAdbBridge::invalidSerialIsRefusedBeforeAdbRuns()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString marker = directory.filePath(QStringLiteral("ran"));
    const QString adb = writeFakeAdb(directory, "touch '" + marker.toUtf8() + "'\n");
    AdbBridge bridge(adb);

    const QFuture<Result<quint16>> forward = bridge.forward(QStringLiteral("-x"), 5210);
    QVERIFY(waitFor(forward));
    QCOMPARE(forward.result().error().code(), ErrorCode::InvalidInput);
    QVERIFY(!QFile::exists(marker));
}

void TestAdbBridge::forwardPassesEachArgumentSeparately()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString arguments = directory.filePath(QStringLiteral("arguments"));
    const QString adb = writeFakeAdb(directory, "printf '%s\\n' \"$@\" > '" + arguments.toUtf8() +
                                                    "'\necho 41234\n");
    AdbBridge bridge(adb);

    const QFuture<Result<quint16>> forward = bridge.forward(QStringLiteral("emulator-5554"), 5210);
    QVERIFY(waitFor(forward));
    QVERIFY(forward.result().hasValue());
    QCOMPARE(forward.result().value(), quint16(41234));
    QCOMPARE(readFile(arguments),
             QByteArrayLiteral("-s\nemulator-5554\nforward\ntcp:0\ntcp:5210\n"));
}

void TestAdbBridge::failingAdbIsReported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString adb = writeFakeAdb(directory, "echo 'error: device offline' >&2\nexit 1\n");
    AdbBridge bridge(adb);

    const QFuture<Result<void>> started = bridge.startServer();
    QVERIFY(waitFor(started));
    QVERIFY(!started.result().hasValue());
    QCOMPARE(started.result().error().code(), ErrorCode::IoFailure);
    QVERIFY(started.result().error().message().contains(QStringLiteral("device offline")));
}

void TestAdbBridge::missingClientIsReported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString adb =
        writeFakeAdb(directory, "echo 'Error: Activity class {com.fjsoft.myphoneexplorer.client/"
                                ".MainActivity} does not exist.'\n");
    AdbBridge bridge(adb);

    const QFuture<Result<void>> started = bridge.startClient(QStringLiteral("emulator-5554"));
    QVERIFY(waitFor(started));
    QCOMPARE(started.result().error().code(), ErrorCode::NotFound);
}

// The error output of adb was cut per read but not in total, so a program
// that kept writing errors could fill the memory.
void TestAdbBridge::floodedErrorOutputStopsAdb()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString adb =
        writeFakeAdb(directory, "head -c 3000000 /dev/zero | tr '\\000' 'e' >&2\nexit 0\n");
    AdbBridge bridge(adb);

    const QFuture<Result<void>> started = bridge.startServer();
    QVERIFY(waitFor(started));

    QVERIFY(!started.result().hasValue());
    QCOMPARE(started.result().error().code(), ErrorCode::LimitExceeded);
}

QTEST_GUILESS_MAIN(TestAdbBridge)

#include "tst_adbbridge.moc"
