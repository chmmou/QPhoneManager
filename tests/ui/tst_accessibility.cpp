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
#include <QMetaEnum>
#include <QMetaProperty>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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

#include <algorithm>
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
    qpm::core::codec::CalendarTask late;
    late.luid = 3;
    late.summary = QStringLiteral("Rechnung bezahlen");
    late.due = QDate::currentDate().addDays(-3);
    return {{meeting.luid, qpm::core::codec::encodeEventAsPhoneSendsIt(meeting)},
            {task.luid, qpm::core::codec::encodeTaskAsPhoneSendsIt(task)},
            {late.luid, qpm::core::codec::encodeTaskAsPhoneSendsIt(late)}};
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

// The text item below `item` whose text holds `text`, or null.
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

// The outline that marks `row` as the current one of its view, or null.
QQuickItem *currentMarkerOf(const QQuickItem *row)
{
    const QList<QQuickItem *> children = row->childItems();
    for (QQuickItem *child : children) {
        if (QString::fromLatin1(child->metaObject()->className())
                .startsWith(QLatin1StringView("CurrentMarker")))
            return child;
    }
    return nullptr;
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
    // The current row of a list holds the focus of the list and shows it by a
    // strong outline inside it.
    if (const QQuickItem *marker = currentMarkerOf(item); marker && marker->isVisible() &&
        marker->property("border").value<QObject *>()->property("width").toReal() >= 2) {
        return true;
    }
    const QList<QQuickItem *> children = item->childItems();
    for (const QQuickItem *child : children) {
        const QString type = QString::fromLatin1(child->metaObject()->className());
        if (type.startsWith(QLatin1StringView("FocusRing")) && child->isVisible())
            return true;
    }
    return false;
}

// The focus ring of the program, placed in `item`, is shown.
bool ringIsShown(const QQuickItem *item)
{
    const QList<QQuickItem *> children = item->childItems();
    for (const QQuickItem *child : children) {
        const QString type = QString::fromLatin1(child->metaObject()->className());
        if (type.startsWith(QLatin1StringView("FocusRing")) && child->isVisible())
            return true;
    }
    return false;
}

// The relative luminance of WCAG 2.2.
double luminance(const QColor &color)
{
    const auto linear = [](double value) {
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
           0.0722 * linear(color.blueF());
}

// The contrast ratio of WCAG 2.2 between `foreground`, which may be
// translucent, and the opaque `background` it is drawn on.
double contrast(const QColor &foreground, const QColor &background)
{
    const double alpha = foreground.alphaF();
    const QColor drawn = QColor::fromRgbF(
        float(foreground.redF() * alpha + background.redF() * (1 - alpha)),
        float(foreground.greenF() * alpha + background.greenF() * (1 - alpha)),
        float(foreground.blueF() * alpha + background.blueF() * (1 - alpha)));
    const double a = luminance(drawn);
    const double b = luminance(background);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

QColor colorOf(const QObject *object, const char *property)
{
    return object ? object->property(property).value<QColor>() : QColor();
}

QString ratioText(const QString &what, const QColor &foreground, const QColor &background)
{
    return QStringLiteral("%1: %2 on %3 is %4:1")
        .arg(what, foreground.name(QColor::HexArgb), background.name(QColor::HexArgb))
        .arg(contrast(foreground, background), 0, 'f', 2);
}

// The visible items below `item` whose class is `className`.
QList<QQuickItem *> itemsOfClass(QQuickItem *item, const char *className)
{
    QList<QQuickItem *> found;
    if (item->isVisible() && item->inherits(className))
        found.append(item);
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        found.append(itemsOfClass(child, className));
    return found;
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
    void controlsKeepTheirContrastInBothSchemes_data();
    void controlsKeepTheirContrastInBothSchemes();
    void primaryButtonsKeepTheirLabelContrast_data();
    void primaryButtonsKeepTheirLabelContrast();
    void dialogButtonsShowTheFocusRing_data();
    void dialogButtonsShowTheFocusRing();
    void calendarsDialogStartsOnACalendar();
    void overdueTaskSaysSoInWords();
    void notificationCountStaysUnsaidWithoutAccess();
    void formsTakeTheFocusWhenOpenedOnPurpose_data();
    void formsTakeTheFocusWhenOpenedOnPurpose();
    void currentRowIsMarkedBeyondItsFill();
    void screenSwitchesShowTheirStateByShape();
    void calendarDaysAndMarkersKeepTheirContrast();
    void menusStandOutFromThePage();
    void cutContentShowsItsScrollBar_data();
    void cutContentShowsItsScrollBar();
    void hiddenFileViewNeverKeepsTheFocus();
    void emptyFolderKeepsAVisibleFocus();

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
    // A binding loop in a view of the application leaves its size to the order
    // of evaluation.
    QTest::failOnWarning(
        QRegularExpression(QStringLiteral("QPhoneManager/Ui/.*Binding loop detected")));
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

void TestAccessibility::controlsKeepTheirContrastInBothSchemes_data()
{
    QTest::addColumn<bool>("dark");

    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

// Text needs 4.5:1, the outline of a field 3:1. The colours are those the
// controls actually draw with; several came from the style rather than the theme.
void TestAccessibility::controlsKeepTheirContrastInBothSchemes()
{
    QFETCH(bool, dark);
    QVERIFY(showOrganizer(6));
    Settings *shared = settings();
    QVERIFY(shared);
    shared->setColorScheme(dark ? Settings::ColorScheme::Dark : Settings::ColorScheme::Light);
    auto *theme = m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "Theme");
    QVERIFY(theme);
    QTRY_COMPARE(theme->property("dark").toBool(), dark);
    const QColor surface = colorOf(theme, "surface");
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("notesPage"));
    QVERIFY(page);
    QVERIFY(QMetaObject::invokeMethod(page, "startNew"));
    QTRY_VERIFY(findItem(m_window->contentItem(), QStringLiteral("noteBodyArea")));
    findItem(m_window->contentItem(), QStringLiteral("noteBodyArea"))
        ->setProperty("text", QStringLiteral("Milch"));

    QQuickItem *save = findItem(m_window->contentItem(), QStringLiteral("saveNoteButton"));
    QVERIFY(save);
    QTRY_VERIFY(save->isEnabled());
    const QColor label = colorOf(save->property("contentItem").value<QQuickItem *>(), "color");
    const QColor fill = colorOf(save->property("background").value<QQuickItem *>(), "color");
    QVERIFY2(contrast(label, fill) >= 4.5, qPrintable(ratioText(QStringLiteral("save"), label, fill)));

    auto *header = m_window->property("header").value<QQuickItem *>();
    QVERIFY(header);
    const QColor headerColor = colorOf(header->property("background").value<QQuickItem *>(), "color");
    for (const QString &name : {QStringLiteral("noteNewCategoryField"),
                                QStringLiteral("noteBodyArea"), QStringLiteral("searchField")}) {
        QQuickItem *field = findItem(m_window->contentItem(), name);
        QVERIFY2(field && field->isEnabled(), qPrintable(name));
        const QColor backdrop = name == QLatin1StringView("searchField") ? headerColor : surface;
        const QColor placeholder = colorOf(field, "placeholderTextColor");
        QVERIFY2(contrast(placeholder, backdrop) >= 4.5,
                 qPrintable(ratioText(name + QStringLiteral(" label"), placeholder, backdrop)));
        const QColor outline =
            colorOf(field->property("background").value<QQuickItem *>(), "outlineColor");
        QVERIFY2(contrast(outline, backdrop) >= 3.0,
                 qPrintable(ratioText(name + QStringLiteral(" outline"), outline, backdrop)));
        const QColor selected = colorOf(field, "selectedTextColor");
        const QColor selection = colorOf(field, "selectionColor");
        QVERIFY2(contrast(selected, selection) >= 4.5,
                 qPrintable(ratioText(name + QStringLiteral(" selection"), selected, selection)));
    }

    QObject *popup = openDialog(QStringLiteral("noteCategoriesDialog"));
    QVERIFY(popup);
    const QColor dialogColor =
        colorOf(popup->property("background").value<QQuickItem *>(), "color");
    auto *footer = popup->property("footer").value<QQuickItem *>();
    QVERIFY(footer);
    const QList<QQuickItem *> buttons = itemsOfClass(footer, "QQuickAbstractButton");
    QVERIFY(!buttons.isEmpty());
    for (QQuickItem *button : buttons) {
        const QColor text = colorOf(button->property("contentItem").value<QQuickItem *>(), "color");
        QColor behind = colorOf(button->property("background").value<QQuickItem *>(), "color");
        behind = behind.alpha() == 0 ? dialogColor : behind;
        QVERIFY2(contrast(text, behind) >= 4.5,
                 qPrintable(ratioText(button->property("text").toString(), text, behind)));
    }
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(!popup->property("visible").toBool());
}

void TestAccessibility::primaryButtonsKeepTheirLabelContrast_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<bool>("switched");

    QTest::newRow("light, as created") << false << false;
    QTest::newRow("light, after dark") << false << true;
    QTest::newRow("dark") << true << false;
    QTest::newRow("dark, after light") << true << true;
}

// The label of a filled main button needs 4.5:1 on its fill in every window.
// After a change from the dark to the light scheme, the answer button of the
// call window took the label colour of the window.
void TestAccessibility::primaryButtonsKeepTheirLabelContrast()
{
    QFETCH(bool, dark);
    QFETCH(bool, switched);
    auto *theme = m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "Theme");
    QVERIFY(theme);
    // The first light row looks at the windows as they were made, without a change of scheme.
    QVERIFY2(!theme->property("dark").toBool(), "the system scheme of the test run is dark");
    const auto useScheme = [this, theme](bool wanted) {
        settings()->setColorScheme(wanted ? Settings::ColorScheme::Dark
                                          : Settings::ColorScheme::Light);
        return QTest::qWaitFor([theme, wanted]() { return theme->property("dark").toBool() == wanted; });
    };
    if (switched)
        QVERIFY(useScheme(!dark));
    if (switched || dark)
        QVERIFY(useScheme(dark));
    const auto verifyLabel = [](QQuickItem *button, const QString &what) {
        QVERIFY2(button && button->isEnabled(), qPrintable(what));
        const QColor label = colorOf(button->property("contentItem").value<QQuickItem *>(), "color");
        const QColor fill = colorOf(button->property("background").value<QQuickItem *>(), "color");
        QVERIFY2(contrast(label, fill) >= 4.5, qPrintable(ratioText(what, label, fill)));
    };

    QVERIFY(showOrganizer(4));
    auto *callWindow = m_window->findChild<QQuickWindow *>(QStringLiteral("callWindow"));
    QVERIFY(callWindow);
    m_phone->setCallState(6, QStringLiteral("+4930999999"));
    QTRY_VERIFY_WITH_TIMEOUT(callWindow->isVisible(), phoneTimeoutMs);
    QQuickItem *answer = findItem(callWindow->contentItem(), QStringLiteral("callAnswerButton"));
    QTRY_VERIFY(answer && answer->isEnabled());
    verifyLabel(answer, QStringLiteral("answer"));
    if (QTest::currentTestFailed())
        return;
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    QVERIFY(controller->call()->hangUp());
    QTRY_VERIFY_WITH_TIMEOUT(!callWindow->isVisible(), phoneTimeoutMs);

    QObject *alarms = openDialog(QStringLiteral("alarmsDialog"));
    QVERIFY(alarms);
    QTRY_VERIFY(findItem(m_window->contentItem(), QStringLiteral("alarmTimeField")));
    findItem(m_window->contentItem(), QStringLiteral("alarmTimeField"))
        ->setProperty("text", QStringLiteral("07:15"));
    QQuickItem *saveAlarm = findItem(m_window->contentItem(), QStringLiteral("saveAlarmButton"));
    QTRY_VERIFY(saveAlarm && saveAlarm->isEnabled());
    verifyLabel(saveAlarm, QStringLiteral("save alarm"));
    if (QTest::currentTestFailed())
        return;
    QMetaObject::invokeMethod(alarms, "close");
    QTRY_VERIFY(!alarms->property("visible").toBool());

    m_window->setProperty("currentSection", 5);
    QTRY_VERIFY(findItem(m_window->contentItem(), QStringLiteral("tasksPage")) &&
                findItem(m_window->contentItem(), QStringLiteral("tasksPage"))->isVisible());
    QObject *categories = openDialog(QStringLiteral("taskCategoriesDialog"));
    QVERIFY(categories);
    QQuickItem *name = findItem(m_window->contentItem(), QStringLiteral("categoryNameField"));
    QVERIFY(name);
    name->setProperty("text", QStringLiteral("Garten"));
    QQuickItem *add = findItem(m_window->contentItem(), QStringLiteral("addCategoryButton"));
    QTRY_VERIFY(add && add->isEnabled());
    verifyLabel(add, QStringLiteral("add category"));
    QMetaObject::invokeMethod(categories, "close");
    QTRY_VERIFY(!categories->property("visible").toBool());
}

void TestAccessibility::dialogButtonsShowTheFocusRing_data()
{
    QTest::addColumn<QString>("dialog");
    QTest::addColumn<QString>("tabs");
    QTest::addColumn<QString>("focused");

    QTest::newRow("QR code") << QStringLiteral("qrDialog") << QString()
                             << QStringLiteral("qrCloseButton");
    QTest::newRow("phones") << QStringLiteral("profilesDialog") << QString()
                            << QStringLiteral("profilesCloseButton");
    QTest::newRow("remove a phone") << QStringLiteral("profilesDialog/removeDialog") << QString()
                                    << QStringLiteral("cancelRemoveProfileButton");
    QTest::newRow("PIN") << QStringLiteral("pinDialog") << QString() << QStringLiteral("pinField");
    QTest::newRow("settings") << QStringLiteral("settingsDialog") << QStringLiteral("settingsTabs")
                              << QString();
}

// The ripple of the style is drawn in a corner of the window by the software
// renderer, so the focus ring of the program is the cue that stays. A dialog
// opening with the focus on a button shows the ring at once, also the second time.
void TestAccessibility::dialogButtonsShowTheFocusRing()
{
    QFETCH(QString, dialog);
    QFETCH(QString, tabs);
    QFETCH(QString, focused);
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    const QList<QQuickItem *> before = controls();

    for (int opening = 1; opening <= 2; ++opening) {
        QObject *popup = nullptr;
        QObject *outer = nullptr;
        if (dialog.contains(u'/')) {
            // A dialog inside another one opens over it and is reached by its id
            // in the context of the other one's content.
            outer = openDialog(dialog.section(u'/', 0, 0));
            QVERIFY(outer);
            auto *content = outer->property("contentItem").value<QQuickItem *>();
            QVERIFY(content && !content->childItems().isEmpty());
            QQmlContext *context = qmlContext(content->childItems().constFirst());
            QVERIFY(context);
            popup = context->objectForName(dialog.section(u'/', 1));
            QVERIFY(popup);
            QMetaObject::invokeMethod(popup, "open");
            QTRY_VERIFY(popup->property("opened").toBool());
        } else {
            popup = openDialog(dialog);
        }
        QVERIFY(popup);
        QQuickItem *focusItem = m_window->activeFocusItem();
        QVERIFY(focusItem);
        if (!focused.isEmpty()) {
            QCOMPARE(focusItem->objectName(), focused);
            QVERIFY2(focusIsShown(focusItem),
                     qPrintable(QStringLiteral("opening %1: %2").arg(opening).arg(describe(focusItem))));
        }
        if (focusItem->inherits("QQuickAbstractButton")) {
            QVERIFY2(ringIsShown(focusItem),
                     qPrintable(QStringLiteral("opening %1: %2").arg(opening).arg(describe(focusItem))));
        }
        QQuickItem *tabBar = tabs.isEmpty() ? nullptr : findItem(m_window->contentItem(), tabs);
        const int pages = tabBar ? tabBar->property("count").toInt() : 1;
        for (int page = 0; page < pages; ++page) {
            if (tabBar)
                tabBar->setProperty("currentIndex", page);
            QList<QQuickItem *> found = controls();
            found.removeIf([&before](QQuickItem *item) { return before.contains(item); });
            for (QQuickItem *control : std::as_const(found)) {
                if (!control->inherits("QQuickAbstractButton") || !control->isEnabled())
                    continue;
                control->forceActiveFocus(Qt::TabFocusReason);
                QTRY_VERIFY2(control->hasActiveFocus(), qPrintable(describe(control)));
                QVERIFY2(ringIsShown(control), qPrintable(describe(control)));
            }
        }
        if (tabBar)
            tabBar->setProperty("currentIndex", 0);
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
        if (outer) {
            QMetaObject::invokeMethod(outer, "close");
            QTRY_VERIFY(!outer->property("visible").toBool());
        }
    }
}

// Space and Enter act on the current calendar; the list used to open without one.
void TestAccessibility::calendarsDialogStartsOnACalendar()
{
    QVERIFY(showOrganizer(4));
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    qpm::ui::CalendarArea *area = controller->calendar();

    for (int opening = 1; opening <= 2; ++opening) {
        QObject *popup = openDialog(QStringLiteral("calendarsDialog"));
        QVERIFY(popup);
        QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("calendarChoiceList"));
        QVERIFY(list);
        QTRY_VERIFY(list->hasActiveFocus());
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        const bool chosen = area->calendars()->isSelected(0);
        QTest::keyClick(m_window, Qt::Key_Space);
        QCOMPARE(area->calendars()->isSelected(0), !chosen);
        // Closing without applying reads the calendars anew.
        QMetaObject::invokeMethod(popup, "reject");
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(area->calendars()->isSelected(0), chosen);
    }
}

// The red colour of an overdue task is not the only sign of it.
void TestAccessibility::overdueTaskSaysSoInWords()
{
    QVERIFY(showOrganizer(5));
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("taskList"));
    QVERIFY(list);
    QQuickItem *late = nullptr;
    QQuickItem *onTime = nullptr;
    QTRY_VERIFY([&]() {
        const QList<QQuickItem *> labels = itemsOfClass(list, "QQuickText");
        for (QQuickItem *label : labels) {
            if (label->property("text").toString() == QLatin1StringView("Rechnung bezahlen"))
                late = label;
            else if (label->property("text").toString() == QLatin1StringView("Fenster putzen"))
                onTime = label;
        }
        return late && onTime;
    }());
    const auto rowOf = [](QQuickItem *item) {
        while (item) {
            QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(item);
            if (accessible && accessible->role() == QAccessible::ListItem)
                return item;
            item = item->parentItem();
        }
        return item;
    };
    const auto dueText = [](QQuickItem *row) {
        QQuickItem *due = findItem(row, QStringLiteral("taskDueText"));
        return due ? due->property("text").toString() : QString();
    };
    QQuickItem *lateRow = rowOf(late);
    QQuickItem *onTimeRow = rowOf(onTime);
    QVERIFY(lateRow && onTimeRow);
    // One sentence instead of fragments: "Overdue since <date>" takes the place of "Due <date>".
    QVERIFY2(dueText(lateRow).startsWith(QStringLiteral("Overdue since ")),
             qPrintable(dueText(lateRow)));
    QVERIFY2(!dueText(lateRow).contains(QStringLiteral("Due ")), qPrintable(dueText(lateRow)));
    QVERIFY2(dueText(onTimeRow).startsWith(QStringLiteral("Due ")), qPrintable(dueText(onTimeRow)));
    const QString description =
        QAccessible::queryAccessibleInterface(lateRow)->text(QAccessible::Description);
    QVERIFY2(description.startsWith(QStringLiteral("Overdue since ")), qPrintable(description));
}

// Without the access, the phone reports no notifications; that is not their number.
void TestAccessibility::notificationCountStaysUnsaidWithoutAccess()
{
    QVERIFY(showNotifications());
    QQuickItem *count = findItem(m_window->contentItem(), QStringLiteral("notificationCount"));
    QVERIFY(count);
    QVERIFY(count->property("text").toString().contains(u'3'));
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    m_phone->config().notificationAccess = qpm::test::FakePhoneConfig::NotificationAccess::Disabled;
    QVERIFY(controller->notifications()->refresh());
    QTRY_VERIFY_WITH_TIMEOUT(controller->notifications()->isAccessMissing(), phoneTimeoutMs);
    QCOMPARE(count->property("text").toString(), QString());
}

void TestAccessibility::formsTakeTheFocusWhenOpenedOnPurpose_data()
{
    QTest::addColumn<int>("section");
    QTest::addColumn<QString>("pageName");
    QTest::addColumn<QString>("listName");
    QTest::addColumn<QString>("fieldName");

    QTest::newRow("task") << 5 << QStringLiteral("tasksPage") << QStringLiteral("taskList")
                          << QStringLiteral("taskSummaryField");
    QTest::newRow("note") << 6 << QStringLiteral("notesPage") << QStringLiteral("noteList")
                          << QStringLiteral("noteBodyArea");
}

// A new or opened task or note puts the focus into its first field; moving
// through the list shows the form as well and leaves the focus in the list.
void TestAccessibility::formsTakeTheFocusWhenOpenedOnPurpose()
{
    QFETCH(int, section);
    QFETCH(QString, pageName);
    QFETCH(QString, listName);
    QFETCH(QString, fieldName);
    QVERIFY(showOrganizer(section));
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *page = findItem(m_window->contentItem(), pageName);
    QVERIFY(page);
    const auto focusedName = [this]() {
        const QQuickItem *focused = m_window->activeFocusItem();
        return focused ? focused->objectName() : QString();
    };

    QVERIFY(QMetaObject::invokeMethod(page, "startNew"));
    QTRY_COMPARE(focusedName(), fieldName);

    page->setProperty("editing", false);
    QQuickItem *list = findItem(m_window->contentItem(), listName);
    QVERIFY(list);
    auto *area = page->property("area").value<QObject *>();
    QVERIFY(area);
    area->setProperty("currentLuid", 0);
    QTRY_COMPARE(list->property("currentIndex").toInt(), -1);
    list->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(list->hasActiveFocus());
    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_VERIFY(page->property("editing").toBool());
    QVERIFY(list->hasActiveFocus());

    QVERIFY(QMetaObject::invokeMethod(page, "startEdit"));
    QTRY_COMPARE(focusedName(), fieldName);
}

// The fill of the current row is too faint for 3:1. The row always carries an
// outline: a thin one in the outline colour, a strong one in the primary colour
// while the list has the focus, set in from the focus ring of the list so that
// the ring never covers it. The sidebar marks the section shown.
void TestAccessibility::currentRowIsMarkedBeyondItsFill()
{
    QVERIFY(showContacts());
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    auto *theme = m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "Theme");
    QVERIFY(theme);
    const QColor surface = colorOf(theme, "surface");
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("contactList"));
    QVERIFY(list);
    list->setProperty("currentIndex", 0);
    list->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(list->hasActiveFocus());
    auto *row = list->property("currentItem").value<QQuickItem *>();
    QVERIFY(row);
    const QColor fill = colorOf(row, "color");
    QQuickItem *marker = currentMarkerOf(row);
    QVERIFY(marker && marker->isVisible());
    auto *border = marker->property("border").value<QObject *>();
    QVERIFY(border);
    QCOMPARE(border->property("width").toReal(), 2.0);
    QVERIFY2(contrast(colorOf(border, "color"), fill) >= 3.0,
             qPrintable(ratioText(QStringLiteral("focused row"), colorOf(border, "color"), fill)));
    // The ring of the list takes its outer two pixels; the outline of the first row lies inside.
    const QRectF inList = marker->mapRectToItem(list, QRectF(0, 0, marker->width(), marker->height()));
    QVERIFY2(inList.left() >= 3 && inList.top() >= 3 && inList.right() <= list->width() - 3,
             qPrintable(QStringLiteral("outline at %1,%2 to %3 in a list %4 wide")
                            .arg(inList.left())
                            .arg(inList.top())
                            .arg(inList.right())
                            .arg(list->width())));

    QQuickItem *button = findItem(m_window->contentItem(), QStringLiteral("newContactButton"));
    QVERIFY(button);
    button->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(!list->hasActiveFocus());
    QVERIFY(marker->isVisible());
    QCOMPARE(border->property("width").toReal(), 1.0);
    QVERIFY2(contrast(colorOf(border, "color"), fill) >= 3.0,
             qPrintable(ratioText(QStringLiteral("row"), colorOf(border, "color"), fill)));

    QQuickItem *section = findItem(m_window->contentItem(), QStringLiteral("section-contacts"));
    QVERIFY(section && section->property("highlighted").toBool());
    QQuickItem *bar = findItem(section, QStringLiteral("currentSectionMarker"));
    QVERIFY(bar && bar->isVisible());
    QVERIFY2(contrast(colorOf(bar, "color"), surface) >= 3.0,
             qPrintable(ratioText(QStringLiteral("sidebar"), colorOf(bar, "color"), surface)));
    QQuickItem *other = findItem(m_window->contentItem(), QStringLiteral("section-calls"));
    QVERIFY(other);
    QVERIFY(!findItem(other, QStringLiteral("currentSectionMarker"))->isVisible());
}

// A switched-on toggle shows it by an outline, not by the icon colour alone.
void TestAccessibility::screenSwitchesShowTheirStateByShape()
{
    QVERIFY(showScreen(true));
    QQuickItem *fit = findItem(m_window->contentItem(), QStringLiteral("screenFitButton"));
    QQuickItem *keyboard = findItem(m_window->contentItem(), QStringLiteral("screenKeyboardButton"));
    QVERIFY(fit && keyboard);
    QQuickItem *fitMarker = findItem(fit, QStringLiteral("fitCheckedMarker"));
    QQuickItem *keyboardMarker = findItem(keyboard, QStringLiteral("keyboardCheckedMarker"));
    QVERIFY(fitMarker && keyboardMarker);
    QVERIFY(fit->property("checked").toBool());
    QVERIFY(fitMarker->isVisible());
    QVERIFY(!keyboard->property("checked").toBool());
    QVERIFY(!keyboardMarker->isVisible());
    QVERIFY(QMetaObject::invokeMethod(fit, "toggle"));
    QVERIFY(!fit->property("checked").toBool());
    QVERIFY(!fitMarker->isVisible());
    auto *theme = m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "Theme");
    QVERIFY(theme);
    auto *border = fitMarker->property("border").value<QObject *>();
    QVERIFY(border && border->property("width").toReal() >= 1);
    QVERIFY(contrast(colorOf(border, "color"), colorOf(theme, "surface")) >= 3.0);
}

// The days of the next and last month and the dots of the calendars of an
// event keep their contrast; the dots carry the colour the user chose.
void TestAccessibility::calendarDaysAndMarkersKeepTheirContrast()
{
    QVERIFY(showOrganizer(4));
    auto *theme = m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "Theme");
    QVERIFY(theme);
    const QColor surface = colorOf(theme, "surface");
    QQuickItem *grid = findItem(m_window->contentItem(), QStringLiteral("calendarGrid"));
    QVERIFY(grid);
    int outside = 0;
    int monthNames = 0;
    int dots = 0;
    const auto dayCells = [grid]() {
        QList<QQuickItem *> days = itemsOfClass(grid, "QQuickRectangle");
        days.removeIf([](const QQuickItem *cell) {
            return cell->metaObject()->indexOfProperty("inPeriod") < 0;
        });
        return days;
    };
    // A month has four weeks at least.
    QTRY_VERIFY(dayCells().size() >= 28);
    const QList<QQuickItem *> cells = itemsOfClass(grid, "QQuickRectangle");
    for (QQuickItem *cell : cells) {
        if (cell->metaObject()->indexOfProperty("inPeriod") >= 0 &&
            !cell->property("inPeriod").toBool()) {
            const QString day = cell->property("dayNumber").toString();
            const QDate date = cell->property("date").toDate();
            // The first day shown of another month says which month it is.
            const bool named = cell->property("index").toInt() == 0 || day == u'1';
            if (named) {
                ++monthNames;
                QVERIFY2(labelWithText(cell, QLocale().toString(date, QStringLiteral("MMM"))),
                         qPrintable(date.toString(Qt::ISODate)));
                const QString name =
                    QAccessible::queryAccessibleInterface(cell)->text(QAccessible::Name);
                QVERIFY2(name.contains(QLocale().monthName(date.month())), qPrintable(name));
            }
            const QList<QQuickItem *> labels = itemsOfClass(cell, "QQuickText");
            for (QQuickItem *label : labels) {
                const QString text = label->property("text").toString();
                if (named ? !text.contains(day) : text != day)
                    continue;
                ++outside;
                QCOMPARE(label->opacity(), 1.0);
                QVERIFY2(contrast(colorOf(label, "color"), surface) >= 4.5,
                         qPrintable(ratioText(day, colorOf(label, "color"), surface)));
            }
        }
        if (qFuzzyCompare(cell->width(), 8.0) && qFuzzyCompare(cell->height(), 8.0) &&
            colorOf(cell, "color").alpha() > 0) {
            ++dots;
            auto *border = cell->property("border").value<QObject *>();
            QVERIFY(border && border->property("width").toReal() >= 1);
            QVERIFY2(contrast(colorOf(border, "color"), surface) >= 3.0,
                     qPrintable(ratioText(QStringLiteral("dot"), colorOf(border, "color"), surface)));
        }
    }
    QVERIFY(outside > 0);
    QVERIFY(monthNames > 0);
    QVERIFY(dots > 0);
}

// A menu has the colour of the page under it; its drawn edge sets it apart.
void TestAccessibility::menusStandOutFromThePage()
{
    QVERIFY(showApps());
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("appsPage"));
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("appList"));
    QVERIFY(page && list);
    list->setProperty("currentIndex", 0);
    QVERIFY(QMetaObject::invokeMethod(page, "showMenu"));
    QObject *menu = page->findChild<QObject *>(QStringLiteral("appMenu"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *background = menu->property("background").value<QQuickItem *>();
    QVERIFY(background);
    // The left edge, halfway down, against the page just outside it.
    const QPointF edge = background->mapToScene(QPointF(0, background->height() / 2));
    const QImage shot = m_window->grabWindow();
    const QColor outside = shot.pixelColor(edge.toPoint() - QPoint(3, 0));
    double best = 0;
    for (int dx = -1; dx <= 2; ++dx)
        best = std::max(best, contrast(shot.pixelColor(edge.toPoint() + QPoint(dx, 0)), outside));
    QVERIFY2(best >= 3.0, qPrintable(QStringLiteral("menu edge %1:1").arg(best, 0, 'f', 2)));
    QMetaObject::invokeMethod(menu, "close");
    QTRY_VERIFY(!menu->property("visible").toBool());
}

void TestAccessibility::cutContentShowsItsScrollBar_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("scroller");

    QTest::newRow("settings") << QStringLiteral("settingsDialog")
                              << QStringLiteral("settingsConnectionPage");
    QTest::newRow("task form") << QStringLiteral("tasksPage") << QStringLiteral("taskForm");
    QTest::newRow("appointment form") << QStringLiteral("calendarPage") << QStringLiteral("eventForm");
    QTest::newRow("note form") << QStringLiteral("notesPage") << QStringLiteral("noteBodyScroll");
    QTest::newRow("contact form") << QStringLiteral("contactsPage") << QStringLiteral("contactForm");
}

// Content cut off at the lower edge shows the scroll bar without a scroll
// first; the style otherwise shows it only while scrolling. Its thumb stands
// out from the window, and the content ends where the bar begins.
void TestAccessibility::cutContentShowsItsScrollBar()
{
    QFETCH(QString, view);
    QFETCH(QString, scroller);
    m_window->resize(m_window->width(), m_window->minimumHeight());
    QTRY_COMPARE(m_window->height(), m_window->minimumHeight());
    QObject *popup = nullptr;
    if (view == QLatin1StringView("settingsDialog")) {
        popup = openDialog(view);
        QVERIFY(popup);
    } else {
        if (view == QLatin1StringView("contactsPage"))
            QVERIFY(showContacts());
        else
            QVERIFY(showOrganizer(view == QLatin1StringView("calendarPage") ? 4
                                  : view == QLatin1StringView("tasksPage")  ? 5
                                                                           : 6));
        QQuickItem *page = findItem(m_window->contentItem(), view);
        QVERIFY(page);
        QVERIFY(QMetaObject::invokeMethod(page, "startNew"));
        QTRY_VERIFY(findItem(m_window->contentItem(), scroller));
        if (view == QLatin1StringView("notesPage")) {
            QStringList lines;
            for (int line = 1; line <= 60; ++line)
                lines.append(QStringLiteral("Zeile %1").arg(line));
            findItem(m_window->contentItem(), QStringLiteral("noteBodyArea"))
                ->setProperty("text", lines.join(u'\n'));
        }
    }
    QQuickItem *page = findItem(m_window->contentItem(), scroller);
    QVERIFY(page);
    QTRY_VERIFY2(page->property("contentHeight").toReal() > page->property("availableHeight").toReal(),
                 "the view fits; nothing is cut off");
    QQuickItem *bar = nullptr;
    const QList<QQuickItem *> bars = itemsOfClass(page, "QQuickScrollBar");
    for (QQuickItem *candidate : bars) {
        if (candidate->property("orientation").toInt() == Qt::Vertical)
            bar = candidate;
    }
    QVERIFY(bar);
    const QMetaProperty policy =
        bar->metaObject()->property(bar->metaObject()->indexOfProperty("policy"));
    QCOMPARE(bar->property("policy").toInt(), policy.enumerator().keyToValue("AlwaysOn"));
    auto *thumb = bar->property("contentItem").value<QQuickItem *>();
    QVERIFY(thumb);
    QTRY_COMPARE(thumb->opacity(), 1.0);
    auto *theme = m_engine->singletonInstance<QObject *>("QPhoneManager.Ui", "Theme");
    QVERIFY(theme);
    const QColor behind = popup ? colorOf(popup->property("background").value<QQuickItem *>(), "color")
                                : colorOf(theme, "surface");
    QVERIFY2(contrast(colorOf(thumb, "color"), behind) >= 3.0,
             qPrintable(ratioText(QStringLiteral("thumb"), colorOf(thumb, "color"), behind)));
    auto *flickable = page->property("contentItem").value<QQuickItem *>();
    QVERIFY(flickable);
    auto *content = flickable->property("contentItem").value<QQuickItem *>();
    QVERIFY(content && !content->childItems().isEmpty());
    QQuickItem *first = content->childItems().constFirst();
    const qreal right = first->mapToItem(page, QPointF(first->width(), 0)).x();
    QVERIFY2(right <= bar->x() + 0.5,
             qPrintable(QStringLiteral("content ends at %1, the bar begins at %2")
                            .arg(right)
                            .arg(bar->x())));
    if (popup) {
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
}

// Switching between list and grid hid the view with the focus; the focus ring
// of the files then showed over the grid while the hidden list held the keys.
void TestAccessibility::hiddenFileViewNeverKeepsTheFocus()
{
    QVERIFY(showFiles());
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *page = findItem(m_window->contentItem(), QStringLiteral("filesPage"));
    QQuickItem *grid = findItem(m_window->contentItem(), QStringLiteral("fileGrid"));
    QQuickItem *list = findItem(m_window->contentItem(), QStringLiteral("fileList"));
    QVERIFY(page && grid && list);
    page->setProperty("showGrid", false);
    QTRY_VERIFY(list->isVisible());
    list->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(list->hasActiveFocus());

    page->setProperty("showGrid", true);
    QTRY_VERIFY(grid->isVisible());
    QVERIFY(!list->isVisible());
    QTRY_VERIFY(!list->hasActiveFocus());
    QVERIFY(grid->hasActiveFocus());

    page->setProperty("showGrid", false);
    QTRY_VERIFY(list->isVisible());
    QTRY_VERIFY(!grid->hasActiveFocus());
    QVERIFY(list->hasActiveFocus());
}

// In an empty folder both file views are hidden; the focus ring around the
// view area still shows where the keys go, and Backspace leads back up.
void TestAccessibility::emptyFolderKeepsAVisibleFocus()
{
    QVERIFY(showFiles());
    m_phone->addFolder(QStringLiteral("\\sdcard\\Leer"));
    auto *controller =
        m_engine->singletonInstance<AppController *>("QPhoneManager.Ui", "AppController");
    qpm::ui::FileBrowserModel *files = controller->files();
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QQuickItem *grid = findItem(m_window->contentItem(), QStringLiteral("fileGrid"));
    QQuickItem *ring = findItem(m_window->contentItem(), QStringLiteral("fileViewFocusRing"));
    QVERIFY(grid && ring);
    grid->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(grid->hasActiveFocus());

    QVERIFY(files->open(QStringLiteral("\\sdcard\\Leer")));
    QTRY_VERIFY_WITH_TIMEOUT(!files->isBusy() && files->path() == QStringLiteral("\\sdcard\\Leer"),
                             phoneTimeoutMs);
    QCOMPARE(files->count(), 0);
    QTRY_VERIFY(!grid->isVisible());
    QVERIFY(grid->hasActiveFocus());
    QVERIFY(ring->isVisible());

    QTest::keyClick(m_window, Qt::Key_Backspace);
    QTRY_COMPARE_WITH_TIMEOUT(files->path(), QStringLiteral("\\sdcard"), phoneTimeoutMs);
}

QTEST_MAIN(TestAccessibility)

#include "tst_accessibility.moc"
