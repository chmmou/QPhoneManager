#include "core/codec/AppListCodec.h"

#include <QTest>
#include <QTimeZone>

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

QString userAppLine()
{
    return QStringLiteral(
        "*EJAVA: \"Grüße =26 Co=2E\",\"\",\"2.4.1\",1,org.example.greetings,"
        "/data/app/~~kX3a=3D=3D/org.example.greetings-Qm9v=3D=3D/base.apk,20261103T091500Z,"
        "5242880");
}

} // namespace

// The app list of the phone as the client writes it: its lines, the lines it
// sends after an installation or a removal, and the commands that act on an
// app. Everything comes from the phone and is checked before it is used.
class TestAppList : public QObject
{
    Q_OBJECT

public:
    explicit TestAppList(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void userAppIsReadWithAllFields();
    void systemAppWithoutApkPathIsRead();
    void versionWithQuotesAndCommasIsKept();
    void missingVersionAndLabelAreFilledIn();
    void directionAndControlCharactersAreDroppedFromTheLabel();
    void malformedLineIsRejected_data();
    void malformedLineIsRejected();
    void listSkipsTheCountAndCountsMalformedAndRepeatedLines();
    void tooManyAppsAreRefused_data();
    void tooManyAppsAreRefused();
    void installationAndRemovalAreReported();
    void unreadableReportIsRejected();
    void commandsCarryOnlyValidPackageNames_data();
    void commandsCarryOnlyValidPackageNames();
    void iconNameNamesPackageAndSize();
    void apkPathOnThePhoneStartsAtThePhoneRoot_data();
    void apkPathOnThePhoneStartsAtThePhoneRoot();
};

void TestAppList::userAppIsReadWithAllFields()
{
    const Result<AppInfo> app = parseAppLine(userAppLine());

    QVERIFY(app.hasValue());
    QCOMPARE(app.value().package, QStringLiteral("org.example.greetings"));
    QCOMPARE(app.value().label, QStringLiteral("Grüße & Co."));
    QCOMPARE(app.value().version, QStringLiteral("2.4.1"));
    QVERIFY(app.value().isUserApp);
    QCOMPARE(app.value().sourcePath,
             QStringLiteral("/data/app/~~kX3a==/org.example.greetings-Qm9v==/base.apk"));
    QCOMPARE(app.value().installed, QDateTime(QDate(2026, 11, 3), QTime(9, 15), QTimeZone::UTC));
    QCOMPARE(app.value().size, std::optional<quint64>(5242880));
}

void TestAppList::systemAppWithoutApkPathIsRead()
{
    const Result<AppInfo> app =
        parseAppLine(QStringLiteral("*EJAVA: \"Settings\",\"\",\"16\",0,com.android.settings"));

    QVERIFY(app.hasValue());
    QCOMPARE(app.value().label, QStringLiteral("Settings"));
    QVERIFY(!app.value().isUserApp);
    QVERIFY(app.value().sourcePath.isEmpty());
    QVERIFY(!app.value().installed.isValid());
    QVERIFY(!app.value().size.has_value());
}

// The phone writes the version name as it is, without escaping it.
void TestAppList::versionWithQuotesAndCommasIsKept()
{
    const Result<AppInfo> app = parseAppLine(QStringLiteral(
        "*EJAVA: \"Beta\",\"\",\"1.0 \"rc\",2\",1,org.example.beta,/data/app/a/base.apk,"
        "20260101T000000Z,10"));

    QVERIFY(app.hasValue());
    QCOMPARE(app.value().version, QStringLiteral("1.0 \"rc\",2"));
    QCOMPARE(app.value().package, QStringLiteral("org.example.beta"));
}

void TestAppList::missingVersionAndLabelAreFilledIn()
{
    const Result<AppInfo> app =
        parseAppLine(QStringLiteral("*EJAVA: \"\",\"\",\"null\",0,android.ext.shared"));

    QVERIFY(app.hasValue());
    QCOMPARE(app.value().label, QStringLiteral("android.ext.shared"));
    QVERIFY(app.value().version.isEmpty());
}

// A label shown in the window must not turn its text around or break the line.
void TestAppList::directionAndControlCharactersAreDroppedFromTheLabel()
{
    const QString line = QStringLiteral("*EJAVA: \"Bank") + QChar(0x202E) +
                         QStringLiteral("ppa=0A\",\"\",\"1") + QChar(0x2066) +
                         QStringLiteral(".0\",1,org.example.bank");
    const Result<AppInfo> app = parseAppLine(line);

    QVERIFY(app.hasValue());
    QCOMPARE(app.value().label, QStringLiteral("Bankppa"));
    QCOMPARE(app.value().version, QStringLiteral("1.0"));
}

void TestAppList::malformedLineIsRejected_data()
{
    QTest::addColumn<QString>("line");

    const QString tail = QStringLiteral(",/data/app/a/base.apk,20260101T000000Z,10");
    QTest::newRow("no prefix") << QStringLiteral("\"A\",\"\",\"1\",1,org.example.a");
    QTest::newRow("other prefix") << QStringLiteral("*EJAVACOUNT: 3");
    QTest::newRow("label without quotes") << QStringLiteral("*EJAVA: A,\"\",\"1\",1,org.example.a");
    QTest::newRow("second field not empty")
        << QStringLiteral("*EJAVA: \"A\",\"x\",\"1\",1,org.example.a");
    QTest::newRow("flag 2") << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",2,org.example.a");
    QTest::newRow("no package") << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,");
    QTest::newRow("package with space")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example a");
    QTest::newRow("package with escaped comma")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example=2Ca");
    QTest::newRow("package starting with a digit")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,1org.example");
    QTest::newRow("package with an empty segment")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org..example");
    QTest::newRow("package too long")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,a") + QString(maxPackageNameLength, u'b');
    QTest::newRow("control character")
        << QStringLiteral("*EJAVA: \"A\tB\",\"\",\"1\",1,org.example.a");
    QTest::newRow("label too long") << QStringLiteral("*EJAVA: \"") +
                                           QString(maxAppLabelLength + 1, u'x') +
                                           QStringLiteral("\",\"\",\"1\",1,org.example.a");
    QTest::newRow("version too long") << QStringLiteral("*EJAVA: \"A\",\"\",\"") +
                                             QString(maxAppVersionLength + 1, u'9') +
                                             QStringLiteral("\",1,org.example.a");
    QTest::newRow("half a tail") << QStringLiteral(
        "*EJAVA: \"A\",\"\",\"1\",1,org.example.a,/data/app/a/base.apk");
    QTest::newRow("relative path")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a,data/a.apk,"
                          "20260101T000000Z,10");
    QTest::newRow("path going up")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a,/data/app/../../etc/x.apk,"
                          "20260101T000000Z,10");
    QTest::newRow("path with a backslash")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a,/data/app/a=5Cb/base.apk,"
                          "20260101T000000Z,10");
    QTest::newRow("path to no APK")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a,/data/app/a/base.odex,"
                          "20260101T000000Z,10");
    QTest::newRow("local time") << QStringLiteral(
        "*EJAVA: \"A\",\"\",\"1\",1,org.example.a,/data/app/a/base.apk,"
        "20260101T000000,10");
    QTest::newRow("impossible date")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a,/data/app/a/base.apk,"
                          "20261301T000000Z,10");
    QTest::newRow("negative size")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a,/data/app/a/base.apk,"
                          "20260101T000000Z,-10");
    QTest::newRow("size too large") << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a,"
                                                      "/data/app/a/base.apk,20260101T000000Z,") +
                                           QString::number(maxApkBytes + 1);
    QTest::newRow("trailing field")
        << QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a") + tail + QStringLiteral(",1");
}

void TestAppList::malformedLineIsRejected()
{
    QFETCH(QString, line);

    const Result<AppInfo> app = parseAppLine(line);

    QVERIFY(!app.hasValue());
    QCOMPARE(app.error().code(), ErrorCode::InvalidInput);
}

void TestAppList::listSkipsTheCountAndCountsMalformedAndRepeatedLines()
{
    const QStringList lines = {
        QStringLiteral("*EJAVACOUNT: 4"),
        userAppLine(),
        QStringLiteral("*EJAVA: \"Settings\",\"\",\"16\",0,com.android.settings"),
        QStringLiteral("*EJAVA: broken"),
        QStringLiteral("*EJAVA: \"Twice\",\"\",\"1\",1,org.example.greetings"),
    };

    const Result<AppList> list = parseAppList(lines);

    QVERIFY(list.hasValue());
    QCOMPARE(list.value().apps.size(), 2);
    QCOMPARE(list.value().apps.at(0).label, QStringLiteral("Grüße & Co."));
    QCOMPARE(list.value().apps.at(1).package, QStringLiteral("com.android.settings"));
    QCOMPARE(list.value().malformed, 2);
}

void TestAppList::tooManyAppsAreRefused_data()
{
    QTest::addColumn<QStringList>("lines");

    QTest::newRow("announced") << QStringList{QStringLiteral("*EJAVACOUNT: %1").arg(maxApps + 1)};
    QStringList sent;
    for (qsizetype i = 0; i <= maxApps; ++i)
        sent.append(QStringLiteral("*EJAVA: \"A\",\"\",\"1\",1,org.example.a%1").arg(i));
    QTest::newRow("sent") << sent;
}

void TestAppList::tooManyAppsAreRefused()
{
    QFETCH(QStringList, lines);

    const Result<AppList> list = parseAppList(lines);

    QVERIFY(!list.hasValue());
    QCOMPARE(list.error().code(), ErrorCode::LimitExceeded);
}

void TestAppList::installationAndRemovalAreReported()
{
    const Result<AppEvent> added = parseAppEvent(userAppLine());
    QVERIFY(added.hasValue());
    QCOMPARE(added.value().kind, AppEvent::Kind::Added);
    QCOMPARE(added.value().app.package, QStringLiteral("org.example.greetings"));
    QCOMPARE(added.value().app.version, QStringLiteral("2.4.1"));

    const Result<AppEvent> removed =
        parseAppEvent(QStringLiteral("*EJAVA: \"\",\"\",\"\",0,org.example.greetings"));
    QVERIFY(removed.hasValue());
    QCOMPARE(removed.value().kind, AppEvent::Kind::Removed);
    QCOMPARE(removed.value().app.package, QStringLiteral("org.example.greetings"));
}

void TestAppList::unreadableReportIsRejected()
{
    for (const QString &line : {QStringLiteral("*EJAVA: \"\",\"\",\"\",0,"),
                                QStringLiteral("*EJAVA: \"\",\"\",\"\",0,org example"),
                                QStringLiteral("*EJAVA: whatever")}) {
        const Result<AppEvent> event = parseAppEvent(line);
        QVERIFY2(!event.hasValue(), qPrintable(line));
        QCOMPARE(event.error().code(), ErrorCode::InvalidInput);
    }
}

void TestAppList::commandsCarryOnlyValidPackageNames_data()
{
    QTest::addColumn<QString>("package");
    QTest::addColumn<bool>("valid");

    QTest::newRow("usual") << QStringLiteral("com.example.app") << true;
    QTest::newRow("one segment") << QStringLiteral("android") << true;
    QTest::newRow("underscore and digits") << QStringLiteral("org.ex_2.app9") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("comma") << QStringLiteral("com.example,2") << false;
    QTest::newRow("line end") << QStringLiteral("com.example\r\nATD123;") << false;
    QTest::newRow("leading dot") << QStringLiteral(".com.example") << false;
    QTest::newRow("trailing dot") << QStringLiteral("com.example.") << false;
    QTest::newRow("non-ASCII letter") << QStringLiteral("com.exämple") << false;
    QTest::newRow("too long") << QStringLiteral("a") + QString(maxPackageNameLength, u'b') << false;
}

void TestAppList::commandsCarryOnlyValidPackageNames()
{
    QFETCH(QString, package);
    QFETCH(bool, valid);

    QCOMPARE(isValidPackageName(package), valid);
    const Result<QString> start = startAppCommand(package);
    const Result<QString> uninstall = uninstallAppCommand(package);
    QCOMPARE(start.hasValue(), valid);
    QCOMPARE(uninstall.hasValue(), valid);
    if (valid) {
        QCOMPARE(start.value(), QStringLiteral("AT*EJAVA=4,") + package);
        QCOMPARE(uninstall.value(), QStringLiteral("AT*EJAVA=2,") + package);
    } else {
        QCOMPARE(start.error().code(), ErrorCode::InvalidInput);
        QCOMPARE(uninstall.error().code(), ErrorCode::InvalidInput);
    }
    QCOMPARE(listAppsCommand(), QStringLiteral("AT*EJAVA=1"));
}

void TestAppList::iconNameNamesPackageAndSize()
{
    const Result<QString> name = appIconName(QStringLiteral("com.example.app"), 48);
    QVERIFY(name.hasValue());
    QCOMPARE(name.value(), QStringLiteral("\\appicon\\com.example.app\\48"));

    QVERIFY(!appIconName(QStringLiteral("com.example.app"), 0).hasValue());
    QVERIFY(!appIconName(QStringLiteral("com.example.app"), 193).hasValue());
    QVERIFY(!appIconName(QStringLiteral("..\\x"), 48).hasValue());
}

void TestAppList::apkPathOnThePhoneStartsAtThePhoneRoot_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("expected");

    QTest::newRow("user app") << QStringLiteral("/data/app/~~kX3a==/org.example-Qm9v==/base.apk")
                              << QStringLiteral("\\phone\\data\\app\\~~kX3a==\\org.example-Qm9v=="
                                                "\\base.apk");
    QTest::newRow("system app") << QStringLiteral("/system/app/Clock/Clock.APK")
                                << QStringLiteral("\\phone\\system\\app\\Clock\\Clock.APK");
    QTest::newRow("relative") << QStringLiteral("data/app/a.apk") << QString();
    QTest::newRow("going up") << QStringLiteral("/data/../a.apk") << QString();
    QTest::newRow("empty part") << QStringLiteral("/data//a.apk") << QString();
    QTest::newRow("backslash") << QStringLiteral("/data/a\\b.apk") << QString();
    QTest::newRow("no APK") << QStringLiteral("/data/app/a/base.jar") << QString();
    QTest::newRow("too long") << QStringLiteral("/") + QString(maxApkPathLength, u'a') +
                                     QStringLiteral(".apk")
                              << QString();
}

void TestAppList::apkPathOnThePhoneStartsAtThePhoneRoot()
{
    QFETCH(QString, source);
    QFETCH(QString, expected);

    const Result<QString> path = apkPhonePath(source);

    QCOMPARE(path.hasValue(), !expected.isEmpty());
    if (path.hasValue())
        QCOMPARE(path.value(), expected);
    else
        QCOMPARE(path.error().code(), ErrorCode::InvalidInput);
}

QTEST_MAIN(TestAppList)

#include "tst_applist.moc"
