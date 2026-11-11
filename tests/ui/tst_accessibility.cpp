#include "ui/Settings.h"

#include <QAccessible>
#include <QFile>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <memory>

Q_IMPORT_QML_PLUGIN(QPhoneManager_UiPlugin)

using qpm::ui::Settings;

namespace {

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
    void darkSchemeDarkensTheWindow();

private:
    [[nodiscard]] QList<QQuickItem *> controls() const;
    [[nodiscard]] QObject *openDialog(const QString &name);
    void verifyNamesAndRoles(const QList<QQuickItem *> &found) const;
    [[nodiscard]] Settings *settings() const;

    QTemporaryDir m_config;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
};

void TestAccessibility::initTestCase()
{
    QVERIFY(m_config.isValid());
    // QSettings reads this once; the shell must not touch the real settings
    // of the user running the test.
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_config.path()));
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
    if (!dialog)
        return nullptr;
    if (name == QLatin1StringView("connectDialog"))
        dialog->setProperty("searchOnOpen", false);
    QMetaObject::invokeMethod(dialog, "open");
    if (!QTest::qWaitFor([dialog]() { return dialog->property("opened").toBool(); }))
        return nullptr;
    return dialog;
}

void TestAccessibility::verifyNamesAndRoles(const QList<QQuickItem *> &found) const
{
    const QSet<QAccessible::Role> roles = {
        QAccessible::Button,   QAccessible::PushButton,   QAccessible::CheckBox,
        QAccessible::ComboBox, QAccessible::EditableText, QAccessible::ListItem,
        QAccessible::PageTab,  QAccessible::RadioButton,
    };
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
