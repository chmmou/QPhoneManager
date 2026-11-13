// helpgen turns the Markdown handbook of one language into a Qt compressed
// help file (.qch): one HTML page per chapter, a help project with a table of
// contents and a keyword per chapter whose id is the chapter's topic, and the
// images the chapters show. The build fails on the first problem it reports.

#include "HandbookChapter.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QXmlStreamWriter>

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

// Marks a work folder as made by helpgen, so that a second run may empty it.
constexpr QLatin1StringView workMarker(".helpgen");
// Read by the program to list the chapters in order: "<topic>\t<title>" per line.
constexpr QLatin1StringView topicsFile("topics.txt");

struct Options
{
    QString language;
    QString sourceDirectory;
    QStringList topics;
    QString qhelpgenerator;
    QString workDirectory;
    QString output;
    QString depfile;
};

void report(const QString &line)
{
    QTextStream(stderr) << "helpgen: " << line << '\n';
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        report(QStringLiteral("%1: cannot write: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

// Empties the work folder, but only one that is empty or was made by helpgen.
bool prepareWorkDirectory(const QString &path)
{
    QDir directory(path);
    if (directory.exists() &&
        !directory.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System) &&
        !QFile::exists(directory.filePath(QString(workMarker)))) {
        report(QStringLiteral("%1: the work folder holds files helpgen did not write; "
                              "choose an empty folder")
                   .arg(path));
        return false;
    }
    if (directory.exists() && !directory.removeRecursively()) {
        report(QStringLiteral("%1: cannot empty the work folder").arg(path));
        return false;
    }
    if (!QDir().mkpath(directory.filePath(QStringLiteral("images")))) {
        report(QStringLiteral("%1: cannot create the work folder").arg(path));
        return false;
    }
    return writeFile(directory.filePath(QString(workMarker)), QByteArray());
}

QString escapedForDepfile(QString path)
{
    path.replace(u'\\', QStringLiteral("\\\\"));
    path.replace(u' ', QStringLiteral("\\ "));
    path.replace(u'#', QStringLiteral("\\#"));
    path.replace(u'$', QStringLiteral("$$"));
    return path;
}

QByteArray helpProject(const Options &options, const QList<qpm::helpgen::Chapter> &chapters,
                       const QStringList &images)
{
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("QtHelpProject"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    writer.writeTextElement(QStringLiteral("namespace"),
                            QStringLiteral("org.qphonemanager.help.") + options.language);
    writer.writeTextElement(QStringLiteral("virtualFolder"), QStringLiteral("doc"));
    writer.writeStartElement(QStringLiteral("filterSection"));

    writer.writeStartElement(QStringLiteral("toc"));
    for (const qpm::helpgen::Chapter &chapter : chapters) {
        writer.writeEmptyElement(QStringLiteral("section"));
        writer.writeAttribute(QStringLiteral("title"), chapter.title);
        writer.writeAttribute(QStringLiteral("ref"), chapter.topic + QStringLiteral(".html"));
    }
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("keywords"));
    for (const qpm::helpgen::Chapter &chapter : chapters) {
        writer.writeEmptyElement(QStringLiteral("keyword"));
        writer.writeAttribute(QStringLiteral("name"), chapter.title);
        writer.writeAttribute(QStringLiteral("id"), chapter.topic);
        writer.writeAttribute(QStringLiteral("ref"),
                              chapter.topic + QStringLiteral(".html#") + chapter.topic);
    }
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("files"));
    for (const qpm::helpgen::Chapter &chapter : chapters)
        writer.writeTextElement(QStringLiteral("file"), chapter.topic + QStringLiteral(".html"));
    writer.writeTextElement(QStringLiteral("file"), QString(topicsFile));
    for (const QString &image : images)
        writer.writeTextElement(QStringLiteral("file"), image);
    writer.writeEndElement();

    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();
    return xml;
}

// qhelpgenerator reports a missing file or a dead link as a warning and still
// succeeds; such a warning fails the build here.
bool runQhelpgenerator(const Options &options, const QString &project)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(options.qhelpgenerator,
                  {project, QStringLiteral("-c"), QStringLiteral("-s"), QStringLiteral("-o"),
                   QFileInfo(options.output).absoluteFilePath()});
    if (!process.waitForStarted() || !process.waitForFinished(-1)) {
        report(QStringLiteral("%1: cannot run: %2")
                   .arg(options.qhelpgenerator, process.errorString()));
        return false;
    }
    const QString output = QString::fromLocal8Bit(process.readAll()).trimmed();
    const bool warned = output.contains(QLatin1StringView("warning"), Qt::CaseInsensitive) ||
                        output.contains(QLatin1StringView("error"), Qt::CaseInsensitive);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || warned) {
        report(QStringLiteral("qhelpgenerator failed for %1:\n%2").arg(project, output));
        return false;
    }
    return true;
}

bool parseOptions(const QCoreApplication &app, Options &options)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Builds the help file of one language."));
    parser.addHelpOption();
    const QCommandLineOption language(QStringLiteral("language"),
                                      QStringLiteral("Language of the chapters, for example de."),
                                      QStringLiteral("code"));
    const QCommandLineOption source(QStringLiteral("source"),
                                    QStringLiteral("Folder with <topic>.md and images/."),
                                    QStringLiteral("folder"));
    const QCommandLineOption topics(QStringLiteral("topics"),
                                    QStringLiteral("Comma-separated topics in contents order."),
                                    QStringLiteral("list"));
    const QCommandLineOption generator(QStringLiteral("qhelpgenerator"),
                                       QStringLiteral("Path of qhelpgenerator."),
                                       QStringLiteral("path"));
    const QCommandLineOption work(QStringLiteral("work"),
                                  QStringLiteral("Folder for the pages; it is emptied first."),
                                  QStringLiteral("folder"));
    const QCommandLineOption output(QStringLiteral("output"),
                                    QStringLiteral("The .qch file to write."),
                                    QStringLiteral("file"));
    const QCommandLineOption depfile(QStringLiteral("depfile"),
                                     QStringLiteral("Optional dependency file for the build."),
                                     QStringLiteral("file"));
    parser.addOptions({language, source, topics, generator, work, output, depfile});
    parser.process(app);

    options.language = parser.value(language);
    options.sourceDirectory = parser.value(source);
    options.topics = parser.value(topics).split(u',', Qt::SkipEmptyParts);
    options.qhelpgenerator = parser.value(generator);
    options.workDirectory = parser.value(work);
    options.output = parser.value(output);
    options.depfile = parser.value(depfile);

    bool valid = true;
    static const QRegularExpression languagePattern(QStringLiteral("^[a-z]{2,3}$"));
    static const QRegularExpression topicPattern(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));
    if (!languagePattern.match(options.language).hasMatch()) {
        report(QStringLiteral("--language needs a code of two or three small letters"));
        valid = false;
    }
    if (options.topics.isEmpty()) {
        report(QStringLiteral("--topics needs at least one topic"));
        valid = false;
    }
    for (const QString &topic : std::as_const(options.topics)) {
        if (!topicPattern.match(topic).hasMatch() || options.topics.count(topic) > 1) {
            report(QStringLiteral("topic \"%1\" is malformed or repeated").arg(topic));
            valid = false;
        }
    }
    const QFileInfo generatorFile(options.qhelpgenerator);
    if (!options.qhelpgenerator.isEmpty() &&
        (!generatorFile.isAbsolute() || !generatorFile.isFile() || !generatorFile.isExecutable())) {
        report(QStringLiteral("--qhelpgenerator needs the absolute path of an executable file"));
        valid = false;
    }
    for (const auto &[name, value] :
         {std::pair{QStringLiteral("--source"), options.sourceDirectory},
          std::pair{QStringLiteral("--qhelpgenerator"), options.qhelpgenerator},
          std::pair{QStringLiteral("--work"), options.workDirectory},
          std::pair{QStringLiteral("--output"), options.output}}) {
        if (value.isEmpty()) {
            report(QStringLiteral("%1 is missing").arg(name));
            valid = false;
        }
    }
    return valid;
}

int run(const Options &options)
{
    const QDir source(options.sourceDirectory);
    const QDir work(options.workDirectory);
    bool failed = false;

    const QStringList markdownFiles = source.entryList({QStringLiteral("*.md")}, QDir::Files);
    for (const QString &file : markdownFiles) {
        if (!options.topics.contains(QFileInfo(file).completeBaseName())) {
            report(QStringLiteral("%1: no area of the program has this chapter")
                       .arg(source.filePath(file)));
            failed = true;
        }
    }

    QList<qpm::helpgen::Chapter> chapters;
    QStringList images;
    // The folders as well, so that a new file in them makes the build run
    // the tool again and report a chapter of no area.
    QStringList dependencies{QFileInfo(options.sourceDirectory).absoluteFilePath()};
    if (source.exists(QStringLiteral("images")))
        dependencies.append(
            QFileInfo(source.filePath(QStringLiteral("images"))).absoluteFilePath());
    for (const QString &topic : options.topics) {
        const QString path = source.filePath(topic + QStringLiteral(".md"));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            report(QStringLiteral("%1: the chapter is missing").arg(path));
            failed = true;
            continue;
        }
        dependencies.append(QFileInfo(path).absoluteFilePath());
        qpm::helpgen::Chapter chapter = qpm::helpgen::convertChapter(
            QString::fromUtf8(file.readAll()), topic, options.language, options.topics);
        for (const QString &image : std::as_const(chapter.images)) {
            const QString imagePath = source.filePath(image);
            if (!QFileInfo(imagePath).isFile())
                chapter.errors.append(QStringLiteral("image \"%1\" does not exist").arg(image));
            else if (!images.contains(image))
                images.append(image);
        }
        for (const QString &error : std::as_const(chapter.errors))
            report(QStringLiteral("%1: %2").arg(path, error));
        failed = failed || !chapter.errors.isEmpty();
        chapters.append(chapter);
    }
    if (failed)
        return EXIT_FAILURE;

    if (!prepareWorkDirectory(options.workDirectory))
        return EXIT_FAILURE;
    QByteArray topicList;
    for (const qpm::helpgen::Chapter &chapter : std::as_const(chapters)) {
        if (!writeFile(work.filePath(chapter.topic + QStringLiteral(".html")),
                       chapter.html.toUtf8()))
            return EXIT_FAILURE;
        topicList += (chapter.topic + u'\t' + chapter.title + u'\n').toUtf8();
    }
    if (!writeFile(work.filePath(QString(topicsFile)), topicList))
        return EXIT_FAILURE;
    for (const QString &image : std::as_const(images)) {
        const QString from = source.filePath(image);
        if (!QFile::copy(from, work.filePath(image))) {
            report(QStringLiteral("%1: cannot copy into the work folder").arg(from));
            return EXIT_FAILURE;
        }
        dependencies.append(QFileInfo(from).absoluteFilePath());
    }
    const QString project =
        work.filePath(QStringLiteral("qphonemanager_%1.qhp").arg(options.language));
    if (!writeFile(project, helpProject(options, chapters, images)))
        return EXIT_FAILURE;
    if (!runQhelpgenerator(options, project))
        return EXIT_FAILURE;

    if (!options.depfile.isEmpty()) {
        QStringList escaped;
        for (const QString &dependency : std::as_const(dependencies))
            escaped.append(escapedForDepfile(dependency));
        const QString rule = escapedForDepfile(QFileInfo(options.output).absoluteFilePath()) +
                             QStringLiteral(": ") + escaped.join(u' ') + u'\n';
        if (!writeFile(options.depfile, rule.toUtf8()))
            return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char *argv[])
{
    // QTextDocument needs a QGuiApplication; the build has no display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("helpgen"));

    Options options;
    if (!parseOptions(app, options))
        return EXIT_FAILURE;
    return run(options);
}
