#include "core/protocol/PhoneConnection.h"
#include "core/service/ScreenMirrorClient.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

Q_DECLARE_METATYPE(qpm::core::ErrorCode)
Q_DECLARE_METATYPE(qpm::test::FakePhoneConfig::ScreenConsent)

using namespace qpm::core;
using qpm::core::protocol::Endpoint;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::ScreenFrame;
using qpm::core::service::ScreenFrameReader;
using qpm::core::service::ScreenMirrorClient;
using qpm::test::FakePhone;
using qpm::test::FakePhoneConfig;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;

// Bytes that look like a JPEG to the reader, with the end text of a frame
// inside, which must not end the picture.
QByteArray jpegLike(const QByteArray &fill)
{
    return QByteArray("\xFF\xD8\xFF\xE0", 4) + "\r\nFileEnd inside" + fill +
           QByteArray("\xFF\xD9", 2);
}

QByteArray pngLike(const QByteArray &fill)
{
    return QByteArray("\x89PNG\r\n\x1A\n", 8) + fill + QByteArray("IEND\xAE\x42\x60\x82", 8);
}

QByteArray frameBytes(const QByteArray &header, const QByteArray &picture)
{
    return "FileBegin;" + header + "\r\n" + picture + "\r\nFileEnd";
}

QByteArray portraitFrame(const QByteArray &fill = "screen")
{
    return frameBytes("1080x2400;0;1080x2400;JPG;70", jpegLike(fill));
}

// A connection to the fake phone, with the screen channel found on its own port.
struct Session
{
    Session() = default;
    Q_DISABLE_COPY_MOVE(Session)

    FakePhone phone;
    PhoneConnection connection;
    std::unique_ptr<ScreenMirrorClient> client;

    bool open()
    {
        if (!phone.listen())
            return false;
        connection.connectTcp(QStringLiteral("127.0.0.1"), phone.port());
        if (!QTest::qWaitFor(
                [this]() { return connection.state() == PhoneConnection::State::Ready; },
                timeoutMs)) {
            return false;
        }
        client = std::make_unique<ScreenMirrorClient>(&connection);
        client->setEndpointResolver([this](quint16 port) -> std::optional<Endpoint> {
            if (port != ScreenMirrorClient::screenPort)
                return std::nullopt;
            return Endpoint{QStringLiteral("127.0.0.1"), phone.screenPort()};
        });
        phone.clearLog();
        return true;
    }

    Result<void> start()
    {
        const QFuture<Result<void>> started = client->start();
        if (!waitFor(started, timeoutMs))
            return Error(ErrorCode::Timeout, QStringLiteral("start did not finish"));
        return started.result();
    }

    ~Session()
    {
        client.reset();
        connection.disconnectFromPhone();
    }
};

} // namespace

class TestScreenMirror : public QObject
{
    Q_OBJECT

public:
    explicit TestScreenMirror(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void streamOfTheClientIsSplitIntoFrames();
    void streamArrivingByteByByteGivesTheSameFrames();
    void paddedPngFrameIsTaken();
    void malformedHeaderIsRefused_data();
    void malformedHeaderIsRefused();
    void oversizedFrameIsRefused();
    void mirroringStartsOnceThePhoneAllowsIt();
    void startFailsWhenThePhoneDoesNotCapture_data();
    void startFailsWhenThePhoneDoesNotCapture();
    void startWithoutSideChannelIsNotSupported();
    void channelIsAskedForAgainWhenThePhoneListensLate();
    void brokenStreamStopsTheMirroring();
    void oversizedFrameStopsTheMirroring();
    void streamOutOfFormStopsTheMirroring();
    void endOfTheConnectionStopsTheMirroring();
    void stopClosesTheChannelWithoutAReport();
    void framesFasterThanTheIntervalAreThinned();
    void inputsBecomeCommandsOfTheClient_data();
    void inputsBecomeCommandsOfTheClient();
    void textIsSentWithItsSpacesAndCommas();
    void inputWithoutAccessibilityServiceIsRefused();
    void accessibilitySettingsAreOpenedOnThePhone();
    void oldPhoneTakesNoInput();
    void unusableInputIsRefusedBeforeSending();
};

void TestScreenMirror::streamOfTheClientIsSplitIntoFrames()
{
    ScreenFrameReader reader;
    const QByteArray stream =
        portraitFrame("first") + frameBytes("2400x1080;1;2416x1080;PNG;100", pngLike("second"));

    QVERIFY(reader.feed(stream).hasValue());

    const QList<ScreenFrame> frames = reader.takeFrames();
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).screenSize, QSize(1080, 2400));
    QCOMPARE(frames.at(0).rotation, 0);
    QCOMPARE(frames.at(0).format, ScreenFrame::Format::Jpeg);
    QCOMPARE(frames.at(0).image, jpegLike("first"));
    QCOMPARE(frames.at(1).screenSize, QSize(2400, 1080));
    QCOMPARE(frames.at(1).imageSize, QSize(2416, 1080));
    QCOMPARE(frames.at(1).rotation, 1);
    QCOMPARE(frames.at(1).format, ScreenFrame::Format::Png);
    QCOMPARE(frames.at(1).image, pngLike("second"));
    QVERIFY(reader.takeFrames().isEmpty());
}

void TestScreenMirror::streamArrivingByteByByteGivesTheSameFrames()
{
    ScreenFrameReader reader;
    const QByteArray stream = portraitFrame("one") + portraitFrame("two");
    QList<ScreenFrame> frames;

    for (qsizetype i = 0; i < stream.size(); ++i) {
        QVERIFY(reader.feed(stream.mid(i, 1)).hasValue());
        frames += reader.takeFrames();
    }

    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).image, jpegLike("one"));
    QCOMPARE(frames.at(1).image, jpegLike("two"));
}

void TestScreenMirror::paddedPngFrameIsTaken()
{
    ScreenFrameReader reader;
    // The end text right behind the PNG signature is no end of the picture.
    const QByteArray picture = pngLike(QByteArray("\r\nFileEnd") + QByteArray(64, 'p'));

    QVERIFY(reader.feed(frameBytes("720x1600;3;736x1600;PNG;5", picture)).hasValue());

    const QList<ScreenFrame> frames = reader.takeFrames();
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.constFirst().image, picture);
    QCOMPARE(frames.constFirst().rotation, 3);
}

void TestScreenMirror::malformedHeaderIsRefused_data()
{
    QTest::addColumn<QByteArray>("stream");
    QTest::addColumn<ErrorCode>("code");

    QTest::newRow("no header") << QByteArrayLiteral("HTTP/1.1 200 OK\r\n")
                               << ErrorCode::ProtocolError;
    QTest::newRow("too few fields")
        << QByteArrayLiteral("FileBegin;1080x2400;0;JPG;70\r\n") << ErrorCode::ProtocolError;
    QTest::newRow("rotation 4") << QByteArrayLiteral("FileBegin;1080x2400;4;1080x2400;JPG;70\r\n")
                                << ErrorCode::ProtocolError;
    QTest::newRow("unknown format")
        << QByteArrayLiteral("FileBegin;1080x2400;0;1080x2400;GIF;70\r\n")
        << ErrorCode::ProtocolError;
    QTest::newRow("quality 101") << QByteArrayLiteral("FileBegin;1080x2400;0;1080x2400;JPG;101\r\n")
                                 << ErrorCode::ProtocolError;
    QTest::newRow("picture smaller than the screen")
        << QByteArrayLiteral("FileBegin;1080x2400;0;1000x2400;JPG;70\r\n")
        << ErrorCode::ProtocolError;
    QTest::newRow("empty screen") << QByteArrayLiteral("FileBegin;0x0;0;1080x2400;JPG;70\r\n")
                                  << ErrorCode::LimitExceeded;
    QTest::newRow("too wide") << QByteArrayLiteral("FileBegin;9000x100;0;9000x100;JPG;70\r\n")
                              << ErrorCode::LimitExceeded;
    QTest::newRow("negative") << QByteArrayLiteral("FileBegin;-5x100;0;1080x2400;JPG;70\r\n")
                              << ErrorCode::LimitExceeded;
    QTest::newRow("endless header")
        << "FileBegin;" + QByteArray(ScreenFrameReader::maxHeaderBytes, 'x')
        << ErrorCode::LimitExceeded;
}

void TestScreenMirror::malformedHeaderIsRefused()
{
    QFETCH(QByteArray, stream);
    QFETCH(ErrorCode, code);
    ScreenFrameReader reader;

    const Result<void> fed = reader.feed(stream);

    QVERIFY(!fed.hasValue());
    QCOMPARE(fed.error().code(), code);
    QVERIFY(!reader.feed(portraitFrame()).hasValue());
    QVERIFY(reader.takeFrames().isEmpty());
}

void TestScreenMirror::oversizedFrameIsRefused()
{
    ScreenFrameReader reader;
    QVERIFY(reader.feed("FileBegin;1080x2400;0;1080x2400;JPG;70\r\n").hasValue());

    const Result<void> fed = reader.feed(QByteArray(ScreenFrameReader::maxImageBytes + 64, 'j'));

    QVERIFY(!fed.hasValue());
    QCOMPARE(fed.error().code(), ErrorCode::LimitExceeded);
}

void TestScreenMirror::mirroringStartsOnceThePhoneAllowsIt()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    QSignalSpy frames(session.client.get(), &ScreenMirrorClient::frameReceived);
    QSignalSpy states(session.client.get(), &ScreenMirrorClient::stateChanged);

    const Result<void> started = session.start();

    QVERIFY2(started.hasValue(), qPrintable(started.error().message()));
    QCOMPARE(session.client->state(), ScreenMirrorClient::State::Running);
    QCOMPARE(session.client->control(), ScreenMirrorClient::Control::Available);
    QVERIFY(states.count() >= 3);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, timeoutMs);
    const auto frame = frames.first().first().value<ScreenFrame>();
    QCOMPARE(frame.screenSize, QSize(1080, 2400));
    QCOMPARE(frame.image, jpegLike("screen"));
    QCOMPARE(session.client->screenSize(), QSize(1080, 2400));
    const QStringList expected = {QStringLiteral("AT*STARTCAPTURESERVER=?"),
                                  QStringLiteral("AT*DISPLAY"), QStringLiteral("AT*K=?"),
                                  QStringLiteral("AT*STARTCAPTURESERVER")};
    QStringList sent = session.phone.receivedCommands();
    sent.removeIf([](const QString &command) {
        return !command.startsWith(QLatin1StringView("AT*STARTCAPTURESERVER")) &&
               command != QLatin1StringView("AT*DISPLAY") && command != QLatin1StringView("AT*K=?");
    });
    QCOMPARE(sent, expected);
    QCOMPARE(session.phone.screenRequests().constFirst(), QByteArrayLiteral("0:JPG:70"));
    QCOMPARE(session.phone.protocolErrors(), QStringList());
}

void TestScreenMirror::startFailsWhenThePhoneDoesNotCapture_data()
{
    QTest::addColumn<FakePhoneConfig::ScreenConsent>("consent");
    QTest::addColumn<int>("androidApi");
    QTest::addColumn<ErrorCode>("code");

    QTest::newRow("declined on the phone")
        << FakePhoneConfig::ScreenConsent::Decline << 34 << ErrorCode::Rejected;
    QTest::newRow("no answer on the phone")
        << FakePhoneConfig::ScreenConsent::NoAnswer << 34 << ErrorCode::Timeout;
    QTest::newRow("Android 4.4") << FakePhoneConfig::ScreenConsent::Allow << 19
                                 << ErrorCode::NotSupported;
}

void TestScreenMirror::startFailsWhenThePhoneDoesNotCapture()
{
    QFETCH(FakePhoneConfig::ScreenConsent, consent);
    QFETCH(int, androidApi);
    QFETCH(ErrorCode, code);
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenConsent = consent;
    session.phone.config().androidApi = androidApi;
    QSignalSpy stopped(session.client.get(), &ScreenMirrorClient::stopped);

    const Result<void> started = session.start();

    QVERIFY(!started.hasValue());
    QCOMPARE(started.error().code(), code);
    QCOMPARE(session.client->state(), ScreenMirrorClient::State::Idle);
    QCOMPARE(session.phone.screenPort(), quint16(0));
    QCOMPARE(stopped.count(), 0);
    QCOMPARE(session.connection.state(), PhoneConnection::State::Ready);
}

void TestScreenMirror::startWithoutSideChannelIsNotSupported()
{
    Session session;
    QVERIFY(session.open());
    session.client->setEndpointResolver([](quint16) { return std::nullopt; });

    const QFuture<Result<void>> started = session.client->start();

    QCOMPARE(started.result().error().code(), ErrorCode::NotSupported);
    QVERIFY(session.phone.receivedCommands().isEmpty());
}

// Over adb forwarding a connection made before the phone listens is taken
// and closed again; the phone keeps listening and is asked again.
void TestScreenMirror::channelIsAskedForAgainWhenThePhoneListensLate()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    session.phone.config().droppedScreenConnections = 3;

    const Result<void> started = session.start();

    QVERIFY2(started.hasValue(), qPrintable(started.error().message()));
    QCOMPARE(session.client->state(), ScreenMirrorClient::State::Running);
    QCOMPARE(session.phone.screenRequests().size(), 1);
}

void TestScreenMirror::brokenStreamStopsTheMirroring()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    QVERIFY(session.start().hasValue());
    QSignalSpy stopped(session.client.get(), &ScreenMirrorClient::stopped);

    session.phone.closeScreen();

    QVERIFY(stopped.wait(timeoutMs));
    QCOMPARE(stopped.first().first().value<Error>().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(session.client->state(), ScreenMirrorClient::State::Idle);
    QCOMPARE(session.connection.state(), PhoneConnection::State::Ready);
}

void TestScreenMirror::oversizedFrameStopsTheMirroring()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    QSignalSpy stopped(session.client.get(), &ScreenMirrorClient::stopped);
    QSignalSpy frames(session.client.get(), &ScreenMirrorClient::frameReceived);
    QVERIFY(session.start().hasValue());
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, timeoutMs);

    session.phone.sendScreenData("FileBegin;1080x2400;0;1080x2400;JPG;70\r\n" +
                                 QByteArray(ScreenFrameReader::maxImageBytes + 1024, 'j'));

    QVERIFY(stopped.wait(timeoutMs));
    QCOMPARE(stopped.first().first().value<Error>().code(), ErrorCode::LimitExceeded);
    QCOMPARE(frames.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!session.phone.isScreenConnected(), timeoutMs);
}

void TestScreenMirror::streamOutOfFormStopsTheMirroring()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    QVERIFY(session.start().hasValue());
    QSignalSpy stopped(session.client.get(), &ScreenMirrorClient::stopped);

    session.phone.sendScreenData("FileBegin;1080x2400;0;1080x2400;BMP;70\r\n");

    QVERIFY(stopped.wait(timeoutMs));
    QCOMPARE(stopped.first().first().value<Error>().code(), ErrorCode::ProtocolError);
}

void TestScreenMirror::endOfTheConnectionStopsTheMirroring()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    QVERIFY(session.start().hasValue());
    QSignalSpy stopped(session.client.get(), &ScreenMirrorClient::stopped);

    session.phone.dropConnection();

    QVERIFY(stopped.wait(timeoutMs));
    QCOMPARE(stopped.first().first().value<Error>().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(session.client->state(), ScreenMirrorClient::State::Idle);
    QCOMPARE(session.client->control(), ScreenMirrorClient::Control::Unknown);
}

void TestScreenMirror::stopClosesTheChannelWithoutAReport()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    QVERIFY(session.start().hasValue());
    QTRY_VERIFY_WITH_TIMEOUT(session.phone.isScreenConnected(), timeoutMs);
    QSignalSpy stopped(session.client.get(), &ScreenMirrorClient::stopped);

    session.client->stop();

    QCOMPARE(session.client->state(), ScreenMirrorClient::State::Idle);
    QTRY_VERIFY_WITH_TIMEOUT(!session.phone.isScreenConnected(), timeoutMs);
    QCOMPARE(stopped.count(), 0);
    // A new start works after a stop.
    QVERIFY(session.start().hasValue());
}

void TestScreenMirror::framesFasterThanTheIntervalAreThinned()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame("start");
    QSignalSpy frames(session.client.get(), &ScreenMirrorClient::frameReceived);
    QVERIFY(session.start().hasValue());
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, timeoutMs);
    QByteArray burst;
    for (int i = 0; i < 10; ++i)
        burst += portraitFrame(QByteArray::number(i));

    session.phone.sendScreenData(burst);

    // The newest frame always arrives, the ones before it mostly not.
    QTRY_COMPARE_WITH_TIMEOUT(frames.last().first().value<ScreenFrame>().image, jpegLike("9"),
                              timeoutMs);
    QVERIFY2(frames.count() <= 3, qPrintable(QString::number(frames.count())));
}

void TestScreenMirror::inputsBecomeCommandsOfTheClient_data()
{
    QTest::addColumn<int>("input");
    QTest::addColumn<QString>("command");

    QTest::newRow("back") << 0 << QStringLiteral("AT*K=back");
    QTest::newRow("home") << 1 << QStringLiteral("AT*K=home");
    QTest::newRow("recents") << 2 << QStringLiteral("AT*K=recents");
    QTest::newRow("backspace") << 3 << QStringLiteral("AT*K=del");
    QTest::newRow("enter") << 4 << QStringLiteral("AT*K=enter");
    QTest::newRow("finger down") << 5 << QStringLiteral("AT*C=0,540,1200,1");
    QTest::newRow("finger moved") << 6 << QStringLiteral("AT*C=0,600,1300,1");
    QTest::newRow("finger lifted") << 7 << QStringLiteral("AT*C=0,600,1300,0");
    QTest::newRow("long press") << 8 << QStringLiteral("AT*C=0,10,20,2");
    QTest::newRow("scroll up") << 9 << QStringLiteral("AT*C=0,540,800,3");
    QTest::newRow("scroll down") << 10 << QStringLiteral("AT*C=0,540,800,4");
    QTest::newRow("bottom right corner") << 11 << QStringLiteral("AT*C=0,1079,2399,1");
}

void TestScreenMirror::inputsBecomeCommandsOfTheClient()
{
    QFETCH(int, input);
    QFETCH(QString, command);
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    QVERIFY(session.start().hasValue());
    session.phone.clearLog();
    ScreenMirrorClient &client = *session.client;
    using Key = ScreenMirrorClient::Key;
    using Pointer = ScreenMirrorClient::Pointer;

    QFuture<Result<void>> sent;
    switch (input) {
    case 0:
        sent = client.pressKey(Key::Back);
        break;
    case 1:
        sent = client.pressKey(Key::Home);
        break;
    case 2:
        sent = client.pressKey(Key::Recents);
        break;
    case 3:
        sent = client.pressKey(Key::Backspace);
        break;
    case 4:
        sent = client.pressKey(Key::Enter);
        break;
    case 5:
        sent = client.pointer(Pointer::Down, QPoint(540, 1200));
        break;
    case 6:
        sent = client.pointer(Pointer::Move, QPoint(600, 1300));
        break;
    case 7:
        sent = client.pointer(Pointer::Up, QPoint(600, 1300));
        break;
    case 8:
        sent = client.longPress(QPoint(10, 20));
        break;
    case 9:
        sent = client.scroll(QPoint(540, 800), ScreenMirrorClient::Scroll::Up);
        break;
    case 10:
        sent = client.scroll(QPoint(540, 800), ScreenMirrorClient::Scroll::Down);
        break;
    default:
        sent = client.pointer(Pointer::Down, QPoint(1079, 2399));
        break;
    }

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY2(sent.result().hasValue(), qPrintable(sent.result().error().message()));
    QCOMPARE(session.phone.receivedCommands(), QStringList{command});
}

void TestScreenMirror::textIsSentWithItsSpacesAndCommas()
{
    Session session;
    QVERIFY(session.open());
    const QString text = QStringLiteral("Grüße, bis bald ");

    const QFuture<Result<void>> typed = session.client->typeText(text);

    QVERIFY(waitFor(typed, timeoutMs));
    QVERIFY(typed.result().hasValue());
    // The phone drops the one comma at the end and keeps everything before it.
    QCOMPARE(session.phone.receivedCommands(), QStringList{QStringLiteral("AT*T=") + text + u','});
    QCOMPARE(session.phone.protocolErrors(), QStringList());
    QCOMPARE(session.client->control(), ScreenMirrorClient::Control::Available);
}

void TestScreenMirror::inputWithoutAccessibilityServiceIsRefused()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().accessibilityService = false;
    session.phone.config().screenFrame = portraitFrame();
    QSignalSpy control(session.client.get(), &ScreenMirrorClient::controlChanged);

    QVERIFY(session.start().hasValue());

    QCOMPARE(session.client->control(), ScreenMirrorClient::Control::ServiceOff);
    QCOMPARE(control.count(), 1);
    const QFuture<Result<void>> key = session.client->pressKey(ScreenMirrorClient::Key::Back);
    QVERIFY(waitFor(key, timeoutMs));
    QCOMPARE(key.result().error().code(), ErrorCode::PermissionDenied);
    const QFuture<Result<void>> text = session.client->typeText(QStringLiteral("a"));
    QVERIFY(waitFor(text, timeoutMs));
    QCOMPARE(text.result().error().code(), ErrorCode::PermissionDenied);
    // The phone takes a touch also when it drops it; that proves nothing.
    const QFuture<Result<void>> touch =
        session.client->pointer(ScreenMirrorClient::Pointer::Down, QPoint(1, 1));
    QVERIFY(waitFor(touch, timeoutMs));
    QVERIFY(touch.result().hasValue());
    QCOMPARE(session.client->control(), ScreenMirrorClient::Control::ServiceOff);

    session.phone.config().accessibilityService = true;
    const QFuture<Result<ScreenMirrorClient::Control>> checked = session.client->checkControl();
    QVERIFY(waitFor(checked, timeoutMs));
    QCOMPARE(checked.result().value(), ScreenMirrorClient::Control::Available);
    QCOMPARE(session.client->control(), ScreenMirrorClient::Control::Available);
}

void TestScreenMirror::accessibilitySettingsAreOpenedOnThePhone()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().accessibilityService = false;

    const QFuture<Result<void>> shown = session.client->showAccessibilitySettings();

    QVERIFY(waitFor(shown, timeoutMs));
    QVERIFY(shown.result().hasValue());
    QCOMPARE(session.phone.receivedCommands(), QStringList{QStringLiteral("AT*SHOWACCSETTINGS")});
}

void TestScreenMirror::oldPhoneTakesNoInput()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().androidApi = 22;

    const QFuture<Result<ScreenMirrorClient::Control>> checked = session.client->checkControl();
    QVERIFY(waitFor(checked, timeoutMs));
    const QFuture<Result<void>> key = session.client->pressKey(ScreenMirrorClient::Key::Home);
    QVERIFY(waitFor(key, timeoutMs));

    QCOMPARE(checked.result().value(), ScreenMirrorClient::Control::NotSupported);
    QCOMPARE(key.result().error().code(), ErrorCode::NotSupported);
    QCOMPARE(session.client->control(), ScreenMirrorClient::Control::NotSupported);
}

void TestScreenMirror::unusableInputIsRefusedBeforeSending()
{
    Session session;
    QVERIFY(session.open());
    session.phone.config().screenFrame = portraitFrame();
    QVERIFY(session.start().hasValue());
    session.phone.clearLog();
    ScreenMirrorClient &client = *session.client;
    using Pointer = ScreenMirrorClient::Pointer;

    QCOMPARE(client.pointer(Pointer::Down, QPoint(-1, 10)).result().error().code(),
             ErrorCode::InvalidInput);
    QCOMPARE(client.pointer(Pointer::Down, QPoint(1080, 10)).result().error().code(),
             ErrorCode::InvalidInput);
    QCOMPARE(client.longPress(QPoint(10, 2400)).result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(client.typeText(QString()).result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(client.typeText(QStringLiteral("a\nATD123;")).result().error().code(),
             ErrorCode::InvalidInput);
    QCOMPARE(client.typeText(QString(ScreenMirrorClient::maxTextLength + 1, u'x'))
                 .result()
                 .error()
                 .code(),
             ErrorCode::InvalidInput);
    QCOMPARE(client.start().result().error().code(), ErrorCode::InvalidInput);
    QVERIFY(session.phone.receivedCommands().isEmpty());
}

QTEST_MAIN(TestScreenMirror)

#include "tst_screenmirror.moc"
