#include "HandbookChapter.h"

#include <QFont>
#include <QList>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QUrl>

namespace qpm::helpgen {

namespace {

const QRegularExpression &chapterLinkPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^([a-z0-9]+(?:-[a-z0-9]+)*)\\.md(#[A-Za-z0-9_-]+)?$"));
    return pattern;
}

const QRegularExpression &imagePattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^images/[A-Za-z0-9_-][A-Za-z0-9._-]*\\.(png|jpg|jpeg)$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

bool isOrderedList(const QTextList *list)
{
    // ListDecimal and the styles below it are numbered; the bullet styles lie above it.
    return list->format().style() <= QTextListFormat::ListDecimal;
}

// Walks the document that QTextDocument made of the Markdown and writes plain
// HTML without the inline styles of QTextDocument::toHtml(), so that the
// viewer decides the colours.
class HtmlWriter
{
public:
    HtmlWriter(Chapter &chapter, const QStringList &topics) : m_chapter(chapter), m_topics(topics)
    {
    }

    void writeFrame(QTextFrame *frame)
    {
        for (QTextFrame::iterator it = frame->begin(); !it.atEnd(); ++it) {
            if (QTextFrame *child = it.currentFrame()) {
                closeOpenBlocks();
                if (auto *table = qobject_cast<QTextTable *>(child))
                    writeTable(table);
                else
                    writeFrame(child);
            } else {
                writeBlock(it.currentBlock());
            }
        }
        closeOpenBlocks();
    }

    [[nodiscard]] QString body() const { return m_body; }
    [[nodiscard]] int topHeadings() const { return m_topHeadings; }

private:
    void writeBlock(const QTextBlock &block)
    {
        const QTextBlockFormat format = block.blockFormat();
        const bool code = format.hasProperty(QTextFormat::BlockCodeLanguage) ||
                          format.hasProperty(QTextFormat::BlockCodeFence);
        if (!code)
            flushCode();

        if (QTextList *list = block.textList()) {
            flushCode();
            openItem(list, block);
            m_body += QStringLiteral("<li>") + inlineHtml(block);
            m_lists.last().itemOpen = true;
            return;
        }
        // The Markdown importer puts only the first paragraph of an item into
        // the list; the blocks after it carry the indent of their list instead.
        closeListsDeeperThan(format.indent());
        if (code) {
            m_codeLines.append(block.text().toHtmlEscaped());
            m_inCode = true;
            return;
        }

        const int level = format.headingLevel();
        if (level == 1) {
            ++m_topHeadings;
            m_chapter.title = block.text().trimmed();
            m_body += QStringLiteral("<h1 id=\"%1\">%2</h1>\n")
                          .arg(m_chapter.topic.toHtmlEscaped(), inlineHtml(block, Heading::Yes));
            return;
        }
        if (level > 1) {
            const int shown = qMin(level, 6);
            m_body += QStringLiteral("<h%1>%2</h%1>\n")
                          .arg(QString::number(shown), inlineHtml(block, Heading::Yes));
            return;
        }
        if (format.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
            m_body += QStringLiteral("<hr/>\n");
            return;
        }
        const QString content = inlineHtml(block);
        if (content.trimmed().isEmpty())
            return;
        if (format.intProperty(QTextFormat::BlockQuoteLevel) > 0)
            m_body += QStringLiteral("<blockquote><p>%1</p></blockquote>\n").arg(content);
        else
            m_body += QStringLiteral("<p>%1</p>\n").arg(content);
    }

    void writeTable(QTextTable *table)
    {
        const int headerRows = table->format().headerRowCount();
        m_body += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n");
        for (int row = 0; row < table->rows(); ++row) {
            m_body += QStringLiteral("<tr>");
            for (int column = 0; column < table->columns(); ++column) {
                const QTextTableCell cell = table->cellAt(row, column);
                // A cell that spans several columns or rows is written once.
                if (cell.row() != row || cell.column() != column)
                    continue;
                const QString tag = row < headerRows ? QStringLiteral("th") : QStringLiteral("td");
                QStringList lines;
                for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it) {
                    if (it.currentFrame())
                        continue;
                    lines.append(inlineHtml(it.currentBlock()));
                }
                m_body +=
                    QStringLiteral("<%1>%2</%1>").arg(tag, lines.join(QStringLiteral("<br/>")));
            }
            m_body += QStringLiteral("</tr>\n");
        }
        m_body += QStringLiteral("</table>\n");
    }

    // The Markdown importer sets headings in bold; the heading tag says that already.
    enum class Heading { No, Yes };

    [[nodiscard]] QString inlineHtml(const QTextBlock &block, Heading heading = Heading::No)
    {
        QString html;
        QString openHref;
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            const QTextCharFormat format = fragment.charFormat();
            const QString href = format.isAnchor() ? format.anchorHref() : QString();
            if (href != openHref) {
                if (!openHref.isEmpty())
                    html += QStringLiteral("</a>");
                if (!href.isEmpty())
                    html += QStringLiteral("<a href=\"%1\">").arg(linkTarget(href).toHtmlEscaped());
                openHref = href;
            }
            if (format.isImageFormat()) {
                const QTextImageFormat image = format.toImageFormat();
                // Two equal images in a row share one fragment.
                for (int i = 0; i < fragment.length(); ++i)
                    html += imageHtml(image);
                continue;
            }
            QString text = fragment.text().toHtmlEscaped();
            text.replace(QChar::LineSeparator, QStringLiteral("<br/>"));
            if (format.fontFixedPitch())
                text = QStringLiteral("<code>%1</code>").arg(text);
            if (heading == Heading::No && format.fontWeight() >= QFont::DemiBold)
                text = QStringLiteral("<b>%1</b>").arg(text);
            if (format.fontItalic())
                text = QStringLiteral("<i>%1</i>").arg(text);
            if (format.fontStrikeOut())
                text = QStringLiteral("<s>%1</s>").arg(text);
            html += text;
        }
        if (!openHref.isEmpty())
            html += QStringLiteral("</a>");
        return html;
    }

    [[nodiscard]] QString imageHtml(const QTextImageFormat &image)
    {
        const QString source = image.name();
        if (!imagePattern().match(source).hasMatch()) {
            m_chapter.errors.append(
                QStringLiteral("image \"%1\" is not of the form images/<name>.png").arg(source));
        } else if (!m_chapter.images.contains(source)) {
            m_chapter.images.append(source);
        }
        return QStringLiteral("<img src=\"%1\" alt=\"%2\"/>")
            .arg(source.toHtmlEscaped(),
                 image.stringProperty(QTextFormat::ImageAltText).toHtmlEscaped());
    }

    [[nodiscard]] QString linkTarget(const QString &href)
    {
        if (href.startsWith(QLatin1StringView("https://"))) {
            const QUrl url(href, QUrl::StrictMode);
            if (url.isValid() && !url.host().isEmpty())
                return href;
        }
        const QRegularExpressionMatch match = chapterLinkPattern().match(href);
        if (match.hasMatch() && m_topics.contains(match.captured(1)))
            return match.captured(1) + QStringLiteral(".html") + match.captured(2);
        m_chapter.errors.append(
            QStringLiteral(
                "link \"%1\" leads neither to a chapter (<topic>.md) nor to an https address")
                .arg(href));
        return href;
    }

    // Closes the item before `block` in its list, or opens the list; a list
    // that has to be opened at another number than 1 says so.
    void openItem(QTextList *list, const QTextBlock &block)
    {
        qsizetype open = m_lists.size();
        while (open > 0 && m_lists.at(open - 1).list != list)
            --open;
        if (open > 0) {
            while (m_lists.size() > open)
                closeInnermostList();
            closeItem();
            return;
        }
        const int indent = list->format().indent();
        while (!m_lists.isEmpty() && m_lists.constLast().indent >= indent)
            closeInnermostList();
        if (isOrderedList(list)) {
            const int number = list->format().start() + list->itemNumber(block);
            m_body += number == 1 ? QStringLiteral("<ol>\n")
                                  : QStringLiteral("<ol start=\"%1\">\n").arg(number);
        } else {
            m_body += QStringLiteral("<ul>\n");
        }
        m_lists.append({list, indent, false});
    }

    void closeItem()
    {
        if (m_lists.isEmpty() || !m_lists.constLast().itemOpen)
            return;
        m_body += QStringLiteral("</li>\n");
        m_lists.last().itemOpen = false;
    }

    void closeInnermostList()
    {
        closeItem();
        const OpenList list = m_lists.takeLast();
        m_body += isOrderedList(list.list) ? QStringLiteral("</ol>\n") : QStringLiteral("</ul>\n");
    }

    void closeListsDeeperThan(int indent)
    {
        while (!m_lists.isEmpty() && m_lists.constLast().indent > indent) {
            flushCode();
            closeInnermostList();
        }
    }

    void closeLists() { closeListsDeeperThan(-1); }

    void flushCode()
    {
        if (!m_inCode)
            return;
        m_body += QStringLiteral("<pre>%1</pre>\n").arg(m_codeLines.join(u'\n'));
        m_codeLines.clear();
        m_inCode = false;
    }

    void closeOpenBlocks()
    {
        flushCode();
        closeLists();
    }

    Chapter &m_chapter;
    const QStringList &m_topics;
    QString m_body;
    struct OpenList
    {
        const QTextList *list = nullptr;
        int indent = 0;
        bool itemOpen = false;
    };

    QList<OpenList> m_lists;
    QStringList m_codeLines;
    bool m_inCode = false;
    int m_topHeadings = 0;
};

} // namespace

Chapter convertChapter(const QString &markdown, const QString &topic, const QString &language,
                       const QStringList &topics)
{
    Chapter chapter;
    chapter.topic = topic;
    QTextDocument document;
    document.setMarkdown(markdown,
                         QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub |
                                                         QTextDocument::MarkdownNoHTML));
    HtmlWriter writer(chapter, topics);
    writer.writeFrame(document.rootFrame());
    if (writer.topHeadings() != 1) {
        chapter.errors.prepend(
            QStringLiteral("has %1 top-level headings (# ...); a chapter needs exactly one")
                .arg(writer.topHeadings()));
    }
    chapter.html = QStringLiteral("<!DOCTYPE html>\n<html lang=\"%1\">\n<head>\n"
                                  "<meta charset=\"utf-8\"/>\n<title>%2</title>\n</head>\n"
                                  "<body>\n%3</body>\n</html>\n")
                       .arg(language.toHtmlEscaped(), chapter.title.toHtmlEscaped(), writer.body());
    return chapter;
}

} // namespace qpm::helpgen
