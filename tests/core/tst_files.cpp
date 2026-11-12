#include "core/protocol/PhoneConnection.h"
#include "core/service/FileService.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::FileService;
using qpm::core::service::RemoteFile;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 20000;

QDateTime summer()
{
    return {QDate(2026, 7, 15), QTime(12, 30, 5), QTimeZone::UTC};
}

QByteArray pattern(qsizetype size)
{
    QByteArray data(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        data[i] = char((i * 7 + i / 251) & 0xFF);
    return data;
}

QStringList names(const QList<FolderEntry> &entries)
{
    QStringList result;
    for (const FolderEntry &entry : entries)
        result.append(entry.name);
    return result;
}

bool writeFile(const QString &path, const QByteArray &data, const QDateTime &modified)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
    file.close();
    return !modified.isValid() || (file.open(QIODevice::ReadWrite) &&
                                   file.setFileTime(modified, QFileDevice::FileModificationTime));
}

QStringList filesDirectlyIn(const QStringList &paths, const QString &folder)
{
    QStringList result;
    for (const QString &path : paths) {
        if (path.startsWith(folder + u'\\') && !path.sliced(folder.size() + 1).contains(u'\\'))
            result.append(path);
    }
    return result;
}

} // namespace

// The file service against the fake phone: listings, both directions of a
// transfer over the fast channel and inside the OBEX session, cancelling,
// folders, renaming, moving and deleting.
class TestFiles : public QObject
{
    Q_OBJECT

public:
    explicit TestFiles(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void rootListsTheStorages();
    void folderIsListedWithSizeAndTimeThroughKeepAlives();
    void treeListingHasEveryFolderBelow();
    void missingFolderIsNotFound();
    void refusedListingNamesTheMissingPermission();
    void downloadIsByteIdenticalAndKeepsItsTime();
    void downloadConnectsAgainWhenThePhoneListensLate();
    void downloadFailsWhenThePhoneNeverListens();
    void uploadIsByteIdenticalAndKeepsItsTime();
    void uploadToATakenNameGetsAFreeName();
    void uploadKeepsItsDataWhenThePhoneReadsLate_data();
    void uploadKeepsItsDataWhenThePhoneReadsLate();
    void uploadStartsAgainWhenThePhoneMissedTheChannel();
    void uploadLeavesNothingWhenThePhoneMissesTheChannelEveryTime();
    void emptyFileMakesTheRoundTrip();
    void cancelledDownloadKeepsTheOldFile();
    void cancelledUploadLeavesNothingOnThePhone();
    void folderIsCreatedRenamedMovedAndDeleted();
    void takenNameRefusesTheRename();
    void folderCannotMoveIntoItself();
    void unusableNamesAreRefusedBeforeAnythingIsSent_data();
    void unusableNamesAreRefusedBeforeAnythingIsSent();
    void withoutTheFastChannelSmallFilesGoInline();
    void withoutTheFastChannelLargeUploadsAreRefused();
    void everyCallNeedsAConnection();

private:
    [[nodiscard]] QStringList fileRequests() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<FileService> m_service;
};

void TestFiles::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->addFolder(QStringLiteral("\\sdcard\\DCIM\\Camera"));
    m_phone->addFolder(QStringLiteral("\\sdcard\\Music\\Live"));
    m_phone->addFolder(QStringLiteral("\\external"));
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\a.mp3"), pattern(1000), summer());
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\Live\\b.mp3"), pattern(2000), summer());

    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_service = std::make_unique<FileService>(m_connection.get());
    m_phone->clearLog();
}

void TestFiles::cleanup()
{
    if (m_phone)
        QCOMPARE(m_phone->protocolErrors(), QStringList());
    m_service.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
    m_phone.reset();
    m_dir.reset();
}

QStringList TestFiles::fileRequests() const
{
    QStringList requests;
    const QStringList received = m_phone->receivedCommands();
    for (const QString &command : received) {
        if (command.startsWith(QLatin1StringView("OBEX ")) &&
            !command.startsWith(QLatin1StringView("OBEX CONNECT")) &&
            !command.startsWith(QLatin1StringView("OBEX DISCONNECT"))) {
            requests.append(command.trimmed());
        }
    }
    return requests;
}

void TestFiles::rootListsTheStorages()
{
    const QFuture<Result<QList<FolderEntry>>> listed = m_service->list(QString());
    QVERIFY(waitFor(listed, timeoutMs));

    QVERIFY(listed.result().hasValue());
    QCOMPARE(names(listed.result().value()),
             (QStringList{QStringLiteral("external"), QStringLiteral("sdcard")}));
    QVERIFY(listed.result().value().constFirst().isFolder);
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("OBEX CONNECT FILEBROWSER")));
    QCOMPARE(fileRequests(), (QStringList{QStringLiteral("OBEX SETPATH"),
                                          QStringLiteral("OBEX GET x-obex/folder-listing")}));
}

void TestFiles::folderIsListedWithSizeAndTimeThroughKeepAlives()
{
    m_phone->config().paddingBeforeReply = 2;

    const QFuture<Result<QList<FolderEntry>>> listed =
        m_service->list(QStringLiteral("\\sdcard\\Music"));
    QVERIFY(waitFor(listed, timeoutMs));

    QVERIFY(listed.result().hasValue());
    const QList<FolderEntry> entries = listed.result().value();
    QCOMPARE(names(entries), (QStringList{QStringLiteral("Live"), QStringLiteral("a.mp3")}));
    QVERIFY(entries.at(0).isFolder);
    QCOMPARE(entries.at(1).size, std::optional<quint64>(1000));
    QCOMPARE(entries.at(1).modified, summer());
    // The root first, then the folder by its full name, never created.
    QCOMPARE(fileRequests(), (QStringList{QStringLiteral("OBEX SETPATH"),
                                          QStringLiteral("OBEX SETPATH \\sdcard\\Music"),
                                          QStringLiteral("OBEX GET x-obex/folder-listing")}));
}

void TestFiles::treeListingHasEveryFolderBelow()
{
    const QFuture<Result<FolderListings>> listed =
        m_service->listTree(QStringLiteral("\\sdcard\\Music"));
    QVERIFY(waitFor(listed, timeoutMs));

    QVERIFY(listed.result().hasValue());
    const QList<FolderListing> folders = listed.result().value().folders;
    QCOMPARE(folders.size(), 2);
    QCOMPARE(folders.at(0).parentPath, QStringLiteral("\\sdcard\\Music\\"));
    QCOMPARE(folders.at(1).parentPath, QStringLiteral("\\sdcard\\Music\\Live\\"));
    QCOMPARE(names(folders.at(1).entries), QStringList{QStringLiteral("b.mp3")});
}

void TestFiles::missingFolderIsNotFound()
{
    const QFuture<Result<QList<FolderEntry>>> listed =
        m_service->list(QStringLiteral("\\sdcard\\Missing"));
    QVERIFY(waitFor(listed, timeoutMs));

    QCOMPARE(listed.result().error().code(), ErrorCode::NotFound);
    QVERIFY(!m_phone->hasFolder(QStringLiteral("\\sdcard\\Missing")));
}

void TestFiles::refusedListingNamesTheMissingPermission()
{
    m_phone->setFailNext(0xC1);

    const QFuture<Result<QList<FolderEntry>>> listed = m_service->list(QString());
    QVERIFY(waitFor(listed, timeoutMs));

    QCOMPARE(listed.result().error().code(), ErrorCode::PermissionDenied);
}

void TestFiles::downloadIsByteIdenticalAndKeepsItsTime()
{
    const QByteArray data = pattern(3 * 1024 * 1024 + 17);
    m_phone->addFile(QStringLiteral("\\sdcard\\DCIM\\Camera\\Foto vom See.jpg"), data, summer());
    QSignalSpy progress(m_service.get(), &FileService::transferProgress);
    const QString target = m_dir->filePath(QStringLiteral("Foto vom See.jpg"));

    const QFuture<Result<void>> downloaded = m_service->download(
        {QStringLiteral("\\sdcard\\DCIM\\Camera\\Foto vom See.jpg"), quint64(data.size()), summer()},
        target);
    QVERIFY(waitFor(downloaded, timeoutMs));

    QVERIFY(downloaded.result().hasValue());
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), data);
    QCOMPARE(QFileInfo(target).lastModified().toUTC(), summer());
    QVERIFY(progress.size() >= 2);
    QCOMPARE(progress.constLast().at(0).toULongLong(), quint64(data.size()));
    QCOMPARE(progress.constLast().at(1).toULongLong(), quint64(data.size()));
    // The phone takes the name relative to the folder it was sent to.
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX SOCKETGET Foto vom See.jpg")));
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX SOCKETSTATUS")));
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files),
             QStringList{QStringLiteral("Foto vom See.jpg")});
}

// Over adb forwarding a connection made before the phone listened was closed
// at once, and the download failed although the phone was about to send.
void TestFiles::downloadConnectsAgainWhenThePhoneListensLate()
{
    m_phone->config().droppedDownloadConnections = 2;
    const QByteArray data = pattern(200000);
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\spät.mp3"), data, summer());
    const QString target = m_dir->filePath(QStringLiteral("spät.mp3"));

    const QFuture<Result<void>> downloaded = m_service->download(
        {QStringLiteral("\\sdcard\\Music\\spät.mp3"), quint64(data.size()), summer()}, target);
    QVERIFY(waitFor(downloaded, timeoutMs));

    QVERIFY(downloaded.result().hasValue());
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), data);
    QCOMPARE(fileRequests().count(QStringLiteral("OBEX SOCKETGET spät.mp3")), 1);
}

void TestFiles::downloadFailsWhenThePhoneNeverListens()
{
    m_phone->config().droppedDownloadConnections = FileService::sideChannelConnectTries;
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\nie.mp3"), pattern(1000), summer());
    const QString target = m_dir->filePath(QStringLiteral("nie.mp3"));

    const QFuture<Result<void>> downloaded =
        m_service->download({QStringLiteral("\\sdcard\\Music\\nie.mp3"), 1000, summer()}, target);
    QVERIFY(waitFor(downloaded, timeoutMs));

    QCOMPARE(downloaded.result().error().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files | QDir::Hidden), QStringList());
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX ABORT")));
}

void TestFiles::uploadIsByteIdenticalAndKeepsItsTime()
{
    const QByteArray data = pattern(2 * 1024 * 1024 + 3);
    const QString source = m_dir->filePath(QStringLiteral("Übersicht.pdf"));
    QVERIFY(writeFile(source, data, summer()));
    QSignalSpy progress(m_service.get(), &FileService::transferProgress);

    const QFuture<Result<QString>> uploaded = m_service->upload(
        source, QStringLiteral("\\sdcard\\DCIM"), QStringLiteral("Übersicht.pdf"));
    QVERIFY(waitFor(uploaded, timeoutMs));

    QVERIFY(uploaded.result().hasValue());
    QCOMPARE(uploaded.result().value(), QStringLiteral("Übersicht.pdf"));
    const std::optional<qpm::test::FakeFile> stored =
        m_phone->file(QStringLiteral("\\sdcard\\DCIM\\Übersicht.pdf"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->data, data);
    QCOMPARE(stored->modified, summer());
    QCOMPARE(progress.constLast().at(0).toULongLong(), quint64(data.size()));
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX SOCKETPUT Übersicht.pdf")));
}

void TestFiles::uploadToATakenNameGetsAFreeName()
{
    const QString source = m_dir->filePath(QStringLiteral("a.mp3"));
    QVERIFY(writeFile(source, "new", summer()));

    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard\\Music"), QStringLiteral("a.mp3"));
    QVERIFY(waitFor(uploaded, timeoutMs));

    QVERIFY(uploaded.result().hasValue());
    QCOMPARE(uploaded.result().value(), QStringLiteral("a (1).mp3"));
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\Music\\a.mp3"))->data, pattern(1000));
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\Music\\a (1).mp3"))->data,
             QByteArrayLiteral("new"));
}

void TestFiles::uploadKeepsItsDataWhenThePhoneReadsLate_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("one line") << QByteArrayLiteral("QPhoneManager test file\n");
    QTest::newRow("several chunks") << pattern(300000);
    QTest::newRow("empty") << QByteArray();
}

// Closing the fast channel right behind the data lost the data over adb
// forwarding. The phone then kept an empty file, and the upload still
// reported success.
void TestFiles::uploadKeepsItsDataWhenThePhoneReadsLate()
{
    QFETCH(QByteArray, data);
    m_phone->config().uploadReadDelay = std::chrono::milliseconds(300);
    const QString source = m_dir->filePath(QStringLiteral("probe.txt"));
    QVERIFY(writeFile(source, data, summer()));

    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard"), QStringLiteral("probe.txt"));
    QVERIFY(waitFor(uploaded, timeoutMs));

    QVERIFY(uploaded.result().hasValue());
    const std::optional<qpm::test::FakeFile> stored =
        m_phone->file(QStringLiteral("\\sdcard\\probe.txt"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->data, data);
    QCOMPARE(stored->modified, summer());
}

// A channel the phone did not listen on yet looked like a finished upload
// over adb forwarding, and the phone kept an empty file under the name.
void TestFiles::uploadStartsAgainWhenThePhoneMissedTheChannel()
{
    m_phone->config().missedUploadChannels = 2;
    const QByteArray data = QByteArrayLiteral("QPhoneManager test file\n");
    const QString source = m_dir->filePath(QStringLiteral("probe.jpg"));
    QVERIFY(writeFile(source, data, summer()));

    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard\\DCIM"), QStringLiteral("probe.jpg"));
    QVERIFY(waitFor(uploaded, timeoutMs));

    QVERIFY(uploaded.result().hasValue());
    QCOMPARE(uploaded.result().value(), QStringLiteral("probe.jpg"));
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\DCIM\\probe.jpg"))->data, data);
    const QStringList inFolder =
        filesDirectlyIn(m_phone->filePaths(), QStringLiteral("\\sdcard\\DCIM"));
    QCOMPARE(inFolder, QStringList{QStringLiteral("\\sdcard\\DCIM\\probe.jpg")});
    QCOMPARE(fileRequests().count(QStringLiteral("OBEX SOCKETPUT probe.jpg")), 3);
}

void TestFiles::uploadLeavesNothingWhenThePhoneMissesTheChannelEveryTime()
{
    m_phone->config().missedUploadChannels = 3;
    const QString source = m_dir->filePath(QStringLiteral("probe.jpg"));
    QVERIFY(writeFile(source, "QPhoneManager test file\n", summer()));

    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard\\DCIM"), QStringLiteral("probe.jpg"));
    QVERIFY(waitFor(uploaded, timeoutMs));

    QVERIFY(!uploaded.result().hasValue());
    QCOMPARE(fileRequests().count(QStringLiteral("OBEX SOCKETPUT probe.jpg")), 3);
    const QStringList inFolder =
        filesDirectlyIn(m_phone->filePaths(), QStringLiteral("\\sdcard\\DCIM"));
    QCOMPARE(inFolder, QStringList());
}

// An empty PUT body would delete the file on the phone, so an empty file
// goes over the fast channel like any other.
void TestFiles::emptyFileMakesTheRoundTrip()
{
    const QString source = m_dir->filePath(QStringLiteral("leer.txt"));
    QVERIFY(writeFile(source, QByteArray(), summer()));

    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard"), QStringLiteral("leer.txt"));
    QVERIFY(waitFor(uploaded, timeoutMs));
    QVERIFY(uploaded.result().hasValue());
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\leer.txt"))->data, QByteArray());
    QVERIFY(!fileRequests().contains(QStringLiteral("OBEX PUT leer.txt")));

    const QString back = m_dir->filePath(QStringLiteral("zurück.txt"));
    const QFuture<Result<void>> downloaded =
        m_service->download({QStringLiteral("\\sdcard\\leer.txt"), 0, summer()}, back);
    QVERIFY(waitFor(downloaded, timeoutMs));
    QVERIFY(downloaded.result().hasValue());
    QVERIFY(QFileInfo::exists(back));
    QCOMPARE(QFileInfo(back).size(), 0);
}

void TestFiles::cancelledDownloadKeepsTheOldFile()
{
    const QByteArray data = pattern(40 * 1024 * 1024);
    m_phone->addFile(QStringLiteral("\\sdcard\\video.mp4"), data, summer());
    const QString target = m_dir->filePath(QStringLiteral("video.mp4"));
    QVERIFY(writeFile(target, "old", QDateTime()));
    connect(m_service.get(), &FileService::transferProgress, this, [this](quint64 done) {
        if (done > 0)
            m_service->cancelTransfer();
    });

    const QFuture<Result<void>> downloaded = m_service->download(
        {QStringLiteral("\\sdcard\\video.mp4"), quint64(data.size()), summer()}, target);
    QVERIFY(waitFor(downloaded, timeoutMs));

    QCOMPARE(downloaded.result().error().code(), ErrorCode::Cancelled);
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("old"));
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files | QDir::Hidden),
             QStringList{QStringLiteral("video.mp4")});
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX ABORT")));

    // The session is usable again afterwards.
    const QFuture<Result<QList<FolderEntry>>> listed = m_service->list(QStringLiteral("\\sdcard"));
    QVERIFY(waitFor(listed, timeoutMs));
    QVERIFY(listed.result().hasValue());
}

void TestFiles::cancelledUploadLeavesNothingOnThePhone()
{
    const QString source = m_dir->filePath(QStringLiteral("gross.bin"));
    QVERIFY(writeFile(source, pattern(40 * 1024 * 1024), summer()));
    connect(m_service.get(), &FileService::transferProgress, this, [this](quint64 done) {
        if (done > 0)
            m_service->cancelTransfer();
    });

    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard"), QStringLiteral("gross.bin"));
    QVERIFY(waitFor(uploaded, timeoutMs));

    QCOMPARE(uploaded.result().error().code(), ErrorCode::Cancelled);
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\gross.bin")).has_value());
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX ABORT")));
}

void TestFiles::folderIsCreatedRenamedMovedAndDeleted()
{
    const QFuture<Result<void>> created =
        m_service->makeFolder(QStringLiteral("\\sdcard"), QStringLiteral("Neuer Ordner"));
    QVERIFY(waitFor(created, timeoutMs));
    QVERIFY(created.result().hasValue());
    QVERIFY(m_phone->hasFolder(QStringLiteral("\\sdcard\\Neuer Ordner")));

    const QFuture<Result<void>> renamed =
        m_service->rename(QStringLiteral("\\sdcard\\Neuer Ordner"), QStringLiteral("Urlaub"));
    QVERIFY(waitFor(renamed, timeoutMs));
    QVERIFY(renamed.result().hasValue());
    QVERIFY(m_phone->hasFolder(QStringLiteral("\\sdcard\\Urlaub")));
    QVERIFY(fileRequests().contains(
        QStringLiteral("OBEX MOVE \\sdcard\\Neuer Ordner -> \\sdcard\\Urlaub")));

    const QFuture<Result<void>> moved = m_service->move(QStringLiteral("\\sdcard\\Music\\a.mp3"),
                                                        QStringLiteral("\\sdcard\\Urlaub"));
    QVERIFY(waitFor(moved, timeoutMs));
    QVERIFY(moved.result().hasValue());
    QVERIFY(m_phone->file(QStringLiteral("\\sdcard\\Urlaub\\a.mp3")).has_value());
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\Music\\a.mp3")).has_value());

    const QFuture<Result<void>> removedFile =
        m_service->remove(QStringLiteral("\\sdcard\\Urlaub\\a.mp3"));
    QVERIFY(waitFor(removedFile, timeoutMs));
    QVERIFY(removedFile.result().hasValue());
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\Urlaub\\a.mp3")).has_value());
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX PUT a.mp3")));

    const QFuture<Result<void>> removedFolder =
        m_service->remove(QStringLiteral("\\sdcard\\Music"));
    QVERIFY(waitFor(removedFolder, timeoutMs));
    QVERIFY(removedFolder.result().hasValue());
    QVERIFY(!m_phone->hasFolder(QStringLiteral("\\sdcard\\Music")));
    QVERIFY(!m_phone->hasFolder(QStringLiteral("\\sdcard\\Music\\Live")));
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\Music\\Live\\b.mp3")).has_value());
}

void TestFiles::takenNameRefusesTheRename()
{
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\c.mp3"), "c", summer());

    const QFuture<Result<void>> renamed =
        m_service->rename(QStringLiteral("\\sdcard\\Music\\a.mp3"), QStringLiteral("c.mp3"));
    QVERIFY(waitFor(renamed, timeoutMs));

    QCOMPARE(renamed.result().error().code(), ErrorCode::Rejected);
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\Music\\c.mp3"))->data, QByteArrayLiteral("c"));
}

void TestFiles::folderCannotMoveIntoItself()
{
    const QFuture<Result<void>> moved =
        m_service->move(QStringLiteral("\\sdcard\\Music"), QStringLiteral("\\sdcard\\Music\\Live"));
    QVERIFY(waitFor(moved, timeoutMs));

    QCOMPARE(moved.result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(fileRequests(), QStringList());
}

void TestFiles::unusableNamesAreRefusedBeforeAnythingIsSent_data()
{
    QTest::addColumn<QString>("name");

    QTest::newRow("empty") << QString();
    QTest::newRow("dot dot") << QStringLiteral("..");
    QTest::newRow("slash") << QStringLiteral("a/b");
    QTest::newRow("backslash") << QStringLiteral("a\\b");
    QTest::newRow("control character") << QStringLiteral("a\u0007b");
}

void TestFiles::unusableNamesAreRefusedBeforeAnythingIsSent()
{
    QFETCH(QString, name);

    const QFuture<Result<void>> renamed =
        m_service->rename(QStringLiteral("\\sdcard\\Music\\a.mp3"), name);
    const QFuture<Result<void>> created = m_service->makeFolder(QStringLiteral("\\sdcard"), name);
    const QString source = m_dir->filePath(QStringLiteral("x.txt"));
    QVERIFY(writeFile(source, "x", QDateTime()));
    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard"), name);

    QCOMPARE(renamed.result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(created.result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(uploaded.result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(fileRequests(), QStringList());
}

void TestFiles::withoutTheFastChannelSmallFilesGoInline()
{
    m_service->setEndpointResolver([](quint16) { return std::nullopt; });
    const QByteArray data = pattern(200000);
    m_phone->addFile(QStringLiteral("\\sdcard\\Music\\long.mp3"), data, summer());
    const QString target = m_dir->filePath(QStringLiteral("long.mp3"));

    const QFuture<Result<void>> downloaded = m_service->download(
        {QStringLiteral("\\sdcard\\Music\\long.mp3"), quint64(data.size()), summer()}, target);
    QVERIFY(waitFor(downloaded, timeoutMs));
    QVERIFY(downloaded.result().hasValue());
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), data);
    QCOMPARE(QFileInfo(target).lastModified().toUTC(), summer());
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX GET long.mp3")));

    const QString source = m_dir->filePath(QStringLiteral("notiz.txt"));
    QVERIFY(writeFile(source, "Grüße", summer()));
    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard"), QStringLiteral("notiz.txt"));
    QVERIFY(waitFor(uploaded, timeoutMs));
    QVERIFY(uploaded.result().hasValue());
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\notiz.txt"))->data, QByteArray("Grüße"));
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\notiz.txt"))->modified, summer());
    QVERIFY(fileRequests().contains(QStringLiteral("OBEX PUT notiz.txt")));
}

void TestFiles::withoutTheFastChannelLargeUploadsAreRefused()
{
    m_service->setEndpointResolver([](quint16) { return std::nullopt; });
    const QString source = m_dir->filePath(QStringLiteral("gross.bin"));
    QVERIFY(writeFile(source, pattern(100000), summer()));

    const QFuture<Result<QString>> uploaded =
        m_service->upload(source, QStringLiteral("\\sdcard"), QStringLiteral("gross.bin"));
    QVERIFY(waitFor(uploaded, timeoutMs));

    QCOMPARE(uploaded.result().error().code(), ErrorCode::NotSupported);
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\gross.bin")).has_value());
}

void TestFiles::everyCallNeedsAConnection()
{
    m_connection->disconnectFromPhone();
    QTRY_COMPARE(m_connection->state(), PhoneConnection::State::Disconnected);

    QCOMPARE(m_service->list(QString()).result().error().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(m_service->remove(QStringLiteral("\\sdcard\\x")).result().error().code(),
             ErrorCode::ConnectionClosed);
    QCOMPARE(m_service
                 ->download({QStringLiteral("\\sdcard\\x"), 1, summer()},
                            m_dir->filePath(QStringLiteral("x")))
                 .result()
                 .error()
                 .code(),
             ErrorCode::ConnectionClosed);
}

QTEST_MAIN(TestFiles)

#include "tst_files.moc"
