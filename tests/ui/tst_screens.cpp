#include "ui/AppController.h"
#include "ui/DeviceStatusModel.h"
#include "ui/Settings.h"

#include "FakePhone.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

#include <memory>

Q_IMPORT_QML_PLUGIN(QPhoneManager_UiPlugin)

using qpm::test::FakePhone;
using qpm::ui::AppController;
using qpm::ui::Settings;

namespace {
constexpr int connectTimeoutMs = 10000;
}

// The main views and dialogs in German, light and dark, against the fake
// phone. With QPM_SCREENSHOT_DIR set, the pictures are kept there for a look
// at contrast and focus.
class TestScreens : public QObject
{
    Q_OBJECT

public:
    explicit TestScreens(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void viewsAreGermanAndRenderInBothSchemes_data();
    void viewsAreGermanAndRenderInBothSchemes();

private:
    [[nodiscard]] QObject *dialog(const QString &name) const;
    [[nodiscard]] bool save(const QString &name);

    QTemporaryDir m_home;
    QTemporaryDir m_shots;
    QString m_shotDir;
    QTranslator m_translator;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    Settings *m_settings = nullptr;
    AppController *m_controller = nullptr;
};

void TestScreens::initTestCase()
{
    QVERIFY(m_home.isValid());
    QVERIFY(m_shots.isValid());
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("config"))));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("data"))));
    QCoreApplication::setOrganizationName(QStringLiteral("QPhoneManagerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_screens"));
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    QVERIFY(m_translator.load(QStringLiteral(QPM_TRANSLATIONS_DIR "/qphonemanager_de.qm")));
    QCoreApplication::installTranslator(&m_translator);
    if (qEnvironmentVariableIsSet("QPM_SOFTWARE_RENDERING"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    m_shotDir = qEnvironmentVariable("QPM_SCREENSHOT_DIR", m_shots.path());

    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->config().model = QStringLiteral("Pixel 8");
    m_phone->config().manufacturer = QStringLiteral("Google");
    m_phone->config().scriptedReplies.insert(
        QStringLiteral("AT*SOPS"),
        {QByteArrayLiteral("AT*SOPS\r\n*SOPS: Beispielnetz,26201,de\r\nOK\r\n")});
    m_phone->config().scriptedReplies.insert(
        QStringLiteral("AT*SIM"),
        {QByteArrayLiteral("AT*SIM\r\n*SIM: 0,,Privat,Beispielnetz,VS\r\nOK\r\n")});

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->loadFromModule("QPhoneManager.Ui", "Main");
    QCOMPARE(m_engine->rootObjects().size(), 1);
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
    QVERIFY(m_window);
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    m_settings = m_engine->singletonInstance<Settings *>("QPhoneManager.Ui", "Settings");
    m_controller = m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    QVERIFY(m_settings);
    QVERIFY(m_controller);

    QVERIFY(m_controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port())));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->isConnected(), connectTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->status()->display().isEmpty() &&
                                 m_controller->status()->cpuPercent() >= 0 &&
                                 !m_controller->status()->simCards().isEmpty(),
                             connectTimeoutMs);
}

void TestScreens::cleanupTestCase()
{
    if (m_controller)
        m_controller->disconnectPhone();
    m_window = nullptr;
    m_engine.reset();
    QCoreApplication::removeTranslator(&m_translator);
    if (qEnvironmentVariableIsSet("QPM_SCREENSHOT_DIR"))
        qInfo() << "Screenshots kept in" << m_shotDir;
}

QObject *TestScreens::dialog(const QString &name) const
{
    return m_window->findChild<QObject *>(name);
}

bool TestScreens::save(const QString &name)
{
    const QImage image = m_window->grabWindow();
    return !image.isNull() && image.save(QDir(m_shotDir).filePath(name + QStringLiteral(".png")));
}

void TestScreens::viewsAreGermanAndRenderInBothSchemes_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("germanTitle");

    QTest::newRow("overview") << QString() << QStringLiteral("Übersicht");
    QTest::newRow("connect") << QStringLiteral("connectDialog")
                             << QStringLiteral("Handy verbinden");
    QTest::newRow("pin") << QStringLiteral("pinDialog") << QStringLiteral("PIN eingeben");
    QTest::newRow("qr") << QStringLiteral("qrDialog") << QStringLiteral("Mit QR-Code verbinden");
    QTest::newRow("settings") << QStringLiteral("settingsDialog")
                              << QStringLiteral("Einstellungen");
}

void TestScreens::viewsAreGermanAndRenderInBothSchemes()
{
    QFETCH(QString, view);
    QFETCH(QString, germanTitle);
    QObject *popup = view.isEmpty() ? nullptr : dialog(view);
    if (popup) {
        if (view == QLatin1StringView("connectDialog"))
            popup->setProperty("searchOnOpen", false);
        if (view == QLatin1StringView("pinDialog")) {
            popup->setProperty("wrongPin", true);
            popup->setProperty("remainingTries", 9);
        }
        QMetaObject::invokeMethod(popup, "open");
        QTRY_VERIFY(popup->property("opened").toBool());
        QCOMPARE(popup->property("title").toString(), germanTitle);
    } else {
        QVERIFY(view.isEmpty());
        QQuickItem *header = m_window->property("header").value<QQuickItem *>();
        QVERIFY(header);
        QCOMPARE(header->property("title").toString(), germanTitle);
    }

    const QString name = view.isEmpty() ? QStringLiteral("overview") : view;
    m_settings->setColorScheme(Settings::ColorScheme::Light);
    QVERIFY(save(name + QStringLiteral("-light")));
    m_settings->setColorScheme(Settings::ColorScheme::Dark);
    QVERIFY(save(name + QStringLiteral("-dark")));
    m_settings->setColorScheme(Settings::ColorScheme::System);

    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    QVERIFY(m_controller->isConnected());
}

QTEST_MAIN(TestScreens)

#include "tst_screens.moc"
