#include "core/backup/ZipStore.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <optional>

using namespace qpm::core;
using namespace qpm::core::backup;

namespace {

// Written by Info-ZIP "zip -0 -r fixture.zip hallo.txt sub" with Zip 3.0: the
// file "hallo.txt", the folder "sub/" and the file "sub/ä.txt", each with the
// extra fields for time and owner that Info-ZIP adds.
constexpr char infoZipFixture[] =
    "UEsDBAoAAAAAAABYbF00q+gSCwAAAAsAAAAJABwAaGFsbG8udHh0VVQJAAMgjvVqqwL2anV4CwAB"
    "BOgDAAAE6AMAAEhhbGxvIFdlbHQKUEsDBAoAAAAAAABYbF0AAAAAAAAAAAAAAAAEABwAc3ViL1VU"
    "CQADII71arEC9mp1eAsAAQToAwAABOgDAABQSwMECgAACAAAAFhsXXFw0/sHAAAABwAAAAoAHABz"
    "dWIvw6QudHh0VVQJAAMgjvVqqwL2anV4CwABBOgDAAAE6AMAAEdyw7zDn2VQSwECHgMKAAAAAAAA"
    "WGxdNKvoEgsAAAALAAAACQAYAAAAAAAAAAAApIEAAAAAaGFsbG8udHh0VVQFAAMgjvVqdXgLAAEE"
    "6AMAAAToAwAAUEsBAh4DCgAAAAAAAFhsXQAAAAAAAAAAAAAAAAQAGAAAAAAAAAAQAO1BTgAAAHN1"
    "Yi9VVAUAAyCO9Wp1eAsAAQToAwAABOgDAABQSwECHgMKAAAIAAAAWGxdcXDT+wcAAAAHAAAACgAY"
    "AAAAAAAAAAAApIGMAAAAc3ViL8OkLnR4dFVUBQADII71anV4CwABBOgDAAAE6AMAAFBLBQYAAAAA"
    "AwADAOkAAADXAAAAAAA=";

void put16(QByteArray &out, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    out.append(bytes, 2);
}

void put32(QByteArray &out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}

QByteArray withField32(QByteArray bytes, qsizetype at, quint32 value)
{
    qToLittleEndian(value, bytes.data() + at);
    return bytes;
}

// One entry of an archive built by hand, with fields a writer could get wrong.
struct RawEntry
{
    QByteArray name;
    QByteArray data;
    quint16 flags = 0x0800;
    quint16 method = 0;
    std::optional<quint32> crc = std::nullopt;
    std::optional<quint32> packedSize = std::nullopt;
    std::optional<quint32> size = std::nullopt;
};

struct RawOptions
{
    std::optional<quint16> totalEntries = std::nullopt;
    bool zip64Locator = false;
};

QByteArray rawArchive(const QList<RawEntry> &entries, const RawOptions &options = {})
{
    QByteArray out;
    QList<quint32> offsets;
    for (const RawEntry &entry : entries) {
        offsets.append(quint32(out.size()));
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, entry.flags);
        put16(out, entry.method);
        put16(out, 0);
        put16(out, 0x21);
        put32(out, entry.crc.value_or(crc32(entry.data)));
        put32(out, entry.packedSize.value_or(quint32(entry.data.size())));
        put32(out, entry.size.value_or(quint32(entry.data.size())));
        put16(out, quint16(entry.name.size()));
        put16(out, 0);
        out += entry.name;
        out += entry.data;
    }
    const quint32 directoryOffset = quint32(out.size());
    for (qsizetype index = 0; index < entries.size(); ++index) {
        const RawEntry &entry = entries.at(index);
        put32(out, 0x02014b50);
        put16(out, 0x031E);
        put16(out, 20);
        put16(out, entry.flags);
        put16(out, entry.method);
        put16(out, 0);
        put16(out, 0x21);
        put32(out, entry.crc.value_or(crc32(entry.data)));
        put32(out, entry.packedSize.value_or(quint32(entry.data.size())));
        put32(out, entry.size.value_or(quint32(entry.data.size())));
        put16(out, quint16(entry.name.size()));
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, 0);
        put32(out, offsets.at(index));
        out += entry.name;
    }
    const quint32 directorySize = quint32(out.size()) - directoryOffset;
    if (options.zip64Locator) {
        put32(out, 0x07064b50);
        put32(out, 0);
        put32(out, 0);
        put32(out, 0);
        put32(out, 1);
    }
    const quint16 total = options.totalEntries.value_or(quint16(entries.size()));
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, total);
    put16(out, total);
    put32(out, directorySize);
    put32(out, directoryOffset);
    put16(out, 0);
    return out;
}

} // namespace

class TestZipStore : public QObject
{
    Q_OBJECT

public:
    explicit TestZipStore(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();

    void crcMatchesTheCheckValueOfTheStandard();
    void writtenArchiveIsReadBackUnchanged();
    void archiveWrittenByInfoZipIsRead();
    void archiveIsListedAndTestedByUnzip();
    void archiveIsTestedByPythonZipfile();
    void unsafeNamesAreRefused_data();
    void unsafeNamesAreRefused();
    void writerRefusesAnUnsafeOrRepeatedName();
    void readerRefusesARepeatedName();
    void compressedEntryIsNotSupported();
    void encryptedEntryIsNotSupported();
    void zip64IsNotSupported_data();
    void zip64IsNotSupported();
    void wrongCrcIsReportedOnRead();
    void archiveCutShortIsRefused_data();
    void archiveCutShortIsRefused();
    void archiveCutInsideAHeaderIsRefused_data();
    void archiveCutInsideAHeaderIsRefused();
    void storedEntryWithTwoSizesIsRefused();
    void moreEntriesThanAllowedAreRefused();
    void writerStopsAtTheEntryLimit();
    void entryAboveTheSizeLimitIsRefused();
    void archiveWithoutCommitIsDiscarded();

private:
    [[nodiscard]] QString writeFile(const QString &name, const QByteArray &bytes) const;
    [[nodiscard]] Result<void> openBytes(const QByteArray &bytes);

    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestZipStore::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

QString TestZipStore::writeFile(const QString &name, const QByteArray &bytes) const
{
    const QString path = m_dir->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return {};
    return path;
}

Result<void> TestZipStore::openBytes(const QByteArray &bytes)
{
    const QString path = writeFile(QStringLiteral("probe.zip"), bytes);
    if (path.isEmpty())
        return Error(ErrorCode::IoFailure, QStringLiteral("cannot write the probe"));
    ZipReader reader(path);
    return reader.open();
}

void TestZipStore::crcMatchesTheCheckValueOfTheStandard()
{
    QCOMPARE(crc32("123456789"), 0xCBF43926U);
    QCOMPARE(crc32(""), 0U);
    QCOMPARE(crc32("56789", crc32("1234")), 0xCBF43926U);
}

void TestZipStore::writtenArchiveIsReadBackUnchanged()
{
    const QString path = m_dir->filePath(QStringLiteral("out.zip"));
    QByteArray binary;
    for (int i = 0; i < 70000; ++i)
        binary.append(char(i * 7));
    ZipWriter writer(path, QDateTime(QDate(2026, 11, 12), QTime(10, 30, 8)));
    QVERIFY(writer.open().hasValue());
    QVERIFY(writer.add(QStringLiteral("manifest.json"), "{}").hasValue());
    QVERIFY(writer.add(QStringLiteral("kontakte/Grüße 😀.vcf"), "BEGIN:VCARD").hasValue());
    QVERIFY(writer.add(QStringLiteral("leer.txt"), QByteArray()).hasValue());
    QVERIFY(writer.add(QStringLiteral("a/b/binary.bin"), binary).hasValue());
    QVERIFY(writer.commit().hasValue());

    ZipReader reader(path);
    QVERIFY(reader.open().hasValue());
    QStringList names;
    for (const ZipEntryInfo &entry : reader.entries())
        names.append(entry.name);
    QCOMPARE(names,
             (QStringList{QStringLiteral("manifest.json"), QStringLiteral("kontakte/Grüße 😀.vcf"),
                          QStringLiteral("leer.txt"), QStringLiteral("a/b/binary.bin")}));
    QCOMPARE(reader.read(QStringLiteral("kontakte/Grüße 😀.vcf")).value(),
             QByteArray("BEGIN:VCARD"));
    QCOMPARE(reader.read(QStringLiteral("leer.txt")).value(), QByteArray());
    QCOMPARE(reader.read(QStringLiteral("a/b/binary.bin")).value(), binary);
    QCOMPARE(reader.read(QStringLiteral("missing")).error().code(), ErrorCode::NotFound);
}

void TestZipStore::archiveWrittenByInfoZipIsRead()
{
    const QString path = writeFile(QStringLiteral("infozip.zip"),
                                   QByteArray::fromBase64(QByteArray(infoZipFixture)));
    QVERIFY(!path.isEmpty());
    ZipReader reader(path);
    const Result<void> opened = reader.open();
    QVERIFY2(opened.hasValue(), qPrintable(opened.error().message()));
    QCOMPARE(reader.entries().size(), 2);
    QCOMPARE(reader.read(QStringLiteral("hallo.txt")).value(), QByteArray("Hallo Welt\n"));
    QCOMPARE(reader.read(QStringLiteral("sub/ä.txt")).value(), QStringLiteral("Grüße").toUtf8());
    QVERIFY(!reader.contains(QStringLiteral("sub")));
}

// Runs only where unzip is installed; it is the tool most people open an archive with.
void TestZipStore::archiveIsListedAndTestedByUnzip()
{
    const QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    if (unzip.isEmpty())
        QSKIP("unzip is not installed");
    const QString path = m_dir->filePath(QStringLiteral("for-unzip.zip"));
    ZipWriter writer(path);
    QVERIFY(writer.open().hasValue());
    QVERIFY(writer.add(QStringLiteral("manifest.json"), "{\"a\":1}").hasValue());
    QVERIFY(writer.add(QStringLiteral("notizen/Grüße.txt"), "Hallo").hasValue());
    QVERIFY(writer.commit().hasValue());

    QProcess test;
    test.start(unzip, {QStringLiteral("-t"), path});
    QVERIFY(test.waitForFinished(30000));
    QCOMPARE(test.exitCode(), 0);
    QVERIFY(test.readAllStandardOutput().contains("No errors detected"));

    const QString target = m_dir->filePath(QStringLiteral("unpacked"));
    QProcess extract;
    extract.start(unzip, {QStringLiteral("-q"), path, QStringLiteral("-d"), target});
    QVERIFY(extract.waitForFinished(30000));
    QCOMPARE(extract.exitCode(), 0);
    QFile unpacked(QDir(target).filePath(QStringLiteral("notizen/Grüße.txt")));
    QVERIFY(unpacked.open(QIODevice::ReadOnly));
    QCOMPARE(unpacked.readAll(), QByteArray("Hallo"));
}

// Runs only where Python is installed; its zipfile module checks every CRC.
void TestZipStore::archiveIsTestedByPythonZipfile()
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        QSKIP("python3 is not installed");
    const QString path = m_dir->filePath(QStringLiteral("for-python.zip"));
    ZipWriter writer(path);
    QVERIFY(writer.open().hasValue());
    QVERIFY(writer.add(QStringLiteral("a.txt"), "eins").hasValue());
    QVERIFY(writer.add(QStringLiteral("b/c.txt"), QByteArray(5000, 'x')).hasValue());
    QVERIFY(writer.commit().hasValue());

    QProcess test;
    test.start(python, {QStringLiteral("-I"), QStringLiteral("-m"), QStringLiteral("zipfile"),
                        QStringLiteral("-t"), path});
    QVERIFY(test.waitForFinished(30000));
    QCOMPARE(test.exitCode(), 0);
    QVERIFY(test.readAllStandardOutput().contains("Done testing"));
}

void TestZipStore::unsafeNamesAreRefused_data()
{
    QTest::addColumn<QString>("name");

    QTest::newRow("parent") << QStringLiteral("../x.txt");
    QTest::newRow("parent inside") << QStringLiteral("a/../../x.txt");
    QTest::newRow("absolute") << QStringLiteral("/etc/passwd");
    QTest::newRow("drive") << QStringLiteral("C:/x.txt");
    QTest::newRow("drive without slash") << QStringLiteral("C:x.txt");
    QTest::newRow("backslash") << QStringLiteral("a\\..\\x.txt");
    QTest::newRow("empty") << QString();
    QTest::newRow("empty segment") << QStringLiteral("a//b.txt");
    QTest::newRow("dot") << QStringLiteral("./a.txt");
    QTest::newRow("control") << QStringLiteral("a\nb.txt");
    QTest::newRow("next line") << QString::fromUtf8("a\u0085b.txt");
    QTest::newRow("writing direction") << QString::fromUtf8("a\u202Etxt.exe");
    QTest::newRow("zero width") << QString::fromUtf8("a\u200Bb.txt");
}

void TestZipStore::unsafeNamesAreRefused()
{
    QFETCH(QString, name);

    QVERIFY(!isSafeEntryName(name));
    const Result<void> opened = openBytes(rawArchive({{name.toUtf8(), "data"}}));
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::InvalidInput);
}

void TestZipStore::writerRefusesAnUnsafeOrRepeatedName()
{
    const QString path = m_dir->filePath(QStringLiteral("names.zip"));
    ZipWriter writer(path);
    QVERIFY(writer.open().hasValue());
    QCOMPARE(writer.add(QStringLiteral("../x"), "a").error().code(), ErrorCode::InvalidInput);
    QVERIFY(writer.add(QStringLiteral("x"), "a").hasValue());
    QCOMPARE(writer.add(QStringLiteral("x"), "b").error().code(), ErrorCode::InvalidInput);
    QVERIFY(writer.commit().hasValue());
    ZipReader reader(path);
    QVERIFY(reader.open().hasValue());
    QCOMPARE(reader.entries().size(), 1);
}

void TestZipStore::readerRefusesARepeatedName()
{
    const Result<void> opened = openBytes(rawArchive({{"a.txt", "1"}, {"a.txt", "2"}}));
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::InvalidInput);
}

void TestZipStore::compressedEntryIsNotSupported()
{
    RawEntry deflated{"a.txt", "data"};
    deflated.method = 8;
    const Result<void> opened = openBytes(rawArchive({deflated}));
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::NotSupported);
}

void TestZipStore::encryptedEntryIsNotSupported()
{
    RawEntry encrypted{"a.txt", "data"};
    encrypted.flags |= 0x0001;
    const Result<void> opened = openBytes(rawArchive({encrypted}));
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::NotSupported);
}

void TestZipStore::zip64IsNotSupported_data()
{
    QTest::addColumn<QByteArray>("archive");

    RawEntry marked{"a.txt", "data"};
    marked.size = 0xFFFFFFFF;
    marked.packedSize = 0xFFFFFFFF;
    QTest::newRow("size marker") << rawArchive({marked});
    QTest::newRow("locator") << rawArchive({{"a.txt", "data"}}, {std::nullopt, true});
    QTest::newRow("count marker") << rawArchive({{"a.txt", "data"}}, {quint16(0xFFFF), false});
}

void TestZipStore::zip64IsNotSupported()
{
    QFETCH(QByteArray, archive);

    const Result<void> opened = openBytes(archive);
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::NotSupported);
}

void TestZipStore::wrongCrcIsReportedOnRead()
{
    RawEntry broken{"a.txt", "data"};
    broken.crc = crc32("other");
    const QString path = writeFile(QStringLiteral("crc.zip"), rawArchive({broken}));
    ZipReader reader(path);
    QVERIFY(reader.open().hasValue());
    const Result<QByteArray> read = reader.read(QStringLiteral("a.txt"));
    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::InvalidInput);
}

void TestZipStore::archiveCutShortIsRefused_data()
{
    QTest::addColumn<QByteArray>("archive");

    const QByteArray whole = rawArchive({{"a.txt", QByteArray(400, 'a')}, {"b.txt", "b"}});
    QTest::newRow("end record missing") << whole.left(whole.size() - 10);
    QTest::newRow("half the file") << whole.left(whole.size() / 2);
    QTest::newRow("too short") << QByteArray("PK\x05\x06", 4);
    QTest::newRow("no archive") << QByteArray(300, 'x');

    RawEntry tooLong{"a.txt", "data"};
    tooLong.size = 4000;
    tooLong.packedSize = 4000;
    QTest::newRow("entry beyond its data") << rawArchive({tooLong});
}

void TestZipStore::archiveCutShortIsRefused()
{
    QFETCH(QByteArray, archive);

    const Result<void> opened = openBytes(archive);
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::InvalidInput);
}

// Each row ends the bytes the reader takes from the file inside a header of
// fixed size, where a missing length check would read past them.
void TestZipStore::archiveCutInsideAHeaderIsRefused_data()
{
    QTest::addColumn<QByteArray>("archive");

    QTest::newRow("empty file") << QByteArray();

    QByteArray endOnly;
    put32(endOnly, 0x06054b50);
    put16(endOnly, 0);
    put16(endOnly, 0);
    put16(endOnly, 1);
    put16(endOnly, 1);
    put32(endOnly, 0);
    put32(endOnly, 0);
    put16(endOnly, 0);
    QTest::newRow("entry promised, directory empty") << endOnly;

    const QByteArray whole = rawArchive({{"a.txt", "data"}});
    const qsizetype endAt = whole.size() - 22;
    const quint32 directoryAt = qFromLittleEndian<quint32>(whole.constData() + endAt + 16);
    QTest::newRow("central header cut short") << withField32(whole, endAt + 12, 20);
    QTest::newRow("local header cut short")
        << withField32(whole, directoryAt + 42, quint32(whole.size() - 10));
}

void TestZipStore::archiveCutInsideAHeaderIsRefused()
{
    QFETCH(QByteArray, archive);

    const Result<void> opened = openBytes(archive);
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::InvalidInput);
}

void TestZipStore::storedEntryWithTwoSizesIsRefused()
{
    RawEntry entry{"a.txt", "data"};
    entry.size = 10;
    const Result<void> opened = openBytes(rawArchive({entry}));
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::InvalidInput);
}

void TestZipStore::moreEntriesThanAllowedAreRefused()
{
    QList<RawEntry> entries;
    for (qsizetype i = 0; i <= maxZipEntries; ++i)
        entries.append({QByteArray::number(i), QByteArray()});
    const Result<void> opened = openBytes(rawArchive(entries));
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::LimitExceeded);
}

void TestZipStore::writerStopsAtTheEntryLimit()
{
    const QString path = m_dir->filePath(QStringLiteral("many.zip"));
    ZipWriter writer(path);
    QVERIFY(writer.open().hasValue());
    for (qsizetype i = 0; i < maxZipEntries; ++i)
        QVERIFY(writer.add(QString::number(i), QByteArray()).hasValue());
    QCOMPARE(writer.add(QStringLiteral("one more"), QByteArray()).error().code(),
             ErrorCode::LimitExceeded);
    QVERIFY(writer.commit().hasValue());
    ZipReader reader(path);
    QVERIFY(reader.open().hasValue());
    QCOMPARE(reader.entries().size(), maxZipEntries);
}

void TestZipStore::entryAboveTheSizeLimitIsRefused()
{
    RawEntry huge{"a.txt", QByteArray()};
    huge.size = quint32(maxZipEntryBytes + 1);
    huge.packedSize = quint32(maxZipEntryBytes + 1);
    const Result<void> opened = openBytes(rawArchive({huge}));
    QVERIFY(!opened.hasValue());
    QCOMPARE(opened.error().code(), ErrorCode::LimitExceeded);
}

void TestZipStore::archiveWithoutCommitIsDiscarded()
{
    const QString path = m_dir->filePath(QStringLiteral("dropped.zip"));
    {
        ZipWriter writer(path);
        QVERIFY(writer.open().hasValue());
        QVERIFY(writer.add(QStringLiteral("a.txt"), "data").hasValue());
    }
    QVERIFY(!QFile::exists(path));
}

QTEST_GUILESS_MAIN(TestZipStore)

#include "tst_zipstore.moc"
