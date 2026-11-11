#include "core/codec/PhoneTime.h"
#include "core/codec/PhoneVCard.h"
#include "ui/AppController.h"
#include "ui/DeviceStatusModel.h"
#include "ui/Settings.h"
#include "ui/calls/CallsArea.h"
#include "ui/contacts/ContactsArea.h"
#include "ui/messages/MessagesArea.h"

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

// Calls of the made-up people above and of numbers no contact has.
QList<qpm::test::FakeCall> exampleCalls()
{
    struct Call
    {
        quint32 id;
        const char *storage;
        int daysAgo;
        QTime time;
        const char *number;
        const char *name;
        qint64 duration;
    };
    const QList<Call> calls = {
        {1, "RC", 0, QTime(9, 12), "+49 151 2000001", "", 65},
        {2, "MC", 0, QTime(8, 40), "+49 30 2000002", "", 0},
        {3, "DC", 0, QTime(7, 55), "+49 170 2000003", "", 312},
        {4, "MC", 1, QTime(19, 3), "", "", 0},
        {5, "RC", 1, QTime(14, 20), "+49 89 2000099", "Praxis Dr. Klein", 1832},
        {6, "DC", 3, QTime(11, 5), "+49 221 3000004", "", 47},
        {7, "DC", 9, QTime(16, 45), "+49 40 2000006", "", 3725},
    };
    QList<qpm::test::FakeCall> result;
    for (const Call &call : calls) {
        qpm::test::FakeCall entry;
        entry.id = call.id;
        entry.storage = QString::fromLatin1(call.storage);
        entry.time = qpm::core::codec::formatCallLogTime(
            QDateTime(QDate::currentDate().addDays(-call.daysAgo), call.time));
        entry.number = QString::fromUtf8(call.number);
        entry.name = QString::fromUtf8(call.name);
        entry.duration = call.duration;
        result.append(entry);
    }
    return result;
}

// Made-up messages with the made-up people above, a sender name and a
// scheduled greeting.
QList<qpm::test::FakeSyncObject> exampleMessages()
{
    const auto stamp = [](int daysAgo, QTime time) {
        return QDateTime(QDate::currentDate().addDays(-daysAgo), time)
            .toUTC()
            .toString(QStringLiteral("yyyyMMddTHHmmssZ"))
            .toLatin1();
    };
    const auto sms = [&stamp](quint32 luid, bool incoming, const char *number, const char *text,
                              int daysAgo, QTime time, bool read) {
        QByteArray data = "BEGIN:VMESSAGE\r\nTYP:SMS;" + QByteArray(incoming ? "IN" : "OUT") +
                          "\r\n";
        data += incoming ? "BOX:INBOX\r\nREAD:" + QByteArray(read ? "1" : "0") + "\r\n"
                         : QByteArray("BOX:SENT\r\n");
        data += "DATE:" + stamp(daysAgo, time) + "\r\nNUMBER:" + QByteArray(number) + "\r\n";
        data += "BODY;CHARSET=UTF-8:" + QByteArray(text) + "\r\n";
        data += "X-IRMC-LUID:" + QByteArray::number(luid, 16).rightJustified(12, '0').toUpper() +
                "\r\nEND:VMESSAGE\r\n";
        return qpm::test::FakeSyncObject{luid, data};
    };
    QImage picture(320, 200, QImage::Format_RGB32);
    picture.fill(QColor(0, 106, 106));
    for (int y = 120; y < 200; ++y) {
        for (int x = 0; x < 320; ++x)
            picture.setPixelColor(x, y, QColor(90, 160, 60));
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    const QByteArray photoMessage =
        "BEGIN:VMESSAGE\r\nTYP:MMS;IN\r\nBOX:INBOX\r\nREAD:1\r\nDATE:" +
        stamp(0, QTime(8, 5)) +
        "\r\nNUMBER:+49 170 2000003\r\nSUBJECT;CHARSET=UTF-8:Ausflug zum See\r\n"
        "ATT;NAME=text_0.txt;TYPE=text/plain;CHARSET=UTF-8:Schöne Grüße vom See!\r\n"
        "ATT;NAME=See.png;TYPE=image/png;SIZE=" +
        QByteArray::number(png.size()) + ";ENCODING=BASE64:" + png.toBase64() +
        "\r\n\r\nX-IRMC-LUID:000010000020\r\nEND:VMESSAGE\r\n";
    return {
        sms(21, true, "+49 151 2000001", "Hast du am Samstag Zeit? Wir wollen grillen.", 1,
            QTime(18, 2), true),
        sms(22, false, "+49 151 2000001", "Ja, gerne! Was soll ich mitbringen?", 1, QTime(18, 10),
            true),
        sms(23, true, "+49 151 2000001", "Einen Salat wäre super.", 1, QTime(18, 12), true),
        sms(24, true, "+49 151 2000001", "Treffpunkt 15 Uhr bei uns, Gartentor ist offen.", 0,
            QTime(9, 30), false),
        sms(25, true, "Paketdienst", "Ihr Paket kommt heute zwischen 10 und 14 Uhr.", 2,
            QTime(7, 45), true),
        sms(26, false, "+49 30 2000002", "Danke für die Unterlagen, ich melde mich morgen.", 3,
            QTime(16, 20), true),
        {0x10000020, photoMessage},
        {0x20000001, "BEGIN:VMESSAGE\r\nTYP:SMS;OUT\r\nBOX:SHEDULED\r\n"
                     "RECIPIENT;CHARSET=UTF-8:Erika Mustermann;+49 151 2000004;SHEDULED;;\r\n"
                     "DATE:" + stamp(-5, QTime(8, 0)) +
                         "\r\nBODY;CHARSET=UTF-8:Alles Gute zum Geburtstag, Erika!\r\n"
                         "X-IRMC-LUID:000020000001\r\nEND:VMESSAGE\r\n"},
    };
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
    void callViewsAreGermanAndRenderInBothSchemes_data();
    void callViewsAreGermanAndRenderInBothSchemes();
    void messageViewsAreGermanAndRenderInBothSchemes_data();
    void messageViewsAreGermanAndRenderInBothSchemes();

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
    m_phone->setCalls(exampleCalls());
    m_phone->setObjects(QStringLiteral("msg"), exampleMessages());

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
    for (const QString &pageName : {QStringLiteral("contactsPage"), QStringLiteral("callsPage"),
                                    QStringLiteral("messagesPage")}) {
        QQuickItem *page = item(pageName);
        if (QObject *found = page ? page->findChild<QObject *>(name) : nullptr)
            return found;
    }
    return nullptr;
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

void TestScreens::callViewsAreGermanAndRenderInBothSchemes_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("germanTitle");

    QTest::newRow("list and detail") << QStringLiteral("calls") << QStringLiteral("Anrufe");
    QTest::newRow("missed only") << QStringLiteral("callsMissed") << QStringLiteral("Anrufe");
    QTest::newRow("delete one") << QStringLiteral("deleteCall")
                                << QStringLiteral("Anruf mit Bernd Albrecht löschen?");
    QTest::newRow("delete shown") << QStringLiteral("deleteShownCalls")
                                  << QStringLiteral("7 Anrufe löschen?");
    QTest::newRow("export") << QStringLiteral("exportCallsDialog")
                            << QStringLiteral("Anrufe exportieren");
}

void TestScreens::callViewsAreGermanAndRenderInBothSchemes()
{
    QFETCH(QString, view);
    QFETCH(QString, germanTitle);
    if (m_controller->contacts()->list()->totalCount() == 0) {
        const QFuture<qpm::core::Result<qpm::core::store::ContactChanges>> contacts =
            m_controller->contacts()->sync();
        QTRY_VERIFY_WITH_TIMEOUT(contacts.isFinished(), connectTimeoutMs);
        QVERIFY(contacts.result().hasValue());
    }
    qpm::ui::CallsArea *area = m_controller->calls();
    if (area->list()->totalCount() == 0) {
        const QFuture<qpm::core::Result<qpm::core::store::CallChanges>> calls = area->sync();
        QTRY_VERIFY_WITH_TIMEOUT(calls.isFinished(), connectTimeoutMs);
        QVERIFY(calls.result().hasValue());
    }
    m_window->setProperty("currentSection", 2);
    QQuickItem *header = m_window->property("header").value<QQuickItem *>();
    QCOMPARE(header->property("title").toString(), QStringLiteral("Anrufe"));
    QQuickItem *page = item(QStringLiteral("callsPage"));
    QVERIFY(page);
    area->list()->setKindFilter(view == QLatin1StringView("callsMissed")
                                    ? qpm::ui::CallListModel::Kind::Missed
                                    : qpm::ui::CallListModel::Kind::All);
    area->setCurrentId(2);
    QTRY_VERIFY(item(QStringLiteral("callTitle")) &&
                item(QStringLiteral("callTitle"))->property("text").toString() ==
                    QStringLiteral("Bernd Albrecht"));

    // The highlight of the tab bar slides to the chosen tab; the picture waits for it.
    QQuickItem *tabs = item(QStringLiteral("callKindTabs"));
    QVERIFY(tabs);
    const auto *tabList = tabs->property("contentItem").value<QQuickItem *>();
    QVERIFY(tabList);
    QTRY_VERIFY([tabList]() {
        const auto *highlight = tabList->property("highlightItem").value<QQuickItem *>();
        const auto *current = tabList->property("currentItem").value<QQuickItem *>();
        return highlight && current && qFuzzyCompare(highlight->x() + 1, current->x() + 1);
    }());

    QObject *popup = nullptr;
    if (view == QLatin1StringView("calls") || view == QLatin1StringView("callsMissed")) {
        // A file dialog of an earlier picture may have taken the window's focus.
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        QQuickItem *list = item(QStringLiteral("callList"));
        QVERIFY(list);
        list->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(list->hasActiveFocus());
    } else {
        popup = dialog(view == QLatin1StringView("exportCallsDialog")
                           ? view
                           : QStringLiteral("deleteCallsDialog"));
        QVERIFY(popup);
        if (view == QLatin1StringView("deleteCall"))
            QVERIFY(QMetaObject::invokeMethod(page, "confirmDelete", Q_ARG(int, 2)));
        else if (view == QLatin1StringView("deleteShownCalls"))
            QVERIFY(QMetaObject::invokeMethod(page, "confirmDeleteShown"));
        else
            QMetaObject::invokeMethod(popup, "open");
        const bool hasOpened = popup->metaObject()->indexOfProperty("opened") >= 0;
        QTRY_VERIFY(popup->property(hasOpened ? "opened" : "visible").toBool());
        QCOMPARE(popup->property("title").toString(), germanTitle);
    }
    // The client refuses to change the call log, and the list then stays as
    // it is; the question must not promise more.
    if (view == QLatin1StringView("deleteCall")) {
        const QQuickItem *text = item(QStringLiteral("deleteCallsText"));
        QVERIFY(text);
        QCOMPARE(text->property("text").toString(),
                 QStringLiteral("Das Handy löscht den Anruf nur, wenn die App es erlaubt. Sonst "
                                "bleibt er auf dem Handy und in dieser Liste."));
    }

    QVERIFY(saveBothSchemes(view));

    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    area->list()->setKindFilter(qpm::ui::CallListModel::Kind::All);
    QVERIFY(m_controller->isConnected());
}

void TestScreens::messageViewsAreGermanAndRenderInBothSchemes_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("germanTitle");

    QTest::newRow("conversation") << QStringLiteral("messages") << QStringLiteral("Anna Becker");
    QTest::newRow("picture") << QStringLiteral("messagesPicture")
                             << QStringLiteral("Clara Schmitz");
    QTest::newRow("compose") << QStringLiteral("compose") << QStringLiteral("Neue Nachricht");
    QTest::newRow("compose later") << QStringLiteral("composeScheduled")
                                   << QStringLiteral("Neue Nachricht");
    QTest::newRow("scheduled") << QStringLiteral("scheduled")
                               << QStringLiteral("Geplante Nachricht ändern");
    QTest::newRow("delete") << QStringLiteral("deleteMessagesDialog")
                            << QStringLiteral("Gespräch mit Anna Becker löschen?");
    QTest::newRow("export") << QStringLiteral("exportMessagesDialog")
                            << QStringLiteral("Gespräch exportieren");
}

void TestScreens::messageViewsAreGermanAndRenderInBothSchemes()
{
    QFETCH(QString, view);
    QFETCH(QString, germanTitle);
    if (m_controller->contacts()->list()->totalCount() == 0) {
        const QFuture<qpm::core::Result<qpm::core::store::ContactChanges>> contacts =
            m_controller->contacts()->sync();
        QTRY_VERIFY_WITH_TIMEOUT(contacts.isFinished(), connectTimeoutMs);
        QVERIFY(contacts.result().hasValue());
    }
    qpm::ui::MessagesArea *area = m_controller->messages();
    if (area->conversations()->totalCount() == 0) {
        const QFuture<qpm::core::Result<qpm::core::store::MessageChanges>> messages = area->sync();
        QTRY_VERIFY_WITH_TIMEOUT(messages.isFinished(), connectTimeoutMs);
        QVERIFY(messages.result().hasValue());
    }
    m_window->setProperty("currentSection", 3);
    QQuickItem *header = m_window->property("header").value<QQuickItem *>();
    QCOMPARE(header->property("title").toString(), QStringLiteral("Nachrichten"));
    QQuickItem *page = item(QStringLiteral("messagesPage"));
    QVERIFY(page);
    QQuickItem *tabs = item(QStringLiteral("messageViewTabs"));
    QVERIFY(tabs);
    tabs->setProperty("currentIndex", view == QLatin1StringView("scheduled") ? 1 : 0);
    const int row = view == QLatin1StringView("messagesPicture") ? 1 : 0;
    area->setCurrentThread(area->conversations()->threadKeyAt(row));

    QObject *popup = nullptr;
    if (view == QLatin1StringView("compose") || view == QLatin1StringView("composeScheduled")) {
        QVERIFY(area->composeTo(QStringLiteral("+49 30 2000002")));
        area->composer()->setText(QStringLiteral("Ich bin in zehn Minuten da. Bis gleich!"));
        area->composer()->setScheduled(view == QLatin1StringView("composeScheduled"));
        QTRY_VERIFY(item(QStringLiteral("composeTitle")) &&
                    item(QStringLiteral("composeTitle"))->property("text").toString() ==
                        germanTitle);
        QCOMPARE(item(QStringLiteral("partsText"))->property("text").toString(),
                 QStringLiteral("1 SMS"));
    } else if (view == QLatin1StringView("scheduled")) {
        QVERIFY(area->editScheduled(area->scheduled()->luidAt(0)));
        QTRY_VERIFY(item(QStringLiteral("composeTitle")) &&
                    item(QStringLiteral("composeTitle"))->property("text").toString() ==
                        germanTitle);
        QQuickItem *list = item(QStringLiteral("scheduledList"));
        QVERIFY(list);
    } else if (view == QLatin1StringView("messages") ||
               view == QLatin1StringView("messagesPicture")) {
        QTRY_VERIFY(item(QStringLiteral("conversationTitle")) &&
                    item(QStringLiteral("conversationTitle"))->property("text").toString() ==
                        germanTitle);
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        QQuickItem *list = item(QStringLiteral("conversationList"));
        QVERIFY(list);
        list->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(list->hasActiveFocus());
    } else {
        QTRY_VERIFY(item(QStringLiteral("conversationTitle")));
        popup = dialog(view);
        QVERIFY(popup);
        if (view == QLatin1StringView("deleteMessagesDialog"))
            QVERIFY(QMetaObject::invokeMethod(page, "confirmDeleteConversation"));
        else
            QVERIFY(QMetaObject::invokeMethod(page, "startExport", Q_ARG(bool, false)));
        const bool hasOpened = popup->metaObject()->indexOfProperty("opened") >= 0;
        QTRY_VERIFY(popup->property(hasOpened ? "opened" : "visible").toBool());
        QCOMPARE(popup->property("title").toString(), germanTitle);
    }
    // A picture in a message is drawn once it is loaded.
    QTRY_VERIFY([this]() {
        QQuickItem *list = item(QStringLiteral("messageList"));
        QList<QQuickItem *> pending = list ? QList<QQuickItem *>{list} : QList<QQuickItem *>();
        while (!pending.isEmpty()) {
            QQuickItem *next = pending.takeLast();
            if (next->inherits("QQuickImage") && next->isVisible() &&
                !next->property("source").toUrl().isEmpty() &&
                next->property("status").toInt() != 1) {
                return false;
            }
            pending.append(next->childItems());
        }
        return true;
    }());

    QVERIFY(saveBothSchemes(view));

    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    area->setComposing(false);
    QVERIFY(m_controller->isConnected());
}

QTEST_MAIN(TestScreens)

#include "tst_screens.moc"
