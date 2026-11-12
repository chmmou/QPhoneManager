#include "core/protocol/PhoneConnection.h"
#include "core/service/FileService.h"
#include "ui/files/TransferQueue.h"

#include "FakePhone.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::FileService;
using qpm::test::FakePhone;
using qpm::ui::TransferQueue;

namespace {

constexpr int timeoutMs = 20000;

QDateTime spring()
{
    return {QDate(2026, 4, 1), QTime(9, 15, 30), QTimeZone::UTC};
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

FolderEntry fileEntry(const QString &name, qsizetype size)
{
    FolderEntry entry;
    entry.name = name;
    entry.size = quint64(size);
    entry.modified = spring();
    return entry;
}

FolderEntry folderEntry(const QString &name)
{
    FolderEntry entry;
    entry.name = name;
    entry.isFolder = true;
    return entry;
}

QList<TransferQueue::State> states(const TransferQueue &queue)
{
    QList<TransferQueue::State> result;
    for (int row = 0; row < queue.rowCount(); ++row) {
        result.append(
            queue.data(queue.index(row), TransferQueue::StateRole).value<TransferQueue::State>());
    }
    return result;
}

} // namespace

// The transfer queue against the fake phone: where downloads may write,
// whole folders in both directions, the questions at a name that exists,
// cancelling, failures and the temporary copies to open or drag.
class TestTransferQueue : public QObject
{
    Q_OBJECT

public:
    explicit TestTransferQueue(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void targetPathStaysInTheChosenFolder_data();
    void targetPathStaysInTheChosenFolder();
    void symbolicLinksCannotLeadOutOfTheChosenFolder();
    void freeNameCountsUp();
    void filesLandInTheChosenFolderWithTheirTime();
    void folderKeepsItsTreeAndEmptyFolders();
    void unusableNameFromThePhoneFailsWithoutWriting();
    void existingLocalFileAsksAndReplaceOverwrites();
    void keepBothAddsANumberedFile();
    void skipForAllAnswersTheRestOfTheBatch();
    void takenPhoneNameAsksAndReplaceWritesOverIt();
    void keepBothOnThePhoneLetsThePhoneChooseTheName();
    void localFolderIsCopiedWithoutItsSymbolicLinks();
    void cancelStopsTheRunningAndTheWaitingJobs();
    void failureIsReportedOnceAtTheEndOfTheBatch();
    void lostConnectionFailsWhatWaits();
    void fileOpensOnTheDesktopFromATemporaryCopy();
    void draggedFilesAreFetchedFirst();

private:
    [[nodiscard]] bool waitIdle() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<FileService> m_service;
    std::unique_ptr<TransferQueue> m_queue;
};

void TestTransferQueue::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
}

void TestTransferQueue::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\a.mp3"), "aaa", spring());
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\Live\\b.mp3"), "bbbb", spring());
    m_phone->addFolder(QStringLiteral("\\sdcard\\Music\\Leer"));
    m_phone->addFile(QStringLiteral("\\sdcard\\Download\\Brief.pdf"), "letter", spring());
    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_service = std::make_unique<FileService>(m_connection.get());
    m_queue = std::make_unique<TransferQueue>(m_service.get());
    m_queue->setConnected(true);
    m_queue->setDesktopOpener([](const QUrl &) { return false; });
}

void TestTransferQueue::cleanup()
{
    m_queue.reset();
    m_service.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
    m_phone.reset();
    m_dir.reset();
}

bool TestTransferQueue::waitIdle() const
{
    return QTest::qWaitFor([this]() { return !m_queue->isActive(); }, timeoutMs);
}

void TestTransferQueue::targetPathStaysInTheChosenFolder_data()
{
    QTest::addColumn<QStringList>("subfolders");
    QTest::addColumn<QString>("name");

    QTest::newRow("dot dot") << QStringList() << QStringLiteral("..");
    QTest::newRow("dot") << QStringList() << QStringLiteral(".");
    QTest::newRow("climbing name") << QStringList() << QStringLiteral("../evil.sh");
    QTest::newRow("absolute name") << QStringList() << QStringLiteral("/etc/passwd");
    QTest::newRow("backslash") << QStringList() << QStringLiteral("..\\evil");
    QTest::newRow("control character") << QStringList() << QStringLiteral("a\u0001b");
    QTest::newRow("empty") << QStringList() << QString();
    QTest::newRow("climbing folder") << QStringList{QStringLiteral("..")} << QStringLiteral("x");
    QTest::newRow("absolute folder") << QStringList{QStringLiteral("/tmp")} << QStringLiteral("x");
    QTest::newRow("folder with line feed")
        << QStringList{QStringLiteral("a\nb")} << QStringLiteral("x");
}

void TestTransferQueue::targetPathStaysInTheChosenFolder()
{
    QFETCH(QStringList, subfolders);
    QFETCH(QString, name);
    const QString base = m_dir->filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(base));

    const Result<QString> path = TransferQueue::targetPath(base, subfolders, name);

    QVERIFY(!path.hasValue());
    QCOMPARE(path.error().code(), ErrorCode::InvalidInput);
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("target")});
    QCOMPARE(QDir(base).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList());
}

void TestTransferQueue::symbolicLinksCannotLeadOutOfTheChosenFolder()
{
    const QString base = m_dir->filePath(QStringLiteral("target"));
    const QString outside = m_dir->filePath(QStringLiteral("outside"));
    QVERIFY(QDir().mkpath(base));
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(QFile::link(outside, QDir(base).filePath(QStringLiteral("folder-link"))));
    QVERIFY(writeFile(QDir(outside).filePath(QStringLiteral("victim.txt")), "keep"));
    QVERIFY(QFile::link(QDir(outside).filePath(QStringLiteral("victim.txt")),
                        QDir(base).filePath(QStringLiteral("file-link"))));

    const Result<QString> throughFolder =
        TransferQueue::targetPath(base, {QStringLiteral("folder-link")}, QStringLiteral("x"));
    const Result<QString> overFile =
        TransferQueue::targetPath(base, {}, QStringLiteral("file-link"));
    const Result<QString> fine = TransferQueue::targetPath(
        base, {QStringLiteral("Neu"), QStringLiteral("Ünter")}, QStringLiteral("x.txt"));

    QCOMPARE(throughFolder.error().code(), ErrorCode::InvalidInput);
    QCOMPARE(overFile.error().code(), ErrorCode::InvalidInput);
    QVERIFY(fine.hasValue());
    QCOMPARE(fine.value(),
             QDir(QFileInfo(base).canonicalFilePath()).filePath(QStringLiteral("Neu/Ünter/x.txt")));
    QCOMPARE(QDir(outside).entryList(QDir::Files), QStringList{QStringLiteral("victim.txt")});
    QCOMPARE(readAll(QDir(outside).filePath(QStringLiteral("victim.txt"))), QByteArray("keep"));
}

void TestTransferQueue::freeNameCountsUp()
{
    const QStringList taken = {QStringLiteral("a (1).txt"), QStringLiteral("a (2).txt")};
    const auto isTaken = [&taken](const QString &name) { return taken.contains(name); };

    QCOMPARE(TransferQueue::freeName(QStringLiteral("a.txt"), isTaken),
             QStringLiteral("a (3).txt"));
    QCOMPARE(TransferQueue::freeName(QStringLiteral("README"), isTaken),
             QStringLiteral("README (1)"));
    QCOMPARE(TransferQueue::freeName(QStringLiteral(".hidden"), isTaken),
             QStringLiteral(".hidden (1)"));
}

void TestTransferQueue::filesLandInTheChosenFolderWithTheirTime()
{
    QSignalSpy errors(m_queue.get(), &TransferQueue::errorOccurred);

    m_queue->download(QStringLiteral("\\sdcard\\Music"), {fileEntry(QStringLiteral("a.mp3"), 3)},
                      m_dir->path());
    QVERIFY(m_queue->isActive());
    QVERIFY(waitIdle());

    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("a.mp3"))), QByteArray("aaa"));
    QCOMPARE(QFileInfo(m_dir->filePath(QStringLiteral("a.mp3"))).lastModified().toUTC(), spring());
    QCOMPARE(states(*m_queue), QList<TransferQueue::State>{TransferQueue::State::Done});
    QCOMPARE(m_queue->data(m_queue->index(0), TransferQueue::ProgressRole).toDouble(), 1.0);
    QCOMPARE(m_queue->statusText(), QString());
    QCOMPARE(errors.count(), 0);
}

void TestTransferQueue::folderKeepsItsTreeAndEmptyFolders()
{
    m_queue->download(QStringLiteral("\\sdcard"), {folderEntry(QStringLiteral("Music"))},
                      m_dir->path());
    QVERIFY(waitIdle());

    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("Music/a.mp3"))), QByteArray("aaa"));
    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("Music/Live/b.mp3"))), QByteArray("bbbb"));
    QVERIFY(QFileInfo(m_dir->filePath(QStringLiteral("Music/Leer"))).isDir());
    QCOMPARE(states(*m_queue),
             (QList<TransferQueue::State>{TransferQueue::State::Done, TransferQueue::State::Done,
                                          TransferQueue::State::Done}));
}

// Names in a listing are checked by its reader already; the queue checks
// again, so that no other way in can write outside the chosen folder.
void TestTransferQueue::unusableNameFromThePhoneFailsWithoutWriting()
{
    const QString base = m_dir->filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(base));
    QSignalSpy errors(m_queue.get(), &TransferQueue::errorOccurred);

    m_queue->download(QStringLiteral("\\sdcard\\Music"), {fileEntry(QStringLiteral("../a.mp3"), 3)},
                      base);
    QVERIFY(waitIdle());

    QCOMPARE(states(*m_queue), QList<TransferQueue::State>{TransferQueue::State::Failed});
    QCOMPARE(errors.count(), 1);
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("target")});
    QCOMPARE(QDir(base).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList());
}

void TestTransferQueue::existingLocalFileAsksAndReplaceOverwrites()
{
    QVERIFY(writeFile(m_dir->filePath(QStringLiteral("a.mp3")), "old"));
    QSignalSpy conflict(m_queue.get(), &TransferQueue::conflictChanged);

    m_queue->download(QStringLiteral("\\sdcard\\Music"), {fileEntry(QStringLiteral("a.mp3"), 3)},
                      m_dir->path());
    QTRY_VERIFY_WITH_TIMEOUT(m_queue->conflictId() != 0, timeoutMs);
    QCOMPARE(m_queue->conflictName(), QStringLiteral("a.mp3"));
    QVERIFY(!m_queue->conflictOnPhone());
    QCOMPARE(states(*m_queue),
             QList<TransferQueue::State>{TransferQueue::State::WaitingForDecision});
    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("a.mp3"))), QByteArray("old"));

    m_queue->resolveConflict(m_queue->conflictId(), TransferQueue::Choice::Replace, false);
    QVERIFY(waitIdle());

    QCOMPARE(m_queue->conflictId(), 0);
    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("a.mp3"))), QByteArray("aaa"));
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files), QStringList{QStringLiteral("a.mp3")});
}

void TestTransferQueue::keepBothAddsANumberedFile()
{
    QVERIFY(writeFile(m_dir->filePath(QStringLiteral("a.mp3")), "old"));

    m_queue->download(QStringLiteral("\\sdcard\\Music"), {fileEntry(QStringLiteral("a.mp3"), 3)},
                      m_dir->path());
    QTRY_VERIFY_WITH_TIMEOUT(m_queue->conflictId() != 0, timeoutMs);
    m_queue->resolveConflict(m_queue->conflictId(), TransferQueue::Choice::KeepBoth, false);
    QVERIFY(waitIdle());

    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("a.mp3"))), QByteArray("old"));
    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("a (1).mp3"))), QByteArray("aaa"));
}

void TestTransferQueue::skipForAllAnswersTheRestOfTheBatch()
{
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\c.mp3"), "ccc", spring());
    QVERIFY(writeFile(m_dir->filePath(QStringLiteral("a.mp3")), "old a"));
    QVERIFY(writeFile(m_dir->filePath(QStringLiteral("c.mp3")), "old c"));
    QSignalSpy conflict(m_queue.get(), &TransferQueue::conflictChanged);

    m_queue->download(
        QStringLiteral("\\sdcard\\Music"),
        {fileEntry(QStringLiteral("a.mp3"), 3), fileEntry(QStringLiteral("c.mp3"), 3)},
        m_dir->path());
    QTRY_VERIFY_WITH_TIMEOUT(m_queue->conflictId() != 0, timeoutMs);
    m_queue->resolveConflict(m_queue->conflictId(), TransferQueue::Choice::Skip, true);
    QVERIFY(waitIdle());

    QCOMPARE(states(*m_queue), (QList<TransferQueue::State>{TransferQueue::State::Skipped,
                                                            TransferQueue::State::Skipped}));
    // Asked once, answered once.
    QCOMPARE(conflict.count(), 2);
    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("a.mp3"))), QByteArray("old a"));
    QCOMPARE(readAll(m_dir->filePath(QStringLiteral("c.mp3"))), QByteArray("old c"));

    // A new batch asks again.
    m_queue->download(QStringLiteral("\\sdcard\\Music"), {fileEntry(QStringLiteral("a.mp3"), 3)},
                      m_dir->path());
    QTRY_VERIFY_WITH_TIMEOUT(m_queue->conflictId() != 0, timeoutMs);
    m_queue->cancel(m_queue->conflictId());
    QVERIFY(waitIdle());
}

// The shared storage of a phone does not tell upper from lower case, so a
// name that differs only in case is taken as well.
void TestTransferQueue::takenPhoneNameAsksAndReplaceWritesOverIt()
{
    const QString source = m_dir->filePath(QStringLiteral("brief.PDF"));
    QVERIFY(writeFile(source, "new letter"));

    m_queue->upload({source}, QStringLiteral("\\sdcard\\Download"), {QStringLiteral("Brief.pdf")});
    QTRY_VERIFY_WITH_TIMEOUT(m_queue->conflictId() != 0, timeoutMs);
    QVERIFY(m_queue->conflictOnPhone());
    QCOMPARE(m_queue->conflictName(), QStringLiteral("brief.PDF"));
    m_queue->resolveConflict(m_queue->conflictId(), TransferQueue::Choice::Replace, false);
    QVERIFY(waitIdle());

    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\Download\\Brief.pdf")).has_value());
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\Download\\brief.PDF"))->data,
             QByteArray("new letter"));
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\Download\\brief (1).PDF")).has_value());
}

void TestTransferQueue::keepBothOnThePhoneLetsThePhoneChooseTheName()
{
    const QString source = m_dir->filePath(QStringLiteral("Brief.pdf"));
    QVERIFY(writeFile(source, "new letter"));
    QSignalSpy changed(m_queue.get(), &TransferQueue::phoneFolderChanged);

    m_queue->upload({source}, QStringLiteral("\\sdcard\\Download"), {QStringLiteral("Brief.pdf")});
    QTRY_VERIFY_WITH_TIMEOUT(m_queue->conflictId() != 0, timeoutMs);
    m_queue->resolveConflict(m_queue->conflictId(), TransferQueue::Choice::KeepBoth, false);
    QVERIFY(waitIdle());

    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\Download\\Brief.pdf"))->data,
             QByteArray("letter"));
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\Download\\Brief (1).pdf"))->data,
             QByteArray("new letter"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().first().toString(), QStringLiteral("\\sdcard\\Download"));
}

void TestTransferQueue::localFolderIsCopiedWithoutItsSymbolicLinks()
{
    const QString folder = m_dir->filePath(QStringLiteral("Urlaub 2026"));
    QVERIFY(QDir().mkpath(QDir(folder).filePath(QStringLiteral("Tag 1"))));
    QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("Strand.jpg")), "beach"));
    QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("Tag 1/Berg.jpg")), "hill"));
    QVERIFY(writeFile(m_dir->filePath(QStringLiteral("secret.txt")), "secret"));
    QVERIFY(QFile::link(m_dir->filePath(QStringLiteral("secret.txt")),
                        QDir(folder).filePath(QStringLiteral("link.txt"))));

    m_queue->upload({folder}, QStringLiteral("\\sdcard"), {});
    QVERIFY(waitIdle());

    QVERIFY(m_phone->hasFolder(QStringLiteral("\\sdcard\\Urlaub 2026")));
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\Urlaub 2026\\Strand.jpg"))->data,
             QByteArray("beach"));
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\Urlaub 2026\\Tag 1\\Berg.jpg"))->data,
             QByteArray("hill"));
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\Urlaub 2026\\link.txt")).has_value());
}

void TestTransferQueue::cancelStopsTheRunningAndTheWaitingJobs()
{
    QByteArray big(30 * 1024 * 1024, 'v');
    m_phone->addFile(QStringLiteral("\\sdcard\\Movies\\film.mp4"), big, spring());
    connect(m_service.get(), &FileService::transferProgress, this, [this](quint64 done) {
        if (done > 0)
            m_queue->cancelAll();
    });

    m_queue->download(QStringLiteral("\\sdcard\\Movies"),
                      {fileEntry(QStringLiteral("film.mp4"), big.size())}, m_dir->path());
    m_queue->download(QStringLiteral("\\sdcard\\Music"), {fileEntry(QStringLiteral("a.mp3"), 3)},
                      m_dir->path());
    QVERIFY(waitIdle());

    QCOMPARE(states(*m_queue), (QList<TransferQueue::State>{TransferQueue::State::Cancelled,
                                                            TransferQueue::State::Cancelled}));
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files | QDir::Hidden), QStringList());
    QVERIFY(m_queue->hasFinished());
    m_queue->clearFinished();
    QCOMPARE(m_queue->count(), 0);
}

void TestTransferQueue::failureIsReportedOnceAtTheEndOfTheBatch()
{
    QSignalSpy errors(m_queue.get(), &TransferQueue::errorOccurred);

    m_queue->download(
        QStringLiteral("\\sdcard\\Music"),
        {fileEntry(QStringLiteral("weg.mp3"), 3), fileEntry(QStringLiteral("a.mp3"), 3)},
        m_dir->path());
    QVERIFY(waitIdle());

    QCOMPARE(states(*m_queue), (QList<TransferQueue::State>{TransferQueue::State::Failed,
                                                            TransferQueue::State::Done}));
    QCOMPARE(errors.count(), 1);
    const QString message = errors.first().first().toString();
    QVERIFY2(message.contains(QStringLiteral("weg.mp3")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("no longer on the phone")), qPrintable(message));
    QCOMPARE(m_queue->data(m_queue->index(0), TransferQueue::StateTextRole).toString(),
             TransferQueue::messageFor(Error(ErrorCode::NotFound, {})));
}

void TestTransferQueue::lostConnectionFailsWhatWaits()
{
    QVERIFY(writeFile(m_dir->filePath(QStringLiteral("a.mp3")), "old"));
    m_queue->download(
        QStringLiteral("\\sdcard\\Music"),
        {fileEntry(QStringLiteral("a.mp3"), 3), fileEntry(QStringLiteral("b.mp3"), 3)},
        m_dir->path());
    QTRY_VERIFY_WITH_TIMEOUT(m_queue->conflictId() != 0, timeoutMs);

    m_queue->setConnected(false);

    QVERIFY(!m_queue->isActive());
    QCOMPARE(m_queue->conflictId(), 0);
    QCOMPARE(states(*m_queue), (QList<TransferQueue::State>{TransferQueue::State::Failed,
                                                            TransferQueue::State::Failed}));
}

void TestTransferQueue::fileOpensOnTheDesktopFromATemporaryCopy()
{
    QUrl opened;
    m_queue->setDesktopOpener([&opened](const QUrl &url) {
        opened = url;
        return true;
    });

    FolderEntry letter = fileEntry(QStringLiteral("Brief.pdf"), 6);
    m_queue->openOnDesktop(QStringLiteral("\\sdcard\\Download"), letter);
    QVERIFY(waitIdle());

    QVERIFY(opened.isLocalFile());
    QCOMPARE(QFileInfo(opened.toLocalFile()).fileName(), QStringLiteral("Brief.pdf"));
    QCOMPARE(readAll(opened.toLocalFile()), QByteArray("letter"));
    QVERIFY(!opened.toLocalFile().startsWith(m_dir->path()));

    // Opening it again replaces the copy without a question.
    m_queue->openOnDesktop(QStringLiteral("\\sdcard\\Download"), letter);
    QVERIFY(waitIdle());
    QCOMPARE(m_queue->conflictId(), 0);
    QCOMPARE(QFileInfo(opened.toLocalFile()).fileName(), QStringLiteral("Brief.pdf"));
}

void TestTransferQueue::draggedFilesAreFetchedFirst()
{
    QSignalSpy ready(m_queue.get(), &TransferQueue::dragFilesReady);

    m_queue->fetchForDrag(QStringLiteral("\\sdcard\\Music"), {fileEntry(QStringLiteral("a.mp3"), 3),
                                                              folderEntry(QStringLiteral("Live"))});
    QVERIFY(ready.wait(timeoutMs));

    const QList<QUrl> urls = ready.first().first().value<QList<QUrl>>();
    QCOMPARE(urls.size(), 1);
    QCOMPARE(readAll(urls.constFirst().toLocalFile()), QByteArray("aaa"));
}

QTEST_MAIN(TestTransferQueue)

#include "tst_transferqueue.moc"
