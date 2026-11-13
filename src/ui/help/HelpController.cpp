#include "ui/help/HelpController.h"

#include "core/Logging.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHelpEngineCore>
#include <QHelpLink>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QtConcurrent/QtConcurrentRun>

namespace qpm::ui {

namespace {

constexpr QLatin1StringView namespacePrefix("org.qphonemanager.help.");
constexpr QLatin1StringView imageFolder("images/");
constexpr qsizetype maxHistory = 100;

const QRegularExpression &languagePattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z]{2,3}$"));
    return pattern;
}

const QRegularExpression &chapterLinkPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^([a-z0-9]+(?:-[a-z0-9]+)*)\\.html(?:#[A-Za-z0-9_-]+)?$"));
    return pattern;
}

const QRegularExpression &imagePathPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^/doc/(images/[A-Za-z0-9_-][A-Za-z0-9._-]*\\.(?:png|jpg|jpeg))$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

QString languageOfNamespace(const QString &name)
{
    if (!name.startsWith(namespacePrefix))
        return {};
    const QString language = name.mid(namespacePrefix.size());
    return languagePattern().match(language).hasMatch() ? language : QString();
}

// Rich text loads images by address, and the help engine is not one; the
// images are therefore written next to the collection.
void extractImages(QHelpEngineCore &engine, const QString &language, const QString &helpNamespace,
                   const QDir &directory)
{
    const QList<QUrl> files = engine.files(helpNamespace, QString());
    const QDir folder(directory.filePath(language));
    for (const QUrl &url : files) {
        const QRegularExpressionMatch match = imagePathPattern().match(url.path());
        if (!match.hasMatch())
            continue;
        QSaveFile image(folder.filePath(match.captured(1)));
        if (!folder.mkpath(QString(imageFolder)) || !image.open(QIODevice::WriteOnly) ||
            image.write(engine.fileData(url)) < 0 || !image.commit()) {
            qCWarning(core::lcUi) << "A help image cannot be written:" << url.path()
                                  << image.errorString();
        }
    }
}

// Runs on a worker thread and shares nothing with the controller: copies the
// help files next to the collection, registers them and writes their images
// out. Its engine is made and destroyed on that thread.
QHash<QString, QString> setUpCollection(const QStringList &helpFiles, const QString &directory)
{
    QHash<QString, QString> namespaces;
    const QDir folder(directory);
    QHelpEngineCore engine(folder.filePath(QStringLiteral("collection.qhc")));
    // The collection is written once here, to register the help files.
    engine.setReadOnly(false);
    if (!engine.setupData()) {
        qCWarning(core::lcUi) << "The help collection cannot be set up:" << engine.error();
        return namespaces;
    }
    for (qsizetype i = 0; i < helpFiles.size(); ++i) {
        const QString &file = helpFiles.at(i);
        // A help file must be a real file for the help engine, and a copy out
        // of the resources is read-only.
        const QString copy = folder.filePath(QStringLiteral("help-%1.qch").arg(i));
        if (!QFile::copy(file, copy) ||
            !QFile::setPermissions(copy, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            qCWarning(core::lcUi) << "The help file cannot be copied:" << file;
            continue;
        }
        const QString helpNamespace = QHelpEngineCore::namespaceName(copy);
        const QString language = languageOfNamespace(helpNamespace);
        if (language.isEmpty() || namespaces.contains(language) ||
            !engine.registerDocumentation(copy)) {
            qCWarning(core::lcUi) << "The help file is left out:" << file << helpNamespace
                                  << engine.error();
            continue;
        }
        namespaces.insert(language, helpNamespace);
    }
    for (auto it = namespaces.cbegin(); it != namespaces.cend(); ++it)
        extractImages(engine, it.key(), it.value(), folder);
    return namespaces;
}

QUrl pageUrl(const QString &helpNamespace, const QString &file)
{
    QUrl url;
    url.setScheme(QStringLiteral("qthelp"));
    url.setHost(helpNamespace);
    url.setPath(QStringLiteral("/doc/") + file);
    return url;
}

} // namespace

HelpController::HelpController(const QStringList &helpFiles, QObject *parent)
    : QObject(parent), m_helpFiles(helpFiles), m_toc(new HelpTopicModel(this)),
      m_opener([](const QUrl &url) { return QDesktopServices::openUrl(url); })
{
    m_linkColor = QColor(0x00, 0x6A, 0x6A);
    const QString system = QLocale().name().section(u'_', 0, 0).toLower();
    m_language = languagePattern().match(system).hasMatch() ? system : QStringLiteral("en");
}

HelpController::~HelpController()
{
    // The worker writes into the temporary folder, which goes with this object.
    m_setupRun.waitForFinished();
}

HelpController *HelpController::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    // The engine takes ownership of a singleton it gets from this factory.
    return new HelpController(bundledHelpFiles());
}

QStringList HelpController::bundledHelpFiles()
{
    return {QStringLiteral(":/help/qphonemanager_de.qch"),
            QStringLiteral(":/help/qphonemanager_en.qch")};
}

void HelpController::setLanguage(const QString &language)
{
    if (language == m_language || !languagePattern().match(language).hasMatch())
        return;
    m_language = language;
    Q_EMIT languageChanged();
    if (m_setup == Setup::Ready)
        updateToc();
    if (!m_topic.isEmpty())
        show(m_topic);
}

QString HelpController::currentHtml() const
{
    if (m_html.isEmpty())
        return {};
    QString html = m_html;
    // helpgen writes every image as src="images/<name>"; the files lie in the
    // folder of the page's language, which does not move while the object lives.
    const QString folder =
        QUrl::fromLocalFile(m_directory.filePath(m_pageLanguage) + u'/').toString();
    html.replace(QStringLiteral("src=\"") + imageFolder,
                 QStringLiteral("src=\"") + folder + imageFolder);
    const qsizetype head = html.indexOf(QLatin1StringView("</head>"));
    // max-width shrinks a wide image to the width of the text and keeps its
    // proportions; a narrower image keeps its own size.
    html.insert(head >= 0 ? head : 0,
                QStringLiteral("<style>a { color: %1; } img { max-width: 100%; }</style>")
                    .arg(m_linkColor.name()));
    return html;
}

void HelpController::setLinkColor(const QColor &color)
{
    if (color == m_linkColor || !color.isValid())
        return;
    m_linkColor = color;
    Q_EMIT linkColorChanged();
    Q_EMIT htmlChanged();
}

void HelpController::setUrlOpener(UrlOpener opener)
{
    m_opener = std::move(opener);
}

bool HelpController::openTopic(const QString &topic)
{
    if (topic.isEmpty())
        return false;
    if (topic == m_topic)
        return !m_html.isEmpty() || isBusy();
    const QString previous = m_topic;
    const bool found = show(topic);
    if (!previous.isEmpty()) {
        m_back.append(previous);
        if (m_back.size() > maxHistory)
            m_back.removeFirst();
        m_forward.clear();
        Q_EMIT historyChanged();
    }
    return found;
}

void HelpController::back()
{
    if (m_back.isEmpty())
        return;
    m_forward.append(m_topic);
    show(m_back.takeLast());
    Q_EMIT historyChanged();
}

void HelpController::forward()
{
    if (m_forward.isEmpty())
        return;
    m_back.append(m_topic);
    show(m_forward.takeLast());
    Q_EMIT historyChanged();
}

bool HelpController::openLink(const QString &link)
{
    const QUrl url(link, QUrl::StrictMode);
    if (url.scheme() == QLatin1StringView("https") && url.isValid() && !url.host().isEmpty()) {
        if (m_opener(url))
            return true;
        qCWarning(core::lcUi) << "The desktop did not open the help link" << url.host();
        setNotice(tr("The browser did not open. Copy the address and open it yourself: %1")
                      .arg(url.toString()));
        return false;
    }
    const QRegularExpressionMatch match = chapterLinkPattern().match(link);
    if (match.hasMatch())
        return openTopic(match.captured(1));
    // The help files are built from the handbook, whose links helpgen checks.
    qCWarning(core::lcUi) << "Help link left alone:" << link;
    return false;
}

QVariantMap HelpController::nextLink(int position, bool forward) const
{
    const auto toMap = [](const Link &link) {
        return QVariantMap{{QStringLiteral("start"), link.start},
                           {QStringLiteral("end"), link.end},
                           {QStringLiteral("href"), link.href}};
    };
    if (forward) {
        for (const Link &link : m_links) {
            if (link.start >= position)
                return toMap(link);
        }
        return {};
    }
    for (auto it = m_links.crbegin(); it != m_links.crend(); ++it) {
        if (it->end <= position)
            return toMap(*it);
    }
    return {};
}

QString HelpController::linkAt(int position) const
{
    for (const Link &link : m_links) {
        if (position >= link.start && position < link.end)
            return link.href;
    }
    return {};
}

void HelpController::startSetUp()
{
    if (!m_directory.isValid()) {
        qCWarning(core::lcUi) << "No temporary folder for the help:" << m_directory.errorString();
        m_setup = Setup::Failed;
        return;
    }
    m_setup = Setup::Running;
    m_setupRun = QtConcurrent::run(
        [helpFiles = m_helpFiles, directory = m_directory.path()]() -> Namespaces {
            // A failure must not escape the worker; the setup then ends as failed.
            try {
                return setUpCollection(helpFiles, directory);
            } catch (...) {
                qCWarning(core::lcUi) << "Setting up the help stopped unexpectedly";
                return {};
            }
        });
    // Without a result the continuation would not run and the help would stay busy.
    m_setupRun
        .onFailed(this,
                  []() {
                      qCWarning(core::lcUi) << "Setting up the help ended without a result";
                      return Namespaces();
                  })
        .then(this, [this](const Namespaces &namespaces) { finishSetUp(namespaces); });
    Q_EMIT busyChanged();
}

void HelpController::finishSetUp(const Namespaces &namespaces)
{
    m_setup = Setup::Failed;
    if (namespaces.isEmpty()) {
        qCWarning(core::lcUi) << "No help file could be registered";
    } else {
        auto engine = std::make_unique<QHelpEngineCore>(
            m_directory.filePath(QStringLiteral("collection.qhc")));
        if (engine->setupData()) {
            m_engine = std::move(engine);
            m_namespaces = namespaces;
            m_setup = Setup::Ready;
            updateToc();
        } else {
            qCWarning(core::lcUi) << "The help collection cannot be opened:" << engine->error();
        }
    }
    if (!m_topic.isEmpty())
        show(m_topic);
    Q_EMIT busyChanged();
}

void HelpController::setNotice(const QString &notice)
{
    if (notice == m_notice)
        return;
    m_notice = notice;
    Q_EMIT noticeChanged();
}

QStringList HelpController::languagesInOrder() const
{
    QStringList languages{m_language};
    for (const QString &fallback : {QStringLiteral("en"), QStringLiteral("de")}) {
        if (!languages.contains(fallback))
            languages.append(fallback);
    }
    return languages;
}

QList<HelpTopicModel::Topic> HelpController::topicsOf(const QString &language) const
{
    const QString helpNamespace = m_namespaces.value(language);
    if (helpNamespace.isEmpty())
        return {};
    const QString text =
        QString::fromUtf8(m_engine->fileData(pageUrl(helpNamespace, QStringLiteral("topics.txt"))));
    QList<HelpTopicModel::Topic> topics;
    const QStringList lines = text.split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const qsizetype tab = line.indexOf(u'\t');
        if (tab > 0)
            topics.append({line.left(tab), line.mid(tab + 1)});
    }
    return topics;
}

void HelpController::updateToc()
{
    QList<HelpTopicModel::Topic> merged;
    QStringList seen;
    const QStringList languages = languagesInOrder();
    for (const QString &language : languages) {
        const QList<HelpTopicModel::Topic> topics = topicsOf(language);
        for (const HelpTopicModel::Topic &topic : topics) {
            if (seen.contains(topic.topic))
                continue;
            seen.append(topic.topic);
            merged.append(topic);
        }
    }
    m_toc->setTopics(merged);
}

bool HelpController::show(const QString &topic)
{
    m_topic = topic;
    m_title.clear();
    m_pageLanguage.clear();
    m_html.clear();
    m_message.clear();
    setNotice(QString());
    if (m_setup == Setup::NotStarted)
        startSetUp();
    if (m_setup == Setup::Ready) {
        const QList<QHelpLink> links = m_engine->documentsForIdentifier(topic, QString());
        const QStringList languages = languagesInOrder();
        for (const QString &language : languages) {
            const QString helpNamespace = m_namespaces.value(language);
            for (const QHelpLink &link : links) {
                if (helpNamespace.isEmpty() || link.url.host() != helpNamespace)
                    continue;
                const QByteArray page = m_engine->fileData(link.url.adjusted(QUrl::RemoveFragment));
                if (page.isEmpty())
                    continue;
                m_html = QString::fromUtf8(page);
                m_pageLanguage = language;
                break;
            }
            if (!m_html.isEmpty())
                break;
        }
        if (m_html.isEmpty())
            m_message = tr(
                "The manual has no chapter on this yet. Choose another chapter from the contents.");
    } else if (m_setup == Setup::Failed) {
        m_message = tr("The help cannot be loaded. Start QPhoneManager again. If that does not "
                       "help, install it again.");
    }
    findLinks();
    Q_EMIT pageChanged();
    Q_EMIT htmlChanged();
    return !m_html.isEmpty() || isBusy();
}

void HelpController::findLinks()
{
    m_links.clear();
    if (m_html.isEmpty())
        return;
    QTextDocument document;
    document.setHtml(currentHtml());
    m_title = document.metaInformation(QTextDocument::DocumentTitle);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            if (!fragment.isValid() || !format.isAnchor() || format.anchorHref().isEmpty())
                continue;
            const int start = fragment.position();
            const int end = start + fragment.length();
            if (!m_links.isEmpty() && m_links.constLast().end == start &&
                m_links.constLast().href == format.anchorHref()) {
                m_links.last().end = end;
            } else {
                m_links.append({start, end, format.anchorHref()});
            }
        }
    }
}

} // namespace qpm::ui
