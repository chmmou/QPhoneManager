#include "core/codec/PhoneVCard.h"
#include "ui/AppController.h"
#include "ui/DeviceStatusModel.h"
#include "ui/Settings.h"
#include "ui/contacts/ContactsArea.h"

#include "FakePhone.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
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

using namespace qpm::core::codec;

ContactValue entry(quint64 dataId, int type, const QString &value)
{
    ContactValue result;
    result.dataId = dataId;
    result.type = type;
    result.value = value;
    return result;
}

QByteArray portrait(const QColor &color)
{
    QImage image(96, 96, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 90);
    return bytes;
}

// Made-up people for the pictures; none of them exists.
QList<qpm::test::FakeSyncObject> exampleContacts()
{
    struct Person
    {
        const char *given;
        const char *family;
        const char *number;
        const char *company;
        QColor photo;
    };
    const QList<Person> people = {
        {"Anna", "Becker", "+49 151 2000001", "", QColor(0, 106, 106)},
        {"Bernd", "Albrecht", "+49 30 2000002", "Albrecht & Söhne", QColor()},
        {"Clara", "Schmitz", "+49 170 2000003", "", QColor(180, 90, 40)},
        {"Erika", "Mustermann", "+49 151 2000004", "Beispiel GmbH", QColor(90, 60, 150)},
        {"Erika", "Musterman", "0151 2000004", "", QColor()},
        {"Jörg", "Weiß", "+49 221 2000005", "", QColor()},
        {"Zoë", "Örtel", "+49 40 2000006", "Hafenbüro", QColor(40, 140, 80)},
    };
    QList<qpm::test::FakeSyncObject> objects;
    quint32 luid = 1;
    for (const Person &person : people) {
        Contact contact;
        contact.luid = luid;
        contact.givenName = QString::fromUtf8(person.given);
        contact.familyName = QString::fromUtf8(person.family);
        contact.displayName = contact.givenName + u' ' + contact.familyName;
        contact.organization = QString::fromUtf8(person.company);
        contact.phones = {entry(luid * 10, phoneType::mobile, QString::fromUtf8(person.number))};
        if (luid == 4) {
            contact.title = QStringLiteral("Einkauf");
            contact.phones.append(entry(41, phoneType::work, QStringLiteral("+49 221 3000004")));
            contact.emails = {entry(42, emailType::work, QStringLiteral("erika@example.org"))};
            ContactValue address;
            address.dataId = 43;
            address.type = addressType::work;
            address.address = {QString(), QString(), QStringLiteral("Musterweg 12"),
                               QStringLiteral("Köln"), QString(), QStringLiteral("50667"),
                               QStringLiteral("Deutschland")};
            contact.addresses = {address};
            contact.birthday = QDate(1979, 4, 23);
            contact.groups = {QStringLiteral("Arbeit")};
            contact.note = QStringLiteral("Mittwochs im Homeoffice.");
            contact.accountType = QStringLiteral("com.google");
            contact.accountName = QStringLiteral("anna@example.org");
        }
        if (person.photo.isValid())
            contact.photo = portrait(person.photo);
        objects.append({luid, encodeAsPhoneSendsIt(contact)});
        ++luid;
    }
    return objects;
}

QByteArray exampleMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<accounts filter_disabled=\"0\">\r\n"
           "<account selected=\"1\" type=\"null\" name=\"null\"  default=\"1\" title=\"Phone\"/>\r\n"
           "<account selected=\"1\" type=\"com.google\" name=\"anna@example.org\"  "
           "title=\"Google\"/>\r\n</accounts>\r\n<groups>\r\n"
           "<group title=\"Arbeit\" sourceid=\"1\" account_name=\"anna@example.org\" "
           "account_type=\"com.google\"/>\r\n</groups>\r\n</metadata>";
}
} // namespace

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
    void contactViewsAreGermanAndRenderInBothSchemes_data();
    void contactViewsAreGermanAndRenderInBothSchemes();

private:
    [[nodiscard]] QObject *dialog(const QString &name) const;
    [[nodiscard]] QQuickItem *item(const QString &name) const;
    [[nodiscard]] bool save(const QString &name);
    [[nodiscard]] bool saveBothSchemes(const QString &name);

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
    m_phone->setObjects(QStringLiteral("pb"), exampleContacts());
    m_phone->setMetadata(QStringLiteral("pb"), exampleMetadata());

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

namespace {

// The visual tree, which also holds what a Loader created.
QQuickItem *findVisual(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findVisual(child, name))
            return found;
    }
    return nullptr;
}

} // namespace

QObject *TestScreens::dialog(const QString &name) const
{
    if (QObject *found = m_window->findChild<QObject *>(name))
        return found;
    QQuickItem *page = item(QStringLiteral("contactsPage"));
    return page ? page->findChild<QObject *>(name) : nullptr;
}

QQuickItem *TestScreens::item(const QString &name) const
{
    return findVisual(m_window->contentItem(), name);
}

bool TestScreens::saveBothSchemes(const QString &name)
{
    m_settings->setColorScheme(Settings::ColorScheme::Light);
    const bool light = save(name + QStringLiteral("-light"));
    m_settings->setColorScheme(Settings::ColorScheme::Dark);
    const bool dark = save(name + QStringLiteral("-dark"));
    m_settings->setColorScheme(Settings::ColorScheme::System);
    return light && dark;
}

bool TestScreens::save(const QString &name)
{
    // Without a native file dialog, as on the offscreen platform, Qt shows
    // its own one in a window of its own; that window is the picture then.
    QQuickWindow *shown = m_window;
    const QList<QWindow *> windows = QGuiApplication::topLevelWindows();
    for (QWindow *window : windows) {
        auto *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (quickWindow && quickWindow != m_window && quickWindow->isVisible())
            shown = quickWindow;
    }
    const QImage image = shown->grabWindow();
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

void TestScreens::contactViewsAreGermanAndRenderInBothSchemes_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("germanTitle");

    QTest::newRow("list and detail") << QStringLiteral("contacts") << QStringLiteral("Kontakte");
    QTest::newRow("form") << QStringLiteral("contactForm") << QStringLiteral("Kontakt bearbeiten");
    QTest::newRow("duplicates") << QStringLiteral("duplicatesDialog")
                                << QStringLiteral("Doppelte Kontakte");
    QTest::newRow("import") << QStringLiteral("importDialog")
                            << QStringLiteral("Kontakte importieren");
}

void TestScreens::contactViewsAreGermanAndRenderInBothSchemes()
{
    QFETCH(QString, view);
    QFETCH(QString, germanTitle);
    qpm::ui::ContactsArea *area = m_controller->contacts();
    if (area->list()->totalCount() == 0) {
        const QFuture<qpm::core::Result<qpm::core::store::ContactChanges>> pulled = area->sync();
        QTRY_VERIFY_WITH_TIMEOUT(pulled.isFinished(), connectTimeoutMs);
        QVERIFY(pulled.result().hasValue());
    }
    m_window->setProperty("currentSection", 1);
    area->setCurrentLuid(4);
    QQuickItem *header = m_window->property("header").value<QQuickItem *>();
    QCOMPARE(header->property("title").toString(), QStringLiteral("Kontakte"));
    // The longest German section name fits without being cut.
    const QQuickItem *longest = item(QStringLiteral("section-notifications"));
    QVERIFY(longest);
    const QQuickItem *label = longest->property("contentItem").value<QQuickItem *>();
    QVERIFY(label);
    QTRY_VERIFY(label->implicitWidth() <= label->width() + 0.5);
    QQuickItem *page = item(QStringLiteral("contactsPage"));
    QVERIFY(page);
    page->setProperty("editing", false);

    QObject *popup = nullptr;
    if (view == QLatin1StringView("contacts")) {
        QTRY_VERIFY(item(QStringLiteral("contactName")) &&
                    item(QStringLiteral("contactName"))->property("text").toString() ==
                        QStringLiteral("Erika Mustermann"));
        QQuickItem *list = item(QStringLiteral("contactList"));
        QVERIFY(list);
        list->forceActiveFocus(Qt::TabFocusReason);
    } else if (view == QLatin1StringView("contactForm")) {
        QVERIFY(QMetaObject::invokeMethod(page, "startEdit"));
        QTRY_VERIFY(item(QStringLiteral("contactForm")));
        QQuickItem *form = item(QStringLiteral("contactForm"));
        bool headingFound = false;
        QList<QQuickItem *> pending = {form};
        while (!pending.isEmpty() && !headingFound) {
            QQuickItem *next = pending.takeLast();
            headingFound = next->property("text").toString() == germanTitle;
            pending.append(next->childItems());
        }
        QVERIFY(headingFound);
    } else {
        popup = dialog(view);
        QVERIFY(popup);
        if (view == QLatin1StringView("duplicatesDialog")) {
            QSignalSpy found(area, &qpm::ui::ContactsArea::duplicatesFound);
            QVERIFY(area->findDuplicates());
            QTRY_COMPARE(found.count(), 1);
        }
        QMetaObject::invokeMethod(popup, "open");
        // A popup is only drawn in full once its opening transition is over.
        const bool hasOpened = popup->metaObject()->indexOfProperty("opened") >= 0;
        QTRY_VERIFY(popup->property(hasOpened ? "opened" : "visible").toBool());
        QCOMPARE(popup->property("title").toString(), germanTitle);
    }

    QVERIFY(saveBothSchemes(view));

    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    page->setProperty("editing", false);
    QVERIFY(m_controller->isConnected());
}

QTEST_MAIN(TestScreens)

#include "tst_screens.moc"
