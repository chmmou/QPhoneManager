#include "core/codec/PhoneTime.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/PhoneVCard.h"
#include "core/codec/VNote.h"
#include "core/service/ScreenMirrorClient.h"
#include "ui/AppController.h"
#include "ui/DeviceStatusModel.h"
#include "ui/Settings.h"
#include "ui/apps/AppListModel.h"
#include "ui/calls/CallsArea.h"
#include "ui/contacts/ContactsArea.h"
#include "ui/files/FileBrowserModel.h"
#include "ui/files/TransferQueue.h"
#include "ui/messages/MessagesArea.h"
#include "ui/messages/MessageComposer.h"
#include "ui/organizer/CalendarArea.h"
#include "ui/organizer/NotesArea.h"
#include "ui/organizer/TaskListModel.h"
#include "ui/organizer/TasksArea.h"
#include "ui/screen/ScreenMirror.h"

#include "FakePhone.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QLocale>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QTranslator>
#include <QUrl>

#include <cstdlib>
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

QByteArray calendarMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<calendars filter_disabled=\"0\">\r\n"
           "<calendar selected=\"1\" default=\"1\" url=\"anna@example.org@com.google/NULL\" "
           "title=\"Familie\" color=\"FF33B679\" hidden=\"0\" timezone=\"Europe/Berlin\" "
           "access_level=\"700\"/>\r\n"
           "<calendar selected=\"1\" url=\"anna@example.org@com.google/NULL\" title=\"Arbeit\" "
           "color=\"FF4285F4\" hidden=\"0\" timezone=\"Europe/Berlin\" access_level=\"700\"/>\r\n"
           "<calendar selected=\"0\" url=\"anna@example.org@com.google/NULL\" title=\"Feiertage\" "
           "color=\"FF0B8043\" hidden=\"0\" timezone=\"Europe/Berlin\" access_level=\"200\"/>\r\n"
           "</calendars>\r\n<tasksupport>1</tasksupport>\r\n<task_categories>\r\n"
           "<category name=\"Haushalt\" color=\"FF8800\" id=\"1\"/>\r\n"
           "<category name=\"Büro\" color=\"4285F4\" id=\"2\"/>\r\n"
           "</task_categories>\r\n</metadata>";
}

CalendarEvent appointment(quint32 luid, const QString &summary, const QDateTime &start, int minutes,
                          const QString &calendar)
{
    CalendarEvent event;
    event.luid = luid;
    event.summary = summary;
    event.start = start;
    event.end = start.addSecs(minutes * 60LL);
    event.calendarUrl = QStringLiteral("anna@example.org@com.google/NULL");
    event.calendarTitle = calendar;
    return event;
}

QDateTime berlin(int day, int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 11, day), QTime(hour, minute),
                     QTimeZone(QByteArrayLiteral("Europe/Berlin")))
        .toUTC();
}

// Made-up appointments, tasks and notes for the pictures.
QList<qpm::test::FakeSyncObject> exampleOrganizer()
{
    CalendarEvent meeting = appointment(0x10000001, QStringLiteral("Elternabend"), berlin(12, 19),
                                        90, QStringLiteral("Familie"));
    meeting.location = QStringLiteral("Grundschule am Park");
    meeting.description = QStringLiteral("Bitte die Unterlagen zur Klassenfahrt mitbringen.");
    meeting.alarms = {meeting.start.addSecs(-1800)};
    CalendarEvent sport = appointment(0x10000002, QStringLiteral("Lauftreff"), berlin(3, 18), 60,
                                      QStringLiteral("Familie"));
    sport.rrule = QStringLiteral("FREQ=WEEKLY;BYDAY=TU");
    CalendarEvent review = appointment(0x10000003, QStringLiteral("Quartalsbericht"),
                                       berlin(12, 9, 30), 60, QStringLiteral("Arbeit"));
    review.color = 0xFFE67C73;
    CalendarEvent trip = appointment(0x10000004, QStringLiteral("Kurzurlaub Ostsee"), berlin(20, 0),
                                     0, QStringLiteral("Familie"));
    trip.allDay = true;
    trip.start = QDateTime(QDate(2026, 11, 20), QTime(0, 0), QTimeZone::UTC);
    trip.end = QDateTime(QDate(2026, 11, 23), QTime(0, 0), QTimeZone::UTC);
    CalendarEvent tyres =
        appointment(0x10000005, QStringLiteral("Winterreifen beim Autohaus am Stadtrand abholen"),
                    berlin(13, 16), 30, QStringLiteral("Familie"));
    CalendarTask windows;
    windows.luid = 5;
    windows.summary = QStringLiteral("Fenster putzen");
    windows.due = QDate(2026, 11, 20);
    windows.categories = {QStringLiteral("Haushalt")};
    CalendarTask tax;
    tax.luid = 6;
    tax.summary = QStringLiteral("Steuererklärung abgeben");
    tax.start = QDate(2026, 10, 15);
    tax.due = QDate(2026, 11, 2);
    tax.priority = TaskPriority::High;
    tax.categories = {QStringLiteral("Büro")};
    CalendarTask done;
    done.luid = 7;
    done.summary = QStringLiteral("Winterreifen aufziehen");
    done.completed = true;
    return {{meeting.luid, encodeEventAsPhoneSendsIt(meeting)},
            {sport.luid, encodeEventAsPhoneSendsIt(sport)},
            {review.luid, encodeEventAsPhoneSendsIt(review)},
            {trip.luid, encodeEventAsPhoneSendsIt(trip)},
            {tyres.luid, encodeEventAsPhoneSendsIt(tyres)},
            {windows.luid, encodeTaskAsPhoneSendsIt(windows)},
            {tax.luid, encodeTaskAsPhoneSendsIt(tax)},
            {done.luid, encodeTaskAsPhoneSendsIt(done)}};
}

QList<qpm::test::FakeSyncObject> exampleNotes()
{
    Note shopping;
    shopping.luid = 1;
    shopping.body = QStringLiteral("Einkaufsliste\nMilch, Brot, Äpfel, Kaffee");
    shopping.lastModified = berlin(10, 8);
    shopping.categories = {QStringLiteral("Privat")};
    Note ideas;
    ideas.luid = 2;
    ideas.body = QStringLiteral("Geschenkideen\nBuch über Gartenpflege, Konzertkarten");
    ideas.lastModified = berlin(11, 20);
    return {{1, encodeNoteAsPhoneSendsIt(shopping)}, {2, encodeNoteAsPhoneSendsIt(ideas)}};
}

QByteArray appIcon(const QColor &color)
{
    QImage image(48, 48, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int y = 4; y < 44; ++y) {
        for (int x = 4; x < 44; ++x)
            image.setPixelColor(x, y, color);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// Made-up apps for the pictures, with plain coloured icons.
QList<qpm::test::FakeApp> exampleApps()
{
    struct App
    {
        const char *package;
        const char *label;
        const char *version;
        bool user;
        qsizetype size;
        int daysAgo;
        QColor color;
    };
    const QList<App> apps = {
        {"org.example.wetter", "Wetter am Ort", "4.2.1", true, 1840000, 12, QColor(0, 106, 106)},
        {"org.example.rezepte", "Rezepte", "2.0", true, 730000, 40, QColor(200, 120, 30)},
        {"org.example.fahrplan", "Fahrplan Nord", "11.3", true, 3210000, 3, QColor(90, 60, 150)},
        {"org.example.uebung", "Übungen für den Rücken", "1.0.7", true, 210000, 90,
         QColor(40, 140, 80)},
        {"com.android.settings", "Einstellungen", "16", false, 4100000, 300, QColor(70, 70, 70)},
        {"com.android.deskclock", "Uhr", "16.1", false, 620000, 300, QColor(30, 90, 180)},
    };
    QList<qpm::test::FakeApp> result;
    for (const App &app : apps) {
        qpm::test::FakeApp fake;
        fake.package = QString::fromLatin1(app.package);
        fake.label = QString::fromUtf8(app.label);
        fake.version = QString::fromLatin1(app.version);
        fake.userApp = app.user;
        fake.sourcePath =
            app.user ? QStringLiteral("/data/app/~~a1==/%1-b2==/base.apk").arg(fake.package)
                     : QStringLiteral("/system/app/%1/base.apk").arg(fake.package);
        fake.installed =
            QDateTime(QDate(2026, 11, 10).addDays(-app.daysAgo), QTime(9, 0), QTimeZone::UTC);
        fake.apk = QByteArray("PK\x03\x04", 4) + QByteArray(app.size - 4, 'a');
        fake.icon = appIcon(app.color);
        result.append(fake);
    }
    return result;
}

// A made-up home screen of a phone, sent as the phone sends its screen.
QByteArray exampleScreen()
{
    const QSize screen(540, 1200);
    QImage image(screen, QImage::Format_RGB32);
    image.fill(QColor(24, 48, 64));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(QRect(0, 0, screen.width(), 40), QColor(16, 32, 44));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(236, 240, 238));
    painter.drawRoundedRect(QRect(40, 120, 460, 140), 24, 24);
    const QList<QColor> colors = {QColor(0, 106, 106), QColor(200, 120, 30), QColor(90, 60, 150),
                                  QColor(40, 140, 80), QColor(30, 90, 180),  QColor(180, 90, 40),
                                  QColor(70, 70, 70),  QColor(160, 40, 90)};
    for (int i = 0; i < colors.size(); ++i) {
        painter.setBrush(colors.at(i));
        painter.drawRoundedRect(QRect(52 + (i % 4) * 116, 760 + (i / 4) * 150, 88, 88), 24, 24);
    }
    painter.setBrush(QColor(236, 240, 238));
    painter.drawRoundedRect(QRect(40, 1080, 460, 72), 36, 36);
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return "FileBegin;540x1200;0;540x1200;PNG;100\r\n" + png + "\r\nFileEnd";
}

QByteArray noteMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\"><note_categories>\r\n"
           "<category name=\"Privat\" color=\"33B679\" id=\"1\"/>\r\n</note_categories>\r\n</metadata>";
}

bool similar(const QColor &a, const QColor &b)
{
    constexpr int tolerance = 24;
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

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

// The label below `item` whose text holds `text`.
QQuickItem *labelWithText(QQuickItem *item, const QString &text)
{
    if (item->inherits("QQuickText") && item->property("text").toString().contains(text))
        return item;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = labelWithText(child, text))
            return found;
    }
    return nullptr;
}

// The software scene graph cannot draw a layer effect, so a raised Material
// button would show no fill at all. Without its layer the fill shows, only
// the shadow is missing.
void dropLayerEffects(QQuickItem *item)
{
    const auto *background = item->inherits("QQuickControl")
                                 ? item->property("background").value<QQuickItem *>()
                                 : nullptr;
    if (QObject *layer = background ? background->property("layer").value<QObject *>() : nullptr) {
        if (layer->property("enabled").toBool() && layer->property("effect").value<QObject *>())
            layer->setProperty("enabled", false);
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        dropLayerEffects(child);
}

// A grab once the picture has stopped changing, so no transition is caught halfway.
QImage settledGrab(QQuickWindow *window)
{
    if (window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        QQuickItem *root = window->contentItem();
        dropLayerEffects(root->parentItem() ? root->parentItem() : root);
    }
    constexpr int attempts = 40;
    constexpr int pauseMs = 50;
    QImage last = window->grabWindow();
    for (int attempt = 0; attempt < attempts; ++attempt) {
        QTest::qWait(pauseMs);
        const QImage next = window->grabWindow();
        if (next == last)
            return next;
        last = next;
    }
    return last;
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
    void contactViewsAreGermanAndRenderInBothSchemes_data();
    void contactViewsAreGermanAndRenderInBothSchemes();
    void callViewsAreGermanAndRenderInBothSchemes_data();
    void callViewsAreGermanAndRenderInBothSchemes();
    void messageViewsAreGermanAndRenderInBothSchemes_data();
    void messageViewsAreGermanAndRenderInBothSchemes();
    void organizerViewsAreGermanAndRenderInBothSchemes_data();
    void organizerViewsAreGermanAndRenderInBothSchemes();
    void fileViewsAreGermanAndRenderInBothSchemes_data();
    void fileViewsAreGermanAndRenderInBothSchemes();
    void appViewsAreGermanAndRenderInBothSchemes_data();
    void appViewsAreGermanAndRenderInBothSchemes();
    void screenViewsAreGermanAndRenderInBothSchemes_data();
    void screenViewsAreGermanAndRenderInBothSchemes();

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
    m_phone->setObjects(QStringLiteral("cal"), exampleOrganizer());
    m_phone->setMetadata(QStringLiteral("cal"), calendarMetadata());
    m_phone->setObjects(QStringLiteral("notes"), exampleNotes());
    m_phone->setMetadata(QStringLiteral("notes"), noteMetadata());
    const QDateTime taken(QDate(2026, 8, 14), QTime(10, 12), QTimeZone::UTC);
    m_phone->addFolder(QStringLiteral("\\external\\DCIM"));
    m_phone->addFolder(QStringLiteral("\\sdcard\\Music"));
    m_phone->addFolder(QStringLiteral("\\sdcard\\DCIM\\Camera\\Urlaub Ostsee"));
    const QList<std::pair<const char *, QColor>> photos = {
        {"Strand.jpg", QColor(0, 106, 106)},
        {"Leuchtturm.jpg", QColor(180, 90, 40)},
        {"Hafen.jpg", QColor(90, 60, 150)},
        {"Dünen.jpg", QColor(40, 140, 80)},
        {"Sonnenuntergang.jpg", QColor(200, 120, 30)}};
    for (const auto &[name, color] : photos) {
        m_phone->addFile(QStringLiteral("\\sdcard\\DCIM\\Camera\\") + QString::fromUtf8(name),
                         portrait(color), taken);
    }
    m_phone->addFile(QStringLiteral("\\sdcard\\DCIM\\Camera\\Ausflug.mp4"),
                     QByteArray(2400000, 'v'), taken);
    m_phone->addFile(QStringLiteral("\\sdcard\\DCIM\\Camera\\Reiseplan.pdf"),
                     QByteArray(184320, 'p'), taken);
    m_phone->setApps(exampleApps());
    m_phone->config().screenFrame = exampleScreen();
    m_phone->config().alarms =
        QStringList{QStringLiteral("\"06:30\",1,0,\"Aufstehen\",\"1,2,3,4,5\",1"),
                    QStringLiteral("\"08:00\",2,0,\"Wochenende\",\"6,7\",0")};

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

    // The fake phone opens its screen channel on a port of its own.
    m_controller->screenClient()->setEndpointResolver(
        [this](quint16) -> std::optional<qpm::core::protocol::Endpoint> {
            return qpm::core::protocol::Endpoint{QStringLiteral("127.0.0.1"),
                                                 m_phone->screenPort()};
        });
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
    if (QObject *found = m_window->findChild<QObject *>(name))
        return found;
    for (const QString &pageName :
         {QStringLiteral("contactsPage"), QStringLiteral("callsPage"),
          QStringLiteral("messagesPage"), QStringLiteral("calendarPage"),
          QStringLiteral("tasksPage"), QStringLiteral("notesPage"), QStringLiteral("filesPage"),
          QStringLiteral("appsPage"), QStringLiteral("screenPage")}) {
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
    const QImage image = settledGrab(shown);
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
        QCOMPARE(item(QStringLiteral("recipientChip0"))->property("text").toString(),
                 QStringLiteral("Bernd Albrecht (+49 30 2000002)"));
        if (view == QLatin1StringView("composeScheduled")) {
            // The form was right, the picture showed the box unticked and the
            // button greyed: it was taken before the tick had been drawn, and
            // the button's fill is a layer effect the software renderer skips.
            QQuickItem *later = item(QStringLiteral("scheduleBox"));
            QQuickItem *submit = item(QStringLiteral("submitMessageButton"));
            QVERIFY(later->property("checked").toBool());
            QVERIFY(area->composer()->canSubmit());
            QVERIFY(submit->isEnabled());
            m_settings->setColorScheme(Settings::ColorScheme::Light);
            const QImage shot = settledGrab(m_window);
            const auto *box = later->property("indicator").value<QQuickItem *>();
            const auto *fill = submit->property("background").value<QQuickItem *>();
            QVERIFY(box && fill);
            const QColor tick = shot.pixelColor(box->mapToScene(QPointF(4, 4)).toPoint());
            const QColor button =
                shot.pixelColor(fill->mapToScene(QPointF(8, fill->height() / 2)).toPoint());
            // A ticked box and the raised button are both filled with the accent colour.
            const QColor accent = fill->property("color").value<QColor>();
            QVERIFY2(similar(tick, accent), qPrintable(tick.name()));
            QVERIFY2(similar(button, accent), qPrintable(button.name()));
            m_settings->setColorScheme(Settings::ColorScheme::System);
        }
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
        // The line between list and detail runs the full height; the style's
        // own handle drew a short grip that read as a stray stroke.
        QQuickItem *split = findVisual(page, QStringLiteral("listDetailSplit"));
        QVERIFY(split);
        const QQuickItem *line = nullptr;
        QList<QQuickItem *> pending = {split};
        while (!pending.isEmpty() && !line) {
            QQuickItem *next = pending.takeLast();
            if (next->inherits("QQuickRectangle") && next->isVisible() && next->width() == 1 &&
                next->height() >= split->height() - 1) {
                line = next;
            }
            pending.append(next->childItems());
        }
        QVERIFY(line);
        m_settings->setColorScheme(Settings::ColorScheme::Light);
        const QImage shot = settledGrab(m_window);
        const QPointF top = line->mapToScene(QPointF(0, 0));
        const QColor lineColor = line->property("color").value<QColor>();
        int drawn = 0;
        const int rows = int(line->height());
        for (int y = 0; y < rows; ++y) {
            if (similar(shot.pixelColor(int(top.x()), int(top.y()) + y), lineColor))
                ++drawn;
        }
        QVERIFY2(drawn >= rows * 95 / 100, qPrintable(QStringLiteral("%1 of %2").arg(drawn).arg(rows)));
        m_settings->setColorScheme(Settings::ColorScheme::System);
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

void TestScreens::organizerViewsAreGermanAndRenderInBothSchemes_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("germanTitle");

    QTest::newRow("month") << QStringLiteral("calendarMonth") << QStringLiteral("November 2026");
    QTest::newRow("week") << QStringLiteral("calendarWeek")
                          << QStringLiteral("KW 46: 09.11.2026 bis 15.11.2026");
    QTest::newRow("day") << QStringLiteral("calendarDay")
                         << QStringLiteral("Donnerstag, 12. November 2026");
    QTest::newRow("list") << QStringLiteral("calendarList") << QStringLiteral("November 2026");
    QTest::newRow("detail") << QStringLiteral("eventDetail") << QStringLiteral("Elternabend");
    QTest::newRow("event form") << QStringLiteral("eventForm")
                                << QStringLiteral("Termin bearbeiten");
    QTest::newRow("calendars") << QStringLiteral("calendarsDialog")
                               << QStringLiteral("Kalender auf dem Handy");
    QTest::newRow("alarms") << QStringLiteral("alarmsDialog")
                            << QStringLiteral("Wecker auf dem Handy");
    QTest::newRow("delete") << QStringLiteral("deleteEventDialog")
                            << QStringLiteral("Termin Elternabend löschen?");
    QTest::newRow("tasks") << QStringLiteral("tasks") << QStringLiteral("Aufgaben");
    QTest::newRow("task form") << QStringLiteral("taskForm")
                               << QStringLiteral("Aufgabe bearbeiten");
    QTest::newRow("task categories") << QStringLiteral("taskCategoriesDialog")
                                     << QStringLiteral("Kategorien für Aufgaben");
    QTest::newRow("notes") << QStringLiteral("notes") << QStringLiteral("Notizen");
    QTest::newRow("note form") << QStringLiteral("noteForm") << QStringLiteral("Notiz bearbeiten");
    QTest::newRow("note categories") << QStringLiteral("noteCategoriesDialog")
                                     << QStringLiteral("Kategorien für Notizen");
}

void TestScreens::organizerViewsAreGermanAndRenderInBothSchemes()
{
    QFETCH(QString, view);
    QFETCH(QString, germanTitle);
    qpm::ui::CalendarArea *calendar = m_controller->calendar();
    if (m_controller->tasks()->list()->totalCount() == 0) {
        const QFuture<qpm::core::Result<qpm::core::store::OrganizerChanges>> events = calendar->sync();
        QTRY_VERIFY_WITH_TIMEOUT(events.isFinished(), connectTimeoutMs);
        QVERIFY(events.result().hasValue());
    }
    if (m_controller->notes()->list()->totalCount() == 0) {
        const QFuture<qpm::core::Result<qpm::core::store::OrganizerChanges>> notes =
            m_controller->notes()->sync();
        QTRY_VERIFY_WITH_TIMEOUT(notes.isFinished(), connectTimeoutMs);
        QVERIFY(notes.result().hasValue());
    }
    m_settings->setHolidayRegion(QStringLiteral("de"));
    calendar->setCurrentDate(QDate(2026, 11, 12));
    calendar->selectEvent(0, 0);
    const bool tasks = view.startsWith(QLatin1StringView("task"));
    const bool notes = view.startsWith(QLatin1StringView("note"));
    const int section = tasks ? 5 : notes ? 6 : 4;
    const QString pageName = tasks   ? QStringLiteral("tasksPage")
                             : notes ? QStringLiteral("notesPage")
                                     : QStringLiteral("calendarPage");
    const QString sectionTitle = tasks   ? QStringLiteral("Aufgaben")
                                 : notes ? QStringLiteral("Notizen")
                                         : QStringLiteral("Kalender");
    m_window->setProperty("currentSection", section);
    QQuickItem *header = m_window->property("header").value<QQuickItem *>();
    QCOMPARE(header->property("title").toString(), sectionTitle);
    QTRY_VERIFY(item(pageName));
    QQuickItem *page = item(pageName);
    page->setProperty("editing", false);
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));

    QObject *popup = nullptr;
    QString focusName;
    if (view == QLatin1StringView("calendarMonth") || view == QLatin1StringView("calendarList") ||
        view == QLatin1StringView("calendarWeek") || view == QLatin1StringView("calendarDay")) {
        calendar->setViewMode(view == QLatin1StringView("calendarMonth")  ? qpm::ui::CalendarArea::ViewMode::Month
                              : view == QLatin1StringView("calendarWeek") ? qpm::ui::CalendarArea::ViewMode::Week
                              : view == QLatin1StringView("calendarDay")  ? qpm::ui::CalendarArea::ViewMode::Day
                                                                          : qpm::ui::CalendarArea::ViewMode::List);
        QTRY_COMPARE(item(QStringLiteral("calendarPeriodTitle"))->property("text").toString(),
                     germanTitle);
        focusName = view == QLatin1StringView("calendarMonth") || view == QLatin1StringView("calendarWeek")
                        ? QStringLiteral("calendarGrid")
                        : QStringLiteral("eventList");
        if (view == QLatin1StringView("calendarMonth") ||
            view == QLatin1StringView("calendarWeek")) {
            // A week shows a long title on up to three lines, a month on one.
            const QString longTitle =
                QStringLiteral("Winterreifen beim Autohaus am Stadtrand abholen");
            const auto lines = [this, &longTitle]() {
                const QQuickItem *label = labelWithText(m_window->contentItem(), longTitle);
                return label ? label->property("lineCount").toInt() : 0;
            };
            if (view == QLatin1StringView("calendarWeek"))
                QTRY_VERIFY2(lines() >= 2 && lines() <= 3, qPrintable(QString::number(lines())));
            else
                QTRY_COMPARE(lines(), 1);
        }
    } else if (view == QLatin1StringView("eventDetail") || view == QLatin1StringView("eventForm")) {
        calendar->setViewMode(qpm::ui::CalendarArea::ViewMode::Day);
        for (int row = 0; row < calendar->list()->rowCount(); ++row) {
            if (calendar->list()->luidAt(row) == 0x10000001)
                calendar->selectRow(row);
        }
        if (view == QLatin1StringView("eventForm"))
            QVERIFY(QMetaObject::invokeMethod(page, "startEdit"));
        const QString heading = view == QLatin1StringView("eventForm") ? QStringLiteral("eventForm")
                                                                        : QStringLiteral("eventTitle");
        QTRY_VERIFY(item(heading));
        if (view == QLatin1StringView("eventDetail")) {
            QTRY_COMPARE(item(heading)->property("text").toString(), germanTitle);
            // The list marks the appointment the detail shows.
            QVERIFY(calendar->currentRow() >= 0);
            QTRY_COMPARE(item(QStringLiteral("eventList"))->property("currentIndex").toInt(),
                         calendar->currentRow());
            // The list joined time and place with a call that Qt 6.9 refuses; the line stayed empty.
            QTRY_VERIFY(
                labelWithText(m_window->contentItem(), QStringLiteral(", Grundschule am Park")));
        }
        if (view == QLatin1StringView("eventForm")) {
            // Every choice of the form shows its whole text.
            for (const QString &box :
                 {QStringLiteral("eventRepeatBox"), QStringLiteral("eventColorBox"),
                  QStringLiteral("eventAvailabilityBox"), QStringLiteral("eventVisibilityBox")}) {
                QTRY_VERIFY(item(box));
                const auto *text = item(box)->property("contentItem").value<QQuickItem *>();
                QVERIFY(text);
                QTRY_VERIFY2(text->property("contentWidth").toReal() <=
                                 text->width() - text->property("leftPadding").toReal() -
                                     text->property("rightPadding").toReal(),
                             qPrintable(QStringLiteral("%1: %2 > %3")
                                            .arg(box)
                                            .arg(text->property("contentWidth").toReal())
                                            .arg(text->width())));
            }
        }
    } else if (view == QLatin1StringView("tasks") || view == QLatin1StringView("notes")) {
        focusName = tasks ? QStringLiteral("taskList") : QStringLiteral("noteList");
        if (tasks)
            m_controller->tasks()->setCurrentLuid(6);
        else
            m_controller->notes()->setCurrentLuid(1);
    } else if (view == QLatin1StringView("taskForm") || view == QLatin1StringView("noteForm")) {
        if (tasks)
            m_controller->tasks()->setCurrentLuid(5);
        else
            m_controller->notes()->setCurrentLuid(1);
        QVERIFY(QMetaObject::invokeMethod(page, "startEdit"));
        QTRY_VERIFY(item(view));
        if (tasks) {
            // An empty date field shows the form of a date, not a date that reads as a value.
            QCOMPARE(item(QStringLiteral("taskStartField"))->property("placeholderText").toString(),
                     QStringLiteral("TT.MM.JJJJ"));
            QCOMPARE(item(QStringLiteral("taskReminderTimeField"))
                         ->property("placeholderText")
                         .toString(),
                     QStringLiteral("hh:mm"));
        }
    } else {
        popup = dialog(view);
        QVERIFY(popup);
        if (view == QLatin1StringView("deleteEventDialog")) {
            QVERIFY(QMetaObject::invokeMethod(page, "confirmDelete", Q_ARG(int, 0x10000001)));
        } else {
            QMetaObject::invokeMethod(popup, "open");
        }
        const bool hasOpened = popup->metaObject()->indexOfProperty("opened") >= 0;
        QTRY_VERIFY(popup->property(hasOpened ? "opened" : "visible").toBool());
        QCOMPARE(popup->property("title").toString(), germanTitle);
        if (view == QLatin1StringView("alarmsDialog")) {
            QTRY_VERIFY(item(QStringLiteral("alarmList")) &&
                        item(QStringLiteral("alarmList"))->property("count").toInt() == 2);
        }
    }
    if (view.endsWith(QLatin1StringView("Form"))) {
        // The heading of a form is a label inside it.
        QQuickItem *form = item(view);
        bool headingFound = false;
        QList<QQuickItem *> pending = {form};
        while (!pending.isEmpty() && !headingFound) {
            QQuickItem *next = pending.takeLast();
            headingFound = next->property("text").toString() == germanTitle;
            pending.append(next->childItems());
        }
        QVERIFY(headingFound);
    }
    if (!focusName.isEmpty()) {
        QQuickItem *focus = item(focusName);
        QVERIFY(focus);
        focus->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(focus->hasActiveFocus());
    }

    QVERIFY(saveBothSchemes(view));

    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    page->setProperty("editing", false);
    QVERIFY(m_controller->isConnected());
}

void TestScreens::fileViewsAreGermanAndRenderInBothSchemes_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("germanTitle");

    QTest::newRow("storages") << QStringLiteral("filesRoot") << QStringLiteral("Dateien");
    QTest::newRow("grid") << QStringLiteral("filesGrid") << QStringLiteral("Dateien");
    QTest::newRow("list") << QStringLiteral("filesList") << QStringLiteral("Dateien");
    QTest::newRow("menu") << QStringLiteral("fileMenu") << QString();
    QTest::newRow("new folder") << QStringLiteral("fileNameDialog")
                                << QStringLiteral("Neuer Ordner");
    QTest::newRow("delete") << QStringLiteral("deleteFileDialog")
                            << QStringLiteral("Reiseplan.pdf löschen?");
    QTest::newRow("conflict") << QStringLiteral("fileConflictDialog")
                              << QStringLiteral("Reiseplan.pdf gibt es schon");
    QTest::newRow("transfers") << QStringLiteral("filesTransfers") << QStringLiteral("Dateien");
}

void TestScreens::fileViewsAreGermanAndRenderInBothSchemes()
{
    QFETCH(QString, view);
    QFETCH(QString, germanTitle);
    qpm::ui::FileBrowserModel *files = m_controller->files();
    qpm::ui::TransferQueue *transfers = m_controller->transfers();
    m_window->setProperty("currentSection", 7);
    QQuickItem *header = m_window->property("header").value<QQuickItem *>();
    QCOMPARE(header->property("title").toString(), QStringLiteral("Dateien"));
    QTRY_VERIFY(item(QStringLiteral("filesPage")));
    QQuickItem *page = item(QStringLiteral("filesPage"));
    page->setProperty("showGrid", view != QLatin1StringView("filesList"));
    const QString folder = view == QLatin1StringView("filesRoot")
                               ? QString()
                               : QStringLiteral("\\sdcard\\DCIM\\Camera");
    QVERIFY(files->open(folder));
    QTRY_VERIFY_WITH_TIMEOUT(!files->isBusy() && files->count() > 0, connectTimeoutMs);
    if (view == QLatin1StringView("filesRoot")) {
        QCOMPARE(files->crumbNames(), QStringList{QStringLiteral("Handy")});
        QCOMPARE(
            files->data(files->index(0), qpm::ui::FileBrowserModel::DisplayNameRole).toString(),
            QStringLiteral("Interner Speicher"));
    } else {
        QCOMPARE(files->crumbNames(),
                 (QStringList{QStringLiteral("Handy"), QStringLiteral("Interner Speicher"),
                              QStringLiteral("DCIM"), QStringLiteral("Camera")}));
        // The pictures are shown once their thumbnails are there.
        const int strand = files->rowOf(QStringLiteral("Strand.jpg"));
        QTRY_VERIFY_WITH_TIMEOUT(
            !files->data(files->index(strand), qpm::ui::FileBrowserModel::ThumbnailUrlRole)
                 .toString()
                 .isEmpty(),
            connectTimeoutMs);
    }
    page->setProperty("currentRow", files->rowOf(QStringLiteral("Reiseplan.pdf")));
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));

    QObject *popup = nullptr;
    QTemporaryDir target;
    QVERIFY(target.isValid());
    if (view == QLatin1StringView("filesRoot") || view == QLatin1StringView("filesGrid") ||
        view == QLatin1StringView("filesList")) {
        QQuickItem *focus =
            item(view == QLatin1StringView("filesList") ? QStringLiteral("fileList")
                                                        : QStringLiteral("fileGrid"));
        QVERIFY(focus);
        focus->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(focus->hasActiveFocus());
    } else if (view == QLatin1StringView("filesTransfers")) {
        QVERIFY(files->download({QStringLiteral("Reiseplan.pdf"), QStringLiteral("Strand.jpg")},
                                QUrl::fromLocalFile(target.path())));
        QTRY_VERIFY_WITH_TIMEOUT(!transfers->isActive(), connectTimeoutMs);
        QTRY_VERIFY(item(QStringLiteral("transferList")) &&
                    item(QStringLiteral("transferList"))->isVisible());
        QCOMPARE(item(QStringLiteral("transferSummary"))->property("text").toString(),
                 QStringLiteral("Übertragungen beendet"));
    } else if (view == QLatin1StringView("fileConflictDialog")) {
        QFile existing(target.filePath(QStringLiteral("Reiseplan.pdf")));
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.close();
        QVERIFY(
            files->download({QStringLiteral("Reiseplan.pdf")}, QUrl::fromLocalFile(target.path())));
        popup = dialog(view);
        QVERIFY(popup);
        QTRY_VERIFY_WITH_TIMEOUT(popup->property("opened").toBool(), connectTimeoutMs);
        QCOMPARE(popup->property("title").toString(), germanTitle);
    } else {
        popup = dialog(view);
        QVERIFY(popup);
        if (view == QLatin1StringView("fileNameDialog"))
            QVERIFY(QMetaObject::invokeMethod(page, "startNewFolder"));
        else if (view == QLatin1StringView("deleteFileDialog"))
            QVERIFY(QMetaObject::invokeMethod(page, "confirmDelete"));
        else
            QVERIFY(QMetaObject::invokeMethod(page, "showMenu"));
        QTRY_VERIFY(popup->property("opened").toBool());
        if (!germanTitle.isEmpty())
            QCOMPARE(popup->property("title").toString(), germanTitle);
    }

    QVERIFY(saveBothSchemes(view));

    if (popup && view == QLatin1StringView("fileConflictDialog")) {
        QMetaObject::invokeMethod(popup, "answer",
                                  Q_ARG(int, int(qpm::ui::TransferQueue::Choice::Skip)));
        QTRY_VERIFY(!transfers->isActive());
    }
    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    transfers->clearFinished();
    QVERIFY(m_controller->isConnected());
}

void TestScreens::appViewsAreGermanAndRenderInBothSchemes_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("germanTitle");

    QTest::newRow("own apps") << QStringLiteral("apps") << QStringLiteral("Apps");
    QTest::newRow("system apps") << QStringLiteral("appsSystem") << QStringLiteral("Apps");
    QTest::newRow("menu") << QStringLiteral("appMenu") << QString();
    QTest::newRow("remove") << QStringLiteral("uninstallAppDialog")
                            << QStringLiteral("Wetter am Ort entfernen?");
    QTest::newRow("install") << QStringLiteral("appsTransfer") << QStringLiteral("Apps");
}

void TestScreens::appViewsAreGermanAndRenderInBothSchemes()
{
    QFETCH(QString, view);
    QFETCH(QString, germanTitle);
    qpm::ui::AppListModel *apps = m_controller->apps();
    m_window->setProperty("currentSection", 8);
    QQuickItem *header = m_window->property("header").value<QQuickItem *>();
    QCOMPARE(header->property("title").toString(), QStringLiteral("Apps"));
    QTRY_VERIFY(item(QStringLiteral("appsPage")));
    QQuickItem *page = item(QStringLiteral("appsPage"));
    QTRY_VERIFY_WITH_TIMEOUT(!apps->isBusy() && apps->totalCount() > 0, connectTimeoutMs);
    apps->setFilter(view == QLatin1StringView("appsSystem") ? qpm::ui::AppListModel::Filter::System
                                                            : qpm::ui::AppListModel::Filter::User);
    // The rows show their icons once these are there.
    const QString shown = view == QLatin1StringView("appsSystem")
                              ? QStringLiteral("com.android.settings")
                              : QStringLiteral("org.example.wetter");
    QTRY_VERIFY_WITH_TIMEOUT(
        !apps->data(apps->index(apps->rowOf(shown)), qpm::ui::AppListModel::IconUrlRole)
             .toString()
             .isEmpty(),
        connectTimeoutMs);
    QQuickItem *list = item(QStringLiteral("appList"));
    QVERIFY(list);
    list->setProperty("currentIndex", apps->rowOf(shown));
    QCOMPARE(page->property("currentPackage").toString(), shown);
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));

    QObject *popup = nullptr;
    QTemporaryDir local;
    QVERIFY(local.isValid());
    if (view == QLatin1StringView("apps") || view == QLatin1StringView("appsSystem")) {
        list->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(list->hasActiveFocus());
    } else if (view == QLatin1StringView("appsTransfer")) {
        // The phone reads the APK late, so the copy is still under way in the picture.
        m_phone->config().uploadReadDelay = std::chrono::milliseconds(4000);
        const QString source = local.filePath(QStringLiteral("Würfelspiel-1.4.apk"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("PK\x03\x04", 4) + QByteArray(3000000, 'w'));
        file.close();
        QVERIFY(apps->install(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(item(QStringLiteral("appTransfer")) &&
                    item(QStringLiteral("appTransfer"))->isVisible());
        QVERIFY(item(QStringLiteral("appTransferText"))
                    ->property("text")
                    .toString()
                    .startsWith(QStringLiteral("Würfelspiel-1.4.apk wird an das Handy gesendet")));
    } else {
        popup = dialog(view);
        QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(
            page, view == QLatin1StringView("appMenu") ? "showMenu" : "confirmUninstall"));
        QTRY_VERIFY(popup->property("opened").toBool());
        if (!germanTitle.isEmpty())
            QCOMPARE(popup->property("title").toString(), germanTitle);
    }

    QVERIFY(saveBothSchemes(view));

    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    if (view == QLatin1StringView("appsTransfer")) {
        QTRY_VERIFY_WITH_TIMEOUT(!apps->isTransferActive(), connectTimeoutMs);
        m_phone->config().uploadReadDelay = std::chrono::milliseconds(0);
        QCOMPARE(m_phone->installRequests().size(), 1);
    }
    QVERIFY(m_phone->uninstallRequests().isEmpty());
    QVERIFY(m_controller->isConnected());
}

void TestScreens::screenViewsAreGermanAndRenderInBothSchemes_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("germanText");

    QTest::newRow("not started") << QStringLiteral("screenIdle")
                                 << QStringLiteral("Bildschirm zeigen");
    QTest::newRow("question on the phone")
        << QStringLiteral("screenConsent") << QStringLiteral("Bestätigen Sie auf dem Handy");
    QTest::newRow("mirrored") << QStringLiteral("screen") << QStringLiteral("Beenden");
    QTest::newRow("no control") << QStringLiteral("screenNoControl")
                                << QStringLiteral("Bedienungshilfe");
    QTest::newRow("screenshot") << QStringLiteral("screenshotDialog")
                                << QStringLiteral("Bildschirmfoto speichern");
}

void TestScreens::screenViewsAreGermanAndRenderInBothSchemes()
{
    QFETCH(QString, view);
    QFETCH(QString, germanText);
    qpm::ui::ScreenMirror *mirror = m_controller->screen();
    m_window->setProperty("currentSection", 9);
    QQuickItem *header = m_window->property("header").value<QQuickItem *>();
    QCOMPARE(header->property("title").toString(), QStringLiteral("Bildschirm"));
    QTRY_VERIFY(item(QStringLiteral("screenPage")));
    QQuickItem *page = item(QStringLiteral("screenPage"));
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    mirror->stop();
    m_phone->config().accessibilityService = view != QLatin1StringView("screenNoControl");

    QObject *popup = nullptr;
    if (view == QLatin1StringView("screenIdle")) {
        QVERIFY(labelWithText(page, QStringLiteral("Hier erscheint der Bildschirm des Handys")));
        QQuickItem *start = item(QStringLiteral("screenStartButton"));
        QCOMPARE(start->property("text").toString(), germanText);
        start->forceActiveFocus(Qt::TabFocusReason);
    } else if (view == QLatin1StringView("screenConsent")) {
        // The phone leaves the question open, as long as its user has not answered.
        m_phone->config().unanswered.insert(QStringLiteral("AT*STARTCAPTURESERVER"));
        QVERIFY(mirror->start());
        QTRY_COMPARE_WITH_TIMEOUT(mirror->state(), qpm::ui::ScreenMirror::State::WaitingForConsent,
                                  connectTimeoutMs);
        QTRY_VERIFY(item(QStringLiteral("screenStatusText"))->isVisible());
        QVERIFY(item(QStringLiteral("screenStatusText"))
                    ->property("text")
                    .toString()
                    .startsWith(germanText));
    } else {
        QVERIFY(mirror->start());
        QTRY_VERIFY_WITH_TIMEOUT(mirror->hasFrame(), connectTimeoutMs);
        QCOMPARE(item(QStringLiteral("screenStartButton"))->property("text").toString(),
                 QStringLiteral("Beenden"));
        if (view == QLatin1StringView("screenNoControl")) {
            QTRY_VERIFY(item(QStringLiteral("screenControlHint"))->isVisible());
            QVERIFY(item(QStringLiteral("screenControlHintText"))
                        ->property("text")
                        .toString()
                        .contains(germanText));
            QVERIFY(!item(QStringLiteral("screenBackButton"))->isEnabled());
        } else {
            QVERIFY(!item(QStringLiteral("screenControlHint"))->isVisible());
            item(QStringLiteral("screenView"))->forceActiveFocus(Qt::TabFocusReason);
        }
        if (view == QLatin1StringView("screenshotDialog")) {
            popup = dialog(view);
            QVERIFY(popup);
            QMetaObject::invokeMethod(popup, "open");
            QTRY_VERIFY(popup->property("visible").toBool());
            QCOMPARE(popup->property("title").toString(), germanText);
        }
    }

    QVERIFY(saveBothSchemes(view));

    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    mirror->stop();
    if (view == QLatin1StringView("screenConsent")) {
        m_phone->config().unanswered.remove(QStringLiteral("AT*STARTCAPTURESERVER"));
        // Frees the session, as the phone does when nobody answers.
        m_phone->sendUnsolicited("AT*STARTCAPTURESERVER\r\nTIMEOUT\r\nERROR");
    }
    m_phone->config().accessibilityService = true;
    QVERIFY(m_controller->isConnected());
}

QTEST_MAIN(TestScreens)

#include "tst_screens.moc"
