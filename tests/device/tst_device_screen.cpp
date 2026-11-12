#include "CommandGuard.h"

#include "core/protocol/AdbBridge.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/ScreenMirrorClient.h"
#include "ui/screen/ScreenMirror.h"

#include "WaitFor.h"

#include <QDir>
#include <QImage>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

using namespace qpm::core;
using namespace qpm::core::protocol;
using qpm::core::service::ScreenMirrorClient;
using qpm::test::CommandGuard;
using qpm::test::waitFor;
using qpm::ui::ScreenMirror;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int stepTimeoutMs = 60000;
constexpr int consentTimeoutMs = 40000;
constexpr int connectTries = 10;

QString clientPackage()
{
    return QStringLiteral("com.fjsoft.myphoneexplorer.client");
}

// The accessibility service of the client, as Android lists it in its settings.
QString inputService()
{
    return clientPackage() + QStringLiteral("/com.fjsoft.myphoneexplorer.client.InputService");
}

bool listsInputService(const QString &setting)
{
    const QStringList services = setting.split(u':', Qt::SkipEmptyParts);
    for (const QString &service : services) {
        const QString trimmed = service.trimmed();
        if (trimmed == inputService() ||
            trimmed == clientPackage() + QStringLiteral("/.InputService"))
            return true;
    }
    return false;
}

struct UiNode
{
    QString text;
    QString resourceId;
    QPoint centre;
};

} // namespace

// Runs against a real client over adb. On the emulator the screen is
// mirrored, the question of the phone is answered there through adb, a
// picture of plausible size arrives, the Back key acts, and a screenshot is
// saved; the accessibility service of the client is switched on for that
// where it is off, and the setting is put back afterwards. On any other
// phone, which may be someone's own, the test only reads whether that
// service is on: the capture would ask the owner, and input would act on
// whatever the phone shows. The guard keeps both away from such a phone.
class TestDeviceScreen : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceScreen(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void accessibilityServiceStateIsRead();
    void screenIsMirroredAndControlledOnTheEmulator();

private:
    [[nodiscard]] bool connectToClient();
    /// Runs adb with `arguments` for the device under test; nothing on a failure.
    [[nodiscard]] std::optional<QByteArray> adb(const QStringList &arguments,
                                                int timeoutMs = readyTimeoutMs) const;
    [[nodiscard]] std::optional<QString> secureSetting(const QString &name) const;
    [[nodiscard]] bool putSecureSetting(const QString &name, const QString &value) const;
    [[nodiscard]] QString topActivity() const;
    /// The window that takes keys on the built-in display, as the window manager reports it.
    [[nodiscard]] QString focusedWindow() const;
    [[nodiscard]] QList<UiNode> screenNodes() const;
    void tap(const QPoint &point) const;
    /// Dismisses questions about sharing the screen that an earlier run left open.
    void dismissLeftoverQuestions() const;
    /// Answers the question of the phone with the whole screen, until `done` holds.
    [[nodiscard]] bool allowCapture(const std::function<bool()> &done) const;
    void restoreAccessibility();

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    quint16 m_screenPort = 0;
    std::optional<QString> m_savedServices;
    std::optional<QString> m_savedEnabled;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<ScreenMirrorClient> m_client;
    std::unique_ptr<ScreenMirror> m_mirror;
    QPointer<CommandGuard> m_guard;
};

void TestDeviceScreen::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    qInfo() << (m_emulator ? "Target: emulator, the screen is mirrored and controlled"
                           : "Target: phone, only the accessibility setting is read");

    m_adb = std::make_unique<AdbBridge>(m_adbPath);
    const QFuture<Result<void>> server = m_adb->startServer();
    QVERIFY(waitFor(server, readyTimeoutMs));
    QVERIFY2(server.result().hasValue(), "adb start-server failed");
}

void TestDeviceScreen::cleanupTestCase()
{
    m_mirror.reset();
    m_client.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
    if (!m_adb || !m_emulator)
        return;
    restoreAccessibility();
    for (const quint16 local : {m_localPort, m_screenPort}) {
        if (local == 0)
            continue;
        const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, local);
        QVERIFY(waitFor(removed, readyTimeoutMs));
    }
}

bool TestDeviceScreen::connectToClient()
{
    for (int attempt = 0; attempt < connectTries; ++attempt) {
        auto guard = std::make_unique<CommandGuard>(
            std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_localPort),
            CommandGuard::Target::Emulator);
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

std::optional<QByteArray> TestDeviceScreen::adb(const QStringList &arguments, int timeoutMs) const
{
    QProcess process;
    process.start(m_adbPath, QStringList{QStringLiteral("-s"), m_serial} + arguments);
    if (!process.waitForFinished(timeoutMs) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

std::optional<QString> TestDeviceScreen::secureSetting(const QString &name) const
{
    const std::optional<QByteArray> value =
        adb({QStringLiteral("shell"), QStringLiteral("settings"), QStringLiteral("get"),
             QStringLiteral("secure"), name});
    if (!value)
        return std::nullopt;
    return QString::fromUtf8(value->trimmed());
}

bool TestDeviceScreen::putSecureSetting(const QString &name, const QString &value) const
{
    Q_ASSERT(m_emulator);
    // "null" is how Android prints a setting that was never set.
    if (value == QLatin1StringView("null")) {
        return adb({QStringLiteral("shell"), QStringLiteral("settings"), QStringLiteral("delete"),
                    QStringLiteral("secure"), name})
            .has_value();
    }
    return adb({QStringLiteral("shell"), QStringLiteral("settings"), QStringLiteral("put"),
                QStringLiteral("secure"), name, value})
        .has_value();
}

void TestDeviceScreen::restoreAccessibility()
{
    if (m_savedServices) {
        QVERIFY(
            putSecureSetting(QStringLiteral("enabled_accessibility_services"), *m_savedServices));
        QCOMPARE(
            secureSetting(QStringLiteral("enabled_accessibility_services")).value_or(QString()),
            *m_savedServices);
        m_savedServices.reset();
    }
    if (m_savedEnabled) {
        QVERIFY(putSecureSetting(QStringLiteral("accessibility_enabled"), *m_savedEnabled));
        m_savedEnabled.reset();
    }
}

QString TestDeviceScreen::topActivity() const
{
    const std::optional<QByteArray> dump =
        adb({QStringLiteral("shell"), QStringLiteral("dumpsys"), QStringLiteral("activity"),
             QStringLiteral("activities")});
    if (!dump)
        return {};
    const QList<QByteArray> lines = dump->split('\n');
    for (const QByteArray &line : lines) {
        if (line.contains("topResumedActivity"))
            return QString::fromUtf8(line.trimmed());
    }
    return {};
}

QString TestDeviceScreen::focusedWindow() const
{
    const std::optional<QByteArray> dump =
        adb({QStringLiteral("shell"), QStringLiteral("dumpsys"), QStringLiteral("window")});
    if (!dump)
        return {};
    // While the screen is captured, the virtual display of the capture is
    // listed as well, and it has no focus of its own.
    bool builtIn = false;
    const QList<QByteArray> lines = dump->split('\n');
    for (const QByteArray &line : lines) {
        if (line.contains("Display: mDisplayId="))
            builtIn = line.contains("mDisplayId=0 ");
        else if (builtIn && line.contains("mCurrentFocus="))
            return QString::fromUtf8(line.trimmed());
    }
    return {};
}

QList<UiNode> TestDeviceScreen::screenNodes() const
{
    static const QRegularExpression node(QStringLiteral("<node [^>]*>"));
    static const QRegularExpression textAttribute(QStringLiteral("text=\"([^\"]*)\""));
    static const QRegularExpression idAttribute(QStringLiteral("resource-id=\"([^\"]*)\""));
    static const QRegularExpression bounds(
        QStringLiteral("bounds=\"\\[(\\d+),(\\d+)\\]\\[(\\d+),(\\d+)\\]\""));
    const std::optional<QByteArray> xml =
        adb({QStringLiteral("exec-out"), QStringLiteral("uiautomator"), QStringLiteral("dump"),
             QStringLiteral("/dev/tty")});
    QList<UiNode> nodes;
    if (!xml)
        return nodes;
    auto it = node.globalMatch(QString::fromUtf8(*xml));
    while (it.hasNext()) {
        const QString element = it.next().captured(0);
        const QRegularExpressionMatch box = bounds.match(element);
        if (!box.hasMatch())
            continue;
        const QString id = idAttribute.match(element).captured(1);
        nodes.append({textAttribute.match(element).captured(1), id.section(u'/', -1),
                      QPoint((box.captured(1).toInt() + box.captured(3).toInt()) / 2,
                             (box.captured(2).toInt() + box.captured(4).toInt()) / 2)});
    }
    return nodes;
}

void TestDeviceScreen::tap(const QPoint &point) const
{
    static_cast<void>(adb({QStringLiteral("shell"), QStringLiteral("input"), QStringLiteral("tap"),
                           QString::number(point.x()), QString::number(point.y())}));
}

void TestDeviceScreen::dismissLeftoverQuestions() const
{
    for (int i = 0; i < 5 && topActivity().contains(QLatin1StringView("mediaprojection")); ++i) {
        static_cast<void>(adb({QStringLiteral("shell"), QStringLiteral("input"),
                               QStringLiteral("keyevent"), QStringLiteral("KEYCODE_BACK")}));
    }
}

bool TestDeviceScreen::allowCapture(const std::function<bool()> &done) const
{
    static const QStringList confirmations = {QStringLiteral("Share screen"),
                                              QStringLiteral("Start now"), QStringLiteral("Start")};
    return QTest::qWaitFor(
        [&]() {
            if (done())
                return true;
            const QList<UiNode> nodes = screenNodes();
            const auto find = [&nodes](const auto &matches) -> std::optional<UiNode> {
                for (const UiNode &candidate : nodes) {
                    if (matches(candidate))
                        return candidate;
                }
                return std::nullopt;
            };
            const std::optional<UiNode> modes = find([](const UiNode &candidate) {
                return candidate.resourceId == QLatin1StringView("screen_share_mode_options");
            });
            const std::optional<UiNode> whole = find([](const UiNode &candidate) {
                return candidate.text == QLatin1StringView("Share entire screen");
            });
            const std::optional<UiNode> confirm = find([](const UiNode &candidate) {
                return candidate.resourceId == QLatin1StringView("button1") &&
                       confirmations.contains(candidate.text);
            });
            // Android 14 and later first offer a single app; the test wants the whole screen.
            if (modes && !whole)
                tap(modes->centre);
            else if (whole && !modes)
                tap(whole->centre);
            else if (confirm)
                tap(confirm->centre);
            return done();
        },
        consentTimeoutMs);
}

void TestDeviceScreen::accessibilityServiceStateIsRead()
{
    const std::optional<QString> services =
        secureSetting(QStringLiteral("enabled_accessibility_services"));
    QVERIFY2(services, "The accessibility setting cannot be read");
    // Only whether the client's service is on; the other services stay unnamed.
    qInfo() << "Accessibility service of the client:"
            << (listsInputService(*services) ? "on" : "off");
    if (!m_emulator) {
        QSKIP("On a phone that is not an emulator the screen is neither captured nor controlled: "
              "the capture asks the owner of the phone, and input acts on what it shows");
    }
}

void TestDeviceScreen::screenIsMirroredAndControlledOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("The screen is mirrored and controlled on the emulator only");
    for (const auto &[phonePort, local] : {std::pair<quint16, quint16 *>{clientPort, &m_localPort},
                                           {ScreenMirrorClient::screenPort, &m_screenPort}}) {
        const QFuture<Result<quint16>> forward = m_adb->forward(m_serial, phonePort);
        QVERIFY(waitFor(forward, readyTimeoutMs));
        QVERIFY2(forward.result().hasValue(), "adb forward failed");
        *local = forward.result().value();
    }
    const QFuture<Result<void>> started = m_adb->startClient(m_serial);
    QVERIFY(waitFor(started, readyTimeoutMs));
    QVERIFY2(started.result().hasValue(), "Starting the client failed");

    // The client takes input only through its accessibility service.
    const std::optional<QString> services =
        secureSetting(QStringLiteral("enabled_accessibility_services"));
    const std::optional<QString> enabled = secureSetting(QStringLiteral("accessibility_enabled"));
    QVERIFY(services && enabled);
    if (!listsInputService(*services)) {
        m_savedServices = services;
        m_savedEnabled = enabled;
        const QString all = *services == QLatin1StringView("null") || services->isEmpty()
                                ? inputService()
                                : *services + u':' + inputService();
        QVERIFY(putSecureSetting(QStringLiteral("enabled_accessibility_services"), all));
        QVERIFY(putSecureSetting(QStringLiteral("accessibility_enabled"), QStringLiteral("1")));
        qInfo() << "Accessibility service of the client switched on for the test";
    }

    m_connection = std::make_unique<PhoneConnection>();
    QVERIFY(connectToClient());
    m_client = std::make_unique<ScreenMirrorClient>(m_connection.get());
    // The guard hides the transport, so the screen channel is named here.
    m_client->setEndpointResolver([this](quint16 phonePort) -> std::optional<Endpoint> {
        if (phonePort == ScreenMirrorClient::screenPort)
            return Endpoint{QStringLiteral("127.0.0.1"), m_screenPort};
        return std::nullopt;
    });
    m_mirror = std::make_unique<ScreenMirror>(m_client.get());
    m_mirror->setConnected(true);
    QVERIFY(QTest::qWaitFor(
        [this]() {
            const QFuture<Result<ScreenMirrorClient::Control>> checked = m_client->checkControl();
            return waitFor(checked, readyTimeoutMs) && checked.result().hasValue() &&
                   checked.result().value() == ScreenMirrorClient::Control::Available;
        },
        readyTimeoutMs));

    dismissLeftoverQuestions();
    static_cast<void>(adb({QStringLiteral("shell"), QStringLiteral("input"),
                           QStringLiteral("keyevent"), QStringLiteral("KEYCODE_HOME")}));
    QSignalSpy errors(m_mirror.get(), &ScreenMirror::errorOccurred);
    QVERIFY(m_mirror->start());
    QVERIFY2(allowCapture([this]() { return m_mirror->hasFrame() || !m_mirror->isStarting(); }),
             "The phone did not start sharing its screen");
    QTRY_VERIFY_WITH_TIMEOUT(m_mirror->hasFrame(), readyTimeoutMs);
    if (!errors.isEmpty())
        QFAIL(qPrintable(errors.first().first().toString()));
    QVERIFY(m_mirror->isActive());
    const QSize frame = m_mirror->frameSize();
    qInfo() << "Screen picture" << frame;
    QCOMPARE(frame, m_client->screenSize());
    QVERIFY(frame.width() >= 240 && frame.height() >= 240);
    const std::optional<QByteArray> size =
        adb({QStringLiteral("shell"), QStringLiteral("wm"), QStringLiteral("size")});
    QVERIFY(size);
    static const QRegularExpression sizePattern(QStringLiteral("(\\d+)x(\\d+)"));
    const QRegularExpressionMatch physical = sizePattern.match(QString::fromUtf8(*size));
    QVERIFY(physical.hasMatch());
    const QSize display(physical.captured(1).toInt(), physical.captured(2).toInt());
    QVERIFY2(
        frame == display || frame == display.transposed(),
        qPrintable(QStringLiteral("display %1x%2").arg(display.width()).arg(display.height())));

    // Back leaves Settings, which the test opened, as the phone's own key does.
    // The notification shade, which the phone can open around the capture,
    // would keep the focus and take the key.
    static_cast<void>(adb({QStringLiteral("shell"), QStringLiteral("cmd"),
                           QStringLiteral("statusbar"), QStringLiteral("collapse")}));
    QVERIFY(adb({QStringLiteral("shell"), QStringLiteral("am"), QStringLiteral("start"),
                 QStringLiteral("-n"), QStringLiteral("com.android.settings/.Settings")}));
    QTRY_VERIFY_WITH_TIMEOUT(
        topActivity().contains(QLatin1StringView("com.android.settings/.Settings ")),
        readyTimeoutMs);
    // Back goes to the window that has the focus, which follows the activity a little later.
    QTRY_VERIFY2_WITH_TIMEOUT(focusedWindow().contains(QLatin1StringView("com.android.settings/")),
                              qPrintable(focusedWindow()), readyTimeoutMs);
    const QString settings = topActivity();
    const QFuture<Result<void>> back = m_client->pressKey(ScreenMirrorClient::Key::Back);
    QVERIFY(waitFor(back, stepTimeoutMs));
    QVERIFY2(back.result().hasValue(), qPrintable(back.result().error().message()));
    QTRY_VERIFY2_WITH_TIMEOUT(topActivity() != settings,
                              qPrintable(QStringLiteral("still %1").arg(settings)), readyTimeoutMs);
    qInfo() << "Back left" << settings.section(u' ', -2, -2) << "for"
            << topActivity().section(u' ', -2, -2);

    QTemporaryDir local;
    QVERIFY(local.isValid());
    const QString shot = local.filePath(QStringLiteral("screen.png"));
    QSignalSpy saved(m_mirror.get(), &ScreenMirror::infoMessage);
    QVERIFY(m_mirror->saveScreenshot(QUrl::fromLocalFile(shot)));
    QVERIFY(saved.wait(stepTimeoutMs));
    const QImage picture(shot);
    QCOMPARE(picture.size(), frame);

    m_mirror->stop();
    QCOMPARE(m_client->state(), ScreenMirrorClient::State::Idle);
    QVERIFY(!m_mirror->hasFrame());
    QVERIFY(m_guard && m_guard->blocked().isEmpty());
    QCOMPARE(m_connection->state(), State::Ready);
}

QTEST_MAIN(TestDeviceScreen)

#include "tst_device_screen.moc"
