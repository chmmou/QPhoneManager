#include "core/protocol/PhoneConnection.h"
#include "core/service/FileService.h"
#include "core/service/ThumbnailChannel.h"
#include "ui/contacts/PhoneImageProvider.h"
#include "ui/files/FileBrowserModel.h"
#include "ui/files/TransferQueue.h"

#include "FakePhone.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUrl>

#include <memory>

using namespace qpm::core;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::FileService;
using qpm::core::service::ThumbnailChannel;
using qpm::test::FakePhone;
using qpm::ui::FileBrowserModel;
using qpm::ui::PhotoSource;
using qpm::ui::TransferQueue;

namespace {

constexpr int timeoutMs = 20000;

QDateTime autumn()
{
    return {QDate(2026, 10, 3), QTime(14, 5), QTimeZone::UTC};
}

QByteArray widePicture()
{
    QImage image(160, 90, QImage::Format_RGB32);
    image.fill(QColor(0, 106, 106));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 90);
    return bytes;
}

QString role(const QAbstractItemModel &model, int row, const QByteArray &name)
{
    return model.data(model.index(row, 0), model.roleNames().key(name)).toString();
}

QStringList column(const QAbstractItemModel &model, const QByteArray &name)
{
    QStringList result;
    for (int row = 0; row < model.rowCount(); ++row)
        result.append(role(model, row, name));
    return result;
}

} // namespace

// The presentation of the phone's files: storages and folders, thumbnails,
// the path bar, and the changes made through it.
class TestFileBrowser : public QObject
{
    Q_OBJECT

public:
    explicit TestFileBrowser(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void rootShowsTheStoragesByTheirNames();
    void folderShowsFoldersFirstWithSizeAndTime();
    void picturesGetUprightThumbnails();
    void pathBarLeadsBackToEveryFolderAbove();
    void folderIsCreatedRenamedAndDeleted();
    void namesAreCheckedBeforeAnythingIsSent();
    void cutAndPasteMovesIntoAnotherFolder();
    void copiesGoThroughTheTransferQueue();
    void rootTakesNoCopiesOrChanges();
    void lostConnectionEmptiesTheList();

private:
    [[nodiscard]] bool openAndWait(const QString &path);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<FileService> m_service;
    std::unique_ptr<ThumbnailChannel> m_thumbnails;
    std::unique_ptr<TransferQueue> m_queue;
    std::shared_ptr<PhotoSource> m_photos;
    std::unique_ptr<FileBrowserModel> m_model;
};

void TestFileBrowser::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
}

void TestFileBrowser::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->addFolder(QStringLiteral("\\external"));
    m_phone->addFolder(QStringLiteral("\\sdcard\\DCIM\\Camera"));
    m_phone->addFile(QStringLiteral("\\sdcard\\DCIM\\Camera\\See.jpg"), widePicture(), autumn());
    m_phone->addFile(QStringLiteral("\\sdcard\\DCIM\\Camera\\Notiz.txt"), QByteArray(1536, 'n'),
                     autumn());
    m_phone->addFolder(QStringLiteral("\\sdcard\\DCIM\\Camera\\Ältere"));
    m_phone->addFolder(QStringLiteral("\\sdcard\\Download"));
    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_service = std::make_unique<FileService>(m_connection.get());
    m_thumbnails = std::make_unique<ThumbnailChannel>(m_connection.get());
    m_queue = std::make_unique<TransferQueue>(m_service.get());
    m_photos = std::make_shared<PhotoSource>();
    m_model = std::make_unique<FileBrowserModel>(m_service.get(), m_thumbnails.get(), m_queue.get(),
                                                 m_photos);
    m_queue->setConnected(true);
    m_model->setConnected(true);
    QCOMPARE(m_model->count(), 0);
    QVERIFY(m_model->load());
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->isBusy() && m_model->count() > 0, timeoutMs);
}

void TestFileBrowser::cleanup()
{
    m_model.reset();
    m_queue.reset();
    m_thumbnails.reset();
    m_service.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
    m_phone.reset();
    m_dir.reset();
}

bool TestFileBrowser::openAndWait(const QString &path)
{
    if (!m_model->open(path))
        return false;
    return QTest::qWaitFor([this]() { return !m_model->isBusy(); }, timeoutMs);
}

void TestFileBrowser::rootShowsTheStoragesByTheirNames()
{
    QVERIFY(m_model->atRoot());
    QCOMPARE(column(*m_model, "name"),
             (QStringList{QStringLiteral("sdcard"), QStringLiteral("external")}));
    QCOMPARE(column(*m_model, "displayName"),
             (QStringList{QStringLiteral("Internal storage"), QStringLiteral("Memory card")}));
    QCOMPARE(role(*m_model, 0, "details"), QStringLiteral("Storage"));
    QCOMPARE(column(*m_model, "iconName"),
             (QStringList{QStringLiteral("device-mobile"), QStringLiteral("device-sd-card")}));
    QCOMPARE(m_model->crumbNames(), QStringList{QStringLiteral("Phone")});
}

void TestFileBrowser::folderShowsFoldersFirstWithSizeAndTime()
{
    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\DCIM\\Camera")));

    QCOMPARE(column(*m_model, "name"),
             (QStringList{QStringLiteral("Ältere"), QStringLiteral("Notiz.txt"),
                          QStringLiteral("See.jpg")}));
    QVERIFY(m_model->isFolderAt(0));
    QCOMPARE(role(*m_model, 0, "details"), QStringLiteral("Folder"));
    QCOMPARE(role(*m_model, 0, "iconName"), QStringLiteral("folder"));
    QCOMPARE(role(*m_model, 1, "sizeText"),
             QLocale().formattedDataSize(1536, 1, QLocale::DataSizeTraditionalFormat));
    const QString time = QLocale().toString(autumn().toLocalTime(), QLocale::ShortFormat);
    QCOMPARE(role(*m_model, 1, "modifiedText"), time);
    QCOMPARE(m_model->data(m_model->index(1), FileBrowserModel::ModifiedRole).toDateTime(),
             autumn());
    QCOMPARE(role(*m_model, 1, "details"),
             QStringLiteral("%1, %2").arg(role(*m_model, 1, "sizeText"), time));
    QCOMPARE(role(*m_model, 1, "iconName"), QStringLiteral("file"));
    QCOMPARE(role(*m_model, 2, "iconName"), QStringLiteral("photo"));
    QCOMPARE(m_model->emptyText(), QStringLiteral("This folder is empty."));
}

void TestFileBrowser::picturesGetUprightThumbnails()
{
    m_phone->config().thumbnailOrientation = 6;

    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\DCIM\\Camera")));
    const int row = m_model->rowOf(QStringLiteral("See.jpg"));
    QTRY_VERIFY_WITH_TIMEOUT(!role(*m_model, row, "thumbnailUrl").isEmpty(), timeoutMs);

    const QString url = role(*m_model, row, "thumbnailUrl");
    QVERIFY(url.startsWith(QStringLiteral("image://qpm/thumb/")));
    const QImage thumbnail =
        m_photos->thumbnail(url.mid(QStringLiteral("image://qpm/thumb/").size()).toULongLong());
    QVERIFY(!thumbnail.isNull());
    // Turned by a quarter, the wide picture stands upright.
    QCOMPARE(thumbnail.size(), QSize(90, 160));
    QCOMPARE(role(*m_model, m_model->rowOf(QStringLiteral("Notiz.txt")), "thumbnailUrl"),
             QString());
    const QByteArray request = m_phone->thumbnailRequests().constFirst();
    QVERIFY(request.contains("See.jpg"));
    QVERIFY(!request.contains("Notiz.txt"));
}

void TestFileBrowser::pathBarLeadsBackToEveryFolderAbove()
{
    QSignalSpy pathChanged(m_model.get(), &FileBrowserModel::pathChanged);
    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\DCIM\\Camera")));

    QCOMPARE(m_model->crumbNames(),
             (QStringList{QStringLiteral("Phone"), QStringLiteral("Internal storage"),
                          QStringLiteral("DCIM"), QStringLiteral("Camera")}));
    QCOMPARE(m_model->crumbPaths(),
             (QStringList{QString(), QStringLiteral("\\sdcard"), QStringLiteral("\\sdcard\\DCIM"),
                          QStringLiteral("\\sdcard\\DCIM\\Camera")}));
    QVERIFY(m_model->up());
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->isBusy(), timeoutMs);
    QCOMPARE(m_model->path(), QStringLiteral("\\sdcard\\DCIM"));
    QCOMPARE(column(*m_model, "name"), QStringList{QStringLiteral("Camera")});
    QVERIFY(m_model->openRow(0));
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->isBusy(), timeoutMs);
    QCOMPARE(m_model->path(), QStringLiteral("\\sdcard\\DCIM\\Camera"));
    QCOMPARE(pathChanged.count(), 3);
}

void TestFileBrowser::folderIsCreatedRenamedAndDeleted()
{
    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\Download")));
    QSignalSpy info(m_model.get(), &FileBrowserModel::infoMessage);
    QSignalSpy errors(m_model.get(), &FileBrowserModel::errorOccurred);

    QVERIFY(m_model->mkdir(QStringLiteral("Rechnungen")));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->rowOf(QStringLiteral("Rechnungen")), 0, timeoutMs);
    QVERIFY(m_phone->hasFolder(QStringLiteral("\\sdcard\\Download\\Rechnungen")));

    QVERIFY(m_model->rename(QStringLiteral("Rechnungen"), QStringLiteral("Belege 2026")));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->rowOf(QStringLiteral("Belege 2026")), 0, timeoutMs);
    QVERIFY(m_phone->hasFolder(QStringLiteral("\\sdcard\\Download\\Belege 2026")));

    QVERIFY(m_model->remove({QStringLiteral("Belege 2026")}));
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->isBusy() && m_model->count() == 0, timeoutMs);
    QVERIFY(!m_phone->hasFolder(QStringLiteral("\\sdcard\\Download\\Belege 2026")));
    QVERIFY(info.constLast().first().toString().contains(QStringLiteral("deleted on the phone")));
    QCOMPARE(errors.count(), 0);
}

void TestFileBrowser::namesAreCheckedBeforeAnythingIsSent()
{
    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\DCIM\\Camera")));
    m_phone->clearLog();
    QSignalSpy errors(m_model.get(), &FileBrowserModel::errorOccurred);

    QCOMPARE(m_model->nameProblem(QStringLiteral("see.JPG")),
             QStringLiteral("There is already an entry named See.jpg here."));
    QCOMPARE(m_model->nameProblem(QStringLiteral("SEE.jpg"), QStringLiteral("See.jpg")), QString());
    QCOMPARE(m_model->nameProblem(QStringLiteral("  ")), QStringLiteral("Enter a name."));
    QCOMPARE(m_model->nameProblem(QStringLiteral(" a")),
             QStringLiteral("A name cannot start or end with a space."));
    QVERIFY(!m_model->nameProblem(QStringLiteral("a/b")).isEmpty());
    QVERIFY(!m_model->nameProblem(QStringLiteral("..")).isEmpty());
    QVERIFY(!m_model->mkdir(QStringLiteral("a/b")));
    QVERIFY(!m_model->rename(QStringLiteral("Notiz.txt"), QStringLiteral("See.jpg")));

    QCOMPARE(errors.count(), 2);
    const QStringList sent = m_phone->receivedCommands();
    for (const QString &command : sent) {
        QVERIFY2(!command.startsWith(QLatin1StringView("OBEX MOVE")) &&
                     !command.contains(QLatin1StringView("a/b")),
                 qPrintable(command));
    }
}

void TestFileBrowser::cutAndPasteMovesIntoAnotherFolder()
{
    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\DCIM\\Camera")));
    QVERIFY(!m_model->canPaste());

    QVERIFY(m_model->cut({QStringLiteral("Notiz.txt")}));
    QVERIFY(m_model->canPaste());
    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\Download")));
    QVERIFY(m_model->paste());
    QTRY_COMPARE_WITH_TIMEOUT(column(*m_model, "name"), QStringList{QStringLiteral("Notiz.txt")},
                              timeoutMs);

    QVERIFY(!m_model->canPaste());
    QVERIFY(m_phone->file(QStringLiteral("\\sdcard\\Download\\Notiz.txt")).has_value());
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\DCIM\\Camera\\Notiz.txt")).has_value());
}

void TestFileBrowser::copiesGoThroughTheTransferQueue()
{
    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\DCIM\\Camera")));

    QVERIFY(m_model->download({QStringLiteral("Notiz.txt")}, QUrl::fromLocalFile(m_dir->path())));
    QTRY_VERIFY_WITH_TIMEOUT(!m_queue->isActive(), timeoutMs);
    QFile copy(m_dir->filePath(QStringLiteral("Notiz.txt")));
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QCOMPARE(copy.readAll(), QByteArray(1536, 'n'));

    const QString source = m_dir->filePath(QStringLiteral("Neu.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("neu");
    file.close();
    QVERIFY(m_model->upload({QUrl::fromLocalFile(source)}));
    // The folder is listed again once the copy is there.
    QTRY_VERIFY_WITH_TIMEOUT(m_model->rowOf(QStringLiteral("Neu.txt")) >= 0, timeoutMs);
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\DCIM\\Camera\\Neu.txt"))->data,
             QByteArray("neu"));

    QVERIFY(!m_model->download({QStringLiteral("Notiz.txt")},
                               QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("fehlt")))));
    QVERIFY(!m_model->upload({QUrl(QStringLiteral("https://example.org/a.txt"))}));
}

void TestFileBrowser::rootTakesNoCopiesOrChanges()
{
    QSignalSpy errors(m_model.get(), &FileBrowserModel::errorOccurred);
    const QString source = m_dir->filePath(QStringLiteral("x.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QVERIFY(!m_model->upload({QUrl::fromLocalFile(source)}));
    QVERIFY(!m_model->mkdir(QStringLiteral("Neu")));
    QVERIFY(!m_model->remove({QStringLiteral("sdcard")}));
    QVERIFY(!m_model->cut({QStringLiteral("sdcard")}));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!m_queue->isActive());
}

void TestFileBrowser::lostConnectionEmptiesTheList()
{
    QVERIFY(openAndWait(QStringLiteral("\\sdcard\\DCIM\\Camera")));

    m_model->setConnected(false);

    QCOMPARE(m_model->count(), 0);
    QVERIFY(m_model->atRoot());
    QVERIFY(!m_model->canEdit());
    QCOMPARE(m_model->emptyText(), QStringLiteral("Connect your phone to browse its files."));
    QVERIFY(!m_model->open(QStringLiteral("\\sdcard")));
    QVERIFY(!m_model->load());
}

QTEST_MAIN(TestFileBrowser)

#include "tst_filebrowser.moc"
