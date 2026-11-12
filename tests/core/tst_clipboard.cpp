#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/ClipboardService.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace qpm::core;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::ClipboardService;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 20000;

int countOf(const QStringList &commands, const QString &prefix)
{
    return int(std::count_if(commands.cbegin(), commands.cend(), [&prefix](const QString &command) {
        return command.startsWith(prefix);
    }));
}

} // namespace

// The clipboard service against the fake phone: writing and reading the
// phone's clipboard, the clips the phone reports on its own, and that a clip
// never travels back to the side it came from.
class TestClipboard : public QObject
{
    Q_OBJECT

public:
    explicit TestClipboard(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void textGoesToThePhoneEscapedWithATrailingComma();
    void phoneClipboardIsReadAlsoWhenTheAnswerComesLate();
    void syncingSwitchesTheReportsOfThePhone();
    void reportedClipArrivesOnceAndIsNotSentBack();
    void desktopClipIsSentOnceAndNotTakenBack();
    void nothingIsExchangedWhileNotSyncing();
    void emptyOverlongAndUnconnectedClipsAreRefused();
    void overlongClipOfThePhoneIsIgnored();

private:
    [[nodiscard]] bool connect(bool watch);
    [[nodiscard]] bool everythingSentIsAnswered();

    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<ClipboardService> m_service;
};

void TestClipboard::init()
{
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_connection = std::make_unique<PhoneConnection>();
    m_service = std::make_unique<ClipboardService>(m_connection.get());
}

void TestClipboard::cleanup()
{
    if (m_phone)
        QCOMPARE(m_phone->protocolErrors(), QStringList());
    m_service.reset();
    m_connection.reset();
    m_phone.reset();
}

bool TestClipboard::connect(bool watch)
{
    PhoneConnection::Options options;
    options.clipboardWatch = watch;
    m_connection->setOptions(options);
    m_service->setSyncing(watch);
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    return QTest::qWaitFor(
        [this]() { return m_connection->state() == PhoneConnection::State::Ready; }, timeoutMs);
}

// The phone answers in order and writes its own lines before the answer that
// follows them, so once this command is answered, everything before it is
// through on both sides.
bool TestClipboard::everythingSentIsAnswered()
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return false;
    const QFuture<protocol::AtReply> reply = session->send(QStringLiteral("AT+CSQ"));
    return waitFor(reply, timeoutMs) && reply.result().status == protocol::AtReply::Status::Ok;
}

void TestClipboard::textGoesToThePhoneEscapedWithATrailingComma()
{
    QVERIFY(connect(false));
    const QString text = QStringLiteral("Grüße, \"Anna\"\nZeile 2; a=b: ok");

    const QFuture<Result<void>> sent = m_service->sendToPhone(text);
    QVERIFY(waitFor(sent, timeoutMs));

    QVERIFY2(sent.result().hasValue(), qPrintable(sent.result().error().message()));
    QCOMPARE(m_phone->config().clipboard, text);
    QVERIFY(m_phone->receivedCommands().contains(
        QStringLiteral("AT*CLIPBOARD=Grüße=2C =22Anna=22=0AZeile 2=3B a=3Db=3A ok,")));
}

void TestClipboard::phoneClipboardIsReadAlsoWhenTheAnswerComesLate()
{
    QVERIFY(connect(false));
    m_phone->config().clipboard = QStringLiteral("Kopiert am Handy, mit Komma");
    // Android lets the app read the clipboard only through a window of its own.
    m_phone->config().clipboardReadDelay = std::chrono::milliseconds(1500);

    const QFuture<Result<QString>> read = m_service->readFromPhone();
    QVERIFY(waitFor(read, timeoutMs));

    QVERIFY2(read.result().hasValue(), qPrintable(read.result().error().message()));
    QCOMPARE(read.result().value(), QStringLiteral("Kopiert am Handy, mit Komma"));
    QCOMPARE(m_connection->state(), PhoneConnection::State::Ready);
}

void TestClipboard::syncingSwitchesTheReportsOfThePhone()
{
    QVERIFY(connect(true));
    QVERIFY(m_phone->clipboardWatched());

    m_service->setSyncing(false);
    QTRY_VERIFY_WITH_TIMEOUT(!m_phone->clipboardWatched(), timeoutMs);
    m_service->setSyncing(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_phone->clipboardWatched(), timeoutMs);
    QCOMPARE(countOf(m_phone->receivedCommands(), QStringLiteral("AT*CLIPWATCH=")), 3);
}

void TestClipboard::reportedClipArrivesOnceAndIsNotSentBack()
{
    QVERIFY(connect(true));
    QSignalSpy arrived(m_service.get(), &ClipboardService::phoneTextChanged);
    m_phone->clearLog();

    m_phone->copyOnPhone(QStringLiteral("Vom Handy: 12,50 €"));
    QTRY_COMPARE_WITH_TIMEOUT(arrived.count(), 1, timeoutMs);
    QCOMPARE(arrived.first().first().toString(), QStringLiteral("Vom Handy: 12,50 €"));
    // This computer puts the clip on its own clipboard and hands it back.
    m_service->offerDesktopText(QStringLiteral("Vom Handy: 12,50 €"));
    m_phone->copyOnPhone(QStringLiteral("Vom Handy: 12,50 €"));

    QVERIFY(everythingSentIsAnswered());
    QCOMPARE(arrived.count(), 1);
    QCOMPARE(countOf(m_phone->receivedCommands(), QStringLiteral("AT*CLIPBOARD")), 0);
}

void TestClipboard::desktopClipIsSentOnceAndNotTakenBack()
{
    QVERIFY(connect(true));
    QSignalSpy arrived(m_service.get(), &ClipboardService::phoneTextChanged);
    m_phone->clearLog();

    m_service->offerDesktopText(QStringLiteral("Vom Rechner"));
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->config().clipboard, QStringLiteral("Vom Rechner"),
                              timeoutMs);
    m_service->offerDesktopText(QStringLiteral("Vom Rechner"));
    // A phone that reports the clip it was just given does not start a round trip.
    m_phone->copyOnPhone(QStringLiteral("Vom Rechner"));

    QVERIFY(everythingSentIsAnswered());
    QCOMPARE(countOf(m_phone->receivedCommands(), QStringLiteral("AT*CLIPBOARD=")), 1);
    QCOMPARE(arrived.count(), 0);
}

void TestClipboard::nothingIsExchangedWhileNotSyncing()
{
    QVERIFY(connect(false));
    QSignalSpy arrived(m_service.get(), &ClipboardService::phoneTextChanged);
    m_phone->clearLog();

    m_service->offerDesktopText(QStringLiteral("Bleibt hier"));
    m_phone->sendUnsolicited("*CLIPWATCH:Bleibt am Handy");

    QVERIFY(everythingSentIsAnswered());
    QCOMPARE(m_phone->receivedCommands(), QStringList{QStringLiteral("AT+CSQ")});
    QCOMPARE(arrived.count(), 0);
}

void TestClipboard::emptyOverlongAndUnconnectedClipsAreRefused()
{
    const QFuture<Result<void>> unconnected = m_service->sendToPhone(QStringLiteral("x"));
    QVERIFY(waitFor(unconnected));
    QCOMPARE(unconnected.result().error().code(), ErrorCode::ConnectionClosed);
    const QFuture<Result<QString>> unread = m_service->readFromPhone();
    QVERIFY(waitFor(unread));
    QCOMPARE(unread.result().error().code(), ErrorCode::ConnectionClosed);

    QVERIFY(connect(false));
    m_phone->clearLog();
    const QFuture<Result<void>> empty = m_service->sendToPhone(QString());
    const QFuture<Result<void>> overlong =
        m_service->sendToPhone(QString(ClipboardService::maxTextLength + 1, u'x'));
    QVERIFY(waitFor(empty) && waitFor(overlong));

    QCOMPARE(empty.result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(overlong.result().error().code(), ErrorCode::LimitExceeded);
    QVERIFY(m_phone->receivedCommands().isEmpty());
}

void TestClipboard::overlongClipOfThePhoneIsIgnored()
{
    QVERIFY(connect(true));
    QSignalSpy arrived(m_service.get(), &ClipboardService::phoneTextChanged);

    m_phone->copyOnPhone(QString(ClipboardService::maxTextLength + 1, u'y'));
    m_phone->config().clipboard = QString(ClipboardService::maxTextLength + 1, u'z');
    const QFuture<Result<QString>> read = m_service->readFromPhone();
    QVERIFY(waitFor(read, timeoutMs));

    QCOMPARE(read.result().error().code(), ErrorCode::LimitExceeded);
    QCOMPARE(arrived.count(), 0);
    QCOMPARE(m_connection->state(), PhoneConnection::State::Ready);
}

QTEST_GUILESS_MAIN(TestClipboard)

#include "tst_clipboard.moc"
