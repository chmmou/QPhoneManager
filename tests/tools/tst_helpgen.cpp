#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHelpEngineCore>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QUrl>

#include <memory>

namespace {

struct Run
{
    int exitCode = -1;
    QString output;
};

bool writeText(const QString &path, const QString &text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) >= 0;
}

} // namespace

// The tool that turns the handbook into help files, run as the build runs it.
class TestHelpgen : public QObject
{
    Q_OBJECT

public:
    explicit TestHelpgen(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();

    void buildsAHelpFileWithContentsAndKeywords();
    void writesPlainHtmlWithTheTopicAsAnchor();
    void rejectsAChapterWithoutExactlyOneTopHeading_data();
    void rejectsAChapterWithoutExactlyOneTopHeading();
    void rejectsAMissingChapterAndAChapterOfNoArea();
    void rejectsLinksAndImagesItCannotCarry_data();
    void rejectsLinksAndImagesItCannotCarry();
    void carriesImagesIntoTheHelpFile();
    void keepsTheBlocksOfANumberedStepInsideItsItem();
    void numbersAListThatStartsLater();
    void leavesAFolderAloneThatItDidNotWrite_data();
    void leavesAFolderAloneThatItDidNotWrite();
    void refusesAGeneratorThatIsNoAbsoluteExecutable_data();
    void refusesAGeneratorThatIsNoAbsoluteExecutable();

private:
    [[nodiscard]] Run
    runHelpgen(const QString &topics, const QString &work = {},
               const QString &generator = QStringLiteral(QPM_QHELPGENERATOR)) const;
    [[nodiscard]] QString source(const QString &file) const;
    [[nodiscard]] QString page(const QString &topic) const;

    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestHelpgen::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    QVERIFY(QDir(m_dir->path()).mkpath(QStringLiteral("source/images")));
}

QString TestHelpgen::source(const QString &file) const
{
    return m_dir->filePath(QStringLiteral("source/") + file);
}

QString TestHelpgen::page(const QString &topic) const
{
    QFile file(m_dir->filePath(QStringLiteral("work/%1.html").arg(topic)));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

Run TestHelpgen::runHelpgen(const QString &topics, const QString &work,
                            const QString &generator) const
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral(QPM_HELPGEN),
                  {QStringLiteral("--language"), QStringLiteral("de"), QStringLiteral("--source"),
                   m_dir->filePath(QStringLiteral("source")), QStringLiteral("--topics"), topics,
                   QStringLiteral("--qhelpgenerator"), generator, QStringLiteral("--work"),
                   work.isEmpty() ? m_dir->filePath(QStringLiteral("work")) : work,
                   QStringLiteral("--output"), m_dir->filePath(QStringLiteral("out.qch")),
                   QStringLiteral("--depfile"), m_dir->filePath(QStringLiteral("out.d"))});
    Run run;
    if (!process.waitForFinished(60000))
        return run;
    run.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    run.output = QString::fromUtf8(process.readAll());
    return run;
}

void TestHelpgen::buildsAHelpFileWithContentsAndKeywords()
{
    QVERIFY(writeText(source(QStringLiteral("alpha.md")),
                      QStringLiteral("# Übersicht\n\nErster Satz.\n\n## Teil\n\nMehr.\n")));
    QVERIFY(writeText(source(QStringLiteral("beta-two.md")),
                      QStringLiteral("# Kontakte\n\nZweiter Satz.\n")));

    const Run run = runHelpgen(QStringLiteral("beta-two,alpha"));

    QVERIFY2(run.exitCode == 0, qPrintable(run.output));
    const QString qch = m_dir->filePath(QStringLiteral("out.qch"));
    QCOMPARE(QHelpEngineCore::namespaceName(qch), QStringLiteral("org.qphonemanager.help.de"));
    QFile topics(m_dir->filePath(QStringLiteral("work/topics.txt")));
    QVERIFY(topics.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(topics.readAll()),
             QStringLiteral("beta-two\tKontakte\nalpha\tÜbersicht\n"));
    QFile project(m_dir->filePath(QStringLiteral("work/qphonemanager_de.qhp")));
    QVERIFY(project.open(QIODevice::ReadOnly));
    const QString qhp = QString::fromUtf8(project.readAll());
    QVERIFY(qhp.contains(QStringLiteral("id=\"alpha\" ref=\"alpha.html#alpha\"")));
    QVERIFY(qhp.indexOf(QStringLiteral("ref=\"beta-two.html\"")) <
            qhp.indexOf(QStringLiteral("ref=\"alpha.html\"")));
    // The build rebuilds the help when a chapter changes.
    QFile depfile(m_dir->filePath(QStringLiteral("out.d")));
    QVERIFY(depfile.open(QIODevice::ReadOnly));
    const QString rule = QString::fromUtf8(depfile.readAll());
    QVERIFY(rule.startsWith(QFileInfo(qch).absoluteFilePath() + QStringLiteral(": ")));
    QVERIFY(rule.contains(QFileInfo(source(QStringLiteral("alpha.md"))).absoluteFilePath()));
    // A new file in the folder of the chapters runs the tool again.
    QVERIFY(rule.contains(QFileInfo(m_dir->filePath(QStringLiteral("source"))).absoluteFilePath() +
                          u' '));
}

void TestHelpgen::writesPlainHtmlWithTheTopicAsAnchor()
{
    QVERIFY(
        writeText(source(QStringLiteral("alpha.md")),
                  QStringLiteral("# Kontakte & Gruppen\n\nText mit **fett**, *schräg*, `code` und "
                                 "[Beta](beta.md) und [Netz](https://example.org/a?b=c).\n\n"
                                 "- eins\n- zwei\n\n1. erst\n2. dann\n\n<b>roh</b>\n")));
    QVERIFY(writeText(source(QStringLiteral("beta.md")), QStringLiteral("# Beta\n")));

    const Run run = runHelpgen(QStringLiteral("alpha,beta"));

    QVERIFY2(run.exitCode == 0, qPrintable(run.output));
    QFile page(m_dir->filePath(QStringLiteral("work/alpha.html")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(page.readAll());
    QVERIFY(html.contains(QStringLiteral("<html lang=\"de\">")));
    QVERIFY(html.contains(QStringLiteral("<title>Kontakte &amp; Gruppen</title>")));
    QVERIFY(html.contains(QStringLiteral("<h1 id=\"alpha\">Kontakte &amp; Gruppen</h1>")));
    QVERIFY(html.contains(QStringLiteral("<b>fett</b>")));
    QVERIFY(html.contains(QStringLiteral("<i>schräg</i>")));
    QVERIFY(html.contains(QStringLiteral("<code>code</code>")));
    QVERIFY(html.contains(QStringLiteral("<a href=\"beta.html\">Beta</a>")));
    QVERIFY(html.contains(QStringLiteral("<a href=\"https://example.org/a?b=c\">Netz</a>")));
    QVERIFY(html.contains(QStringLiteral("<ul>\n<li>eins</li>\n<li>zwei</li>\n</ul>")));
    QVERIFY(html.contains(QStringLiteral("<ol>\n<li>erst</li>\n<li>dann</li>\n</ol>")));
    // Raw HTML in the handbook is shown, not interpreted.
    QVERIFY(html.contains(QStringLiteral("&lt;b&gt;roh&lt;/b&gt;")));
    QVERIFY(!html.contains(QStringLiteral("style=")));
}

void TestHelpgen::rejectsAChapterWithoutExactlyOneTopHeading_data()
{
    QTest::addColumn<QString>("markdown");
    QTest::addColumn<QString>("expected");

    QTest::newRow("none") << QStringLiteral("## Nur Unterkapitel\n\nText.\n")
                          << QStringLiteral("has 0 top-level headings");
    QTest::newRow("two") << QStringLiteral("# Eins\n\nText.\n\n# Zwei\n")
                         << QStringLiteral("has 2 top-level headings");
}

void TestHelpgen::rejectsAChapterWithoutExactlyOneTopHeading()
{
    QFETCH(QString, markdown);
    QFETCH(QString, expected);
    QVERIFY(writeText(source(QStringLiteral("alpha.md")), markdown));

    const Run run = runHelpgen(QStringLiteral("alpha"));

    QCOMPARE(run.exitCode, 1);
    QVERIFY2(run.output.contains(source(QStringLiteral("alpha.md"))), qPrintable(run.output));
    QVERIFY2(run.output.contains(expected), qPrintable(run.output));
    QVERIFY(!QFile::exists(m_dir->filePath(QStringLiteral("out.qch"))));
}

void TestHelpgen::rejectsAMissingChapterAndAChapterOfNoArea()
{
    QVERIFY(writeText(source(QStringLiteral("alpha.md")), QStringLiteral("# Alpha\n")));
    QVERIFY(writeText(source(QStringLiteral("stray.md")), QStringLiteral("# Stray\n")));

    const Run run = runHelpgen(QStringLiteral("alpha,beta"));

    QCOMPARE(run.exitCode, 1);
    QVERIFY2(run.output.contains(source(QStringLiteral("beta.md")) +
                                 QStringLiteral(": the chapter is missing")),
             qPrintable(run.output));
    QVERIFY2(run.output.contains(source(QStringLiteral("stray.md")) +
                                 QStringLiteral(": no area of the program has this chapter")),
             qPrintable(run.output));
}

void TestHelpgen::rejectsLinksAndImagesItCannotCarry_data()
{
    QTest::addColumn<QString>("markdown");
    QTest::addColumn<QString>("expected");

    QTest::newRow("http") << QStringLiteral("[Netz](http://example.org)")
                          << QStringLiteral("link \"http://example.org\"");
    QTest::newRow("file") << QStringLiteral("[Datei](file:///etc/passwd)")
                          << QStringLiteral("link \"file:///etc/passwd\"");
    QTest::newRow("unknown chapter")
        << QStringLiteral("[Gamma](gamma.md)") << QStringLiteral("link \"gamma.md\"");
    QTest::newRow("image outside images/")
        << QStringLiteral("![Bild](../geheim.png)") << QStringLiteral("image \"../geheim.png\"");
    QTest::newRow("image of another type") << QStringLiteral("![Bild](images/bild.svg)")
                                           << QStringLiteral("image \"images/bild.svg\"");
    QTest::newRow("missing image") << QStringLiteral("![Bild](images/fehlt.png)")
                                   << QStringLiteral("image \"images/fehlt.png\" does not exist");
}

void TestHelpgen::rejectsLinksAndImagesItCannotCarry()
{
    QFETCH(QString, markdown);
    QFETCH(QString, expected);
    QVERIFY(writeText(source(QStringLiteral("alpha.md")),
                      QStringLiteral("# Alpha\n\nSiehe %1.\n").arg(markdown)));

    const Run run = runHelpgen(QStringLiteral("alpha"));

    QCOMPARE(run.exitCode, 1);
    QVERIFY2(run.output.contains(expected), qPrintable(run.output));
}

void TestHelpgen::carriesImagesIntoTheHelpFile()
{
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QVERIFY(image.save(source(QStringLiteral("images/liste.png"))));
    QVERIFY(writeText(source(QStringLiteral("alpha.md")),
                      QStringLiteral("# Alpha\n\n![Die Liste](images/liste.png)\n")));

    const Run run = runHelpgen(QStringLiteral("alpha"));

    QVERIFY2(run.exitCode == 0, qPrintable(run.output));
    QHelpEngineCore engine(m_dir->filePath(QStringLiteral("collection.qhc")));
    engine.setReadOnly(false);
    QVERIFY(engine.setupData());
    QVERIFY2(engine.registerDocumentation(m_dir->filePath(QStringLiteral("out.qch"))),
             qPrintable(engine.error()));
    const QByteArray stored = engine.fileData(
        QUrl(QStringLiteral("qthelp://org.qphonemanager.help.de/doc/images/liste.png")));
    QImage read;
    QVERIFY(read.loadFromData(stored, "PNG"));
    QCOMPARE(read.size(), QSize(40, 30));
    const QString page = QString::fromUtf8(
        engine.fileData(QUrl(QStringLiteral("qthelp://org.qphonemanager.help.de/doc/alpha.html"))));
    QVERIFY2(page.contains(QStringLiteral("<img src=\"images/liste.png\" alt=\"Die Liste\"/>")),
             qPrintable(page));
}

// The Markdown importer puts only the first paragraph of an item into its
// list; the picture between two steps ended the list and the second step
// started a new one at 1.
void TestHelpgen::keepsTheBlocksOfANumberedStepInsideItsItem()
{
    QImage image(20, 10, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QVERIFY(image.save(source(QStringLiteral("images/schritt.png"))));
    QVERIFY(
        writeText(source(QStringLiteral("alpha.md")),
                  QStringLiteral("# Alpha\n\n1. Erst das.\n\n   ![Schritt](images/schritt.png)\n\n"
                                 "   Dann weiter.\n\n2. Danach das.\n\nEnde.\n")));

    const Run run = runHelpgen(QStringLiteral("alpha"));

    QVERIFY2(run.exitCode == 0, qPrintable(run.output));
    const QString html = page(QStringLiteral("alpha"));
    QCOMPARE(html.count(QStringLiteral("<ol")), 1);
    QCOMPARE(html.count(QStringLiteral("</ol>")), 1);
    const qsizetype first = html.indexOf(QStringLiteral("<li>Erst das."));
    const qsizetype picture = html.indexOf(QStringLiteral("<img src=\"images/schritt.png\""));
    const qsizetype more = html.indexOf(QStringLiteral("Dann weiter."));
    const qsizetype second = html.indexOf(QStringLiteral("<li>Danach das."));
    QVERIFY2(first >= 0 && first < picture && picture < more && more < second, qPrintable(html));
    QVERIFY2(html.indexOf(QStringLiteral("</li>"), first) > more, qPrintable(html));
    QVERIFY2(html.indexOf(QStringLiteral("</ol>")) < html.indexOf(QStringLiteral("Ende.")),
             qPrintable(html));
    // Shown as rich text, the picture and the paragraph stay in the first step.
    QTextDocument shown;
    shown.setHtml(html);
    QTextList *steps = nullptr;
    for (QTextBlock block = shown.begin(); block.isValid() && !steps; block = block.next())
        steps = block.textList();
    QVERIFY(steps);
    QCOMPARE(steps->count(), 2);
}

void TestHelpgen::numbersAListThatStartsLater()
{
    QVERIFY(writeText(source(QStringLiteral("alpha.md")),
                      QStringLiteral("# Alpha\n\n3. Drei\n4. Vier\n")));

    const Run run = runHelpgen(QStringLiteral("alpha"));

    QVERIFY2(run.exitCode == 0, qPrintable(run.output));
    QVERIFY2(page(QStringLiteral("alpha"))
                 .contains(QStringLiteral("<ol start=\"3\">\n<li>Drei</li>\n<li>Vier</li>\n</ol>")),
             qPrintable(page(QStringLiteral("alpha"))));
}

void TestHelpgen::leavesAFolderAloneThatItDidNotWrite_data()
{
    QTest::addColumn<QString>("entry");

    QTest::newRow("a file") << QStringLiteral("keep.txt");
    QTest::newRow("only a hidden file") << QStringLiteral(".keep");
}

// The tool empties its work folder; a folder with files of someone else stays.
void TestHelpgen::leavesAFolderAloneThatItDidNotWrite()
{
    QFETCH(QString, entry);
    QVERIFY(writeText(source(QStringLiteral("alpha.md")), QStringLiteral("# Alpha\n")));
    const QString foreign = m_dir->filePath(QStringLiteral("foreign"));
    QVERIFY(QDir().mkpath(foreign));
    QVERIFY(writeText(foreign + u'/' + entry, QStringLiteral("mine")));

    const Run run = runHelpgen(QStringLiteral("alpha"), foreign);

    QCOMPARE(run.exitCode, 1);
    QVERIFY2(run.output.contains(QStringLiteral("holds files helpgen did not write")),
             qPrintable(run.output));
    QVERIFY(QFile::exists(foreign + u'/' + entry));
}

void TestHelpgen::refusesAGeneratorThatIsNoAbsoluteExecutable_data()
{
    QTest::addColumn<QString>("generator");

    QTest::newRow("name on PATH") << QStringLiteral("qhelpgenerator");
    QTest::newRow("relative path") << QStringLiteral("libexec/qhelpgenerator");
    QTest::newRow("not executable") << QStringLiteral("source/alpha.md");
}

void TestHelpgen::refusesAGeneratorThatIsNoAbsoluteExecutable()
{
    QFETCH(QString, generator);
    QVERIFY(writeText(source(QStringLiteral("alpha.md")), QStringLiteral("# Alpha\n")));
    const QString path =
        generator.startsWith(QLatin1StringView("source/")) ? m_dir->filePath(generator) : generator;

    const Run run = runHelpgen(QStringLiteral("alpha"), {}, path);

    QCOMPARE(run.exitCode, 1);
    QVERIFY2(run.output.contains(
                 QStringLiteral("--qhelpgenerator needs the absolute path of an executable file")),
             qPrintable(run.output));
    QVERIFY(!QFile::exists(m_dir->filePath(QStringLiteral("out.qch"))));
}

QTEST_MAIN(TestHelpgen)

#include "tst_helpgen.moc"
