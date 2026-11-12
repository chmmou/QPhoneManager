#include "core/codec/SafeXml.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/ThumbnailChannel.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QTest>

#include <memory>

using namespace qpm::core;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::FileService;
using qpm::core::service::Thumbnail;
using qpm::core::service::ThumbnailChannel;
using qpm::core::service::ThumbnailStreamReader;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;

// Picture bytes that hold the end text of a file without the next header,
// which must not end the picture.
QByteArray trickyPicture()
{
    return QByteArray("\xFF\xD8\xFF\xE0", 4) + "\r\nFileEnd inside" + QByteArray(300, '\x7F') +
           QByteArray("\xFF\xD9", 2);
}

QByteArray clientStream(const QByteArray &first, const QByteArray &second)
{
    return "FileBegin;\\sdcard\\DCIM\\a.jpg;Orientation=6\r\n" + first +
           "\r\nFileEndFileBegin;\\sdcard\\DCIM\\b.mp4;Orientation=0\r\n" + second +
           "\r\nFileEndFileBegin;EndOfStream\r\n";
}

} // namespace

class TestThumbnails : public QObject
{
    Q_OBJECT

public:
    explicit TestThumbnails(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void streamOfTheClientIsSplitIntoPictures();
    void streamArrivingByteByByteGivesTheSamePictures();
    void unaskedOrRepeatedNameIsRefused_data();
    void unaskedOrRepeatedNameIsRefused();
    void oversizedPictureIsRefused();
    void malformedHeaderIsRefused();
    void requestNamesFolderAndFilesEscaped();
    void phoneSendsThumbnailsOfPicturesOnly();
    void streamIsAskedForAgainWhenThePhoneListensLate();
    void fetchFailsWhenThePhoneNeverListens();
    void requestWithoutSideChannelIsNotSupported();
    void unusableRequestIsRefusedBeforeSending();
};

void TestThumbnails::streamOfTheClientIsSplitIntoPictures()
{
    ThumbnailStreamReader reader(
        {QStringLiteral("a.jpg"), QStringLiteral("b.mp4"), QStringLiteral("c.txt")});

    QVERIFY(reader.feed(clientStream(trickyPicture(), "video frame")).hasValue());

    QVERIFY(reader.isFinished());
    const QList<Thumbnail> pictures = reader.thumbnails();
    QCOMPARE(pictures.size(), 2);
    QCOMPARE(pictures.at(0).name, QStringLiteral("a.jpg"));
    QCOMPARE(pictures.at(0).orientation, 6);
    QCOMPARE(pictures.at(0).image, trickyPicture());
    QCOMPARE(pictures.at(1).name, QStringLiteral("b.mp4"));
    QCOMPARE(pictures.at(1).orientation, 0);
    QCOMPARE(pictures.at(1).image, QByteArrayLiteral("video frame"));
}

void TestThumbnails::streamArrivingByteByByteGivesTheSamePictures()
{
    ThumbnailStreamReader reader({QStringLiteral("a.jpg"), QStringLiteral("b.mp4")});
    const QByteArray stream = clientStream(trickyPicture(), "frame");

    for (qsizetype i = 0; i < stream.size(); ++i) {
        QVERIFY(!reader.isFinished());
        QVERIFY(reader.feed(stream.mid(i, 1)).hasValue());
    }

    QVERIFY(reader.isFinished());
    QCOMPARE(reader.thumbnails().size(), 2);
    QCOMPARE(reader.thumbnails().at(0).image, trickyPicture());
}

void TestThumbnails::unaskedOrRepeatedNameIsRefused_data()
{
    QTest::addColumn<QStringList>("asked");

    QTest::newRow("not asked") << QStringList{QStringLiteral("b.mp4")};
    QTest::newRow("asked once, sent twice") << QStringList{QStringLiteral("a.jpg")};
}

void TestThumbnails::unaskedOrRepeatedNameIsRefused()
{
    QFETCH(QStringList, asked);
    ThumbnailStreamReader reader(asked);
    const QByteArray twice = "FileBegin;\\x\\a.jpg;Orientation=0\r\n1\r\nFileEnd"
                             "FileBegin;\\x\\a.jpg;Orientation=0\r\n2\r\nFileEnd"
                             "FileBegin;EndOfStream\r\n";

    const Result<void> fed = reader.feed(twice);

    QVERIFY(!fed.hasValue());
    QCOMPARE(fed.error().code(), ErrorCode::ProtocolError);
    QVERIFY(!reader.isFinished());
}

void TestThumbnails::oversizedPictureIsRefused()
{
    ThumbnailStreamReader reader({QStringLiteral("a.jpg")});
    QVERIFY(reader.feed("FileBegin;\\x\\a.jpg;Orientation=1\r\n").hasValue());

    const Result<void> fed =
        reader.feed(QByteArray(ThumbnailStreamReader::maxImageBytes + 64, 'j'));

    QVERIFY(!fed.hasValue());
    QCOMPARE(fed.error().code(), ErrorCode::LimitExceeded);
    QVERIFY(!reader.feed("\r\nFileEndFileBegin;EndOfStream\r\n").hasValue());
}

void TestThumbnails::malformedHeaderIsRefused()
{
    ThumbnailStreamReader garbage({QStringLiteral("a.jpg")});
    QCOMPARE(garbage.feed("HTTP/1.1 200 OK\r\n").error().code(), ErrorCode::ProtocolError);

    ThumbnailStreamReader endless({QStringLiteral("a.jpg")});
    QCOMPARE(endless.feed("FileBegin;" + QByteArray(5000, 'x')).error().code(),
             ErrorCode::LimitExceeded);

    ThumbnailStreamReader noOrientation({QStringLiteral("a.jpg")});
    QCOMPARE(noOrientation.feed("FileBegin;\\x\\a.jpg\r\n").error().code(),
             ErrorCode::ProtocolError);
}

void TestThumbnails::requestNamesFolderAndFilesEscaped()
{
    const QByteArray body =
        ThumbnailChannel::requestBody(QStringLiteral("\\sdcard\\Tom & Jerry"),
                                      {QStringLiteral("a \"b\".jpg"), QStringLiteral("<c>.png")});

    const Result<codec::XmlElement> parsed = codec::parseSafeXml(body);
    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().name, QStringLiteral("thumbnails"));
    const QList<codec::XmlElement> &children = parsed.value().children;
    QCOMPARE(children.size(), 3);
    QCOMPARE(children.at(0).name, QStringLiteral("folder"));
    QCOMPARE(children.at(0).attribute(QStringLiteral("path")),
             QStringLiteral("\\sdcard\\Tom & Jerry"));
    QCOMPARE(children.at(1).attribute(QStringLiteral("name")), QStringLiteral("a \"b\".jpg"));
    QCOMPARE(children.at(2).attribute(QStringLiteral("name")), QStringLiteral("<c>.png"));
}

void TestThumbnails::phoneSendsThumbnailsOfPicturesOnly()
{
    FakePhone phone;
    QVERIFY(phone.listen());
    phone.config().thumbnailOrientation = 8;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    phone.addFile(QStringLiteral("\\sdcard\\DCIM\\See.jpg"), trickyPicture(), now);
    phone.addFile(QStringLiteral("\\sdcard\\DCIM\\Notiz.txt"), "text", now);
    PhoneConnection connection;
    connection.connectTcp(QStringLiteral("127.0.0.1"), phone.port());
    QTRY_COMPARE_WITH_TIMEOUT(connection.state(), PhoneConnection::State::Ready, timeoutMs);
    ThumbnailChannel channel(&connection);

    const QFuture<Result<QList<Thumbnail>>> fetched = channel.fetch(
        QStringLiteral("\\sdcard\\DCIM"),
        {QStringLiteral("See.jpg"), QStringLiteral("Notiz.txt"), QStringLiteral("weg.jpg")});
    QVERIFY(waitFor(fetched, timeoutMs));

    QVERIFY(fetched.result().hasValue());
    const QList<Thumbnail> pictures = fetched.result().value();
    QCOMPARE(pictures.size(), 1);
    QCOMPARE(pictures.constFirst().name, QStringLiteral("See.jpg"));
    QCOMPARE(pictures.constFirst().orientation, 8);
    QCOMPARE(pictures.constFirst().image, trickyPicture());
    QCOMPARE(phone.thumbnailRequests().size(), 1);
    QVERIFY(phone.receivedCommands().contains(QStringLiteral("OBEX PUT \\getthumbnail")));
    QCOMPARE(phone.protocolErrors(), QStringList());
    connection.disconnectFromPhone();
}

// Over adb forwarding a connection made before the phone listened was closed
// at once, and the thumbnails were missing although the phone was about to
// send them.
void TestThumbnails::streamIsAskedForAgainWhenThePhoneListensLate()
{
    FakePhone phone;
    QVERIFY(phone.listen());
    phone.config().droppedThumbnailConnections = 2;
    phone.addFile(QStringLiteral("\\sdcard\\DCIM\\See.jpg"), trickyPicture(),
                  QDateTime::currentDateTimeUtc());
    PhoneConnection connection;
    connection.connectTcp(QStringLiteral("127.0.0.1"), phone.port());
    QTRY_COMPARE_WITH_TIMEOUT(connection.state(), PhoneConnection::State::Ready, timeoutMs);
    ThumbnailChannel channel(&connection);

    const QFuture<Result<QList<Thumbnail>>> fetched =
        channel.fetch(QStringLiteral("\\sdcard\\DCIM"), {QStringLiteral("See.jpg")});
    QVERIFY(waitFor(fetched, timeoutMs));

    QVERIFY(fetched.result().hasValue());
    QCOMPARE(fetched.result().value().size(), 1);
    QCOMPARE(fetched.result().value().constFirst().image, trickyPicture());
    QCOMPARE(phone.thumbnailRequests().size(), 1);
    connection.disconnectFromPhone();
}

void TestThumbnails::fetchFailsWhenThePhoneNeverListens()
{
    FakePhone phone;
    QVERIFY(phone.listen());
    phone.config().droppedThumbnailConnections = FileService::sideChannelConnectTries;
    phone.addFile(QStringLiteral("\\sdcard\\DCIM\\See.jpg"), trickyPicture(),
                  QDateTime::currentDateTimeUtc());
    PhoneConnection connection;
    connection.connectTcp(QStringLiteral("127.0.0.1"), phone.port());
    QTRY_COMPARE_WITH_TIMEOUT(connection.state(), PhoneConnection::State::Ready, timeoutMs);
    ThumbnailChannel channel(&connection);

    const QFuture<Result<QList<Thumbnail>>> fetched =
        channel.fetch(QStringLiteral("\\sdcard\\DCIM"), {QStringLiteral("See.jpg")});
    QVERIFY(waitFor(fetched, timeoutMs));

    QCOMPARE(fetched.result().error().code(), ErrorCode::ConnectionClosed);
    connection.disconnectFromPhone();
}

void TestThumbnails::requestWithoutSideChannelIsNotSupported()
{
    FakePhone phone;
    QVERIFY(phone.listen());
    PhoneConnection connection;
    connection.connectTcp(QStringLiteral("127.0.0.1"), phone.port());
    QTRY_COMPARE_WITH_TIMEOUT(connection.state(), PhoneConnection::State::Ready, timeoutMs);
    ThumbnailChannel channel(&connection);
    channel.setEndpointResolver([](quint16) { return std::nullopt; });

    const QFuture<Result<QList<Thumbnail>>> fetched =
        channel.fetch(QStringLiteral("\\sdcard"), {QStringLiteral("a.jpg")});

    QCOMPARE(fetched.result().error().code(), ErrorCode::NotSupported);
    QVERIFY(phone.thumbnailRequests().isEmpty());
    connection.disconnectFromPhone();
}

void TestThumbnails::unusableRequestIsRefusedBeforeSending()
{
    FakePhone phone;
    QVERIFY(phone.listen());
    PhoneConnection connection;
    connection.connectTcp(QStringLiteral("127.0.0.1"), phone.port());
    QTRY_COMPARE_WITH_TIMEOUT(connection.state(), PhoneConnection::State::Ready, timeoutMs);
    ThumbnailChannel channel(&connection);
    QStringList tooMany;
    for (int i = 0; i <= ThumbnailChannel::maxNamesPerRequest; ++i)
        tooMany.append(QStringLiteral("%1.jpg").arg(i));

    QCOMPARE(channel.fetch(QStringLiteral("\\sdcard"), tooMany).result().error().code(),
             ErrorCode::InvalidInput);
    QCOMPARE(channel.fetch(QStringLiteral("\\sdcard"), {QStringLiteral("../a.jpg")})
                 .result()
                 .error()
                 .code(),
             ErrorCode::InvalidInput);
    QCOMPARE(
        channel.fetch(QStringLiteral("sdcard"), {QStringLiteral("a.jpg")}).result().error().code(),
        ErrorCode::InvalidInput);
    QVERIFY(phone.thumbnailRequests().isEmpty());
    connection.disconnectFromPhone();
}

QTEST_MAIN(TestThumbnails)

#include "tst_thumbnails.moc"
