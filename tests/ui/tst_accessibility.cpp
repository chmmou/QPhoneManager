#include "core/codec/PhoneTime.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/PhoneVCard.h"
#include "core/codec/VNote.h"
#include "core/service/ScreenMirrorClient.h"
#include "platform/SecretStore.h"
#include "ui/AppController.h"
#include "ui/Settings.h"
#include "ui/apps/AppListModel.h"
#include "ui/backup/BackupController.h"
#include "ui/backup/BackupSectionModel.h"
#include "ui/calls/CallsArea.h"
#include "ui/contacts/ContactsArea.h"
#include "ui/desktop/CallControl.h"
#include "ui/desktop/NotificationListModel.h"
#include "ui/files/FileBrowserModel.h"
#include "ui/messages/MessagesArea.h"
#include "ui/organizer/CalendarArea.h"
#include "ui/organizer/NotesArea.h"
#include "ui/organizer/TasksArea.h"
#include "ui/screen/ScreenMirror.h"

#include "FakePhone.h"

#include <QAccessible>
#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QUrl>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <cmath>
#include <memory>

Q_IMPORT_QML_PLUGIN(QPhoneManager_UiPlugin)

using qpm::ui::AppController;
using qpm::ui::Settings;

namespace {

constexpr int phoneTimeoutMs = 15000;

qpm::core::codec::ContactValue number(quint64 dataId, const QString &value)
{
    qpm::core::codec::ContactValue entry;
    entry.dataId = dataId;
    entry.type = qpm::core::codec::phoneType::mobile;
    entry.value = value;
    return entry;
}

QByteArray contactCard(quint32 luid, const QString &given, const QString &family,
                       const QString &phone)
{
    qpm::core::codec::Contact contact;
    contact.luid = luid;
    contact.givenName = given;
    contact.familyName = family;
    contact.displayName = given + u' ' + family;
    contact.phones = {number(luid * 10, phone)};
    contact.addresses = {[]() {
        qpm::core::codec::ContactValue address;
        address.dataId = 99;
        address.type = qpm::core::codec::addressType::home;
        address.address = {QString(), QString(), QStringLiteral("Weg 1"), QStringLiteral("Bonn"),
                           QString(), QStringLiteral("53111"), QString()};
        return address;
    }()};
    return qpm::core::codec::encodeAsPhoneSendsIt(contact);
}

qpm::test::FakeCall fakeCall(quint32 id, const QString &storage, const QString &number,
                             qint64 duration)
{
    qpm::test::FakeCall call;
    call.id = id;
    call.storage = storage;
    call.number = number;
    call.duration = duration;
    call.time = qpm::core::codec::formatCallLogTime(
        QDateTime(QDate::currentDate(), QTime(10, 0)).addSecs(-3600 * qint64(id)));
    return call;
}

QByteArray organizerMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<calendars filter_disabled=\"0\">\r\n"
           "<calendar selected=\"1\" default=\"1\" url=\"anna@example.org@com.google/NULL\" "
           "title=\"Familie\" color=\"FF33B679\" hidden=\"0\" timezone=\"Europe/Berlin\" "
           "access_level=\"700\"/>\r\n"
           "<calendar selected=\"1\" url=\"anna@example.org@com.google/NULL\" title=\"Feiertage\" "
           "color=\"FF0B8043\" hidden=\"0\" timezone=\"Europe/Berlin\" access_level=\"200\"/>\r\n"
           "</calendars>\r\n<tasksupport>1</tasksupport>\r\n<task_categories>\r\n"
           "<category name=\"Haushalt\" color=\"FF8800\" id=\"1\"/>\r\n"
           "</task_categories>\r\n</metadata>";
}

QList<qpm::test::FakeSyncObject> organizerObjects()
{
    qpm::core::codec::CalendarEvent meeting;
    meeting.luid = 0x10000001;
    meeting.summary = QStringLiteral("Elternabend");
    meeting.location = QStringLiteral("Schule");
    meeting.start = QDateTime(QDate(2026, 11, 12), QTime(11, 0), QTimeZone::UTC);
    meeting.end = meeting.start.addSecs(5400);
    meeting.calendarUrl = QStringLiteral("anna@example.org@com.google/NULL");
    meeting.calendarTitle = QStringLiteral("Familie");
    meeting.rrule = QStringLiteral("FREQ=WEEKLY");
    qpm::core::codec::CalendarTask task;
    task.luid = 2;
    task.summary = QStringLiteral("Fenster putzen");
    task.due = QDate(2026, 11, 20);
    task.categories = {QStringLiteral("Haushalt")};
    return {{meeting.luid, qpm::core::codec::encodeEventAsPhoneSendsIt(meeting)},
            {task.luid, qpm::core::codec::encodeTaskAsPhoneSendsIt(task)}};
}

QByteArray exampleNote()
{
    qpm::core::codec::Note note;
    note.luid = 1;
    note.body = QStringLiteral("Einkaufsliste:\nMilch, Brot");
    note.lastModified = QDateTime(QDate(2026, 11, 10), QTime(8, 0), QTimeZone::UTC);
    return qpm::core::codec::encodeNoteAsPhoneSendsIt(note);
}

qpm::test::FakeApp fakeApp(const QString &package, const QString &label, bool user)
{
    QImage icon(48, 48, QImage::Format_ARGB32);
    icon.fill(QColor(0, 106, 106));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    icon.save(&buffer, "PNG");
    qpm::test::FakeApp app;
    app.package = package;
    app.label = label;
    app.version = QStringLiteral("1.0");
    app.userApp = user;
    app.sourcePath = QStringLiteral("/data/app/a/%1/base.apk").arg(package);
    app.installed = QDateTime(QDate(2026, 11, 2), QTime(9, 0), QTimeZone::UTC);
    app.apk = QByteArray("PK\x03\x04", 4) + QByteArray(2000, 'a');
    app.icon = png;
    return app;
}

bool isControl(const QQuickItem *item)
{
    return item->inherits("QQuickAbstractButton") || item->inherits("QQuickComboBox") ||
           item->inherits("QQuickTextInput") || item->activeFocusOnTab();
}

// Visible controls below `item`; the parts inside a control are not listed
// on their own.
void collectControls(QQuickItem *item, QList<QQuickItem *> &controls)
{
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (!child->isVisible())
            continue;
        if (isControl(child)) {
            controls.append(child);
            continue;
        }
        collectControls(child, controls);
    }
}

QQuickItem *findItem(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

QQuickItem *findItemOfClass(QQuickItem *item, const char *className)
{
    if (item->inherits(className))
        return item;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItemOfClass(child, className))
            return found;
    }
    return nullptr;
}

// The colour of the dimming the overlay shows behind a modal dialog, or an
// invalid colour while there is none.
QColor scrimColor(QQuickItem *root)
{
    QQuickItem *overlay = findItemOfClass(root, "QQuickOverlay");
    if (!overlay)
        return {};
    const QList<QQuickItem *> children = overlay->childItems();
    for (const QQuickItem *child : children) {
        if (child->isVisible() && child->inherits("QQuickRectangle"))
            return child->property("color").value<QColor>();
    }
    return {};
}

// A control shows its focus ring while its focus came from the keyboard; any
// other item shows the ring placed in it while it has the focus.
bool focusIsShown(const QQuickItem *item)
{
    if (!item)
        return false;
    if (item->metaObject()->indexOfProperty("visualFocus") >= 0)
        return item->property("visualFocus").toBool();
    // An outlined text field draws its border in the accent colour while it has the focus.
    if (item->inherits("QQuickTextField") || item->inherits("QQuickTextArea"))
        return item->hasActiveFocus();
    const QList<QQuickItem *> children = item->childItems();
    for (const QQuickItem *child : children) {
        const QString type = QString::fromLatin1(child->metaObject()->className());
        if (type.startsWith(QLatin1StringView("FocusRing")) && child->isVisible())
            return true;
    }
    return false;
}

QString describe(const QQuickItem *item)
{
    return QStringLiteral("%1 \"%2\"")
        .arg(QString::fromLatin1(item->metaObject()->className()), item->objectName());
}

double meanLightness(const QImage &image)
{
    double sum = 0;
    for (int y = 0; y < image.height(); y += 4) {
        for (int x = 0; x < image.width(); x += 4)
            sum += qGray(image.pixel(x, y));
    }
    const double samples = std::ceil(image.height() / 4.0) * std::ceil(image.width() / 4.0);
    return sum / samples / 255.0;
}

} // namespace

class TestAccessibility : public QObject
{
    Q_OBJECT

public:
    explicit TestAccessibility(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void everyControlHasANameAndARole_data();
    void everyControlHasANameAndARole();
    void everyDialogControlHasANameAndARole_data();
    void everyDialogControlHasANameAndARole();
    void pinFieldKeepsThePinFromAssistiveTools();
    void focusChainReachesEveryControlOnce();
    void sectionShortcutsSwitchTheSection();
    void narrowWindowKeepsTheSidebarLabelsForAssistiveTools();
    void sidebarIsWideEnoughForItsLongestLabel();
    void headerIconFollowsTheSection();
    void darkSchemeDarkensTheWindow();
    void contactViewControlsHaveANameAndARole_data();
    void contactViewControlsHaveANameAndARole();
    void contactDialogControlsHaveANameAndARole_data();
    void contactDialogControlsHaveANameAndARole();
    void contactDialogsShowTheFocusOnOpening_data();
    void contactDialogsShowTheFocusOnOpening();
    void contactsFocusChainReachesEveryControlOnce();
    void dialogScrimIsDarkInBothSchemes_data();
    void dialogScrimIsDarkInBothSchemes();
    void callViewControlsHaveANameAndARole();
    void callDialogControlsHaveANameAndARole();
    void messageViewControlsHaveANameAndARole_data();
    void messageViewControlsHaveANameAndARole();
    void messageDialogShowsItsFocusAndNamesItsControls();
    void messagesFocusChainReachesEveryControlOnce();
    void callsFocusChainReachesEveryControlOnce();
    void textFieldsShowTheirFocusWithoutARingOnTop();
    void organizerViewControlsHaveANameAndARole_data();
    void organizerViewControlsHaveANameAndARole();
    void organizerDialogsShowTheFocusAndNameTheirControls_data();
    void organizerDialogsShowTheFocusAndNameTheirControls();
    void calendarDaysAreChosenByKeyboard();
    void calendarSwitchesBetweenMonthAndWeekWithoutWarnings();
    void organizerFocusChainReachesEveryControlOnce_data();
    void organizerFocusChainReachesEveryControlOnce();
    void fileViewControlsHaveANameAndARole_data();
    void fileViewControlsHaveANameAndARole();
    void fileDialogsShowTheFocusAndNameTheirControls_data();
    void fileDialogsShowTheFocusAndNameTheirControls();
    void filesFocusChainReachesEveryControlOnce();
    void filesAreBrowsedByKeyboard();
    void appViewControlsHaveANameAndARole();
    void appDialogsShowTheFocusAndNameTheirControls_data();
    void appDialogsShowTheFocusAndNameTheirControls();
    void appsFocusChainReachesEveryControlOnce();
    void appsAreUsedByKeyboard();
    void screenViewControlsHaveANameAndARole_data();
    void screenViewControlsHaveANameAndARole();
    void screenFocusChainReachesEveryControlOnce();
    void screenIsUsedByKeyboard();
    void notificationViewControlsHaveANameAndARole();
    void notificationsFocusChainReachesEveryControlOnce();
    void notificationsAreUsedByKeyboard();
    void callWindowNamesItsControlsAndShowsItsFocus();
    void backupDialogIsUsedByKeyboard();
    void helpPageControlsHaveANameAndARole();
    void helpFocusChainReachesEveryControlOnce();
    void helpIsUsedByKeyboard();
    void aboutDialogShowsItsFocusAndNamesItsControls();

private:
    [[nodiscard]] QList<QQuickItem *> controls() const;
    [[nodiscard]] QObject *openDialog(const QString &name);
    void verifyNamesAndRoles(const QList<QQuickItem *> &found,
                             const QSet<QAccessible::Role> &extraRoles = {}) const;
    [[nodiscard]] Settings *settings() const;
    [[nodiscard]] bool showContacts();
    [[nodiscard]] bool showCalls();
    [[nodiscard]] bool showMessages();
    /// Connects a phone with a calendar, a task and a note and shows the section `section`.
    [[nodiscard]] bool showOrganizer(int section);
    /// Connects a phone with a few files and shows its internal storage.
    [[nodiscard]] bool showFiles();
    /// Connects a phone with a few apps and shows them.
    [[nodiscard]] bool showApps();
    /// Connects a phone and shows its mirrored screen; `control` is its accessibility service.
    [[nodiscard]] bool showScreen(bool control);
    /// Connects a phone with three notifications and shows them, the first one chosen.
    [[nodiscard]] bool showNotifications();
    /// Brings the contacts area into the state in which `dialog` has something to show.
    [[nodiscard]] bool prepareContactDialog(const QString &dialog);
    /// Opens the help with F1 in the contacts area, as a user does.
    [[nodiscard]] bool showHelp();
    static void announceAccessibility();

    QTemporaryDir m_config;
    std::unique_ptr<qpm::test::FakePhone> m_phone;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
};

void TestAccessibility::initTestCase()
{
    QVERIFY(m_config.isValid());
    // QSettings reads this once; the shell must not touch the real settings
    // of the user running the test.
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_config.filePath(QStringLiteral("config"))));
    // Phone data stored by the contacts views stays in the temporary folder.
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_config.filePath(QStringLiteral("data"))));
    QCoreApplication::setOrganizationName(QStringLiteral("QPhoneManagerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_accessibility"));
    // The offscreen platform offers no GPU, and the software renderer can
    // still grab the window.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
}

void TestAccessibility::init()
{
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->loadFromModule("QPhoneManager.Ui", "Main");
    QCOMPARE(m_engine->rootObjects().size(), 1);
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
    QVERIFY(m_window);
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    // The offscreen platform has no screen reader, so accessibility is off.
    // This tells the existing controls to fill in name and role as they do
    // when an assistive tool starts.
    QAccessible::setActive(true);
}

void TestAccessibility::cleanup()
{
    if (Settings *shared = settings())
        shared->setColorScheme(Settings::ColorScheme::System);
    m_window = nullptr;
    m_engine.reset();
    m_phone.reset();
}

QList<QQuickItem *> TestAccessibility::controls() const
{
    QList<QQuickItem *> found;
    collectControls(m_window->contentItem(), found);
    return found;
}

Settings *TestAccessibility::settings() const
{
    return m_engine ? m_engine->singletonInstance<Settings *>("QPhoneManager.Ui", "Settings")
                    : nullptr;
}

void TestAccessibility::everyControlHasANameAndARole_data()
{
    QTest::addColumn<int>("width");

    QTest::newRow("wide sidebar") << 1280;
    QTest::newRow("icon sidebar") << 1100;
}

void TestAccessibility::everyControlHasANameAndARole()
{
    QFETCH(int, width);
    m_window->resize(width, m_window->height());
    QTRY_COMPARE(m_window->width(), width);

    const QSet<QAccessible::Role> roles = {
        QAccessible::Button,   QAccessible::PushButton, QAccessible::CheckBox,
        QAccessible::ComboBox, QAccessible::EditableText, QAccessible::ListItem,
        QAccessible::PageTab,  QAccessible::RadioButton,
    };
    const QList<QQuickItem *> found = controls();

    QVERIFY2(found.size() >= 15, qPrintable(QString::number(found.size())));
    for (QQuickItem *item : found) {
        QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(item);
        QVERIFY2(accessible, qPrintable(describe(item)));
        QString name = accessible->text(QAccessible::Name);
        // While no assistive tool runs, Qt Quick Controls do not copy the
        // visible text into the name; that text is what a tool reads.
        if (name.isEmpty() && !QAccessible::isActive())
            name = item->property("text").toString();
        QVERIFY2(!name.trimmed().isEmpty(), qPrintable(describe(item)));
        QVERIFY2(roles.contains(accessible->role()),
                 qPrintable(describe(item) + QStringLiteral(" role %1").arg(accessible->role())));
    }
}

QObject *TestAccessibility::openDialog(const QString &name)
{
    QObject *dialog = m_window->findChild<QObject *>(name);
    // Dialogs of a page live with the page, which a Loader created.
    for (const QString &pageName :
         {QStringLiteral("contactsPage"), QStringLiteral("callsPage"),
          QStringLiteral("messagesPage"), QStringLiteral("calendarPage"),
          QStringLiteral("tasksPage"), QStringLiteral("notesPage"), QStringLiteral("filesPage"),
          QStringLiteral("appsPage"), QStringLiteral("screenPage"),
          QStringLiteral("notificationsPage")}) {
        QQuickItem *page = findItem(m_window->contentItem(), pageName);
        if (!dialog && page)
            dialog = page->findChild<QObject *>(name);
    }
    if (!dialog)
        return nullptr;
    if (name == QLatin1StringView("connectDialog"))
        dialog->setProperty("searchOnOpen", false);
    QMetaObject::invokeMethod(dialog, "open");
    if (!QTest::qWaitFor([dialog]() { return dialog->property("opened").toBool(); }))
        return nullptr;
    return dialog;
}

void TestAccessibility::verifyNamesAndRoles(const QList<QQuickItem *> &found,
                                            const QSet<QAccessible::Role> &extraRoles) const
{
    QSet<QAccessible::Role> roles = {
        QAccessible::Button,   QAccessible::PushButton,   QAccessible::CheckBox,
        QAccessible::ComboBox, QAccessible::EditableText, QAccessible::ListItem,
        QAccessible::PageTab,  QAccessible::RadioButton,
    };
    roles.unite(extraRoles);
    for (QQuickItem *item : found) {
        QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(item);
        QVERIFY2(accessible, qPrintable(describe(item)));
        QString name = accessible->text(QAccessible::Name);
        if (name.isEmpty() && !QAccessible::isActive())
            name = item->property("text").toString();
        // Qt Quick 6.9 reports no name for a password field, only a
        // description, and a screen reader announces that one.
        if (name.isEmpty() && accessible->state().passwordEdit)
            name = accessible->text(QAccessible::Description);
        QVERIFY2(!name.trimmed().isEmpty(), qPrintable(describe(item)));
        QVERIFY2(roles.contains(accessible->role()),
                 qPrintable(describe(item) + QStringLiteral(" role %1").arg(accessible->role())));
    }
}

void TestAccessibility::everyDialogControlHasANameAndARole_data()
{
    QTest::addColumn<QString>("dialog");
    QTest::addColumn<QString>("tabs");

    QTest::newRow("connect") << QStringLiteral("connectDialog") << QStringLiteral("connectTabs");
    QTest::newRow("QR code") << QStringLiteral("qrDialog") << QString();
    QTest::newRow("PIN") << QStringLiteral("pinDialog") << QString();
    QTest::newRow("settings") << QStringLiteral("settingsDialog") << QStringLiteral("settingsTabs");
    QTest::newRow("phones") << QStringLiteral("profilesDialog") << QString();
    QTest::newRow("backup") << QStringLiteral("backupDialog") << QStringLiteral("backupTabs");
}

// The dialogs live in the overlay of the window; every page of a dialog with
// tabs is looked at.
void TestAccessibility::everyDialogControlHasANameAndARole()
{
    QFETCH(QString, dialog);
    QFETCH(QString, tabs);
    const QList<QQuickItem *> before = controls();
    QObject *popup = openDialog(dialog);
    QVERIFY(popup);
    QQuickItem *tabBar = tabs.isEmpty() ? nullptr : findItem(m_window->contentItem(), tabs);
    const int pages = tabBar ? tabBar->property("count").toInt() : 1;
    QVERIFY(pages >= 1);

    for (int page = 0; page < pages; ++page) {
        if (tabBar)
            tabBar->setProperty("currentIndex", page);
        QList<QQuickItem *> found = controls();
        found.removeIf([&before](QQuickItem *item) { return before.contains(item); });
        QVERIFY2(!found.isEmpty(), qPrintable(dialog));
        verifyNamesAndRoles(found);
    }
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());
}

void TestAccessibility::pinFieldKeepsThePinFromAssistiveTools()
{
    QObject *popup = openDialog(QStringLiteral("pinDialog"));
    QVERIFY(popup);
    QQuickItem *field = findItem(m_window->contentItem(), QStringLiteral("pinField"));
    QVERIFY(field);
    field->setProperty("text", QStringLiteral("482916"));

    QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(field);
    QVERIFY(accessible);
    QVERIFY(accessible->state().passwordEdit);
    QVERIFY(!accessible->text(QAccessible::Value).contains(QStringLiteral("482916")));
    QVERIFY(!accessible->text(QAccessible::Name).contains(QStringLiteral("482916")));
    QMetaObject::invokeMethod(popup, "close");
}

void TestAccessibility::focusChainReachesEveryControlOnce()
{
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
}

void TestAccessibility::sectionShortcutsSwitchTheSection()
{
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));

    for (int number = 9; number >= 1; --number) {
        QTest::keyClick(m_window, static_cast<Qt::Key>(Qt::Key_0 + number), Qt::ControlModifier);
        QTRY_COMPARE(m_window->property("currentSection").toInt(), number - 1);
    }
    const QQuickItem *contacts = findItem(m_window->contentItem(), QStringLiteral("page-contacts"));
    QVERIFY(contacts);
    QTest::keyClick(m_window, Qt::Key_2, Qt::ControlModifier);
    QTRY_VERIFY(contacts->isVisible());
}

void TestAccessibility::narrowWindowKeepsTheSidebarLabelsForAssistiveTools()
{
    QQuickItem *sidebar = findItem(m_window->contentItem(), QStringLiteral("sidebar"));
    QVERIFY(sidebar);
    QVERIFY(!sidebar->property("compact").toBool());

    m_window->resize(1100, m_window->height());

    QTRY_VERIFY(sidebar->property("compact").toBool());
    QQuickItem *calls = findItem(m_window->contentItem(), QStringLiteral("section-calls"));
    QVERIFY(calls);
    QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(calls);
    QVERIFY(accessible);
    QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
    QVERIFY(sidebar->width() < 100);
}

void TestAccessibility::sidebarIsWideEnoughForItsLongestLabel()
{
    QQuickItem *sidebar = findItem(m_window->contentItem(), QStringLiteral("sidebar"));
    QQuickItem *longest = findItem(m_window->contentItem(), QStringLiteral("section-notifications"));
    QVERIFY(sidebar);
    QVERIFY(longest);

    const QQuickItem *label = longest->property("contentItem").value<QQuickItem *>();
    QVERIFY(label);
    QVERIFY2(label->implicitWidth() <= label->width() + 0.5,
             qPrintable(QStringLiteral("label needs %1, gets %2")
                            .arg(label->implicitWidth())
                            .arg(label->width())));
    QVERIFY(sidebar->width() >= sidebar->property("fittingWidth").toInt());
}

void TestAccessibility::headerIconFollowsTheSection()
{
    QQuickItem *icon = findItem(m_window->contentItem(), QStringLiteral("sectionIcon"));
    QVERIFY(icon);
    QCOMPARE(icon->property("name").toString(), QStringLiteral("home"));

    m_window->setProperty("currentSection", 1);

    QCOMPARE(icon->property("name").toString(), QStringLiteral("address-book"));
}

bool TestAccessibility::showContacts()
{
    m_phone = std::make_unique<qpm::test::FakePhone>();
    if (!m_phone->listen())
        return false;
    m_phone->setObjects(QStringLiteral("pb"),
                        {{1, contactCard(1, QStringLiteral("Erika"), QStringLiteral("Mustermann"),
                                         QStringLiteral("+491511234567"))},
                         {2, contactCard(2, QStringLiteral("Erika"), QStringLiteral("Musterman"),
                                         QStringLiteral("01511234567"))}});
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    if (!controller ||
        !controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port()))) {
        return false;
    }
    if (!QTest::qWaitFor([controller]() { return controller->isConnected(); }, phoneTimeoutMs))
        return false;
    const QFuture<qpm::core::Result<qpm::core::store::ContactChanges>> pulled =
        controller->contacts()->sync();
    if (!QTest::qWaitFor([&pulled]() { return pulled.isFinished(); }, phoneTimeoutMs) ||
        !pulled.result().hasValue()) {
        return false;
    }
    m_window->setProperty("currentSection", 1);
    controller->contacts()->setCurrentLuid(1);
    const bool shown = QTest::qWaitFor(
        [this]() {
            const QQuickItem *detail = findItem(m_window->contentItem(), QStringLiteral("contactDetail"));
            return detail && detail->isVisible();
        },
        phoneTimeoutMs);
    announceAccessibility();
    return shown;
}

// Controls made after accessibility was switched on only fill in name and
// role when they hear of it, as they do when an assistive tool starts.
void TestAccessibility::announceAccessibility()
{
    QAccessible::setActive(true);
}

void TestAccessibility::contactViewControlsHaveANameAndARole_data()
{
    QTest::addColumn<bool>("form");

    QTest::newRow("list and detail") << false;
    QTest::newRow("form") << true;
}

void TestAccessibility::contactViewControlsHaveANameAndARole()
{
    QFETCH(bool, form);
    QVERIFY(showContacts());
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("contactsPage"));
    QVERIFY(page);
    if (form) {
        QVERIFY(QMetaObject::invokeMethod(page, "startEdit"));
        QTRY_VERIFY(findItem(m_window->contentItem(), QStringLiteral("contactForm")));
        announceAccessibility();
    }

    const QList<QQuickItem *> found = controls();

    // The contact list takes the focus as a whole and moves through its
    // entries with the arrow keys, so it reports itself as a list.
    verifyNamesAndRoles(found, {QAccessible::List});
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("contactList"));
    QVERIFY(list && found.contains(list));
    QCOMPARE(QAccessible::queryAccessibleInterface(list)->role(), QAccessible::List);
    const QStringList required = form ? QStringList{QStringLiteral("givenNameField"),
                                                    QStringLiteral("saveContactButton"),
                                                    QStringLiteral("choosePhotoButton")}
                                      : QStringList{QStringLiteral("newContactButton"),
                                                    QStringLiteral("importContactsButton"),
                                                    QStringLiteral("editContactButton")};
    for (const QString &name : required) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
    }
}

void TestAccessibility::contactDialogControlsHaveANameAndARole_data()
{
    QTest::addColumn<QString>("dialog");

    QTest::newRow("delete") << QStringLiteral("deleteContactDialog");
    QTest::newRow("duplicates") << QStringLiteral("duplicatesDialog");
    QTest::newRow("conflict") << QStringLiteral("conflictDialog");
    QTest::newRow("crop") << QStringLiteral("cropDialog");
}

void TestAccessibility::contactDialogControlsHaveANameAndARole()
{
    QFETCH(QString, dialog);
    QVERIFY(showContacts());
    QVERIFY(prepareContactDialog(dialog));
    const QList<QQuickItem *> before = controls();
    QObject *popup = openDialog(dialog);
    QVERIFY(popup);
    announceAccessibility();

    QList<QQuickItem *> found = controls();
    found.removeIf([&before](QQuickItem *item) { return before.contains(item); });

    QVERIFY2(!found.isEmpty(), qPrintable(dialog));
    // The crop frame is moved with the arrow keys and sized with a slider.
    verifyNamesAndRoles(found, {QAccessible::List, QAccessible::Slider, QAccessible::Graphic});
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());
}

bool TestAccessibility::prepareContactDialog(const QString &dialog)
{
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    qpm::ui::ContactsArea *area = controller->contacts();
    if (dialog == QLatin1StringView("duplicatesDialog")) {
        QSignalSpy found(area, &qpm::ui::ContactsArea::duplicatesFound);
        if (!area->findDuplicates())
            return false;
        return QTest::qWaitFor([&found]() { return found.count() == 1; }) &&
               area->duplicates()->count() == 1;
    }
    if (dialog == QLatin1StringView("cropDialog")) {
        QImage picture(400, 300, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        const QString path = m_config.filePath(QStringLiteral("picture.png"));
        return picture.save(path) && area->editor()->load(1) &&
               area->editor()->loadPhotoCandidate(QUrl::fromLocalFile(path));
    }
    if (dialog == QLatin1StringView("deleteContactDialog")) {
        QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("contactsPage"));
        return page && page->setProperty("pendingDelete", 1);
    }
    return true;
}

void TestAccessibility::contactDialogsShowTheFocusOnOpening_data()
{
    contactDialogControlsHaveANameAndARole_data();
}

// A dialog that opens without a visible focus leaves a keyboard user guessing
// where a key press goes; the second opening has to show it as well.
void TestAccessibility::contactDialogsShowTheFocusOnOpening()
{
    QFETCH(QString, dialog);
    QVERIFY(showContacts());
    QVERIFY(prepareContactDialog(dialog));
    const QList<QQuickItem *> before = controls();

    for (int opening = 1; opening <= 2; ++opening) {
        if (opening == 2)
            QVERIFY(prepareContactDialog(dialog));
        QObject *popup = openDialog(dialog);
        QVERIFY(popup);
        QQuickItem *focused = m_window->activeFocusItem();
        QVERIFY2(focused, qPrintable(dialog));
        QVERIFY2(!before.contains(focused), qPrintable(describe(focused)));
        QVERIFY2(focusIsShown(focused),
                 qPrintable(QStringLiteral("opening %1: %2").arg(opening).arg(describe(focused))));
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
}

void TestAccessibility::dialogScrimIsDarkInBothSchemes_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<QString>("dialog");

    QTest::newRow("settings, light") << false << QStringLiteral("settingsDialog");
    QTest::newRow("settings, dark") << true << QStringLiteral("settingsDialog");
    QTest::newRow("delete calls, dark") << true << QStringLiteral("deleteCallsDialog");
}

// A light dimming over a dark window washes the window out instead of
// stepping it back behind the dialog.
void TestAccessibility::dialogScrimIsDarkInBothSchemes()
{
    QFETCH(bool, dark);
    QFETCH(QString, dialog);
    Settings *shared = settings();
    QVERIFY(shared);
    shared->setColorScheme(dark ? Settings::ColorScheme::Dark : Settings::ColorScheme::Light);

    QObject *popup = nullptr;
    if (dialog == QLatin1StringView("deleteCallsDialog")) {
        QVERIFY(showCalls());
        QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("callsPage"));
        QVERIFY(page);
        popup = page->findChild<QObject *>(dialog);
        QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(page, "confirmDelete", Q_ARG(int, 2)));
        QTRY_VERIFY(popup->property("opened").toBool());
    } else {
        popup = openDialog(dialog);
        QVERIFY(popup);
    }

    QColor scrim;
    QTRY_VERIFY((scrim = scrimColor(m_window->contentItem())).isValid());
    QVERIFY2(qGray(scrim.rgb()) < 64, qPrintable(scrim.name(QColor::HexArgb)));
    QVERIFY2(scrim.alphaF() >= 0.3, qPrintable(scrim.name(QColor::HexArgb)));
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());
}

void TestAccessibility::contactsFocusChainReachesEveryControlOnce()
{
    QVERIFY(showContacts());
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    QVERIFY(chain.contains(findItem(m_window->contentItem(), QStringLiteral("contactList"))));
}

bool TestAccessibility::showCalls()
{
    m_phone = std::make_unique<qpm::test::FakePhone>();
    if (!m_phone->listen())
        return false;
    m_phone->setObjects(QStringLiteral("pb"),
                        {{1, contactCard(1, QStringLiteral("Erika"), QStringLiteral("Mustermann"),
                                         QStringLiteral("+491511234567"))}});
    m_phone->setCalls({fakeCall(1, QStringLiteral("RC"), QStringLiteral("+491511234567"), 65),
                       fakeCall(2, QStringLiteral("MC"), QStringLiteral("+49302000002"), 0),
                       fakeCall(3, QStringLiteral("DC"), QStringLiteral("+49402000003"), 12)});
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    if (!controller ||
        !controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port()))) {
        return false;
    }
    if (!QTest::qWaitFor([controller]() { return controller->isConnected(); }, phoneTimeoutMs))
        return false;
    const QFuture<qpm::core::Result<qpm::core::store::ContactChanges>> contacts =
        controller->contacts()->sync();
    if (!QTest::qWaitFor([&contacts]() { return contacts.isFinished(); }, phoneTimeoutMs) ||
        !contacts.result().hasValue()) {
        return false;
    }
    const QFuture<qpm::core::Result<qpm::core::store::CallChanges>> calls =
        controller->calls()->sync();
    if (!QTest::qWaitFor([&calls]() { return calls.isFinished(); }, phoneTimeoutMs) ||
        !calls.result().hasValue()) {
        return false;
    }
    m_window->setProperty("currentSection", 2);
    controller->calls()->setCurrentId(2);
    const bool shown = QTest::qWaitFor(
        [this]() {
            const QQuickItem *detail =
                findItem(m_window->contentItem(), QStringLiteral("callDetail"));
            return detail && detail->isVisible();
        },
        phoneTimeoutMs);
    announceAccessibility();
    return shown;
}

void TestAccessibility::callViewControlsHaveANameAndARole()
{
    QVERIFY(showCalls());

    const QList<QQuickItem *> found = controls();

    verifyNamesAndRoles(found, {QAccessible::List});
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("callList"));
    QVERIFY(list && found.contains(list));
    QCOMPARE(QAccessible::queryAccessibleInterface(list)->role(), QAccessible::List);
    for (const QString &name :
         {QStringLiteral("allCallsTab"), QStringLiteral("incomingCallsTab"),
          QStringLiteral("outgoingCallsTab"), QStringLiteral("missedCallsTab"),
          QStringLiteral("exportCallsButton"), QStringLiteral("deleteShownCallsButton"),
          QStringLiteral("callBackButton"), QStringLiteral("callMessageButton"),
          QStringLiteral("addCallToContactsButton"), QStringLiteral("deleteCallButton")}) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
    }
    // Icon-only buttons carry their action as their name.
    for (const QString &name :
         {QStringLiteral("exportCallsButton"), QStringLiteral("deleteShownCallsButton")}) {
        QQuickItem *button = findItem(m_window->contentItem(), name);
        const QString accessibleName =
            QAccessible::queryAccessibleInterface(button)->text(QAccessible::Name);
        QVERIFY2(!accessibleName.isEmpty(), qPrintable(name));
        QCOMPARE(accessibleName, button->property("text").toString());
    }
}

void TestAccessibility::callDialogControlsHaveANameAndARole()
{
    QVERIFY(showCalls());
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("callsPage"));
    QVERIFY(page);
    QObject *popup = page->findChild<QObject *>(QStringLiteral("deleteCallsDialog"));
    QVERIFY(popup);
    const QList<QQuickItem *> before = controls();
    QVERIFY(QMetaObject::invokeMethod(page, "confirmDelete", Q_ARG(int, 2)));
    QTRY_VERIFY(popup->property("opened").toBool());
    announceAccessibility();

    QList<QQuickItem *> inDialog = controls();
    inDialog.removeIf([&before](QQuickItem *control) { return before.contains(control); });

    QVERIFY(!inDialog.isEmpty());
    verifyNamesAndRoles(inDialog);
    QQuickItem *focused = m_window->activeFocusItem();
    QVERIFY(focused);
    QCOMPARE(focused->property("text").toString(), QStringLiteral("Cancel"));
    QVERIFY2(focused->property("visualFocus").toBool(), "the focus is not shown");
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());

    // Opened a second time, the dialog shows the focus again.
    QVERIFY(QMetaObject::invokeMethod(page, "confirmDeleteShown"));
    QTRY_VERIFY(popup->property("opened").toBool());
    QVERIFY(m_window->activeFocusItem());
    QCOMPARE(m_window->activeFocusItem()->property("text").toString(), QStringLiteral("Cancel"));
    QVERIFY2(m_window->activeFocusItem()->property("visualFocus").toBool(),
             "the focus is not shown after opening again");
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());
}

void TestAccessibility::callsFocusChainReachesEveryControlOnce()
{
    QVERIFY(showCalls());
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    for (const QString &name :
         {QStringLiteral("callList"), QStringLiteral("missedCallsTab"),
          QStringLiteral("exportCallsButton"), QStringLiteral("callBackButton")}) {
        QVERIFY2(chain.contains(findItem(m_window->contentItem(), name)), qPrintable(name));
    }
}

// A conversation with a picture, one with a sender name, and a scheduled message.
bool TestAccessibility::showMessages()
{
    m_phone = std::make_unique<qpm::test::FakePhone>();
    if (!m_phone->listen())
        return false;
    m_phone->config().scriptedReplies.insert(
        QStringLiteral("AT*SIM"), {QByteArrayLiteral("AT*SIM\r\n*SIM: 0,,Privat,Beispielnetz,VS\r\n"
                                                     "*SIM: 1,,Arbeit,Beispielnetz,\r\nOK\r\n")});
    m_phone->setObjects(QStringLiteral("pb"),
                        {{1, contactCard(1, QStringLiteral("Erika"), QStringLiteral("Mustermann"),
                                         QStringLiteral("+491511234567"))}});
    const QByteArray stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ")).toLatin1();
    m_phone->setObjects(
        QStringLiteral("msg"),
        {{11, "BEGIN:VMESSAGE\r\nTYP:SMS;IN\r\nBOX:INBOX\r\nREAD:0\r\nDATE:20261101T080000Z\r\n"
              "NUMBER:Paketdienst\r\nBODY:Ihr Paket kommt heute.\r\nX-IRMC-LUID:00000000000B\r\n"
              "END:VMESSAGE\r\n"},
         {0x10000002, "BEGIN:VMESSAGE\r\nTYP:MMS;IN\r\nBOX:INBOX\r\nREAD:0\r\nDATE:" + stamp +
                          "\r\nNUMBER:+491511234567\r\nSUBJECT:Ausflug\r\n"
                          "ATT;NAME=text_0.txt;TYPE=text/plain:Schau mal\r\n"
                          "ATT;NAME=bild.png;TYPE=image/png;SIZE=3;ENCODING=BASE64:UE5H\r\n\r\n"
                          "X-IRMC-LUID:000010000002\r\nEND:VMESSAGE\r\n"},
         {0x20000001, "BEGIN:VMESSAGE\r\nTYP:SMS;OUT\r\nBOX:SHEDULED\r\n"
                      "RECIPIENT:Erika;+491511234567;SHEDULED;;\r\nDATE:20301224T170000Z\r\n"
                      "BODY:Frohes Fest\r\nX-IRMC-LUID:000020000001\r\nEND:VMESSAGE\r\n"}});
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    if (!controller ||
        !controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port()))) {
        return false;
    }
    if (!QTest::qWaitFor([controller]() { return controller->isConnected(); }, phoneTimeoutMs))
        return false;
    const QFuture<qpm::core::Result<qpm::core::store::ContactChanges>> contacts =
        controller->contacts()->sync();
    if (!QTest::qWaitFor([&contacts]() { return contacts.isFinished(); }, phoneTimeoutMs) ||
        !contacts.result().hasValue()) {
        return false;
    }
    const QFuture<qpm::core::Result<qpm::core::store::MessageChanges>> messages =
        controller->messages()->sync();
    if (!QTest::qWaitFor([&messages]() { return messages.isFinished(); }, phoneTimeoutMs) ||
        !messages.result().hasValue()) {
        return false;
    }
    m_window->setProperty("currentSection", 3);
    qpm::ui::MessagesArea *area = controller->messages();
    area->setCurrentThread(area->conversations()->threadKeyAt(0));
    const bool shown = QTest::qWaitFor(
        [this]() {
            const QQuickItem *view =
                findItem(m_window->contentItem(), QStringLiteral("conversationView"));
            return view && view->isVisible();
        },
        phoneTimeoutMs);
    announceAccessibility();
    return shown;
}

void TestAccessibility::messageViewControlsHaveANameAndARole_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QStringList>("required");

    QTest::newRow("conversation") << QStringLiteral("conversation")
                                  << QStringList{QStringLiteral("conversationsTab"),
                                                 QStringLiteral("scheduledTab"),
                                                 QStringLiteral("newMessageButton"),
                                                 QStringLiteral("importArchiveButton"),
                                                 QStringLiteral("exportAllMessagesButton"),
                                                 QStringLiteral("conversationList"),
                                                 QStringLiteral("messageList"),
                                                 QStringLiteral("replyButton"),
                                                 QStringLiteral("markReadButton"),
                                                 QStringLiteral("archiveConversationButton"),
                                                 QStringLiteral("exportConversationButton"),
                                                 QStringLiteral("deleteConversationButton")};
    QTest::newRow("compose") << QStringLiteral("compose")
                             << QStringList{QStringLiteral("recipientChip0"),
                                            QStringLiteral("recipientField"),
                                            QStringLiteral("addRecipientButton"),
                                            QStringLiteral("messageTextArea"),
                                            QStringLiteral("simBox"),
                                            QStringLiteral("scheduleBox"),
                                            QStringLiteral("scheduleDateField"),
                                            QStringLiteral("scheduleTimeField"),
                                            QStringLiteral("cancelComposeButton"),
                                            QStringLiteral("submitMessageButton")};
    QTest::newRow("scheduled") << QStringLiteral("scheduled")
                               << QStringList{QStringLiteral("scheduledList"),
                                              QStringLiteral("scheduledTab")};
}

void TestAccessibility::messageViewControlsHaveANameAndARole()
{
    QFETCH(QString, view);
    QFETCH(QStringList, required);
    QVERIFY(showMessages());
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    qpm::ui::MessagesArea *area = controller->messages();
    if (view == QLatin1StringView("compose")) {
        QVERIFY(area->composeTo(QStringLiteral("0151 1234567")));
        area->composer()->setScheduled(true);
        QTRY_VERIFY(
            findItem(m_window->contentItem(), QStringLiteral("scheduleDateField")) &&
            findItem(m_window->contentItem(), QStringLiteral("scheduleDateField"))->isVisible());
        announceAccessibility();
    } else if (view == QLatin1StringView("scheduled")) {
        QQuickItem *tabs = findItem(m_window->contentItem(), QStringLiteral("messageViewTabs"));
        QVERIFY(tabs);
        tabs->setProperty("currentIndex", 1);
        QTRY_VERIFY(findItem(m_window->contentItem(), QStringLiteral("scheduledList")));
        announceAccessibility();
    }

    const QList<QQuickItem *> found = controls();

    verifyNamesAndRoles(found, {QAccessible::List});
    for (const QString &name : std::as_const(required)) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
    }
    // Icon-only buttons carry their action as their name and as their tooltip.
    for (QQuickItem *item : found) {
        // AbstractButton.IconOnly is the first value of the display enumeration.
        const bool iconOnly = item->metaObject()->indexOfProperty("display") >= 0 &&
                              item->property("display").toInt() == 0;
        if (!iconOnly || item->property("text").toString().isEmpty())
            continue;
        const QString accessibleName =
            QAccessible::queryAccessibleInterface(item)->text(QAccessible::Name);
        QCOMPARE(accessibleName, item->property("text").toString());
    }
}

void TestAccessibility::messageDialogShowsItsFocusAndNamesItsControls()
{
    QVERIFY(showMessages());
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("messagesPage"));
    QVERIFY(page);
    QObject *popup = page->findChild<QObject *>(QStringLiteral("deleteMessagesDialog"));
    QVERIFY(popup);
    const QList<QQuickItem *> before = controls();

    for (int opening = 1; opening <= 2; ++opening) {
        QVERIFY(QMetaObject::invokeMethod(page, "confirmDeleteConversation"));
        QTRY_VERIFY(popup->property("opened").toBool());
        announceAccessibility();
        QList<QQuickItem *> inDialog = controls();
        inDialog.removeIf([&before](QQuickItem *control) { return before.contains(control); });
        QVERIFY(!inDialog.isEmpty());
        verifyNamesAndRoles(inDialog);
        QQuickItem *focused = m_window->activeFocusItem();
        QVERIFY(focused);
        QCOMPARE(focused->property("text").toString(), QStringLiteral("Cancel"));
        QVERIFY2(focusIsShown(focused), qPrintable(QStringLiteral("opening %1").arg(opening)));
        QVERIFY(qGray(scrimColor(m_window->contentItem()).rgb()) < 64);
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
}

void TestAccessibility::messagesFocusChainReachesEveryControlOnce()
{
    QVERIFY(showMessages());
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    for (const QString &name : {QStringLiteral("conversationList"), QStringLiteral("messageList"),
                                QStringLiteral("replyButton"), QStringLiteral("scheduledTab")}) {
        QVERIFY2(chain.contains(findItem(m_window->contentItem(), name)), qPrintable(name));
    }
}

// The outlined fields of the message form show their focus in their own
// border; a ring drawn on top crossed the floating label.
void TestAccessibility::textFieldsShowTheirFocusWithoutARingOnTop()
{
    QVERIFY(showMessages());
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    QVERIFY(controller->messages()->composeTo(QStringLiteral("0151 1234567")));
    controller->messages()->composer()->setScheduled(true);
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));

    for (const QString &name : {QStringLiteral("messageTextArea"), QStringLiteral("recipientField"),
                                QStringLiteral("scheduleDateField"),
                                QStringLiteral("scheduleTimeField")}) {
        QTRY_VERIFY2(findItem(m_window->contentItem(), name), qPrintable(name));
        QQuickItem *field = findItem(m_window->contentItem(), name);
        field->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY2(field->hasActiveFocus(), qPrintable(name));
        const QList<QQuickItem *> children = field->childItems();
        for (const QQuickItem *child : children) {
            const QString type = QString::fromLatin1(child->metaObject()->className());
            QVERIFY2(!(type.startsWith(QLatin1StringView("FocusRing")) && child->isVisible()),
                     qPrintable(name));
        }
        QVERIFY2(focusIsShown(field), qPrintable(name));
    }
}

bool TestAccessibility::showOrganizer(int section)
{
    m_phone = std::make_unique<qpm::test::FakePhone>();
    if (!m_phone->listen())
        return false;
    m_phone->setObjects(QStringLiteral("cal"), organizerObjects());
    m_phone->setMetadata(QStringLiteral("cal"), organizerMetadata());
    m_phone->setObjects(QStringLiteral("notes"), {{1, exampleNote()}});
    m_phone->config().alarms = QStringList{QStringLiteral("\"07:30\",1,0,\"Wecker\",\"1,2,3,4,5\",1")};
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    if (!controller ||
        !controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port()))) {
        return false;
    }
    if (!QTest::qWaitFor([controller]() { return controller->isConnected(); }, phoneTimeoutMs))
        return false;
    const QFuture<qpm::core::Result<qpm::core::store::OrganizerChanges>> calendar =
        controller->calendar()->sync();
    if (!QTest::qWaitFor([&calendar]() { return calendar.isFinished(); }, phoneTimeoutMs) ||
        !calendar.result().hasValue()) {
        return false;
    }
    const QFuture<qpm::core::Result<qpm::core::store::OrganizerChanges>> notes =
        controller->notes()->sync();
    if (!QTest::qWaitFor([&notes]() { return notes.isFinished(); }, phoneTimeoutMs) ||
        !notes.result().hasValue()) {
        return false;
    }
    controller->calendar()->setViewMode(qpm::ui::CalendarArea::ViewMode::Month);
    controller->calendar()->setCurrentDate(QDate(2026, 11, 12));
    m_window->setProperty("currentSection", section);
    const QString pageName = section == 4   ? QStringLiteral("calendarPage")
                             : section == 5 ? QStringLiteral("tasksPage")
                                            : QStringLiteral("notesPage");
    const bool shown = QTest::qWaitFor(
        [this, pageName]() {
            const QQuickItem *page = findItem(m_window->contentItem(), pageName);
            return page && page->isVisible();
        },
        phoneTimeoutMs);
    announceAccessibility();
    return shown;
}

void TestAccessibility::organizerViewControlsHaveANameAndARole_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QStringList>("required");

    QTest::newRow("month") << QStringLiteral("month")
                           << QStringList{QStringLiteral("monthTab"), QStringLiteral("weekTab"),
                                          QStringLiteral("dayTab"), QStringLiteral("listTab"),
                                          QStringLiteral("previousPeriodButton"),
                                          QStringLiteral("todayButton"),
                                          QStringLiteral("nextPeriodButton"),
                                          QStringLiteral("newEventButton"),
                                          QStringLiteral("importCalendarButton"),
                                          QStringLiteral("exportCalendarButton"),
                                          QStringLiteral("chooseCalendarsButton"),
                                          QStringLiteral("alarmsButton"),
                                          QStringLiteral("holidayBox"),
                                          QStringLiteral("calendarGrid"),
                                          QStringLiteral("newEventOnDayButton")};
    QTest::newRow("day and detail")
        << QStringLiteral("day")
        << QStringList{QStringLiteral("eventList"), QStringLiteral("editEventButton"),
                       QStringLiteral("skipOccurrenceButton"), QStringLiteral("deleteEventButton"),
                       QStringLiteral("backToDayButton")};
    QTest::newRow("event form")
        << QStringLiteral("eventForm")
        << QStringList{QStringLiteral("eventSummaryField"), QStringLiteral("eventLocationField"),
                       QStringLiteral("allDayBox"), QStringLiteral("eventStartDateField"),
                       QStringLiteral("eventStartTimeField"), QStringLiteral("eventEndDateField"),
                       QStringLiteral("eventEndTimeField"), QStringLiteral("eventCalendarBox"),
                       QStringLiteral("eventRepeatBox"), QStringLiteral("eventColorBox"),
                       QStringLiteral("reminderChoiceBox"), QStringLiteral("addReminderButton"),
                       QStringLiteral("eventDescriptionArea"), QStringLiteral("saveEventButton"),
                       QStringLiteral("cancelEventButton")};
    QTest::newRow("tasks") << QStringLiteral("tasks")
                           << QStringList{QStringLiteral("openTasksTab"),
                                          QStringLiteral("doneTasksTab"),
                                          QStringLiteral("allTasksTab"),
                                          QStringLiteral("newTaskButton"),
                                          QStringLiteral("importTasksButton"),
                                          QStringLiteral("exportTasksButton"),
                                          QStringLiteral("taskCategoriesButton"),
                                          QStringLiteral("taskList")};
    QTest::newRow("task form")
        << QStringLiteral("taskForm")
        << QStringList{QStringLiteral("taskSummaryField"), QStringLiteral("taskCompletedBox"),
                       QStringLiteral("taskDueField"), QStringLiteral("taskPriorityBox"),
                       QStringLiteral("taskNewCategoryField"), QStringLiteral("saveTaskButton")};
    QTest::newRow("notes") << QStringLiteral("notes")
                           << QStringList{QStringLiteral("noteCategoryFilter"),
                                          QStringLiteral("newNoteButton"),
                                          QStringLiteral("noteCategoriesButton"),
                                          QStringLiteral("noteList")};
    QTest::newRow("note form") << QStringLiteral("noteForm")
                               << QStringList{QStringLiteral("noteBodyArea"),
                                              QStringLiteral("noteNewCategoryField"),
                                              QStringLiteral("saveNoteButton"),
                                              QStringLiteral("cancelNoteButton")};
}

void TestAccessibility::organizerViewControlsHaveANameAndARole()
{
    QFETCH(QString, view);
    QFETCH(QStringList, required);
    const int section = view.startsWith(QLatin1StringView("task"))   ? 5
                        : view.startsWith(QLatin1StringView("note")) ? 6
                                                                     : 4;
    QVERIFY(showOrganizer(section));
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    const QString pageName = section == 4   ? QStringLiteral("calendarPage")
                             : section == 5 ? QStringLiteral("tasksPage")
                                            : QStringLiteral("notesPage");
    QQuickItem *page = findItem(m_window->contentItem(), pageName);
    QVERIFY(page);
    if (view == QLatin1StringView("day")) {
        controller->calendar()->setViewMode(qpm::ui::CalendarArea::ViewMode::Day);
        controller->calendar()->selectRow(0);
        QTRY_VERIFY(findItem(m_window->contentItem(), QStringLiteral("eventDetail")));
    } else if (view == QLatin1StringView("eventForm") || view == QLatin1StringView("taskForm") ||
               view == QLatin1StringView("noteForm")) {
        QVERIFY(QMetaObject::invokeMethod(page, "startNew"));
        QTRY_VERIFY(findItem(m_window->contentItem(), view));
    }
    announceAccessibility();

    const QList<QQuickItem *> found = controls();

    verifyNamesAndRoles(found, {QAccessible::List, QAccessible::Table});
    for (const QString &name : std::as_const(required)) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
    }
    for (QQuickItem *item : found) {
        const bool iconOnly = item->metaObject()->indexOfProperty("display") >= 0 &&
                              item->property("display").toInt() == 0;
        if (!iconOnly || item->property("text").toString().isEmpty())
            continue;
        QCOMPARE(QAccessible::queryAccessibleInterface(item)->text(QAccessible::Name),
                 item->property("text").toString());
    }
    page->setProperty("editing", false);
}

void TestAccessibility::organizerDialogsShowTheFocusAndNameTheirControls_data()
{
    QTest::addColumn<int>("section");
    QTest::addColumn<QString>("dialog");

    QTest::newRow("calendars") << 4 << QStringLiteral("calendarsDialog");
    QTest::newRow("alarms") << 4 << QStringLiteral("alarmsDialog");
    QTest::newRow("delete appointment") << 4 << QStringLiteral("deleteEventDialog");
    QTest::newRow("task categories") << 5 << QStringLiteral("taskCategoriesDialog");
    QTest::newRow("note categories") << 6 << QStringLiteral("noteCategoriesDialog");
}

void TestAccessibility::organizerDialogsShowTheFocusAndNameTheirControls()
{
    QFETCH(int, section);
    QFETCH(QString, dialog);
    QVERIFY(showOrganizer(section));
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    const QList<QQuickItem *> before = controls();

    QObject *popup = openDialog(dialog);
    QVERIFY(popup);
    if (dialog == QLatin1StringView("alarmsDialog")) {
        QTRY_VERIFY(findItem(m_window->contentItem(), QStringLiteral("alarmList")) &&
                    findItem(m_window->contentItem(), QStringLiteral("alarmList"))->isVisible());
    }
    announceAccessibility();

    QList<QQuickItem *> inDialog = controls();
    inDialog.removeIf([&before](QQuickItem *control) { return before.contains(control); });
    QVERIFY(!inDialog.isEmpty());
    verifyNamesAndRoles(inDialog, {QAccessible::List, QAccessible::Table});
    QQuickItem *focused = m_window->activeFocusItem();
    QVERIFY(focused);
    QVERIFY2(focusIsShown(focused), qPrintable(describe(focused)));
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());
}

void TestAccessibility::calendarDaysAreChosenByKeyboard()
{
    QVERIFY(showOrganizer(4));
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    qpm::ui::CalendarArea *area = controller->calendar();
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *grid = findItem(m_window->contentItem(), QStringLiteral("calendarGrid"));
    QVERIFY(grid);
    QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(grid);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::Table);
    grid->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(grid->hasActiveFocus());

    QTest::keyClick(m_window, Qt::Key_Right);
    QCOMPARE(area->currentDate(), QDate(2026, 11, 13));
    QTest::keyClick(m_window, Qt::Key_Down);
    QCOMPARE(area->currentDate(), QDate(2026, 11, 20));
    QTest::keyClick(m_window, Qt::Key_Left);
    QTest::keyClick(m_window, Qt::Key_Up);
    QCOMPARE(area->currentDate(), QDate(2026, 11, 12));
    QTest::keyClick(m_window, Qt::Key_PageDown);
    QCOMPARE(area->currentDate(), QDate(2026, 12, 12));
    QTest::keyClick(m_window, Qt::Key_PageUp);
    QCOMPARE(area->currentDate(), QDate(2026, 11, 12));
    QVERIFY(grid->hasActiveFocus());
}

// Switching from month to week builds the new cells with month entries first
// and swaps them for week entries once the row count follows. The swapped-out
// entries had the index -1 for a moment, and the colour dot of each warned that
// it got no colour.
void TestAccessibility::calendarSwitchesBetweenMonthAndWeekWithoutWarnings()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral("CalendarGrid\\.qml")));
    QVERIFY(showOrganizer(4));
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    qpm::ui::CalendarArea *area = controller->calendar();
    // The line limit of the first entry the grid draws: three in a week, none in a month.
    const auto entryLineLimit = [this]() {
        const QQuickItem *grid = findItem(m_window->contentItem(), QStringLiteral("calendarGrid"));
        QList<const QQuickItem *> pending;
        if (grid)
            pending.append(grid);
        while (!pending.isEmpty()) {
            const QQuickItem *next = pending.takeLast();
            if (next->inherits("QQuickText") &&
                next->property("text").toString().contains(QStringLiteral("Elternabend"))) {
                return next->property("maximumLineCount").toInt();
            }
            const QList<QQuickItem *> children = next->childItems();
            for (const QQuickItem *child : children)
                pending.append(child);
        }
        return 0;
    };
    constexpr int weekLines = 3;

    QTRY_VERIFY(entryLineLimit() > weekLines);
    // The warning only came once the month had been drawn with the grid holding the focus.
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *grid = findItem(m_window->contentItem(), QStringLiteral("calendarGrid"));
    QVERIFY(grid);
    grid->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(grid->hasActiveFocus());
    QVERIFY(!m_window->grabWindow().isNull());
    area->setViewMode(qpm::ui::CalendarArea::ViewMode::Week);
    QTRY_COMPARE(entryLineLimit(), weekLines);
    area->setViewMode(qpm::ui::CalendarArea::ViewMode::Month);
    QTRY_VERIFY(entryLineLimit() > weekLines);
}

void TestAccessibility::organizerFocusChainReachesEveryControlOnce_data()
{
    QTest::addColumn<int>("section");
    QTest::addColumn<QStringList>("required");

    QTest::newRow("calendar") << 4
                              << QStringList{QStringLiteral("calendarGrid"),
                                             QStringLiteral("newEventButton"),
                                             QStringLiteral("holidayBox")};
    QTest::newRow("tasks") << 5 << QStringList{QStringLiteral("taskList"),
                                               QStringLiteral("newTaskButton")};
    QTest::newRow("notes") << 6 << QStringList{QStringLiteral("noteList"),
                                               QStringLiteral("noteCategoryFilter")};
}

void TestAccessibility::organizerFocusChainReachesEveryControlOnce()
{
    QFETCH(int, section);
    QFETCH(QStringList, required);
    QVERIFY(showOrganizer(section));
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    for (const QString &name : std::as_const(required))
        QVERIFY2(chain.contains(findItem(m_window->contentItem(), name)), qPrintable(name));
}

void TestAccessibility::darkSchemeDarkensTheWindow()
{
    Settings *shared = settings();
    QVERIFY(shared);
    QTemporaryDir shots;
    QVERIFY(shots.isValid());
    shots.setAutoRemove(qEnvironmentVariableIsEmpty("QPM_KEEP_SCREENSHOTS"));

    shared->setColorScheme(Settings::ColorScheme::Light);
    QTRY_VERIFY(meanLightness(m_window->grabWindow()) > 0.7);
    const QImage light = m_window->grabWindow();
    shared->setColorScheme(Settings::ColorScheme::Dark);
    QTRY_VERIFY(meanLightness(m_window->grabWindow()) < 0.3);
    const QImage dark = m_window->grabWindow();

    QVERIFY(light.save(shots.filePath(QStringLiteral("shell-light.png"))));
    QVERIFY(dark.save(shots.filePath(QStringLiteral("shell-dark.png"))));
    if (!shots.autoRemove())
        qInfo() << "Screenshots kept in" << shots.path();
}

bool TestAccessibility::showFiles()
{
    m_phone = std::make_unique<qpm::test::FakePhone>();
    if (!m_phone->listen())
        return false;
    const QDateTime when(QDate(2026, 11, 2), QTime(9, 0), QTimeZone::UTC);
    m_phone->addFile(QStringLiteral("\\sdcard\\DCIM\\Camera\\See.jpg"), "jpeg", when);
    m_phone->addFile(QStringLiteral("\\sdcard\\Download\\Brief.pdf"), "pdf", when);
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    if (!controller ||
        !controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port()))) {
        return false;
    }
    if (!QTest::qWaitFor([controller]() { return controller->isConnected(); }, phoneTimeoutMs))
        return false;
    m_window->setProperty("currentSection", 7);
    const bool shown = QTest::qWaitFor(
        [this]() {
            const QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("filesPage"));
            return page && page->isVisible();
        },
        phoneTimeoutMs);
    qpm::ui::FileBrowserModel *files = controller->files();
    if (!shown || !files->open(QStringLiteral("\\sdcard")))
        return false;
    const bool listed = QTest::qWaitFor(
        [files]() { return !files->isBusy() && files->count() == 2; }, phoneTimeoutMs);
    announceAccessibility();
    return listed;
}

void TestAccessibility::fileViewControlsHaveANameAndARole_data()
{
    QTest::addColumn<bool>("grid");

    QTest::newRow("grid") << true;
    QTest::newRow("list") << false;
}

void TestAccessibility::fileViewControlsHaveANameAndARole()
{
    QFETCH(bool, grid);
    QVERIFY(showFiles());
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("filesPage"));
    QVERIFY(page);
    page->setProperty("showGrid", grid);
    page->setProperty("currentRow", 0);
    QTRY_VERIFY(findItem(m_window->contentItem(),
                         grid ? QStringLiteral("fileGrid") : QStringLiteral("fileList"))
                    ->isVisible());
    announceAccessibility();

    const QList<QQuickItem *> found = controls();

    verifyNamesAndRoles(found, {QAccessible::List});
    const QStringList required = {QStringLiteral("folderUpButton"),
                                  QStringLiteral("crumb0"),
                                  QStringLiteral("crumb1"),
                                  QStringLiteral("newFolderButton"),
                                  QStringLiteral("uploadFilesButton"),
                                  QStringLiteral("downloadFilesButton"),
                                  QStringLiteral("pasteFilesButton"),
                                  QStringLiteral("refreshFilesButton"),
                                  QStringLiteral("fileViewButton"),
                                  grid ? QStringLiteral("fileGrid") : QStringLiteral("fileList")};
    for (const QString &name : required) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
    }
    for (QQuickItem *item : found) {
        const bool iconOnly = item->metaObject()->indexOfProperty("display") >= 0 &&
                              item->property("display").toInt() == 0;
        if (!iconOnly || item->property("text").toString().isEmpty())
            continue;
        QCOMPARE(QAccessible::queryAccessibleInterface(item)->text(QAccessible::Name),
                 item->property("text").toString());
    }
    QQuickItem *view = findItem(m_window->contentItem(),
                                grid ? QStringLiteral("fileGrid") : QStringLiteral("fileList"));
    const QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(view);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::List);
    QVERIFY(!accessible->text(QAccessible::Description).isEmpty());
}

void TestAccessibility::fileDialogsShowTheFocusAndNameTheirControls_data()
{
    QTest::addColumn<QString>("dialog");

    QTest::newRow("new folder") << QStringLiteral("fileNameDialog");
    QTest::newRow("delete") << QStringLiteral("deleteFileDialog");
    QTest::newRow("conflict") << QStringLiteral("fileConflictDialog");
    QTest::newRow("menu") << QStringLiteral("fileMenu");
}

void TestAccessibility::fileDialogsShowTheFocusAndNameTheirControls()
{
    QFETCH(QString, dialog);
    QVERIFY(showFiles());
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("filesPage"));
    QVERIFY(page);
    page->setProperty("currentRow", 1);
    const QList<QQuickItem *> before = controls();

    QObject *popup = nullptr;
    if (dialog == QLatin1StringView("fileNameDialog")) {
        QVERIFY(QMetaObject::invokeMethod(page, "startNewFolder"));
    } else if (dialog == QLatin1StringView("deleteFileDialog")) {
        QVERIFY(QMetaObject::invokeMethod(page, "confirmDelete"));
    } else if (dialog == QLatin1StringView("fileMenu")) {
        QVERIFY(QMetaObject::invokeMethod(page, "showMenu"));
    } else {
        popup = openDialog(dialog);
        QVERIFY(popup);
    }
    if (!popup)
        popup = page->findChild<QObject *>(dialog);
    QVERIFY(popup);
    QTRY_VERIFY(popup->property("opened").toBool());
    announceAccessibility();

    QList<QQuickItem *> inDialog = controls();
    inDialog.removeIf([&before](QQuickItem *control) { return before.contains(control); });
    QVERIFY(!inDialog.isEmpty());
    verifyNamesAndRoles(inDialog, {QAccessible::MenuItem});
    if (dialog != QLatin1StringView("fileMenu")) {
        QQuickItem *focused = m_window->activeFocusItem();
        QVERIFY(focused);
        QVERIFY2(focusIsShown(focused), qPrintable(describe(focused)));
    }
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());
}

void TestAccessibility::filesFocusChainReachesEveryControlOnce()
{
    QVERIFY(showFiles());
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    for (const QString &name : {QStringLiteral("fileGrid"), QStringLiteral("folderUpButton"),
                                QStringLiteral("crumb0"), QStringLiteral("newFolderButton")}) {
        QVERIFY2(chain.contains(findItem(m_window->contentItem(), name)), qPrintable(name));
    }
}

// Every action on a file is reached from the keyboard: opening, going up,
// renaming, deleting and the menu.
void TestAccessibility::filesAreBrowsedByKeyboard()
{
    QVERIFY(showFiles());
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    qpm::ui::FileBrowserModel *files = controller->files();
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("filesPage"));
    QQuickItem *grid = findItem(m_window->contentItem(), QStringLiteral("fileGrid"));
    QVERIFY(page && grid);
    page->setProperty("currentRow", 0);
    grid->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(grid->hasActiveFocus());

    QTest::keyClick(m_window, Qt::Key_Right);
    QTRY_COMPARE(page->property("currentRow").toInt(), 1);
    QTest::keyClick(m_window, Qt::Key_Left);
    QTRY_COMPARE(page->property("currentRow").toInt(), 0);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_COMPARE(files->path(), QStringLiteral("\\sdcard\\DCIM"));
    QTRY_VERIFY(!files->isBusy());
    QTest::keyClick(m_window, Qt::Key_Backspace);
    QTRY_COMPARE(files->path(), QStringLiteral("\\sdcard"));
    // The folder left is the one chosen again.
    QTRY_COMPARE(page->property("currentName").toString(), QStringLiteral("DCIM"));
    QVERIFY(grid->hasActiveFocus());

    QObject *nameDialog = page->findChild<QObject *>(QStringLiteral("fileNameDialog"));
    QTest::keyClick(m_window, Qt::Key_F2);
    QTRY_VERIFY(nameDialog->property("opened").toBool());
    QCOMPARE(nameDialog->property("oldName").toString(), QStringLiteral("DCIM"));
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!nameDialog->property("visible").toBool());

    grid->forceActiveFocus(Qt::TabFocusReason);
    QObject *deleteDialog = page->findChild<QObject *>(QStringLiteral("deleteFileDialog"));
    QTest::keyClick(m_window, Qt::Key_Delete);
    QTRY_VERIFY(deleteDialog->property("opened").toBool());
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!deleteDialog->property("visible").toBool());

    grid->forceActiveFocus(Qt::TabFocusReason);
    QObject *menu = page->findChild<QObject *>(QStringLiteral("fileMenu"));
    QTest::keyClick(m_window, Qt::Key_Menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QVERIFY(m_phone->hasFolder(QStringLiteral("\\sdcard\\DCIM")));
}

bool TestAccessibility::showApps()
{
    m_phone = std::make_unique<qpm::test::FakePhone>();
    if (!m_phone->listen())
        return false;
    m_phone->setApps(
        {fakeApp(QStringLiteral("org.example.notes"), QStringLiteral("Notizen"), true),
         fakeApp(QStringLiteral("org.example.weather"), QStringLiteral("Wetter"), true),
         fakeApp(QStringLiteral("com.android.settings"), QStringLiteral("Einstellungen"), false)});
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    if (!controller ||
        !controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port()))) {
        return false;
    }
    if (!QTest::qWaitFor([controller]() { return controller->isConnected(); }, phoneTimeoutMs))
        return false;
    m_window->setProperty("currentSection", 8);
    qpm::ui::AppListModel *apps = controller->apps();
    const bool listed = QTest::qWaitFor(
        [this, apps]() {
            const QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("appsPage"));
            return page && page->isVisible() && !apps->isBusy() && apps->count() == 2;
        },
        phoneTimeoutMs);
    announceAccessibility();
    return listed;
}

void TestAccessibility::appViewControlsHaveANameAndARole()
{
    QVERIFY(showApps());
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("appList"));
    QVERIFY(list);
    list->setProperty("currentIndex", 0);
    announceAccessibility();

    const QList<QQuickItem *> found = controls();

    verifyNamesAndRoles(found, {QAccessible::List});
    const QStringList required = {
        QStringLiteral("userAppsTab"),      QStringLiteral("systemAppsTab"),
        QStringLiteral("allAppsTab"),       QStringLiteral("startAppButton"),
        QStringLiteral("backupAppButton"),  QStringLiteral("uninstallAppButton"),
        QStringLiteral("installAppButton"), QStringLiteral("refreshAppsButton"),
        QStringLiteral("appList")};
    for (const QString &name : required) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
    }
    for (const QString &name : {QStringLiteral("startAppButton"), QStringLiteral("backupAppButton"),
                                QStringLiteral("uninstallAppButton")}) {
        QVERIFY2(findItem(m_window->contentItem(), name)->isEnabled(), qPrintable(name));
    }
    for (QQuickItem *item : found) {
        const bool iconOnly = item->metaObject()->indexOfProperty("display") >= 0 &&
                              item->property("display").toInt() == 0;
        if (!iconOnly || item->property("text").toString().isEmpty())
            continue;
        QCOMPARE(QAccessible::queryAccessibleInterface(item)->text(QAccessible::Name),
                 item->property("text").toString());
    }
    const QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(list);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::List);
    QVERIFY(!accessible->text(QAccessible::Description).isEmpty());
}

void TestAccessibility::appDialogsShowTheFocusAndNameTheirControls_data()
{
    QTest::addColumn<QString>("dialog");

    QTest::newRow("remove") << QStringLiteral("uninstallAppDialog");
    QTest::newRow("menu") << QStringLiteral("appMenu");
}

void TestAccessibility::appDialogsShowTheFocusAndNameTheirControls()
{
    QFETCH(QString, dialog);
    QVERIFY(showApps());
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("appsPage"));
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("appList"));
    QVERIFY(page && list);
    list->setProperty("currentIndex", 1);
    const QList<QQuickItem *> before = controls();

    QVERIFY(QMetaObject::invokeMethod(
        page, dialog == QLatin1StringView("appMenu") ? "showMenu" : "confirmUninstall"));
    QObject *popup = page->findChild<QObject *>(dialog);
    QVERIFY(popup);
    QTRY_VERIFY(popup->property("opened").toBool());
    announceAccessibility();

    QList<QQuickItem *> inDialog = controls();
    inDialog.removeIf([&before](QQuickItem *control) { return before.contains(control); });
    QVERIFY(!inDialog.isEmpty());
    verifyNamesAndRoles(inDialog, {QAccessible::MenuItem});
    if (dialog != QLatin1StringView("appMenu")) {
        // The question names the app it is about.
        QCOMPARE(popup->property("title").toString(), QStringLiteral("Remove Wetter?"));
        QQuickItem *focused = m_window->activeFocusItem();
        QVERIFY(focused);
        QVERIFY2(focusIsShown(focused), qPrintable(describe(focused)));
    }
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());
    QVERIFY(m_phone->uninstallRequests().isEmpty());
}

void TestAccessibility::appsFocusChainReachesEveryControlOnce()
{
    QVERIFY(showApps());
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("appList"));
    QVERIFY(list);
    list->setProperty("currentIndex", 0);
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    for (const QString &name :
         {QStringLiteral("appList"), QStringLiteral("userAppsTab"),
          QStringLiteral("startAppButton"), QStringLiteral("installAppButton")}) {
        QVERIFY2(chain.contains(findItem(m_window->contentItem(), name)), qPrintable(name));
    }
}

// Every action on an app is reached from the keyboard: choosing it, opening
// it on the phone, the question before its removal and the menu.
void TestAccessibility::appsAreUsedByKeyboard()
{
    QVERIFY(showApps());
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("appsPage"));
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("appList"));
    QVERIFY(page && list);
    list->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(list->hasActiveFocus());
    QVERIFY(findItem(m_window->contentItem(), QStringLiteral("appListFocusRing"))->isVisible());

    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_COMPARE(page->property("currentPackage").toString(),
                 QStringLiteral("org.example.weather"));
    QTest::keyClick(m_window, Qt::Key_Up);
    QTRY_COMPARE(page->property("currentPackage").toString(), QStringLiteral("org.example.notes"));

    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->startRequests(),
                              QStringList{QStringLiteral("org.example.notes")}, phoneTimeoutMs);

    QObject *removeDialog = page->findChild<QObject *>(QStringLiteral("uninstallAppDialog"));
    QTest::keyClick(m_window, Qt::Key_Delete);
    QTRY_VERIFY(removeDialog->property("opened").toBool());
    QCOMPARE(removeDialog->property("title").toString(), QStringLiteral("Remove Notizen?"));
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!removeDialog->property("visible").toBool());

    list->forceActiveFocus(Qt::TabFocusReason);
    QObject *menu = page->findChild<QObject *>(QStringLiteral("appMenu"));
    QTest::keyClick(m_window, Qt::Key_Menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!menu->property("visible").toBool());

    // The filter is chosen from the keyboard as well.
    QQuickItem *systemTab = findItem(m_window->contentItem(), QStringLiteral("systemAppsTab"));
    systemTab->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(m_window, Qt::Key_Space);
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    QTRY_COMPARE(controller->apps()->filter(), qpm::ui::AppListModel::Filter::System);
    QVERIFY(m_phone->uninstallRequests().isEmpty());
}

bool TestAccessibility::showScreen(bool control)
{
    QImage screen(270, 600, QImage::Format_RGB32);
    screen.fill(QColor(24, 48, 64));
    QPainter painter(&screen);
    painter.fillRect(QRect(20, 60, 230, 70), QColor(236, 240, 238));
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    screen.save(&buffer, "PNG");
    m_phone = std::make_unique<qpm::test::FakePhone>();
    if (!m_phone->listen())
        return false;
    m_phone->config().screenFrame = "FileBegin;270x600;0;270x600;PNG;100\r\n" + png + "\r\nFileEnd";
    m_phone->config().accessibilityService = control;
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    if (!controller ||
        !controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port()))) {
        return false;
    }
    if (!QTest::qWaitFor([controller]() { return controller->isConnected(); }, phoneTimeoutMs))
        return false;
    // The fake phone opens its screen channel on a port of its own.
    controller->screenClient()->setEndpointResolver(
        [this](quint16) -> std::optional<qpm::core::protocol::Endpoint> {
            return qpm::core::protocol::Endpoint{QStringLiteral("127.0.0.1"),
                                                 m_phone->screenPort()};
        });
    m_window->setProperty("currentSection", 9);
    qpm::ui::ScreenMirror *mirror = controller->screen();
    if (!QTest::qWaitFor(
            [this]() {
                const QQuickItem *page =
                    findItem(m_window->contentItem(), QStringLiteral("screenPage"));
                return page && page->isVisible();
            },
            phoneTimeoutMs) ||
        !mirror->start()) {
        return false;
    }
    const bool shown = QTest::qWaitFor([mirror]() { return mirror->hasFrame(); }, phoneTimeoutMs);
    announceAccessibility();
    return shown;
}

void TestAccessibility::screenViewControlsHaveANameAndARole_data()
{
    QTest::addColumn<bool>("control");

    QTest::newRow("controlled") << true;
    QTest::newRow("accessibility service off") << false;
}

void TestAccessibility::screenViewControlsHaveANameAndARole()
{
    QFETCH(bool, control);
    QVERIFY(showScreen(control));

    const QList<QQuickItem *> found = controls();

    verifyNamesAndRoles(found, {QAccessible::Graphic});
    QStringList required = {QStringLiteral("screenStartButton"),
                            QStringLiteral("screenBackButton"),
                            QStringLiteral("screenHomeButton"),
                            QStringLiteral("screenRecentsButton"),
                            QStringLiteral("screenKeyboardButton"),
                            QStringLiteral("screenRotateButton"),
                            QStringLiteral("screenFitButton"),
                            QStringLiteral("screenshotButton"),
                            QStringLiteral("screenView")};
    if (!control)
        required << QStringLiteral("screenAccessibilityButton")
                 << QStringLiteral("screenCheckButton");
    for (const QString &name : std::as_const(required)) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
    }
    // Keys that do nothing without control stay in view, switched off.
    for (const QString &name :
         {QStringLiteral("screenBackButton"), QStringLiteral("screenHomeButton"),
          QStringLiteral("screenRecentsButton"), QStringLiteral("screenKeyboardButton")}) {
        QCOMPARE(findItem(m_window->contentItem(), name)->isEnabled(), control);
    }
    for (QQuickItem *item : found) {
        const bool iconOnly = item->metaObject()->indexOfProperty("display") >= 0 &&
                              item->property("display").toInt() == 0;
        if (!iconOnly || item->property("text").toString().isEmpty())
            continue;
        QCOMPARE(QAccessible::queryAccessibleInterface(item)->text(QAccessible::Name),
                 item->property("text").toString());
    }
    QQuickItem *view = findItem(m_window->contentItem(), QStringLiteral("screenView"));
    const QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(view);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::Graphic);
    QVERIFY(!accessible->text(QAccessible::Description).isEmpty());
}

void TestAccessibility::screenFocusChainReachesEveryControlOnce()
{
    QVERIFY(showScreen(false));
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    for (const QString &name :
         {QStringLiteral("screenStartButton"), QStringLiteral("screenView"),
          QStringLiteral("screenAccessibilityButton"), QStringLiteral("screenshotButton")}) {
        QVERIFY2(chain.contains(findItem(m_window->contentItem(), name)), qPrintable(name));
    }
}

// The keys of the phone, typing into it and leaving the picture all work
// from the keyboard, and the picture shows when it has the focus.
void TestAccessibility::screenIsUsedByKeyboard()
{
    QVERIFY(showScreen(true));
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *back = findItem(m_window->contentItem(), QStringLiteral("screenBackButton"));
    QQuickItem *keyboard =
        findItem(m_window->contentItem(), QStringLiteral("screenKeyboardButton"));
    QQuickItem *view = findItem(m_window->contentItem(), QStringLiteral("screenView"));
    QVERIFY(back && keyboard && view);
    m_phone->clearLog();

    back->forceActiveFocus(Qt::TabFocusReason);
    QVERIFY(focusIsShown(back));
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(m_phone->receivedCommands().contains(QStringLiteral("AT*K=back")),
                             phoneTimeoutMs);

    keyboard->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_VERIFY(view->hasActiveFocus());
    QVERIFY(keyboard->property("checked").toBool());
    QVERIFY(findItem(m_window->contentItem(), QStringLiteral("screenViewFocusRing"))->isVisible());
    QTest::keyClick(m_window, Qt::Key_A);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(m_phone->receivedCommands().contains(QStringLiteral("AT*K=enter")),
                             phoneTimeoutMs);
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("AT*T=a,")));

    QTest::keyClick(m_window, Qt::Key_Tab);
    QTRY_VERIFY(!view->hasActiveFocus());
}

bool TestAccessibility::showNotifications()
{
    m_phone = std::make_unique<qpm::test::FakePhone>();
    if (!m_phone->listen())
        return false;
    QList<qpm::test::FakeNotification> shown;
    for (int id = 1; id <= 3; ++id) {
        qpm::test::FakeNotification notification;
        notification.id = id;
        notification.title = QStringLiteral("Nachricht %1").arg(id);
        notification.text = QStringLiteral("Text %1").arg(id);
        notification.actions = {QStringLiteral("Antworten"), QStringLiteral("Gelesen")};
        notification.posted =
            QDateTime(QDate(2026, 11, 12), QTime(9, 0), QTimeZone::UTC).addSecs(-60 * id);
        shown.append(notification);
    }
    m_phone->setNotifications(shown);
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    if (!controller ||
        !controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port()))) {
        return false;
    }
    if (!QTest::qWaitFor([controller]() { return controller->isConnected(); }, phoneTimeoutMs))
        return false;
    m_window->setProperty("currentSection", 10);
    qpm::ui::NotificationListModel *notifications = controller->notifications();
    const bool listed = QTest::qWaitFor(
        [this, notifications]() {
            const QQuickItem *page =
                findItem(m_window->contentItem(), QStringLiteral("notificationsPage"));
            return page && page->isVisible() && notifications->count() == 3;
        },
        phoneTimeoutMs);
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("notificationList"));
    if (!listed || !list)
        return false;
    list->setProperty("currentIndex", 0);
    announceAccessibility();
    return true;
}

void TestAccessibility::notificationViewControlsHaveANameAndARole()
{
    QVERIFY(showNotifications());

    const QList<QQuickItem *> found = controls();

    verifyNamesAndRoles(found, {QAccessible::List});
    for (const QString &name :
         {QStringLiteral("notificationList"), QStringLiteral("openNotificationButton"),
          QStringLiteral("dismissNotificationButton"),
          QStringLiteral("refreshNotificationsButton")}) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
        QVERIFY2(item->isEnabled(), qPrintable(name));
    }
    QStringList actions;
    for (QQuickItem *item : found) {
        if (item->parentItem() &&
            item->parentItem()->objectName() == QLatin1StringView("notificationActions"))
            actions.append(item->property("text").toString());
    }
    QCOMPARE(actions, (QStringList{QStringLiteral("Antworten"), QStringLiteral("Gelesen")}));
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("notificationList"));
    QCOMPARE(QAccessible::queryAccessibleInterface(list)->role(), QAccessible::List);
}

void TestAccessibility::notificationsFocusChainReachesEveryControlOnce()
{
    QVERIFY(showNotifications());
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    for (const QString &name :
         {QStringLiteral("notificationList"), QStringLiteral("dismissNotificationButton")}) {
        QVERIFY2(chain.contains(findItem(m_window->contentItem(), name)), qPrintable(name));
    }
}

// Choosing a notification, opening it on the phone, dismissing it and its
// actions all work from the keyboard, and the list shows its focus.
void TestAccessibility::notificationsAreUsedByKeyboard()
{
    QVERIFY(showNotifications());
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("notificationsPage"));
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("notificationList"));
    QVERIFY(page && list);
    list->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(list->hasActiveFocus());
    QVERIFY(findItem(m_window->contentItem(), QStringLiteral("notificationListFocusRing"))
                ->isVisible());

    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_COMPARE(page->property("currentKey").toString(), QStringLiteral("org.example.chat|2|"));
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->notificationActions().size(), 1, phoneTimeoutMs);
    QCOMPARE(m_phone->notificationActions().constFirst(),
             (std::pair<QString, QString>{QStringLiteral("org.example.chat|2|"), QString()}));
    QTest::keyClick(m_window, Qt::Key_Delete);
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->notifications().size(), 2, phoneTimeoutMs);
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    QTRY_COMPARE_WITH_TIMEOUT(controller->notifications()->count(), 2, phoneTimeoutMs);
    QTRY_COMPARE(page->property("currentKey").toString(), QStringLiteral("org.example.chat|3|"));

    // The actions of the chosen notification are reached with Tab.
    QQuickItem *actions = findItem(m_window->contentItem(), QStringLiteral("notificationActions"));
    QTRY_VERIFY(actions && actions->isVisible());
    QList<QQuickItem *> buttons;
    collectControls(actions, buttons);
    QQuickItem *first = buttons.value(0);
    QVERIFY(first);
    first->forceActiveFocus(Qt::TabFocusReason);
    QVERIFY(focusIsShown(first));
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->notificationActions().size(), 2, phoneTimeoutMs);
    QVERIFY(!m_phone->notificationActions().constLast().second.isEmpty());
}

// The call window is a window of its own; its buttons are named, those that
// do not apply are switched off rather than hidden, and the focus starts on
// the button that fits the call.
void TestAccessibility::callWindowNamesItsControlsAndShowsItsFocus()
{
    QVERIFY(showNotifications());
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    auto *callWindow = m_window->findChild<QQuickWindow *>(QStringLiteral("callWindow"));
    QVERIFY(callWindow);

    m_phone->setCallState(6, QStringLiteral("+4930999999"));
    QTRY_VERIFY_WITH_TIMEOUT(callWindow->isVisible(), phoneTimeoutMs);
    callWindow->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(callWindow));
    announceAccessibility();
    QList<QQuickItem *> found;
    collectControls(callWindow->contentItem(), found);

    verifyNamesAndRoles(found);
    QStringList names;
    for (const QQuickItem *control : std::as_const(found))
        names.append(control->objectName());
    QCOMPARE(names,
             (QStringList{QStringLiteral("callAnswerButton"), QStringLiteral("callHangUpButton"),
                          QStringLiteral("callMuteButton"), QStringLiteral("callSpeakerButton")}));
    QQuickItem *answer = found.at(0);
    QVERIFY(answer->isEnabled());
    QVERIFY(!found.at(2)->isEnabled());
    QVERIFY(!found.at(3)->isEnabled());
    QTRY_VERIFY(answer->hasActiveFocus());
    QVERIFY(focusIsShown(answer));
    QCOMPARE(found.at(1)->property("text").toString(), QStringLiteral("Decline"));

    QVERIFY(controller->call()->hangUp());
    QTRY_VERIFY_WITH_TIMEOUT(!callWindow->isVisible(), phoneTimeoutMs);
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("ATH")));
}

// The backup dialog starts on its first tab with the focus shown; the sections
// are chosen with the space bar, and after a preview the sections to restore
// carry a name, a role and what restoring them would do.
void TestAccessibility::backupDialogIsUsedByKeyboard()
{
    QVERIFY(showContacts());
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    qpm::ui::BackupController *backup = controller->backup();
    // Moves the focus with the tab key until it reaches `name`, as a user does.
    const auto tabTo = [this](const QString &name) {
        for (int press = 0; press < 40; ++press) {
            const QQuickItem *focused = m_window->activeFocusItem();
            if (focused && focused->objectName() == name)
                return true;
            QTest::keyClick(m_window, Qt::Key_Tab);
        }
        return false;
    };
    const QList<QQuickItem *> before = controls();
    QObject *popup = openDialog(QStringLiteral("backupDialog"));
    QVERIFY(popup);
    announceAccessibility();
    QQuickItem *focused = m_window->activeFocusItem();
    QVERIFY(focused);
    QCOMPARE(focused->objectName(), QStringLiteral("backupTab"));
    QVERIFY(focusIsShown(focused));

    QVERIFY(tabTo(QStringLiteral("backup-messages")));
    QVERIFY(focusIsShown(m_window->activeFocusItem()));
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_VERIFY(!backup->sections()->chosenKeys().contains(QStringLiteral("messages")));
    QTest::keyClick(m_window, Qt::Key_Tab);
    QTRY_COMPARE(m_window->activeFocusItem()->objectName(), QStringLiteral("saveBackupButton"));
    QVERIFY(focusIsShown(m_window->activeFocusItem()));

    // The file dialog is the system's; the backup itself is made directly.
    const QUrl url = QUrl::fromLocalFile(m_config.filePath(QStringLiteral("a11y.qpmbackup")));
    QVERIFY(backup->create(url, backup->sections()->chosenKeys()));
    QTRY_VERIFY_WITH_TIMEOUT(!backup->isBusy(), phoneTimeoutMs);
    m_phone->removeObject(QStringLiteral("pb"), 2);

    QQuickItem *tabs = findItem(m_window->contentItem(), QStringLiteral("backupTabs"));
    QVERIFY(tabs);
    QVERIFY(tabTo(QStringLiteral("restoreTab")));
    QVERIFY(focusIsShown(m_window->activeFocusItem()));
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_COMPARE(tabs->property("currentIndex").toInt(), 1);
    popup->setProperty("openedFile", url);
    QVERIFY(backup->preview(url));
    QTRY_VERIFY_WITH_TIMEOUT(!backup->isBusy(), phoneTimeoutMs);
    QVERIFY(backup->hasPreview());
    announceAccessibility();

    QList<QQuickItem *> inDialog = controls();
    inDialog.removeIf([&before](QQuickItem *control) { return before.contains(control); });
    verifyNamesAndRoles(inDialog);
    QQuickItem *contacts = findItem(m_window->contentItem(), QStringLiteral("restore-contacts"));
    QVERIFY(contacts && inDialog.contains(contacts) && contacts->isEnabled());
    QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(contacts);
    QCOMPARE(accessible->role(), QAccessible::CheckBox);
    QVERIFY(accessible->text(QAccessible::Description).contains(QStringLiteral("New: 1")));
    QQuickItem *restore = findItem(m_window->contentItem(), QStringLiteral("restoreBackupButton"));
    QTRY_VERIFY(restore->isEnabled());
    QVERIFY(tabTo(QStringLiteral("restore-contacts")));
    QVERIFY(focusIsShown(m_window->activeFocusItem()));
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_VERIFY(backup->restoreSections()->chosenKeys().isEmpty());
    QTRY_VERIFY(!restore->isEnabled());

    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!popup->property("visible").toBool());
}

bool TestAccessibility::showHelp()
{
    m_window->requestActivate();
    if (!QTest::qWaitForWindowActive(m_window))
        return false;
    m_window->setProperty("currentSection", 1);
    QTest::keyClick(m_window, Qt::Key_F1);
    const QObject *help =
        m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "HelpController");
    const bool shown = QTest::qWaitFor([this, help]() {
        const QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("helpPage"));
        return page && page->isVisible() && !help->property("busy").toBool();
    });
    announceAccessibility();
    return shown;
}

void TestAccessibility::helpPageControlsHaveANameAndARole()
{
    QVERIFY(showHelp());

    const QList<QQuickItem *> found = controls();

    verifyNamesAndRoles(found, {QAccessible::List});
    for (const QString &name : {QStringLiteral("helpBackButton"),
                                QStringLiteral("helpForwardButton"), QStringLiteral("aboutButton"),
                                QStringLiteral("helpContents"), QStringLiteral("helpText")}) {
        QQuickItem *item = findItem(m_window->contentItem(), name);
        QVERIFY2(item && found.contains(item), qPrintable(name));
    }
    // Back and forward stay in view, switched off, while there is no page to go to.
    QVERIFY(!findItem(m_window->contentItem(), QStringLiteral("helpBackButton"))->isEnabled());
    for (QQuickItem *item : found) {
        const bool iconOnly = item->metaObject()->indexOfProperty("display") >= 0 &&
                              item->property("display").toInt() == 0;
        if (!iconOnly || item->property("text").toString().isEmpty())
            continue;
        QCOMPARE(QAccessible::queryAccessibleInterface(item)->text(QAccessible::Name),
                 item->property("text").toString());
    }
    QQuickItem *text = findItem(m_window->contentItem(), QStringLiteral("helpText"));
    QCOMPARE(QAccessible::queryAccessibleInterface(text)->text(QAccessible::Name),
             m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "HelpController")
                 ->property("currentTitle")
                 .toString());
    QQuickItem *contents = findItem(m_window->contentItem(), QStringLiteral("helpContents"));
    QCOMPARE(QAccessible::queryAccessibleInterface(contents)->role(), QAccessible::List);
    QVERIFY(
        !QAccessible::queryAccessibleInterface(contents)->text(QAccessible::Description).isEmpty());
    // The chapters are plain rows; each says what it is and which chapter it opens.
    auto *chapter = contents->property("currentItem").value<QQuickItem *>();
    QVERIFY(chapter);
    QAccessibleInterface *row = QAccessible::queryAccessibleInterface(chapter);
    QVERIFY(row);
    QCOMPARE(row->role(), QAccessible::ListItem);
    QVERIFY(!row->text(QAccessible::Name).isEmpty());
    QCOMPARE(row->text(QAccessible::Name), chapter->property("title").toString());
}

void TestAccessibility::helpFocusChainReachesEveryControlOnce()
{
    QVERIFY(showHelp());
    QList<QQuickItem *> chain;
    QQuickItem *item = m_window->contentItem()->nextItemInFocusChain(true);
    while (item && !chain.contains(item) && chain.size() < 500) {
        chain.append(item);
        item = item->nextItemInFocusChain(true);
    }

    QVERIFY(item);
    QCOMPARE(item, chain.constFirst());
    const QList<QQuickItem *> found = controls();
    for (QQuickItem *control : found) {
        if (control->isEnabled() && control->activeFocusOnTab())
            QVERIFY2(chain.contains(control), qPrintable(describe(control)));
    }
    for (const QString &name : {QStringLiteral("helpContents"), QStringLiteral("helpText"),
                                QStringLiteral("aboutButton")}) {
        QVERIFY2(chain.contains(findItem(m_window->contentItem(), name)), qPrintable(name));
    }
}

// F1 opens the help with the focus shown in the text; the contents choose
// another chapter with the arrow keys and Enter; the About dialog opens from
// the keyboard.
void TestAccessibility::helpIsUsedByKeyboard()
{
    QVERIFY(showHelp());
    QObject *help = m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "HelpController");
    QCOMPARE(help->property("currentTopic").toString(), QStringLiteral("contacts"));
    QQuickItem *text = findItem(m_window->contentItem(), QStringLiteral("helpText"));
    QTRY_VERIFY(text->hasActiveFocus());
    QVERIFY(findItem(m_window->contentItem(), QStringLiteral("helpTextFocusRing"))->isVisible());

    QQuickItem *contents = findItem(m_window->contentItem(), QStringLiteral("helpContents"));
    contents->forceActiveFocus(Qt::TabFocusReason);
    QVERIFY(
        findItem(m_window->contentItem(), QStringLiteral("helpContentsFocusRing"))->isVisible());
    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_COMPARE(help->property("currentTopic").toString(), QStringLiteral("calls"));
    QVERIFY(contents->hasActiveFocus());

    QQuickItem *back = findItem(m_window->contentItem(), QStringLiteral("helpBackButton"));
    QTRY_VERIFY(back->isEnabled());
    back->forceActiveFocus(Qt::TabFocusReason);
    QVERIFY(focusIsShown(back));
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_COMPARE(help->property("currentTopic").toString(), QStringLiteral("contacts"));

    QQuickItem *about = findItem(m_window->contentItem(), QStringLiteral("aboutButton"));
    about->forceActiveFocus(Qt::TabFocusReason);
    QVERIFY(focusIsShown(about));
    QObject *dialog = m_window->findChild<QObject *>(QStringLiteral("aboutDialog"));
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());

    // A section of the sidebar leaves the help again.
    QTest::keyClick(m_window, Qt::Key_3, Qt::ControlModifier);
    QTRY_VERIFY(!m_window->property("helpShown").toBool());
    QCOMPARE(m_window->property("currentSection").toInt(), 2);
}

// The About dialog starts on the list of components with its focus shown;
// the arrow keys choose a component and its license shows next to the list.
void TestAccessibility::aboutDialogShowsItsFocusAndNamesItsControls()
{
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    const QList<QQuickItem *> before = controls();
    for (int opening = 1; opening <= 2; ++opening) {
        QObject *popup = openDialog(QStringLiteral("aboutDialog"));
        QVERIFY(popup);
        announceAccessibility();
        QList<QQuickItem *> inDialog = controls();
        inDialog.removeIf([&before](QQuickItem *control) { return before.contains(control); });
        verifyNamesAndRoles(inDialog, {QAccessible::List});
        QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("componentList"));
        QVERIFY(list && inDialog.contains(list));
        QVERIFY(list->hasActiveFocus());
        QVERIFY2(findItem(m_window->contentItem(), QStringLiteral("componentListFocusRing"))
                     ->isVisible(),
                 qPrintable(QStringLiteral("opening %1").arg(opening)));
        // The components are plain rows; each names component, version and license.
        auto *component = list->property("currentItem").value<QQuickItem *>();
        QVERIFY(component);
        QAccessibleInterface *row = QAccessible::queryAccessibleInterface(component);
        QVERIFY(row);
        QCOMPARE(row->role(), QAccessible::ListItem);
        QCOMPARE(row->text(QAccessible::Name), QStringLiteral("%1 %2, %3")
                                                   .arg(component->property("name").toString(),
                                                        component->property("version").toString(),
                                                        component->property("license").toString()));
        QVERIFY(!row->text(QAccessible::Description).isEmpty());
        QQuickItem *license = findItem(m_window->contentItem(), QStringLiteral("licenseText"));
        QVERIFY(license && inDialog.contains(license));
        const QString first = license->property("text").toString();
        QTest::keyClick(m_window, Qt::Key_Down);
        QTRY_VERIFY(license->property("text").toString() != first);
        QVERIFY(!QAccessible::queryAccessibleInterface(license)->text(QAccessible::Name).isEmpty());
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
}

QTEST_MAIN(TestAccessibility)

#include "tst_accessibility.moc"
