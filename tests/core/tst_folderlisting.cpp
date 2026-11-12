#include "core/codec/FolderListing.h"

#include <QTest>
#include <QTimeZone>

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

// The form of the listing in the client log, with made-up names.
QByteArray clientListing()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
           "<!DOCTYPE folder-listing SYSTEM \"obex-folder-listing.dtd\">\r\n"
           "<folder-listing version=\"1.0\">\r\n"
           "<parent-folder path=\"\\sdcard\\\"/>\r\n"
           "<folder name=\"Alarms\"/>\r\n"
           "<file name=\"Beispiel.txt\" size=\"1234\" modified=\"20261102T081500Z\"/>\r\n"
           "<folder name=\"DCIM\"/>\r\n"
           "<file name=\"Foto vom See.jpg\" size=\"0\" modified=\"20260715T120000Z\" />\r\n"
           "</folder-listing>";
}

} // namespace

class TestFolderListing : public QObject
{
    Q_OBJECT

public:
    explicit TestFolderListing(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void clientListingIsRead();
    void rootListingNamesTheStorages();
    void listingWithSubfoldersHasOneBlockPerFolder();
    void onlyAmpersandAndQuoteAreDecoded();
    void angleBracketsStayInNames();
    void unusableNamesAreLeftOut_data();
    void unusableNamesAreLeftOut();
    void fileWithoutReadableSizeIsLeftOut();
    void unreadableTimeLeavesTheTimeOpen();
    void storageAccessPermissionIsKept();
    void malformedListingIsRefused_data();
    void malformedListingIsRefused();
    void tooManyEntriesAreRefused();
    void namesWithUmlautsAndEmojiAreRead();
};

void TestFolderListing::clientListingIsRead()
{
    const Result<FolderListings> read = parseFolderListing(clientListing());

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().folders.size(), 1);
    QCOMPARE(read.value().skipped, 0);
    const FolderListing &listing = read.value().folders.constFirst();
    QCOMPARE(listing.parentPath, QStringLiteral("\\sdcard\\"));
    QCOMPARE(listing.entries.size(), 4);
    QCOMPARE(listing.entries.at(0).name, QStringLiteral("Alarms"));
    QVERIFY(listing.entries.at(0).isFolder);
    QVERIFY(!listing.entries.at(0).size.has_value());
    const FolderEntry &file = listing.entries.at(1);
    QCOMPARE(file.name, QStringLiteral("Beispiel.txt"));
    QVERIFY(!file.isFolder);
    QCOMPARE(file.size, std::optional<quint64>(1234));
    QCOMPARE(file.modified, QDateTime(QDate(2026, 11, 2), QTime(8, 15), QTimeZone::UTC));
    QCOMPARE(listing.entries.at(3).name, QStringLiteral("Foto vom See.jpg"));
    QCOMPARE(listing.entries.at(3).size, std::optional<quint64>(0));
}

void TestFolderListing::rootListingNamesTheStorages()
{
    const QByteArray root = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                            "<folder-listing version=\"1.0\">\r\n"
                            "<parent-folder path=\"\"/>\r\n"
                            "<folder name=\"sdcard\"/>\r\n<folder name=\"external\"/>\r\n"
                            "</folder-listing>";

    const Result<FolderListings> read = parseFolderListing(root);

    QVERIFY(read.hasValue());
    const FolderListing &listing = read.value().folders.constFirst();
    QCOMPARE(listing.parentPath, QString());
    QCOMPARE(listing.entries.size(), 2);
    QCOMPARE(listing.entries.at(1).name, QStringLiteral("external"));
}

// With subfolders the phone appends one block per folder to the same stream,
// without a common root element.
void TestFolderListing::listingWithSubfoldersHasOneBlockPerFolder()
{
    const QByteArray tree =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
        "<folder-listing version=\"1.0\">\r\n<parent-folder path=\"\\sdcard\\Music\\\"/>\r\n"
        "<folder name=\"Live\"/>\r\n"
        "<file name=\"a.mp3\" size=\"10\" modified=\"20260101T000000Z\"/>\r\n"
        "</folder-listing><folder-listing version=\"1.0\">\r\n"
        "<parent-folder path=\"\\sdcard\\Music\\Live\\\"/>\r\n"
        "<file name=\"b.mp3\" size=\"20\" modified=\"20260102T000000Z\"/>\r\n"
        "</folder-listing>";

    const Result<FolderListings> read = parseFolderListing(tree);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().folders.size(), 2);
    QCOMPARE(read.value().folders.at(0).parentPath, QStringLiteral("\\sdcard\\Music\\"));
    QCOMPARE(read.value().folders.at(0).entries.size(), 2);
    QCOMPARE(read.value().folders.at(1).parentPath, QStringLiteral("\\sdcard\\Music\\Live\\"));
    QCOMPARE(read.value().folders.at(1).entries.constFirst().name, QStringLiteral("b.mp3"));
}

void TestFolderListing::onlyAmpersandAndQuoteAreDecoded()
{
    const QByteArray listing =
        "<folder-listing version=\"1.0\"><parent-folder path=\"\\sdcard\\A &amp; B\\\"/>"
        "<file name=\"Tom &amp; &quot;Jerry&quot; &lt;3 &amp;amp;.txt\" size=\"1\" "
        "modified=\"20260101T000000Z\"/></folder-listing>";

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().folders.constFirst().parentPath, QStringLiteral("\\sdcard\\A & B\\"));
    QCOMPARE(read.value().folders.constFirst().entries.constFirst().name,
             QStringLiteral("Tom & \"Jerry\" &lt;3 &amp;.txt"));
}

// The phone does not escape "<" and ">", so they arrive as they are inside a
// quoted name.
void TestFolderListing::angleBracketsStayInNames()
{
    const QByteArray listing = "<folder-listing version=\"1.0\"><parent-folder path=\"\"/>"
                               "<folder name=\"<Backup> 2026\"/>"
                               "<file name=\"a>b<c.txt\" size=\"3\" modified=\"20260101T000000Z\"/>"
                               "</folder-listing>";

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(read.hasValue());
    const QList<FolderEntry> &entries = read.value().folders.constFirst().entries;
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("<Backup> 2026"));
    QCOMPARE(entries.at(1).name, QStringLiteral("a>b<c.txt"));
}

void TestFolderListing::unusableNamesAreLeftOut_data()
{
    QTest::addColumn<QByteArray>("name");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("dot") << QByteArray(".");
    QTest::newRow("dot dot") << QByteArray("..");
    QTest::newRow("slash") << QByteArray("../../.bashrc");
    QTest::newRow("absolute") << QByteArray("/etc/passwd");
    QTest::newRow("backslash") << QByteArray("..\\..\\x");
    QTest::newRow("control character") << QByteArray("a\x01"
                                                     "b");
    QTest::newRow("line feed") << QByteArray("a\nb");
    QTest::newRow("right-to-left override") << QByteArray("a\xE2\x80\xAE"
                                                          "gpj.exe");
    QTest::newRow("too long") << QByteArray(256, 'x');
}

void TestFolderListing::unusableNamesAreLeftOut()
{
    QFETCH(QByteArray, name);
    const QByteArray listing = "<folder-listing version=\"1.0\"><parent-folder path=\"\"/>"
                               "<file name=\"" +
                               name +
                               "\" size=\"1\" modified=\"20260101T000000Z\"/>"
                               "<folder name=\"" +
                               name + "\"/><file name=\"ok.txt\" size=\"1\"/></folder-listing>";

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().skipped, 2);
    QCOMPARE(read.value().folders.constFirst().entries.size(), 1);
    QCOMPARE(read.value().folders.constFirst().entries.constFirst().name, QStringLiteral("ok.txt"));
}

void TestFolderListing::fileWithoutReadableSizeIsLeftOut()
{
    const QByteArray listing =
        "<folder-listing version=\"1.0\">"
        "<file name=\"a\" size=\"-1\"/><file name=\"b\" size=\"1e3\"/>"
        "<file name=\"c\"/><file name=\"d\" size=\"99999999999999999999999\"/>"
        "<file name=\"e\" size=\"5\"/></folder-listing>";

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().skipped, 4);
    QCOMPARE(read.value().folders.constFirst().entries.size(), 1);
    QCOMPARE(read.value().folders.constFirst().entries.constFirst().size,
             std::optional<quint64>(5));
}

void TestFolderListing::unreadableTimeLeavesTheTimeOpen()
{
    const QByteArray listing =
        "<folder-listing version=\"1.0\">"
        "<file name=\"a\" size=\"1\" modified=\"gestern\"/></folder-listing>";

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(read.hasValue());
    QVERIFY(!read.value().folders.constFirst().entries.constFirst().modified.isValid());
}

void TestFolderListing::storageAccessPermissionIsKept()
{
    const QByteArray listing = "<folder-listing version=\"1.0\">"
                               "<folder name=\"com.example\" user-perm=\"R\"/></folder-listing>";

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().folders.constFirst().entries.constFirst().userPerm, QStringLiteral("R"));
}

void TestFolderListing::malformedListingIsRefused_data()
{
    QTest::addColumn<QByteArray>("listing");
    QTest::addColumn<int>("code");

    QTest::newRow("empty") << QByteArray() << int(ErrorCode::InvalidInput);
    QTest::newRow("no block") << QByteArray("<?xml version=\"1.0\"?><other/>")
                              << int(ErrorCode::InvalidInput);
    QTest::newRow("cut inside a tag")
        << QByteArray("<folder-listing version=\"1.0\"><file name=\"a")
        << int(ErrorCode::InvalidInput);
    QTest::newRow("unquoted value")
        << QByteArray("<folder-listing version=1.0>") << int(ErrorCode::InvalidInput);
    QTest::newRow("entity declaration")
        << QByteArray("<!DOCTYPE folder-listing [<!ENTITY x \"y\">]><folder-listing/>")
        << int(ErrorCode::InvalidInput);
    QTest::newRow("too large") << QByteArray(maxFolderListingBytes + 1, ' ')
                               << int(ErrorCode::LimitExceeded);
}

void TestFolderListing::malformedListingIsRefused()
{
    QFETCH(QByteArray, listing);
    QFETCH(int, code);

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(!read.hasValue());
    QCOMPARE(int(read.error().code()), code);
}

void TestFolderListing::tooManyEntriesAreRefused()
{
    QByteArray listing = "<folder-listing version=\"1.0\">";
    const QByteArray entry = "<folder name=\"x\"/>";
    listing.reserve(listing.size() + entry.size() * (maxFolderListingEntries + 1) + 20);
    for (qsizetype i = 0; i <= maxFolderListingEntries; ++i)
        listing += entry;
    listing += "</folder-listing>";

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::LimitExceeded);
}

void TestFolderListing::namesWithUmlautsAndEmojiAreRead()
{
    const QByteArray listing = "<folder-listing version=\"1.0\">"
                               "<file name=\"Grüße \xF0\x9F\x98\x80.txt\" size=\"1\"/>"
                               "<folder name=\"Übersicht\"/></folder-listing>";

    const Result<FolderListings> read = parseFolderListing(listing);

    QVERIFY(read.hasValue());
    const QList<FolderEntry> &entries = read.value().folders.constFirst().entries;
    QCOMPARE(entries.at(0).name, QString::fromUtf8("Grüße \xF0\x9F\x98\x80.txt"));
    QCOMPARE(entries.at(1).name, QStringLiteral("Übersicht"));
}

QTEST_APPLESS_MAIN(TestFolderListing)

#include "tst_folderlisting.moc"
