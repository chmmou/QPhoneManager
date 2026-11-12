#include "CommandGuard.h"

#include "core/codec/FolderListing.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/FileService.h"
#include "core/service/ThumbnailChannel.h"

#include "WaitFor.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

using namespace qpm::core;
using namespace qpm::core::codec;
using namespace qpm::core::protocol;
using qpm::core::service::FileService;
using qpm::core::service::RemoteFile;
using qpm::core::service::Thumbnail;
using qpm::core::service::ThumbnailChannel;
using qpm::test::CommandGuard;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int stepTimeoutMs = 60000;
constexpr int largeTimeoutMs = 300000;
constexpr int connectTries = 10;
constexpr int thumbnailTries = 3;
constexpr int listingTries = 10;
constexpr qint64 largeFileBytes = 100LL * 1024 * 1024;

QString downloadFolder()
{
    return QStringLiteral("\\sdcard\\Download");
}

QString clientPackage()
{
    return QStringLiteral("com.fjsoft.myphoneexplorer.client");
}

QDateTime testTime()
{
    return {QDate(2026, 3, 4), QTime(5, 6, 8), QTimeZone::UTC};
}

// Every folder a test writes carries such a name; nothing else is touched.
bool isTestFolder(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^QPM-Test-[0-9a-f]{8}$"));
    return pattern.match(name).hasMatch();
}

QString newTestFolderName()
{
    return QStringLiteral("QPM-Test-%1")
        .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
}

QByteArray pattern(qsizetype size, int seed)
{
    QByteArray data(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        data[i] = char((i * 31 + seed + i / 4093) & 0xFF);
    return data;
}

bool writeFile(const QString &path, const QByteArray &data, const QDateTime &modified)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
    file.close();
    return file.open(QIODevice::ReadWrite) &&
           file.setFileTime(modified, QFileDevice::FileModificationTime);
}

QByteArray sha256Of(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return hash.result();
}

QByteArray testPicture()
{
    QImage image(640, 360, QImage::Format_RGB32);
    image.fill(QColor(0, 106, 106));
    for (int y = 200; y < 360; ++y) {
        for (int x = 0; x < 640; ++x)
            image.setPixelColor(x, y, QColor(90, 160, 60));
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 85);
    return bytes;
}

std::optional<FolderEntry> entryNamed(const QList<FolderEntry> &entries, const QString &name)
{
    for (const FolderEntry &entry : entries) {
        if (entry.name == name)
            return entry;
    }
    return std::nullopt;
}

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling, with the fast file channel and the thumbnail channel forwarded.
// Every write happens in a folder "QPM-Test-<8 hex digits>" below
// \sdcard\Download that the test creates and deletes again; such folders
// left over from an earlier run are removed first. On the emulator a file of
// 100 MB makes the round trip as well, and an upload is cancelled halfway. On
// any other phone, which may be someone's own, the storages are only counted
// and checked for their form, and only the test's own small files and its
// own picture go back and forth. No name or content of a file on the phone
// is printed or kept beyond the test. The client needs access to all files;
// where the phone has not granted it, the tests are skipped rather than the
// permission asked for on the phone.
class TestDeviceFiles : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceFiles(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void leftoversOfEarlierRunsAreRemoved();
    void storagesAreListedAndWellFormed();
    void smallFilesMakeTheRoundTrip();
    void ownPictureGetsAThumbnail();
    void largeFileMakesTheRoundTripOnTheEmulator();
    void cancelledUploadLeavesNothingOnTheEmulator();

private:
    [[nodiscard]] bool connectToClient();
    [[nodiscard]] bool clientMayUseAllFiles() const;
    [[nodiscard]] Result<QList<FolderEntry>> list(const QString &folder);
    /**
     * Lists `folder` until `ready` holds for the listing, a few times at
     * most; the phone's media index can show a new file with its old size
     * for a moment.
     */
    [[nodiscard]] Result<QList<FolderEntry>>
    listUntil(const QString &folder, const std::function<bool(const QList<FolderEntry> &)> &ready);
    [[nodiscard]] Result<void> makeTestFolder();
    void removeTestFolder();

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    bool m_filesAllowed = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    quint16 m_filePort = 0;
    quint16 m_thumbnailPort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<FileService> m_files;
    std::unique_ptr<ThumbnailChannel> m_thumbnails;
    QPointer<CommandGuard> m_guard;
    QString m_testFolder;
};

void TestDeviceFiles::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    qInfo() << (m_emulator ? "Target: emulator, full round trips with 100 MB"
                           : "Target: phone, own test folder only");

    m_adb = std::make_unique<AdbBridge>(m_adbPath);
    const QFuture<Result<void>> server = m_adb->startServer();
    QVERIFY(waitFor(server, readyTimeoutMs));
    QVERIFY2(server.result().hasValue(), "adb start-server failed");
    // The error text of adb can name the device, so only the code is shown.
    for (const auto &[phonePort, local] : {std::pair<quint16, quint16 *>{clientPort, &m_localPort},
                                           {FileService::fileChannelPort, &m_filePort},
                                           {ThumbnailChannel::thumbnailPort, &m_thumbnailPort}}) {
        const QFuture<Result<quint16>> forward = m_adb->forward(m_serial, phonePort);
        QVERIFY(waitFor(forward, readyTimeoutMs));
        QVERIFY2(forward.result().hasValue(),
                 qPrintable(QStringLiteral("adb forward failed with code %1")
                                .arg(int(forward.result().error().code()))));
        *local = forward.result().value();
    }
    const QFuture<Result<void>> client = m_adb->startClient(m_serial);
    QVERIFY(waitFor(client, readyTimeoutMs));
    QVERIFY2(client.result().hasValue(),
             qPrintable(QStringLiteral("Starting the client failed with code %1")
                            .arg(int(client.result().error().code()))));
    m_filesAllowed = clientMayUseAllFiles();
    if (!m_filesAllowed)
        qInfo() << "The client on this device has no access to all files";
}

void TestDeviceFiles::cleanupTestCase()
{
    if (!m_adb)
        return;
    for (const quint16 local : {m_localPort, m_filePort, m_thumbnailPort}) {
        if (local == 0)
            continue;
        const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, local);
        QVERIFY(waitFor(removed, readyTimeoutMs));
    }
}

void TestDeviceFiles::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    QVERIFY(connectToClient());
    m_files = std::make_unique<FileService>(m_connection.get());
    m_thumbnails = std::make_unique<ThumbnailChannel>(m_connection.get());
    // The guard hides the transport, so the side channels are named here.
    const auto resolver = [this](quint16 phonePort) -> std::optional<Endpoint> {
        if (phonePort == FileService::fileChannelPort)
            return Endpoint{QStringLiteral("127.0.0.1"), m_filePort};
        if (phonePort == ThumbnailChannel::thumbnailPort)
            return Endpoint{QStringLiteral("127.0.0.1"), m_thumbnailPort};
        return std::nullopt;
    };
    m_files->setEndpointResolver(resolver);
    m_thumbnails->setEndpointResolver(resolver);
}

void TestDeviceFiles::cleanup()
{
    if (!m_testFolder.isEmpty() && m_connection && m_connection->state() == State::Ready)
        removeTestFolder();
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    m_thumbnails.reset();
    m_files.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
}

bool TestDeviceFiles::connectToClient()
{
    for (int attempt = 0; attempt < connectTries; ++attempt) {
        auto guard = std::make_unique<CommandGuard>(
            std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_localPort));
        m_guard = guard.get();
        m_connection->connectTransport(std::move(guard));
        const bool settled = QTest::qWaitFor(
            [this]() {
                return m_connection->state() == State::Ready ||
                       m_connection->state() == State::Lost;
            },
            readyTimeoutMs);
        if (settled && m_connection->state() == State::Ready)
            return true;
        // Only spaces out the tries; no result depends on how long it is.
        QTest::qWait(1000);
    }
    return false;
}

// Asks the phone without changing anything; a listing would make the client
// ask the user on the phone for the permission.
bool TestDeviceFiles::clientMayUseAllFiles() const
{
    QProcess appops;
    appops.start(m_adbPath, {QStringLiteral("-s"), m_serial, QStringLiteral("shell"),
                             QStringLiteral("appops"), QStringLiteral("get"), clientPackage(),
                             QStringLiteral("MANAGE_EXTERNAL_STORAGE")});
    if (!appops.waitForFinished(readyTimeoutMs) || appops.exitCode() != 0)
        return false;
    return appops.readAllStandardOutput().contains(": allow");
}

Result<QList<FolderEntry>> TestDeviceFiles::list(const QString &folder)
{
    const QFuture<Result<QList<FolderEntry>>> listed = m_files->list(folder);
    if (!waitFor(listed, stepTimeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("listing did not finish"));
    return listed.result();
}

Result<QList<FolderEntry>>
TestDeviceFiles::listUntil(const QString &folder,
                           const std::function<bool(const QList<FolderEntry> &)> &ready)
{
    Result<QList<FolderEntry>> listed = list(folder);
    for (int attempt = 1; attempt < listingTries && listed.hasValue() && !ready(listed.value());
         ++attempt) {
        // Only spaces out the tries; no result depends on how long it is.
        QTest::qWait(500);
        listed = list(folder);
    }
    return listed;
}

Result<void> TestDeviceFiles::makeTestFolder()
{
    m_testFolder = FileService::childPath(downloadFolder(), newTestFolderName());
    const QFuture<Result<void>> made =
        m_files->makeFolder(downloadFolder(), FileService::leafName(m_testFolder));
    if (!waitFor(made, stepTimeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("creating the folder did not finish"));
    return made.result();
}

void TestDeviceFiles::removeTestFolder()
{
    const QFuture<Result<void>> removed = m_files->remove(m_testFolder);
    QVERIFY(waitFor(removed, stepTimeoutMs));
    QVERIFY2(removed.result().hasValue(), qPrintable(removed.result().error().message()));
    m_testFolder.clear();
}

void TestDeviceFiles::leftoversOfEarlierRunsAreRemoved()
{
    if (!m_filesAllowed)
        QSKIP("The client may not access all files on this device");
    const Result<QList<FolderEntry>> before = list(downloadFolder());
    QVERIFY2(before.hasValue(), qPrintable(before.error().message()));

    int removed = 0;
    for (const FolderEntry &entry : before.value()) {
        if (!entry.isFolder || !isTestFolder(entry.name))
            continue;
        const QFuture<Result<void>> gone =
            m_files->remove(FileService::childPath(downloadFolder(), entry.name));
        QVERIFY(waitFor(gone, stepTimeoutMs));
        QVERIFY2(gone.result().hasValue(), qPrintable(gone.result().error().message()));
        ++removed;
    }
    qInfo() << "Test folders left over and removed:" << removed;

    const Result<QList<FolderEntry>> after = list(downloadFolder());
    QVERIFY(after.hasValue());
    for (const FolderEntry &entry : after.value())
        QVERIFY(!isTestFolder(entry.name));
}

void TestDeviceFiles::storagesAreListedAndWellFormed()
{
    if (!m_filesAllowed)
        QSKIP("The client may not access all files on this device");
    const Result<QList<FolderEntry>> roots = list(QString());
    QVERIFY2(roots.hasValue(), qPrintable(roots.error().message()));
    QStringList storages;
    for (const FolderEntry &entry : roots.value()) {
        QVERIFY(entry.isFolder);
        storages.append(entry.name);
    }
    // The storage names are the client's own words, not anything personal.
    qInfo() << "Storages:" << storages;
    QVERIFY(storages.contains(QStringLiteral("sdcard")));

    const Result<QList<FolderEntry>> internal = list(QStringLiteral("\\sdcard"));
    QVERIFY2(internal.hasValue(), qPrintable(internal.error().message()));
    int folders = 0;
    int files = 0;
    for (const FolderEntry &entry : internal.value()) {
        QVERIFY(isUsableFileName(entry.name));
        if (entry.isFolder) {
            ++folders;
        } else {
            ++files;
            QVERIFY(entry.size.has_value());
            QVERIFY(entry.modified.isValid());
        }
    }
    qInfo() << "Internal storage, top level:" << folders << "folders," << files << "files";
    QVERIFY(folders > 0);
}

void TestDeviceFiles::smallFilesMakeTheRoundTrip()
{
    if (!m_filesAllowed)
        QSKIP("The client may not access all files on this device");
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QVERIFY2(makeTestFolder().hasValue(), "The test folder could not be created");

    struct Sample
    {
        QString name;
        QByteArray data;
    };
    const QList<Sample> samples = {
        {QStringLiteral("probe.txt"), QByteArrayLiteral("QPhoneManager test file\n")},
        {QStringLiteral("Grüße & Co.bin"), pattern(300000, 7)},
        {QStringLiteral("leer.dat"), QByteArray()},
    };
    for (const Sample &sample : samples) {
        const QString source = local.filePath(sample.name);
        QVERIFY(writeFile(source, sample.data, testTime()));
        const QFuture<Result<QString>> uploaded =
            m_files->upload(source, m_testFolder, sample.name);
        QVERIFY(waitFor(uploaded, stepTimeoutMs));
        QVERIFY2(uploaded.result().hasValue(), qPrintable(uploaded.result().error().message()));
        QCOMPARE(uploaded.result().value(), sample.name);
    }

    const auto allThere = [&samples](const QList<FolderEntry> &entries) {
        return std::all_of(samples.cbegin(), samples.cend(), [&entries](const Sample &sample) {
            const std::optional<FolderEntry> found = entryNamed(entries, sample.name);
            return found && found->size == quint64(sample.data.size()) &&
                   found->modified == testTime();
        });
    };
    Result<QList<FolderEntry>> listed = listUntil(m_testFolder, allThere);
    QVERIFY2(listed.hasValue(), qPrintable(listed.error().message()));
    QCOMPARE(listed.value().size(), samples.size());
    for (const Sample &sample : samples) {
        const std::optional<FolderEntry> found = entryNamed(listed.value(), sample.name);
        QVERIFY2(found, qPrintable(sample.name));
        QCOMPARE(found->size, std::optional<quint64>(quint64(sample.data.size())));
        QCOMPARE(found->modified, testTime());
    }

    const QFuture<Result<void>> renamed =
        m_files->rename(FileService::childPath(m_testFolder, QStringLiteral("probe.txt")),
                        QStringLiteral("umbenannt.txt"));
    QVERIFY(waitFor(renamed, stepTimeoutMs));
    QVERIFY2(renamed.result().hasValue(), qPrintable(renamed.result().error().message()));

    const QString back = local.filePath(QStringLiteral("zurück"));
    QVERIFY(QDir().mkpath(back));
    const QList<std::pair<QString, QByteArray>> expected = {
        {QStringLiteral("umbenannt.txt"), samples.at(0).data},
        {samples.at(1).name, samples.at(1).data},
        {samples.at(2).name, samples.at(2).data}};
    for (const auto &[name, data] : expected) {
        const QString target = QDir(back).filePath(name);
        const RemoteFile remote{FileService::childPath(m_testFolder, name), quint64(data.size()),
                                testTime()};
        const QFuture<Result<void>> downloaded = m_files->download(remote, target);
        QVERIFY(waitFor(downloaded, stepTimeoutMs));
        QVERIFY2(downloaded.result().hasValue(), qPrintable(downloaded.result().error().message()));
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), data);
        QCOMPARE(QFileInfo(target).lastModified().toUTC(), testTime());
    }

    const QFuture<Result<void>> removedFile =
        m_files->remove(FileService::childPath(m_testFolder, samples.at(1).name));
    QVERIFY(waitFor(removedFile, stepTimeoutMs));
    QVERIFY(removedFile.result().hasValue());
    listed = list(m_testFolder);
    QVERIFY(listed.hasValue());
    QCOMPARE(listed.value().size(), samples.size() - 1);
}

void TestDeviceFiles::ownPictureGetsAThumbnail()
{
    if (!m_filesAllowed)
        QSKIP("The client may not access all files on this device");
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QVERIFY2(makeTestFolder().hasValue(), "The test folder could not be created");
    const QString source = local.filePath(QStringLiteral("probe.jpg"));
    QVERIFY(writeFile(source, testPicture(), testTime()));
    const QFuture<Result<QString>> uploaded =
        m_files->upload(source, m_testFolder, QStringLiteral("probe.jpg"));
    QVERIFY(waitFor(uploaded, stepTimeoutMs));
    QVERIFY2(uploaded.result().hasValue(), qPrintable(uploaded.result().error().message()));

    QList<Thumbnail> thumbnails;
    // The phone may still be scanning the new picture.
    for (int attempt = 0; attempt < thumbnailTries && thumbnails.isEmpty(); ++attempt) {
        const QFuture<Result<QList<Thumbnail>>> fetched =
            m_thumbnails->fetch(m_testFolder, {QStringLiteral("probe.jpg")});
        QVERIFY(waitFor(fetched, stepTimeoutMs));
        QVERIFY2(fetched.result().hasValue(), qPrintable(fetched.result().error().message()));
        thumbnails = fetched.result().value();
    }

    QCOMPARE(thumbnails.size(), 1);
    QCOMPARE(thumbnails.constFirst().name, QStringLiteral("probe.jpg"));
    const QImage image = QImage::fromData(thumbnails.constFirst().image);
    QVERIFY(!image.isNull());
    QVERIFY(image.width() <= 640 && image.height() <= 360);
    qInfo() << "Thumbnail" << image.size() << "orientation" << thumbnails.constFirst().orientation;
}

void TestDeviceFiles::largeFileMakesTheRoundTripOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("100 MB are copied on the emulator only");
    if (!m_filesAllowed)
        QSKIP("The client may not access all files on this device");
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QVERIFY2(makeTestFolder().hasValue(), "The test folder could not be created");
    const QString source = local.filePath(QStringLiteral("gross.bin"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        constexpr qsizetype chunk = 1024 * 1024;
        for (qint64 written = 0; written < largeFileBytes; written += chunk)
            QCOMPARE(file.write(pattern(chunk, int(written / chunk))), chunk);
    }
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(testTime(), QFileDevice::FileModificationTime));
    }
    const QByteArray hash = sha256Of(source);

    QElapsedTimer timer;
    timer.start();
    const QFuture<Result<QString>> uploaded =
        m_files->upload(source, m_testFolder, QStringLiteral("gross.bin"));
    QVERIFY(waitFor(uploaded, largeTimeoutMs));
    QVERIFY2(uploaded.result().hasValue(), qPrintable(uploaded.result().error().message()));
    const qint64 upMs = std::max<qint64>(timer.restart(), 1);

    const Result<QList<FolderEntry>> listed =
        listUntil(m_testFolder, [](const QList<FolderEntry> &entries) {
            const std::optional<FolderEntry> found =
                entryNamed(entries, QStringLiteral("gross.bin"));
            return found && found->size == quint64(largeFileBytes);
        });
    QVERIFY(listed.hasValue());
    const std::optional<FolderEntry> onPhone =
        entryNamed(listed.value(), QStringLiteral("gross.bin"));
    QVERIFY(onPhone);
    QCOMPARE(onPhone->size, std::optional<quint64>(quint64(largeFileBytes)));
    QCOMPARE(onPhone->modified, testTime());

    const QString target = local.filePath(QStringLiteral("zurueck.bin"));
    timer.restart();
    const QFuture<Result<void>> downloaded = m_files->download(
        RemoteFile{FileService::childPath(m_testFolder, QStringLiteral("gross.bin")),
                   quint64(largeFileBytes), testTime()},
        target);
    QVERIFY(waitFor(downloaded, largeTimeoutMs));
    QVERIFY2(downloaded.result().hasValue(), qPrintable(downloaded.result().error().message()));
    const qint64 downMs = std::max<qint64>(timer.elapsed(), 1);

    QCOMPARE(sha256Of(target), hash);
    QCOMPARE(QFileInfo(target).lastModified().toUTC(), testTime());
    qInfo() << "100 MB up in" << upMs << "ms," << (largeFileBytes / 1000 / upMs) << "MB/s; down in"
            << downMs << "ms," << (largeFileBytes / 1000 / downMs) << "MB/s";
}

void TestDeviceFiles::cancelledUploadLeavesNothingOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Cancelling a large upload is tried on the emulator only");
    if (!m_filesAllowed)
        QSKIP("The client may not access all files on this device");
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QVERIFY2(makeTestFolder().hasValue(), "The test folder could not be created");
    const QString source = local.filePath(QStringLiteral("abbruch.bin"));
    QVERIFY(writeFile(source, pattern(40 * 1024 * 1024, 3), testTime()));
    connect(m_files.get(), &FileService::transferProgress, this, [this](quint64 done) {
        if (done >= 4 * 1024 * 1024)
            m_files->cancelTransfer();
    });

    const QFuture<Result<QString>> uploaded =
        m_files->upload(source, m_testFolder, QStringLiteral("abbruch.bin"));
    QVERIFY(waitFor(uploaded, largeTimeoutMs));

    QCOMPARE(uploaded.result().error().code(), ErrorCode::Cancelled);
    const Result<QList<FolderEntry>> listed = list(m_testFolder);
    QVERIFY2(listed.hasValue(), qPrintable(listed.error().message()));
    QVERIFY(!entryNamed(listed.value(), QStringLiteral("abbruch.bin")));
}

QTEST_MAIN(TestDeviceFiles)

#include "tst_device_files.moc"
