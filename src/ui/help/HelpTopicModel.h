#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The chapters of the help in the order of its table of contents, each with
 * its topic and its title in the language it is shown in. Filled by
 * HelpController. Lives in the main thread.
 */
class HelpTopicModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(HelpTopicModel)
    QML_UNCREATABLE("Provided by HelpController")

public:
    enum Role {
        TopicRole = Qt::UserRole + 1,
        TitleRole,
    };
    Q_ENUM(Role)

    struct Topic
    {
        QString topic;
        QString title;
    };

    explicit HelpTopicModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setTopics(const QList<Topic> &topics);
    [[nodiscard]] QList<Topic> topics() const { return m_topics; }
    /// The row of `topic`, or -1 when the help has no such chapter.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &topic) const;
    /// The topic in `row`, or empty for a row that does not exist.
    [[nodiscard]] Q_INVOKABLE QString topicAt(int row) const;

private:
    QList<Topic> m_topics;
};

} // namespace qpm::ui
