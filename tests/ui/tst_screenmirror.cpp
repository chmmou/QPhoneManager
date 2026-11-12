#include "core/protocol/PhoneConnection.h"
#include "core/service/ScreenMirrorClient.h"
#include "ui/screen/ScreenMirror.h"
#include "ui/screen/ScreenMirrorItem.h"

#include "FakePhone.h"

#include <QBuffer>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

using namespace qpm::core;
using qpm::core::protocol::Endpoint;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::ScreenMirrorClient;
using qpm::test::FakePhone;
using qpm::ui::ScreenMirror;
using qpm::ui::ScreenMirrorItem;

namespace {

constexpr int timeoutMs = 15000;
const QColor screenColor(0, 106, 106);
const QColor paddingColor(200, 40, 40);

QByteArray sizeText(const QSize &size)
{
    return QByteArray::number(size.width()) + 'x' + QByteArray::number(size.height());
}

// A frame as the phone sends it: the screen at the left, padded rows at the right.
QByteArray pngFrame(const QSize &screen, int padding = 0)
{
    QImage image(screen.width() + padding, screen.height(), QImage::Format_RGB32);
    image.fill(paddingColor);
    for (int y = 0; y < screen.height(); ++y) {
        for (int x = 0; x < screen.width(); ++x)
            image.setPixelColor(x, y, screenColor);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return "FileBegin;" + sizeText(screen) + ";0;" + sizeText(image.size()) + ";PNG;100\r\n" + png +
           "\r\nFileEnd";
}

struct Mirror
{
    FakePhone phone;
    PhoneConnection connection;
    std::unique_ptr<ScreenMirrorClient> client;
    std::unique_ptr<ScreenMirror> mirror;

    bool open(const QByteArray &frame)
    {
        if (!phone.listen())
            return false;
        phone.config().screenFrame = frame;
        connection.connectTcp(QStringLiteral("127.0.0.1"), phone.port());
        if (!QTest::qWaitFor(
                [this]() { return connection.state() == PhoneConnection::State::Ready; },
                timeoutMs)) {
            return false;
        }
        client = std::make_unique<ScreenMirrorClient>(&connection);
        client->setEndpointResolver([this](quint16) -> std::optional<Endpoint> {
            return Endpoint{QStringLiteral("127.0.0.1"), phone.screenPort()};
        });
        mirror = std::make_unique<ScreenMirror>(client.get());
        mirror->setConnected(true);
        return true;
    }

    bool start()
    {
        if (!mirror->start())
            return false;
        return QTest::qWaitFor([this]() { return mirror->hasFrame(); }, timeoutMs);
    }

    ~Mirror()
    {
        mirror.reset();
        client.reset();
        connection.disconnectFromPhone();
    }
};

} // namespace

class TestScreenMirrorView : public QObject
{
    Q_OBJECT

public:
    explicit TestScreenMirrorView(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void paddedPictureIsCroppedToTheScreen();
    void brokenPictureIsLeftOutAndTheNextOneShown();
    void screenshotIsSavedAsPng();
    void screenshotWithoutPictureIsRefused();
    void brokenStreamIsReported();
    void endOfTheConnectionForgetsThePictureQuietly();
    void itemMapsPositionsToPhonePixels_data();
    void itemMapsPositionsToPhonePixels();
    void fingerMovesAreCombinedWhileThePhoneIsBusy();
    void typedTextIsCombinedWhileThePhoneIsBusy();
    void missingAccessibilityServiceShowsAsHint();
    void mouseOnTheItemBecomesTouches();
    void keysReachThePhoneOnlyWithKeyboardInput();
};

void TestScreenMirrorView::paddedPictureIsCroppedToTheScreen()
{
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(120, 260), 8)));
    QSignalSpy frames(session.mirror.get(), &ScreenMirror::frameChanged);

    QVERIFY(session.start());

    QCOMPARE(session.mirror->frameSize(), QSize(120, 260));
    const QImage frame = session.mirror->frame();
    QCOMPARE(frame.pixelColor(119, 259), screenColor);
    QCOMPARE(frame.pixelColor(0, 0), screenColor);
    QVERIFY(session.mirror->isActive());
    QVERIFY(frames.count() >= 1);
}

void TestScreenMirrorView::brokenPictureIsLeftOutAndTheNextOneShown()
{
    Mirror session;
    // A picture that ends like a JPEG but is none.
    QVERIFY(session.open("FileBegin;120x260;0;120x260;JPG;70\r\n" + QByteArray("\xFF\xD8no", 4) +
                         QByteArray("\xFF\xD9", 2) + "\r\nFileEnd"));
    QSignalSpy errors(session.mirror.get(), &ScreenMirror::errorOccurred);
    QVERIFY(session.mirror->start());
    QTRY_VERIFY_WITH_TIMEOUT(session.mirror->isActive(), timeoutMs);
    QVERIFY(!session.mirror->hasFrame());

    session.phone.sendScreenData(pngFrame(QSize(120, 260)));

    QTRY_VERIFY_WITH_TIMEOUT(session.mirror->hasFrame(), timeoutMs);
    QVERIFY(session.mirror->isActive());
    QVERIFY(errors.isEmpty());
}

void TestScreenMirrorView::screenshotIsSavedAsPng()
{
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(120, 260), 8)));
    QVERIFY(session.start());
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString path = folder.filePath(QStringLiteral("Bildschirm ä.png"));
    QSignalSpy saved(session.mirror.get(), &ScreenMirror::infoMessage);

    QVERIFY(session.mirror->saveScreenshot(QUrl::fromLocalFile(path)));

    QVERIFY(saved.wait(timeoutMs));
    QImage picture;
    QVERIFY(picture.load(path, "PNG"));
    QCOMPARE(picture.size(), QSize(120, 260));
    QCOMPARE(picture.pixelColor(119, 0), screenColor);
}

void TestScreenMirrorView::screenshotWithoutPictureIsRefused()
{
    Mirror session;
    QVERIFY(session.open(QByteArray()));
    QSignalSpy errors(session.mirror.get(), &ScreenMirror::errorOccurred);
    QTemporaryDir folder;
    QVERIFY(folder.isValid());

    QVERIFY(!session.mirror->saveScreenshot(
        QUrl::fromLocalFile(folder.filePath(QStringLiteral("a.png")))));

    QCOMPARE(errors.count(), 1);
    QVERIFY(QDir(folder.path()).isEmpty());
}

void TestScreenMirrorView::brokenStreamIsReported()
{
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(120, 260))));
    QVERIFY(session.start());
    QSignalSpy errors(session.mirror.get(), &ScreenMirror::errorOccurred);

    session.phone.closeScreen();

    QVERIFY(errors.wait(timeoutMs));
    QVERIFY(!errors.first().first().toString().isEmpty());
    QVERIFY(!session.mirror->hasFrame());
    QCOMPARE(session.mirror->state(), ScreenMirror::State::Off);
}

void TestScreenMirrorView::endOfTheConnectionForgetsThePictureQuietly()
{
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(120, 260))));
    QVERIFY(session.start());
    QSignalSpy errors(session.mirror.get(), &ScreenMirror::errorOccurred);

    session.mirror->setConnected(false);

    QVERIFY(!session.mirror->hasFrame());
    QCOMPARE(session.mirror->state(), ScreenMirror::State::Off);
    QVERIFY(!session.mirror->canControl());
    QVERIFY(!session.mirror->key(QStringLiteral("back")));
    QCOMPARE(errors.count(), 0);
}

void TestScreenMirrorView::itemMapsPositionsToPhonePixels_data()
{
    QTest::addColumn<int>("turns");
    QTest::addColumn<QPointF>("position");
    QTest::addColumn<QPoint>("phone");

    // A 100 x 200 screen in a 400 x 400 item: upright it is shown at 200 x 400
    // from x = 100, turned at 400 x 200 from y = 100.
    QTest::newRow("upright, top left") << 0 << QPointF(100, 0) << QPoint(0, 0);
    QTest::newRow("upright, centre") << 0 << QPointF(200, 200) << QPoint(50, 100);
    QTest::newRow("upright, bottom right") << 0 << QPointF(299.9, 399.9) << QPoint(99, 199);
    QTest::newRow("upright, beside the picture") << 0 << QPointF(50, 50) << QPoint(-1, -1);
    QTest::newRow("quarter, top left") << 1 << QPointF(0, 100) << QPoint(0, 199);
    QTest::newRow("quarter, top right") << 1 << QPointF(399.9, 100) << QPoint(0, 0);
    QTest::newRow("quarter, above the picture") << 1 << QPointF(200, 50) << QPoint(-1, -1);
    QTest::newRow("half, top left") << 2 << QPointF(100, 0) << QPoint(99, 199);
    QTest::newRow("three quarters, top left") << 3 << QPointF(0, 100) << QPoint(99, 0);
    QTest::newRow("five quarters as one") << 5 << QPointF(0, 100) << QPoint(0, 199);
}

void TestScreenMirrorView::itemMapsPositionsToPhonePixels()
{
    QFETCH(int, turns);
    QFETCH(QPointF, position);
    QFETCH(QPoint, phone);
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(100, 200))));
    QVERIFY(session.start());
    ScreenMirrorItem item;
    item.setMirror(session.mirror.get());
    item.setSize(QSizeF(400, 400));

    item.setTurns(turns);

    QCOMPARE(item.turns(), turns % 4);
    QCOMPARE(item.phonePointAt(position.x(), position.y()), phone);
    QCOMPARE(item.implicitWidth(), turns % 2 == 0 ? 100.0 : 200.0);
}

void TestScreenMirrorView::fingerMovesAreCombinedWhileThePhoneIsBusy()
{
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(100, 200))));
    QVERIFY(session.start());
    session.phone.clearLog();
    session.phone.config().replyDelay = std::chrono::milliseconds(150);

    session.mirror->pressPointer(QPoint(10, 10));
    for (int y = 11; y < 60; ++y)
        session.mirror->movePointer(QPoint(10, y));
    session.mirror->releasePointer(QPoint(10, 60));

    QTRY_VERIFY_WITH_TIMEOUT(
        session.phone.receivedCommands().contains(QStringLiteral("AT*C=0,10,60,0")), timeoutMs);
    const QStringList sent = session.phone.receivedCommands();
    QCOMPARE(sent.constFirst(), QStringLiteral("AT*C=0,10,10,1"));
    QCOMPARE(sent.constLast(), QStringLiteral("AT*C=0,10,60,0"));
    QVERIFY2(sent.size() <= 4, qPrintable(sent.join(u' ')));
    QCOMPARE(session.phone.protocolErrors(), QStringList());
}

void TestScreenMirrorView::typedTextIsCombinedWhileThePhoneIsBusy()
{
    Mirror session;
    QVERIFY(session.open(QByteArray()));
    session.phone.clearLog();
    session.phone.config().replyDelay = std::chrono::milliseconds(150);

    QVERIFY(session.mirror->typeText(QStringLiteral("H")));
    QVERIFY(session.mirror->typeText(QStringLiteral("al")));
    QVERIFY(session.mirror->typeText(QStringLiteral("lö\n")));

    QTRY_COMPARE_WITH_TIMEOUT(
        session.phone.receivedCommands(),
        (QStringList{QStringLiteral("AT*T=H,"), QStringLiteral("AT*T=allö,")}), timeoutMs);
}

void TestScreenMirrorView::missingAccessibilityServiceShowsAsHint()
{
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(100, 200))));
    session.phone.config().accessibilityService = false;
    QSignalSpy errors(session.mirror.get(), &ScreenMirror::errorOccurred);
    QVERIFY(session.start());

    QCOMPARE(session.mirror->control(), ScreenMirror::Control::ServiceOff);
    QVERIFY(!session.mirror->canControl());
    QVERIFY(!session.mirror->hintText().isEmpty());
    QVERIFY(session.mirror->key(QStringLiteral("home")));
    QTRY_VERIFY_WITH_TIMEOUT(session.phone.receivedCommands().contains(QStringLiteral("AT*K=home")),
                             timeoutMs);
    QVERIFY(session.mirror->showAccessibilitySettings());
    QTRY_VERIFY_WITH_TIMEOUT(
        session.phone.receivedCommands().contains(QStringLiteral("AT*SHOWACCSETTINGS")), timeoutMs);
    QCOMPARE(errors.count(), 0);

    session.phone.config().accessibilityService = true;
    QSignalSpy infos(session.mirror.get(), &ScreenMirror::infoMessage);
    QVERIFY(session.mirror->checkControl());
    QTRY_COMPARE_WITH_TIMEOUT(session.mirror->control(), ScreenMirror::Control::Available,
                              timeoutMs);
    QVERIFY(session.mirror->hintText().isEmpty());
    QVERIFY(!infos.isEmpty());
}

void TestScreenMirrorView::mouseOnTheItemBecomesTouches()
{
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(100, 200))));
    QVERIFY(session.start());
    session.phone.clearLog();
    QQuickWindow window;
    window.resize(400, 400);
    ScreenMirrorItem item(window.contentItem());
    item.setSize(QSizeF(400, 400));
    item.setMirror(session.mirror.get());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTest::mousePress(&window, Qt::LeftButton, {}, QPoint(200, 200));
    QTest::mouseMove(&window, QPoint(200, 300));
    QTest::mouseRelease(&window, Qt::LeftButton, {}, QPoint(200, 300));
    QTest::mouseClick(&window, Qt::RightButton, {}, QPoint(150, 100));
    QTest::mouseClick(&window, Qt::LeftButton, {}, QPoint(20, 20));

    QTRY_COMPARE_WITH_TIMEOUT(
        session.phone.receivedCommands(),
        (QStringList{QStringLiteral("AT*C=0,50,100,1"), QStringLiteral("AT*C=0,50,150,1"),
                     QStringLiteral("AT*C=0,50,150,0"), QStringLiteral("AT*C=0,25,50,2")}),
        timeoutMs);
}

void TestScreenMirrorView::keysReachThePhoneOnlyWithKeyboardInput()
{
    Mirror session;
    QVERIFY(session.open(pngFrame(QSize(100, 200))));
    QVERIFY(session.start());
    session.phone.clearLog();
    QQuickWindow window;
    window.resize(400, 400);
    ScreenMirrorItem item(window.contentItem());
    item.setSize(QSizeF(400, 400));
    item.setMirror(session.mirror.get());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    item.forceActiveFocus();
    QTRY_VERIFY(item.hasActiveFocus());

    QTest::keyClick(&window, Qt::Key_A);
    item.setKeyboardInput(true);
    QTest::keyClick(&window, Qt::Key_B);
    QTest::keyClick(&window, Qt::Key_Backspace);
    QTest::keyClick(&window, Qt::Key_Return);
    QTest::keyClick(&window, Qt::Key_Down);
    // Left to the window, so that the focus can leave and shortcuts work.
    QTest::keyClick(&window, Qt::Key_Tab);
    QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier);

    QTRY_COMPARE_WITH_TIMEOUT(
        session.phone.receivedCommands(),
        (QStringList{QStringLiteral("AT*T=b,"), QStringLiteral("AT*K=del"),
                     QStringLiteral("AT*K=enter"), QStringLiteral("AT*K=down")}),
        timeoutMs);
}

QTEST_MAIN(TestScreenMirrorView)

#include "tst_screenmirror.moc"
