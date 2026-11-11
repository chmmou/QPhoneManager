#include "core/codec/Limits.h"
#include "core/codec/SafeXml.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace qpm::core;
using namespace qpm::core::codec;

class TestLimits : public QObject
{
    Q_OBJECT

public:
    explicit TestLimits(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void plainFileNamesStayInTheTargetDirectory_data();
    void plainFileNamesStayInTheTargetDirectory();
    void namesThatLeaveTheTargetDirectoryAreRefused_data();
    void namesThatLeaveTheTargetDirectoryAreRefused();
    void existingSymbolicLinkIsRefused();
    void missingTargetDirectoryIsReported();
    void metadataIsParsedIntoATree();
    void folderListingWithExternalDoctypeIsAccepted();
    void unsafeOrBrokenXmlIsRefused_data();
    void unsafeOrBrokenXmlIsRefused();
};

void TestLimits::plainFileNamesStayInTheTargetDirectory_data()
{
    QTest::addColumn<QString>("name");

    QTest::newRow("ascii") << QStringLiteral("photo.jpg");
    QTest::newRow("non-ascii") << QStringLiteral("Grüße ä 東京.txt");
    QTest::newRow("dot file") << QStringLiteral(".hidden");
}

void TestLimits::plainFileNamesStayInTheTargetDirectory()
{
    QFETCH(QString, name);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const Result<QString> path = limits::safeDownloadPath(directory.path(), name);
    QVERIFY(path.hasValue());
    const QString base = QFileInfo(directory.path()).canonicalFilePath();
    QCOMPARE(path.value(), QDir(base).filePath(name));
}

void TestLimits::namesThatLeaveTheTargetDirectoryAreRefused_data()
{
    QTest::addColumn<QString>("name");

    QTest::newRow("empty") << QString();
    QTest::newRow("dot") << QStringLiteral(".");
    QTest::newRow("dot dot") << QStringLiteral("..");
    QTest::newRow("parent with slash") << QStringLiteral("../x");
    QTest::newRow("parent with backslash") << QStringLiteral("..\\x");
    QTest::newRow("absolute") << QStringLiteral("/etc/passwd");
    QTest::newRow("sub folder") << QStringLiteral("a/b");
    QTest::newRow("nul") << QString(QStringLiteral("a") + QChar(u'\0') + QStringLiteral("b"));
    QTest::newRow("direction override")
        << QString(QStringLiteral("a") + QChar(0x202E) + QStringLiteral("gpj.exe"));
    QTest::newRow("too long") << QString(300, u'a');
}

void TestLimits::namesThatLeaveTheTargetDirectoryAreRefused()
{
    QFETCH(QString, name);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const Result<QString> path = limits::safeDownloadPath(directory.path(), name);
    QVERIFY(!path.hasValue());
    QCOMPARE(path.error().code(), ErrorCode::InvalidInput);
}

void TestLimits::existingSymbolicLinkIsRefused()
{
    QTemporaryDir directory;
    QTemporaryDir outside;
    QVERIFY(directory.isValid() && outside.isValid());
    const QString target = outside.filePath(QStringLiteral("secret.txt"));
    QFile file(target);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(QFile::link(target, directory.filePath(QStringLiteral("link.txt"))));

    const Result<QString> path =
        limits::safeDownloadPath(directory.path(), QStringLiteral("link.txt"));
    QVERIFY(!path.hasValue());
    QCOMPARE(path.error().code(), ErrorCode::InvalidInput);
}

void TestLimits::missingTargetDirectoryIsReported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const Result<QString> path = limits::safeDownloadPath(
        directory.filePath(QStringLiteral("missing")), QStringLiteral("a.txt"));
    QVERIFY(!path.hasValue());
    QCOMPARE(path.error().code(), ErrorCode::NotFound);
}

void TestLimits::metadataIsParsedIntoATree()
{
    const QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                           "<metadata version=\"1.0\">\r\n"
                           "<accounts filter_disabled=\"0\">\r\n"
                           "<account selected=\"1\" type=\"com.example\" name=\"user@example.org\" "
                           "title=\"Example &amp; Co\"/>\r\n"
                           "</accounts>\r\n"
                           "</metadata>";

    const Result<XmlElement> root = parseSafeXml(xml);
    QVERIFY(root.hasValue());
    QCOMPARE(root.value().name, QStringLiteral("metadata"));
    QCOMPARE(root.value().children.size(), 1);
    const XmlElement &accounts = root.value().children.first();
    QCOMPARE(accounts.attribute(QStringLiteral("filter_disabled")), QStringLiteral("0"));
    QCOMPARE(accounts.children.first().attribute(QStringLiteral("title")),
             QStringLiteral("Example & Co"));
}

void TestLimits::folderListingWithExternalDoctypeIsAccepted()
{
    const QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                           "<!DOCTYPE folder-listing SYSTEM \"obex-folder-listing.dtd\">\r\n"
                           "<folder-listing version=\"1.0\">\r\n"
                           "<parent-folder path=\"\\sdcard\\\"/>\r\n"
                           "<folder name=\"DCIM\"/>\r\n"
                           "<file name=\"a.jpg\" size=\"12\" modified=\"20240305T140709Z\"/>\r\n"
                           "</folder-listing>";

    const Result<XmlElement> root = parseSafeXml(xml);
    QVERIFY(root.hasValue());
    QCOMPARE(root.value().children.size(), 3);
    QCOMPARE(root.value().children.at(2).attribute(QStringLiteral("size")), QStringLiteral("12"));
}

void TestLimits::unsafeOrBrokenXmlIsRefused_data()
{
    QTest::addColumn<QByteArray>("xml");
    QTest::addColumn<int>("code");

    const int invalid = int(ErrorCode::InvalidInput);
    const int limit = int(ErrorCode::LimitExceeded);

    QTest::newRow("entity expansion")
        << QByteArrayLiteral("<!DOCTYPE a [<!ENTITY x \"xxxxxxxxxx\"><!ENTITY y \"&x;&x;&x;\">]>"
                             "<a>&y;</a>")
        << invalid;
    QTest::newRow("external entity")
        << QByteArrayLiteral("<!DOCTYPE a [<!ENTITY e SYSTEM \"file:///etc/passwd\">]><a>&e;</a>")
        << invalid;
    QTest::newRow("undeclared entity")
        << QByteArrayLiteral("<!DOCTYPE a SYSTEM \"a.dtd\"><a>&e;</a>") << invalid;
    QTest::newRow("malformed") << QByteArrayLiteral("<a><b></a>") << invalid;
    QTest::newRow("no root") << QByteArrayLiteral("<?xml version=\"1.0\"?>") << invalid;

    QByteArray deep;
    for (int i = 0; i <= limits::maxXmlDepth; ++i)
        deep += "<e>";
    for (int i = 0; i <= limits::maxXmlDepth; ++i)
        deep += "</e>";
    QTest::newRow("too deep") << deep << limit;
    QTest::newRow("too large") << QByteArray("<a>") + QByteArray(limits::maxXmlBytes, 'x') + "</a>"
                               << limit;
}

void TestLimits::unsafeOrBrokenXmlIsRefused()
{
    QFETCH(QByteArray, xml);
    QFETCH(int, code);

    const Result<XmlElement> root = parseSafeXml(xml);
    QVERIFY(!root.hasValue());
    QCOMPARE(int(root.error().code()), code);
}

QTEST_GUILESS_MAIN(TestLimits)

#include "tst_limits.moc"
