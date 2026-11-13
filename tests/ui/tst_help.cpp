#include "ui/help/AboutInfo.h"
#include "ui/help/HelpController.h"
#include "ui/help/HelpTopicModel.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QMap>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVariantMap>

#include <map>
#include <memory>
#include <utility>

Q_IMPORT_QML_PLUGIN(QPhoneManager_UiPlugin)

using qpm::ui::AboutInfo;
using qpm::ui::HelpController;

namespace {

QString sourceText(const QString &relativePath)
{
    QFile file(QStringLiteral(QPM_SOURCE_DIR "/") + relativePath);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// The top-level heading of a chapter of the handbook.
QString heading(const QString &language, const QString &topic)
{
    const QStringList lines =
        sourceText(QStringLiteral("doc/handbook/%1/%2.md").arg(language, topic)).split(u'\n');
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1StringView("# ")))
            return line.mid(2).trimmed();
    }
    return {};
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

QString selected(const QString &html, int start, int end)
{
    QTextDocument document;
    document.setHtml(html);
    QTextCursor cursor(&document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    return cursor.selectedText();
}

} // namespace

// The help in the window: F1 in every area in German and English, the
// contents, links by keyboard, the fallback between languages, and the About
// dialog with its licenses.
class TestHelp : public QObject
{
    Q_OBJECT

public:
    explicit TestHelp(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void f1OpensTheChapterOfEveryArea_data();
    void f1OpensTheChapterOfEveryArea();
    void contentsListEveryAreaOfTheProgram_data();
    void contentsListEveryAreaOfTheProgram();
    void sidebarOpensTheStartOfTheHelp();
    void linksOfTheStartChapterAreFollowedByKeyboard();
    void missingChapterShowsAPlainMessage();
    void fallsBackToEnglishThenGerman();
    void historyGoesBackAndForward();
    void onlyChapterLinksAndHttpsAreFollowed();
    void linkPositionsMatchTheText();
    void imagesOfAChapterAreShown();
    void firstChapterWaitsForTheHelpWithoutBlocking();
    void helpThatCannotBeLoadedSaysSo();
    void failedWebLinkShowsItsAddress();
    void linkColourKeepsThePageAndTheSelection();
    void licenseTextsAreReflowedWithTheirWording();
    void aboutShowsVersionLicenseAndComponents();
    void aboutDialogOpensFromTheHelpAndShowsTheLicenses();

private:
    /**
     * Builds a help file for `language` from `chapters` (topic to Markdown)
     * and `images` (file name to picture) with helpgen; empty when that fails.
     */
    [[nodiscard]] QString buildHelpFile(const QString &language,
                                        const std::map<QString, QString> &chapters,
                                        const QStringList &topics,
                                        const std::map<QString, QImage> &images = {});
    /// The keys of the areas in the sidebar, in their order.
    [[nodiscard]] QStringList sectionKeys() const;
    [[nodiscard]] QQuickItem *item(const QString &name) const;
    void showSection(int index);

    QTemporaryDir m_home;
    QTemporaryDir m_fixtures;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    HelpController *m_help = nullptr;
};

void TestHelp::initTestCase()
{
    QVERIFY(m_home.isValid());
    QVERIFY(m_fixtures.isValid());
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("config"))));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("data"))));
    QCoreApplication::setOrganizationName(QStringLiteral("QPhoneManagerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_help"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->loadFromModule("QPhoneManager.Ui", "Main");
    QCOMPARE(m_engine->rootObjects().size(), 1);
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
    QVERIFY(m_window);
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    m_help = m_engine->singletonInstance<HelpController *>("QPhoneManager.Ui", "HelpController");
    QVERIFY(m_help);
}

void TestHelp::cleanupTestCase()
{
    m_window = nullptr;
    m_help = nullptr;
    m_engine.reset();
}

QQuickItem *TestHelp::item(const QString &name) const
{
    return findItem(m_window->contentItem(), name);
}

QStringList TestHelp::sectionKeys() const
{
    QQuickItem *list = item(QStringLiteral("sectionList"));
    if (!list)
        return {};
    QStringList keys;
    const int count = list->property("count").toInt();
    const int shown = m_window->property("currentSection").toInt();
    for (int index = 0; index < count; ++index) {
        m_window->setProperty("currentSection", index);
        keys.append(m_window->property("currentKey").toString());
    }
    m_window->setProperty("currentSection", shown);
    return keys;
}

void TestHelp::showSection(int index)
{
    m_window->setProperty("helpShown", false);
    m_window->setProperty("currentSection", index);
}

QString TestHelp::buildHelpFile(const QString &language, const std::map<QString, QString> &chapters,
                                const QStringList &topics, const std::map<QString, QImage> &images)
{
    const QDir base(m_fixtures.path());
    const QString name =
        QStringLiteral("%1-%2").arg(language).arg(base.entryList(QDir::Dirs).size());
    const QString source = base.filePath(name + QStringLiteral("/source"));
    if (!QDir().mkpath(source + QStringLiteral("/images")))
        return {};
    for (const auto &[topic, markdown] : chapters) {
        QFile file(source + u'/' + topic + QStringLiteral(".md"));
        if (!file.open(QIODevice::WriteOnly) || file.write(markdown.toUtf8()) < 0)
            return {};
    }
    for (const auto &[fileName, picture] : images) {
        if (!picture.save(source + QStringLiteral("/images/") + fileName))
            return {};
    }
    const QString output = base.filePath(name + QStringLiteral("/help.qch"));
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral(QPM_HELPGEN),
                  {QStringLiteral("--language"), language, QStringLiteral("--source"), source,
                   QStringLiteral("--topics"), topics.join(u','),
                   QStringLiteral("--qhelpgenerator"), QStringLiteral(QPM_QHELPGENERATOR),
                   QStringLiteral("--work"), base.filePath(name + QStringLiteral("/work")),
                   QStringLiteral("--output"), output});
    if (!process.waitForFinished(60000) || process.exitCode() != 0) {
        qWarning().noquote() << process.readAll();
        return {};
    }
    return output;
}

void TestHelp::f1OpensTheChapterOfEveryArea_data()
{
    QTest::addColumn<QString>("language");

    QTest::newRow("German") << QStringLiteral("de");
    QTest::newRow("English") << QStringLiteral("en");
}

void TestHelp::f1OpensTheChapterOfEveryArea()
{
    QFETCH(QString, language);
    m_help->setLanguage(language);
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    const QStringList keys = sectionKeys();
    QVERIFY(keys.size() >= 11);
    QQuickItem *header = m_window->property("header").value<QQuickItem *>();
    QVERIFY(header);

    for (int index = 0; index < keys.size(); ++index) {
        const QString &key = keys.at(index);
        showSection(index);
        QTest::keyClick(m_window, Qt::Key_F1);
        QTRY_VERIFY2(m_window->property("helpShown").toBool(), qPrintable(key));
        QTRY_VERIFY(!m_help->isBusy());
        QCOMPARE(m_help->currentTopic(), key);
        QCOMPARE(m_help->currentLanguage(), language);
        QCOMPARE(m_help->currentTitle(), heading(language, key));
        QVERIFY(m_help->currentHtml().contains(QStringLiteral("id=\"%1\"").arg(key)));
        QCOMPARE(header->property("title").toString(), QStringLiteral("Help"));
        QTRY_VERIFY2(item(QStringLiteral("helpText"))->hasActiveFocus(), qPrintable(key));
    }

    // The two dialogs of the sidebar have chapters of their own.
    for (const auto &[dialogName, topic] :
         {std::pair{QStringLiteral("settingsDialog"), QStringLiteral("settings")},
          std::pair{QStringLiteral("backupDialog"), QStringLiteral("backup")}}) {
        showSection(0);
        QObject *dialog = m_window->findChild<QObject *>(dialogName);
        QVERIFY(dialog);
        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(m_window, Qt::Key_F1);
        QTRY_VERIFY2(!dialog->property("visible").toBool(), qPrintable(dialogName));
        QVERIFY(m_window->property("helpShown").toBool());
        QTRY_VERIFY(!m_help->isBusy());
        QCOMPARE(m_help->currentTopic(), topic);
        QCOMPARE(m_help->currentTitle(), heading(language, topic));
    }
    showSection(0);
}

void TestHelp::contentsListEveryAreaOfTheProgram_data()
{
    f1OpensTheChapterOfEveryArea_data();
}

// The contents hold the start, every area of the sidebar, backup and
// settings, in this order, with the titles of the handbook.
void TestHelp::contentsListEveryAreaOfTheProgram()
{
    QFETCH(QString, language);
    m_help->setLanguage(language);
    QVERIFY(m_help->openTopic(QStringLiteral("getting-started")));
    QTRY_VERIFY(!m_help->isBusy());
    const QStringList expected = QStringList{QStringLiteral("getting-started")} + sectionKeys() +
                                 QStringList{QStringLiteral("backup"), QStringLiteral("settings")};

    const QList<qpm::ui::HelpTopicModel::Topic> topics = m_help->toc()->topics();

    QStringList found;
    for (const qpm::ui::HelpTopicModel::Topic &topic : topics) {
        found.append(topic.topic);
        QCOMPARE(topic.title, heading(language, topic.topic));
    }
    QCOMPARE(found, expected);
}

void TestHelp::sidebarOpensTheStartOfTheHelp()
{
    m_help->setLanguage(QStringLiteral("de"));
    showSection(1);
    QQuickItem *button = item(QStringLiteral("section-help"));
    QVERIFY(button);
    QVERIFY(button->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(button, "click"));

    QTRY_VERIFY(m_window->property("helpShown").toBool());
    QCOMPARE(m_help->currentTopic(), QStringLiteral("getting-started"));
    QVERIFY(button->property("highlighted").toBool());
    QVERIFY(!item(QStringLiteral("section-contacts"))->property("highlighted").toBool());
    QVERIFY(item(QStringLiteral("helpPage"))->isVisible());
    QVERIFY(!item(QStringLiteral("page-contacts"))->isVisible());

    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("section-contacts")), "click"));
    QTRY_VERIFY(!m_window->property("helpShown").toBool());
    QVERIFY(item(QStringLiteral("page-contacts"))->isVisible());
}

// The start chapter links to every area; Tab moves from link to link inside
// the text, Enter follows the chosen one, and Tab after the last link leaves
// the text.
void TestHelp::linksOfTheStartChapterAreFollowedByKeyboard()
{
    m_help->setLanguage(QStringLiteral("en"));
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("section-help")), "click"));
    QTRY_COMPARE(m_help->currentTopic(), QStringLiteral("getting-started"));
    QTRY_VERIFY(!m_help->isBusy());
    QQuickItem *text = item(QStringLiteral("helpText"));
    QTRY_VERIFY(text->hasActiveFocus());

    QTest::keyClick(m_window, Qt::Key_Tab);
    QCOMPARE(text->property("selectedText").toString(),
             heading(QStringLiteral("en"), QStringLiteral("overview")));
    QTest::keyClick(m_window, Qt::Key_Tab);
    QCOMPARE(text->property("selectedText").toString(),
             heading(QStringLiteral("en"), QStringLiteral("contacts")));
    QTest::keyClick(m_window, Qt::Key_Backtab);
    QCOMPARE(text->property("selectedText").toString(),
             heading(QStringLiteral("en"), QStringLiteral("overview")));
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_COMPARE(m_help->currentTopic(), QStringLiteral("overview"));
    QVERIFY(m_help->canGoBack());

    QQuickItem *back = item(QStringLiteral("helpBackButton"));
    back->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_COMPARE(m_help->currentTopic(), QStringLiteral("getting-started"));

    // Every area of the sidebar, backup and settings.
    const qsizetype links = sectionKeys().size() + 2;
    text->forceActiveFocus(Qt::TabFocusReason);
    int presses = 0;
    while (text->hasActiveFocus() && presses < 40) {
        QTest::keyClick(m_window, Qt::Key_Tab);
        ++presses;
    }
    // One press per area link, and one more that leaves the text.
    QCOMPARE(presses, links + 1);
}

void TestHelp::missingChapterShowsAPlainMessage()
{
    m_window->setProperty("helpShown", true);
    QVERIFY(m_help->openTopic(QStringLiteral("getting-started")));
    QTRY_VERIFY(!m_help->isBusy());

    QVERIFY(!m_help->openTopic(QStringLiteral("no-such-area")));

    QCOMPARE(m_help->currentTopic(), QStringLiteral("no-such-area"));
    QVERIFY(m_help->currentHtml().isEmpty());
    QVERIFY(!m_help->message().isEmpty());
    QQuickItem *message = item(QStringLiteral("helpMessage"));
    QVERIFY(message);
    QTRY_VERIFY(message->isVisible());
    QCOMPARE(message->property("text").toString(), m_help->message());
    QCOMPARE(message->property("textFormat").toInt(), int(Qt::PlainText));
    QVERIFY(!item(QStringLiteral("helpText"))->isVisible());

    QVERIFY(m_help->openTopic(QStringLiteral("contacts")));
    QVERIFY(m_help->message().isEmpty());
    QTRY_VERIFY(!message->isVisible());
    showSection(0);
}

void TestHelp::fallsBackToEnglishThenGerman()
{
    const QString german =
        buildHelpFile(QStringLiteral("de"),
                      {{QStringLiteral("alpha"), QStringLiteral("# Alpha auf Deutsch\n")},
                       {QStringLiteral("beta"), QStringLiteral("# Beta auf Deutsch\n")}},
                      {QStringLiteral("alpha"), QStringLiteral("beta")});
    const QString english =
        buildHelpFile(QStringLiteral("en"),
                      {{QStringLiteral("alpha"), QStringLiteral("# Alpha in English\n")},
                       {QStringLiteral("gamma"), QStringLiteral("# Gamma in English\n")}},
                      {QStringLiteral("alpha"), QStringLiteral("gamma")});
    QVERIFY(!german.isEmpty() && !english.isEmpty());
    HelpController help({german, english});

    help.setLanguage(QStringLiteral("de"));
    QVERIFY(help.openTopic(QStringLiteral("alpha")));
    QTRY_VERIFY(!help.isBusy());
    QCOMPARE(help.currentTitle(), QStringLiteral("Alpha auf Deutsch"));
    QVERIFY(help.openTopic(QStringLiteral("gamma")));
    QCOMPARE(help.currentLanguage(), QStringLiteral("en"));

    help.setLanguage(QStringLiteral("fr"));
    QVERIFY(help.openTopic(QStringLiteral("alpha")));
    QCOMPARE(help.currentTitle(), QStringLiteral("Alpha in English"));
    QVERIFY(help.openTopic(QStringLiteral("beta")));
    QCOMPARE(help.currentTitle(), QStringLiteral("Beta auf Deutsch"));
    QVERIFY(!help.openTopic(QStringLiteral("delta")));
    QVERIFY(!help.message().isEmpty());

    // A new language shows the open chapter again, in that language.
    QVERIFY(help.openTopic(QStringLiteral("alpha")));
    help.setLanguage(QStringLiteral("de"));
    QCOMPARE(help.currentTitle(), QStringLiteral("Alpha auf Deutsch"));
    QStringList contents;
    const QList<qpm::ui::HelpTopicModel::Topic> topics = help.toc()->topics();
    for (const qpm::ui::HelpTopicModel::Topic &topic : topics)
        contents.append(topic.title);
    QCOMPARE(contents,
             (QStringList{QStringLiteral("Alpha auf Deutsch"), QStringLiteral("Beta auf Deutsch"),
                          QStringLiteral("Gamma in English")}));
}

void TestHelp::historyGoesBackAndForward()
{
    const QString file =
        buildHelpFile(QStringLiteral("en"),
                      {{QStringLiteral("alpha"), QStringLiteral("# Alpha\n")},
                       {QStringLiteral("beta"), QStringLiteral("# Beta\n")},
                       {QStringLiteral("gamma"), QStringLiteral("# Gamma\n")}},
                      {QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")});
    QVERIFY(!file.isEmpty());
    HelpController help({file});
    help.setLanguage(QStringLiteral("en"));
    QVERIFY(!help.canGoBack());

    QVERIFY(help.openTopic(QStringLiteral("alpha")));
    QTRY_VERIFY(!help.isBusy());
    QVERIFY(help.openTopic(QStringLiteral("beta")));
    QVERIFY(help.openTopic(QStringLiteral("gamma")));
    help.back();
    QCOMPARE(help.currentTopic(), QStringLiteral("beta"));
    help.back();
    QCOMPARE(help.currentTopic(), QStringLiteral("alpha"));
    QVERIFY(!help.canGoBack());
    help.back();
    QCOMPARE(help.currentTopic(), QStringLiteral("alpha"));
    help.forward();
    QCOMPARE(help.currentTopic(), QStringLiteral("beta"));
    QVERIFY(help.canGoForward());
    QVERIFY(help.openTopic(QStringLiteral("alpha")));
    QVERIFY(!help.canGoForward());
}

void TestHelp::onlyChapterLinksAndHttpsAreFollowed()
{
    const QString file = buildHelpFile(QStringLiteral("en"),
                                       {{QStringLiteral("alpha"), QStringLiteral("# Alpha\n")},
                                        {QStringLiteral("beta"), QStringLiteral("# Beta\n")}},
                                       {QStringLiteral("alpha"), QStringLiteral("beta")});
    QVERIFY(!file.isEmpty());
    HelpController help({file});
    QList<QUrl> opened;
    help.setUrlOpener([&opened](const QUrl &url) {
        opened.append(url);
        return true;
    });
    QVERIFY(help.openTopic(QStringLiteral("alpha")));
    QTRY_VERIFY(!help.isBusy());

    QVERIFY(help.openLink(QStringLiteral("https://example.org/manual")));
    QCOMPARE(opened, QList<QUrl>{QUrl(QStringLiteral("https://example.org/manual"))});
    for (const QString &refused :
         {QStringLiteral("http://example.org"), QStringLiteral("file:///etc/passwd"),
          QStringLiteral("javascript:alert(1)"), QStringLiteral("../beta.html"),
          QStringLiteral("https://"), QString()}) {
        QTest::ignoreMessage(
            QtWarningMsg, qPrintable(QStringLiteral("Help link left alone: \"%1\"").arg(refused)));
        QVERIFY2(!help.openLink(refused), qPrintable(refused));
    }
    QCOMPARE(opened.size(), 1);
    QCOMPARE(help.currentTopic(), QStringLiteral("alpha"));
    QVERIFY(help.openLink(QStringLiteral("beta.html#beta")));
    QCOMPARE(help.currentTopic(), QStringLiteral("beta"));
}

void TestHelp::linkPositionsMatchTheText()
{
    const QString file = buildHelpFile(
        QStringLiteral("en"),
        {{QStringLiteral("alpha"),
          QStringLiteral(
              "# Alpha\n\nSee [**Beta** here](beta.md) and [the site](https://example.org).\n")},
         {QStringLiteral("beta"), QStringLiteral("# Beta\n")}},
        {QStringLiteral("alpha"), QStringLiteral("beta")});
    QVERIFY(!file.isEmpty());
    HelpController help({file});
    QVERIFY(help.openTopic(QStringLiteral("alpha")));
    QTRY_VERIFY(!help.isBusy());
    const QString html = help.currentHtml();

    const QVariantMap first = help.nextLink(0, true);
    QCOMPARE(selected(html, first.value(QStringLiteral("start")).toInt(),
                      first.value(QStringLiteral("end")).toInt()),
             QStringLiteral("Beta here"));
    QCOMPARE(first.value(QStringLiteral("href")).toString(), QStringLiteral("beta.html"));
    const QVariantMap second = help.nextLink(first.value(QStringLiteral("end")).toInt(), true);
    QCOMPARE(selected(html, second.value(QStringLiteral("start")).toInt(),
                      second.value(QStringLiteral("end")).toInt()),
             QStringLiteral("the site"));
    QVERIFY(help.nextLink(second.value(QStringLiteral("end")).toInt(), true).isEmpty());
    QCOMPARE(help.nextLink(second.value(QStringLiteral("start")).toInt(), false), first);
    QCOMPARE(help.linkAt(first.value(QStringLiteral("start")).toInt()),
             QStringLiteral("beta.html"));
    QVERIFY(help.linkAt(0).isEmpty());
}

void TestHelp::imagesOfAChapterAreShown()
{
    QImage picture(60, 40, QImage::Format_RGB32);
    picture.fill(Qt::darkCyan);
    const QString file = buildHelpFile(
        QStringLiteral("de"),
        {{QStringLiteral("alpha"), QStringLiteral("# Alpha\n\n![Die Liste](images/liste.png)\n")}},
        {QStringLiteral("alpha")}, {{QStringLiteral("liste.png"), picture}});
    QVERIFY(!file.isEmpty());
    HelpController help({file});
    help.setLanguage(QStringLiteral("de"));

    QVERIFY(help.openTopic(QStringLiteral("alpha")));
    QTRY_VERIFY(!help.isBusy());

    static const QRegularExpression imageSource(QStringLiteral("<img src=\"([^\"]+)\""));
    const QRegularExpressionMatch match = imageSource.match(help.currentHtml());
    QVERIFY2(match.hasMatch(), qPrintable(help.currentHtml()));
    const QUrl url(match.captured(1));
    QVERIFY(url.isLocalFile());
    QVERIFY(url.toLocalFile().endsWith(QStringLiteral("/de/images/liste.png")));
    QCOMPARE(QImage(url.toLocalFile()).size(), QSize(60, 40));
}

// The first chapter sets up the help on a worker thread; the call returns
// at once and the page shows the chapter when the help is ready.
void TestHelp::firstChapterWaitsForTheHelpWithoutBlocking()
{
    const QString file = buildHelpFile(QStringLiteral("en"),
                                       {{QStringLiteral("alpha"), QStringLiteral("# Alpha\n")}},
                                       {QStringLiteral("alpha")});
    QVERIFY(!file.isEmpty());
    HelpController help({file});
    QSignalSpy busy(&help, &HelpController::busyChanged);
    QVERIFY(!help.isBusy());

    QVERIFY(help.openTopic(QStringLiteral("alpha")));

    QVERIFY(help.isBusy());
    QCOMPARE(help.currentTopic(), QStringLiteral("alpha"));
    QVERIFY(help.currentHtml().isEmpty());
    QTRY_VERIFY(!help.isBusy());
    QCOMPARE(busy.count(), 2);
    QCOMPARE(help.currentTitle(), QStringLiteral("Alpha"));
    QCOMPARE(help.toc()->rowCount(), 1);
}

void TestHelp::helpThatCannotBeLoadedSaysSo()
{
    HelpController help({m_fixtures.filePath(QStringLiteral("missing.qch"))});

    QVERIFY(help.openTopic(QStringLiteral("alpha")));

    QTRY_VERIFY(!help.isBusy());
    QVERIFY(help.currentHtml().isEmpty());
    QCOMPARE(
        help.message(),
        QStringLiteral("The help cannot be loaded. Start QPhoneManager again. If that does not "
                       "help, install it again."));
    QVERIFY(!help.openTopic(QStringLiteral("beta")));
    QCOMPARE(help.toc()->rowCount(), 0);
}

// The address stays visible and can be copied when the desktop does not open it.
void TestHelp::failedWebLinkShowsItsAddress()
{
    m_window->setProperty("helpShown", true);
    QVERIFY(m_help->openTopic(QStringLiteral("contacts")));
    QTRY_VERIFY(!m_help->isBusy());
    m_help->setUrlOpener([](const QUrl &) { return false; });
    QQuickItem *notice = item(QStringLiteral("helpNotice"));
    QVERIFY(notice);
    QVERIFY(!notice->isVisible());

    QTest::ignoreMessage(QtWarningMsg, "The desktop did not open the help link \"example.org\"");
    QVERIFY(!m_help->openLink(QStringLiteral("https://example.org/hilfe")));

    QCOMPARE(m_help->notice(),
             QStringLiteral("The browser did not open. Copy the address and open it yourself: "
                            "https://example.org/hilfe"));
    QTRY_VERIFY(notice->isVisible());
    QCOMPARE(notice->property("text").toString(), m_help->notice());
    QVERIFY(notice->property("readOnly").toBool());
    QVERIFY(m_help->openTopic(QStringLiteral("calls")));
    QVERIFY(m_help->notice().isEmpty());
    QTRY_VERIFY(!notice->isVisible());
    m_help->setUrlOpener([](const QUrl &) { return true; });
    showSection(0);
}

// A switch of the colour scheme recolours the links and keeps the page, the
// selection in it and its scroll position.
void TestHelp::linkColourKeepsThePageAndTheSelection()
{
    m_help->setLanguage(QStringLiteral("en"));
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("section-help")), "click"));
    QTRY_COMPARE(m_help->currentTopic(), QStringLiteral("getting-started"));
    QTRY_VERIFY(!m_help->isBusy());
    QQuickItem *text = item(QStringLiteral("helpText"));
    QTRY_VERIFY(text->hasActiveFocus());
    // A large font makes the start chapter taller than its view, so it scrolls.
    const QVariant font = text->property("font");
    QFont large = font.value<QFont>();
    large.setPixelSize(48);
    text->setProperty("font", large);
    QQuickItem *flickable = text->parentItem();
    while (flickable && !flickable->inherits("QQuickFlickable"))
        flickable = flickable->parentItem();
    QVERIFY(flickable);
    // The last link of the chapter lies below the view; choosing it scrolls there.
    const qsizetype links = sectionKeys().size() + 2;
    for (qsizetype press = 0; press < links; ++press)
        QTest::keyClick(m_window, Qt::Key_Tab);
    const QString chosen = text->property("selectedText").toString();
    QCOMPARE(chosen, heading(QStringLiteral("en"), QStringLiteral("settings")));
    QTRY_VERIFY(flickable->property("contentY").toReal() > 0);
    const qreal scrolled = flickable->property("contentY").toReal();
    QSignalSpy page(m_help, &HelpController::pageChanged);
    QSignalSpy html(m_help, &HelpController::htmlChanged);
    const QColor before = m_help->linkColor();

    m_help->setLinkColor(QColor(0x12, 0x34, 0x56));

    QCOMPARE(page.count(), 0);
    QCOMPARE(html.count(), 1);
    QCOMPARE(m_help->currentTopic(), QStringLiteral("getting-started"));
    QVERIFY(m_help->currentHtml().contains(QStringLiteral("#123456")));
    QCOMPARE(text->property("selectedText").toString(), chosen);
    QCOMPARE(flickable->property("contentY").toReal(), scrolled);
    m_help->setLinkColor(before);
    QCOMPARE(text->property("selectedText").toString(), chosen);
    QCOMPARE(flickable->property("contentY").toReal(), scrolled);
    text->setProperty("font", font);
    showSection(0);
}

void TestHelp::licenseTextsAreReflowedWithTheirWording()
{
    for (const QString &path :
         {QStringLiteral("LICENSE"), QStringLiteral("res/icons/tabler/LICENSE"),
          QStringLiteral("third_party/qrcodegen/LICENSE")}) {
        const QString original = sourceText(path);
        const QString shown = qpm::ui::reflowedLicenseText(original);
        QCOMPARE(shown.simplified(), original.simplified());
        static const QRegularExpression blankLine(QStringLiteral("\n[ \t]*\n"));
        QCOMPARE(shown.count(QStringLiteral("\n\n")),
                 original.trimmed().split(blankLine).size() - 1);
        QVERIFY2(shown.count(u'\n') < original.trimmed().count(u'\n'), qPrintable(path));
    }
    const QString bsd = qpm::ui::reflowedLicenseText(sourceText(QStringLiteral("LICENSE")));
    for (const QString &clause : {QStringLiteral("\n1. Redistributions of source code"),
                                  QStringLiteral("\n2. Redistributions in binary form"),
                                  QStringLiteral("\n3. Neither the name")}) {
        QVERIFY2(bsd.contains(clause), qPrintable(clause));
    }
}

void TestHelp::aboutShowsVersionLicenseAndComponents()
{
    AboutInfo about;

    static const QRegularExpression ownLicense(QStringLiteral("SPDX: `([^`]+)`"));
    const QRegularExpressionMatch spdx =
        ownLicense.match(sourceText(QStringLiteral("THIRD_PARTY.md")));
    QVERIFY(spdx.hasMatch());

    QCOMPARE(about.version(), QStringLiteral(QPM_PROJECT_VERSION));
    QCOMPARE(about.license(), spdx.captured(1));
    QCOMPARE(about.copyright(), QStringLiteral("Copyright 2027 by Alexander Saal"));
    QCOMPARE(about.data(about.index(0), AboutInfo::NameRole).toString(),
             QStringLiteral("QPhoneManager"));
    QCOMPARE(about.data(about.index(0), AboutInfo::LicenseTextRole).toString(),
             qpm::ui::reflowedLicenseText(sourceText(QStringLiteral("LICENSE"))));

    // Every component of THIRD_PARTY.md is shown with its version and license.
    static const QRegularExpression row(
        QStringLiteral("^\\| ([^|]+?) \\| ([^|]+?) \\| `([^`]+)` \\|"),
        QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator rows =
        row.globalMatch(sourceText(QStringLiteral("THIRD_PARTY.md")));
    int listed = 0;
    while (rows.hasNext()) {
        const QRegularExpressionMatch match = rows.next();
        const QString name = match.captured(1);
        int found = -1;
        for (int i = 0; i < about.rowCount(); ++i) {
            if (about.data(about.index(i), AboutInfo::NameRole).toString() == name)
                found = i;
        }
        QVERIFY2(found > 0, qPrintable(name));
        QCOMPARE(about.data(about.index(found), AboutInfo::LicenseRole).toString(),
                 match.captured(3));
        if (name != QLatin1StringView("Qt")) {
            QCOMPARE(about.data(about.index(found), AboutInfo::VersionRole).toString(),
                     match.captured(2));
        }
        QVERIFY(!about.data(about.index(found), AboutInfo::LicenseTextRole).toString().isEmpty());
        ++listed;
    }
    QCOMPARE(listed, about.rowCount() - 1);
    const QMap<QString, QString> texts = {
        {QStringLiteral("Tabler Icons"), QStringLiteral("res/icons/tabler/LICENSE")},
        {QStringLiteral("QR Code generator library (Project Nayuki)"),
         QStringLiteral("third_party/qrcodegen/LICENSE")}};
    for (int i = 0; i < about.rowCount(); ++i) {
        const QString name = about.data(about.index(i), AboutInfo::NameRole).toString();
        if (texts.contains(name)) {
            QCOMPARE(about.data(about.index(i), AboutInfo::LicenseTextRole).toString(),
                     qpm::ui::reflowedLicenseText(sourceText(texts.value(name))));
        }
        if (name == QLatin1StringView("Qt")) {
            const QString notice =
                about.data(about.index(i), AboutInfo::LicenseTextRole).toString();
            QVERIFY(notice.startsWith(
                QStringLiteral("Copyright (C) The Qt Company Ltd. and other contributors\n")));
            QVERIFY(notice.contains(QStringLiteral("https://download.qt.io")));
            QVERIFY(notice.contains(QStringLiteral("https://www.gnu.org/licenses/lgpl-3.0.html")));
        }
    }
}

void TestHelp::aboutDialogOpensFromTheHelpAndShowsTheLicenses()
{
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("section-help")), "click"));
    QTRY_VERIFY(m_window->property("helpShown").toBool());
    QObject *dialog = m_window->findChild<QObject *>(QStringLiteral("aboutDialog"));
    QVERIFY(dialog);
    QQuickItem *button = item(QStringLiteral("aboutButton"));
    QVERIFY(button);
    button->forceActiveFocus(Qt::TabFocusReason);

    QTest::keyClick(m_window, Qt::Key_Space);

    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(dialog->property("title").toString(), QStringLiteral("About QPhoneManager"));
    QQuickItem *list = item(QStringLiteral("componentList"));
    QVERIFY(list);
    QTRY_VERIFY(list->hasActiveFocus());
    QVERIFY(item(QStringLiteral("aboutVersion"))
                ->property("text")
                .toString()
                .contains(QStringLiteral(QPM_PROJECT_VERSION)));
    QCOMPARE(item(QStringLiteral("aboutCopyright"))->property("text").toString(),
             QStringLiteral("Copyright 2027 by Alexander Saal"));
    QQuickItem *license = item(QStringLiteral("licenseText"));
    QCOMPARE(license->property("text").toString(),
             qpm::ui::reflowedLicenseText(sourceText(QStringLiteral("LICENSE"))));
    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_VERIFY(license->property("text").toString().contains(QStringLiteral("LGPL-3.0-only")));

    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    showSection(0);
}

QTEST_MAIN(TestHelp)

#include "tst_help.moc"
