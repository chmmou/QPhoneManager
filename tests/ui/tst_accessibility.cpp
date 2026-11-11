#include "core/codec/PhoneTime.h"
#include "core/codec/PhoneVCard.h"
#include "platform/SecretStore.h"
#include "ui/AppController.h"
#include "ui/Settings.h"
#include "ui/calls/CallsArea.h"
#include "ui/contacts/ContactsArea.h"
#include "ui/messages/MessagesArea.h"

#include "FakePhone.h"

#include <QAccessible>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QSignalSpy>
#include <QUrl>
#include <QTemporaryDir>
#include <QTest>

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

private:
    [[nodiscard]] QList<QQuickItem *> controls() const;
    [[nodiscard]] QObject *openDialog(const QString &name);
    void verifyNamesAndRoles(const QList<QQuickItem *> &found,
                             const QSet<QAccessible::Role> &extraRoles = {}) const;
    [[nodiscard]] Settings *settings() const;
    [[nodiscard]] bool showContacts();
    [[nodiscard]] bool showCalls();
    [[nodiscard]] bool showMessages();
    /// Brings the contacts area into the state in which `dialog` has something to show.
    [[nodiscard]] bool prepareContactDialog(const QString &dialog);
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
    for (const QString &pageName : {QStringLiteral("contactsPage"), QStringLiteral("callsPage"),
                                    QStringLiteral("messagesPage")}) {
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

QTEST_MAIN(TestAccessibility)

#include "tst_accessibility.moc"
