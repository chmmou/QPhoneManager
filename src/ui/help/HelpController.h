#pragma once

#include "ui/help/HelpTopicModel.h"

#include <QColor>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>

class QHelpEngineCore;
class QJSEngine;
class QQmlEngine;

namespace qpm::ui {

/**
 * Shows the chapters of the help that the program carries as compressed help
 * files, one per language. A chapter is found by its topic, which is the key
 * of an area of the program, in the language set in `language`; where that
 * language lacks the chapter, English and then German stand in.
 *
 * The help collection lives in a temporary folder that goes with the object.
 * The first openTopic() sets it up on a worker thread; `busy` is true until
 * then, and the chapter asked for shows once the collection is ready. No call
 * blocks for longer than reading one page from the help file. The destructor
 * waits for a setup that still runs.
 *
 * A link in a chapter leads to another chapter or, only for https, to the
 * desktop browser. Lives in the main thread.
 */
class HelpController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(HelpController)
    QML_SINGLETON

    /**
     * The language the chapters are wanted in, as a code of two or three
     * small letters; other values are ignored. Starts with the language of
     * the program.
     */
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    /// The topic of the page shown; empty before the first openTopic().
    Q_PROPERTY(QString currentTopic READ currentTopic NOTIFY pageChanged FINAL)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY pageChanged FINAL)
    /// The language of the chapter shown, which differs from `language` after a fallback.
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY pageChanged FINAL)
    /**
     * The chapter as rich text; empty while `busy` and when the help has no
     * chapter for the topic. Changes with the page and with `linkColor`. An
     * image in it is never wider than the text width of the document that
     * shows it, and is never enlarged.
     */
    Q_PROPERTY(QString currentHtml READ currentHtml NOTIFY htmlChanged FINAL)
    /// What to tell the user when `currentHtml` is empty and the help is not busy.
    Q_PROPERTY(QString message READ message NOTIFY pageChanged FINAL)
    /**
     * A message about the page shown, such as a web address the desktop did
     * not open; empty otherwise. Cleared with the next page.
     */
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged FINAL)
    /// The help collection is being set up.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged FINAL)
    /// The colour of links in `currentHtml`.
    Q_PROPERTY(QColor linkColor READ linkColor WRITE setLinkColor NOTIFY linkColorChanged FINAL)
    Q_PROPERTY(qpm::ui::HelpTopicModel *toc READ toc CONSTANT FINAL)

public:
    /// Opens an https address outside the program; returns whether that worked.
    using UrlOpener = std::function<bool(const QUrl &)>;

    /**
     * `helpFiles` are the compressed help files to show, as file or resource
     * paths. Each carries the namespace "org.qphonemanager.help.<language>";
     * a file that cannot be read is left out and logged.
     */
    explicit HelpController(const QStringList &helpFiles, QObject *parent = nullptr);
    ~HelpController() override;

    /// Factory for the QML engine; uses bundledHelpFiles().
    static HelpController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    /// The help files embedded in the program, one per language.
    [[nodiscard]] static QStringList bundledHelpFiles();

    [[nodiscard]] QString language() const { return m_language; }
    void setLanguage(const QString &language);
    [[nodiscard]] QString currentTopic() const { return m_topic; }
    [[nodiscard]] QString currentTitle() const { return m_title; }
    [[nodiscard]] QString currentLanguage() const { return m_pageLanguage; }
    [[nodiscard]] QString currentHtml() const;
    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] QString notice() const { return m_notice; }
    [[nodiscard]] bool isBusy() const { return m_setup == Setup::Running; }
    [[nodiscard]] bool canGoBack() const { return !m_back.isEmpty(); }
    [[nodiscard]] bool canGoForward() const { return !m_forward.isEmpty(); }
    [[nodiscard]] QColor linkColor() const { return m_linkColor; }
    void setLinkColor(const QColor &color);
    [[nodiscard]] HelpTopicModel *toc() const { return m_toc; }

    /// Replaces how https addresses are opened; the default asks the desktop.
    void setUrlOpener(UrlOpener opener);

    /**
     * Shows the chapter of `topic` and keeps the page shown before for back().
     * Returns false when no language has the chapter or the help cannot be
     * loaded; the page then shows `message` instead. While `busy`, returns
     * true and shows the chapter, or the message, once the help is ready.
     */
    Q_INVOKABLE bool openTopic(const QString &topic);
    /// Shows the page before; does nothing at the start of the history.
    Q_INVOKABLE void back();
    /// Shows the page that back() left; does nothing at the end of the history.
    Q_INVOKABLE void forward();

    /**
     * Follows a link of the page: "<topic>.html" opens that chapter, an https
     * address goes to the desktop browser. When the desktop does not open the
     * address, `notice` tells the user to open it by hand and false is
     * returned. Returns false for any other link, which is left alone.
     */
    Q_INVOKABLE bool openLink(const QString &link);

    /**
     * The first link of the page that starts at or after `position`, or with
     * `forward` false the last one that ends at or before it, as a map with
     * "start", "end" and "href". Positions count characters of the page as
     * rich text. An empty map when there is none.
     */
    [[nodiscard]] Q_INVOKABLE QVariantMap nextLink(int position, bool forward) const;
    /// The target of the link that covers `position`, or empty.
    [[nodiscard]] Q_INVOKABLE QString linkAt(int position) const;

Q_SIGNALS:
    void languageChanged();
    /// Another page is shown, or the same topic in another language.
    void pageChanged();
    void htmlChanged();
    void noticeChanged();
    void busyChanged();
    void linkColorChanged();
    void historyChanged();

private:
    struct Link
    {
        int start = 0;
        int end = 0;
        QString href;
    };

    enum class Setup { NotStarted, Running, Ready, Failed };

    /// Language code to the namespace of its help file; empty when no file was registered.
    using Namespaces = QHash<QString, QString>;

    void startSetUp();
    void finishSetUp(const Namespaces &namespaces);
    void setNotice(const QString &notice);
    void updateToc();
    [[nodiscard]] QStringList languagesInOrder() const;
    [[nodiscard]] QList<HelpTopicModel::Topic> topicsOf(const QString &language) const;
    bool show(const QString &topic);
    void findLinks();

    QStringList m_helpFiles;
    QTemporaryDir m_directory;
    std::unique_ptr<QHelpEngineCore> m_engine;
    Setup m_setup = Setup::NotStarted;
    QFuture<Namespaces> m_setupRun;
    Namespaces m_namespaces;
    HelpTopicModel *m_toc = nullptr;
    UrlOpener m_opener;

    QString m_language;
    QString m_topic;
    QString m_title;
    QString m_pageLanguage;
    QString m_html;
    QString m_message;
    QString m_notice;
    QColor m_linkColor;
    QList<Link> m_links;
    QStringList m_back;
    QStringList m_forward;
};

} // namespace qpm::ui
