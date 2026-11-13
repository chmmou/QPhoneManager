#include "ui/help/HelpTopicModel.h"

namespace qpm::ui {

HelpTopicModel::HelpTopicModel(QObject *parent) : QAbstractListModel(parent) {}

int HelpTopicModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_topics.size());
}

QVariant HelpTopicModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Topic &topic = m_topics.at(index.row());
    switch (role) {
    case TopicRole:
        return topic.topic;
    case Qt::DisplayRole:
    case TitleRole:
        return topic.title;
    default:
        return {};
    }
}

QHash<int, QByteArray> HelpTopicModel::roleNames() const
{
    return {
        {TopicRole, QByteArrayLiteral("topic")},
        {TitleRole, QByteArrayLiteral("title")},
    };
}

void HelpTopicModel::setTopics(const QList<Topic> &topics)
{
    beginResetModel();
    m_topics = topics;
    endResetModel();
}

int HelpTopicModel::rowOf(const QString &topic) const
{
    for (qsizetype row = 0; row < m_topics.size(); ++row) {
        if (m_topics.at(row).topic == topic)
            return int(row);
    }
    return -1;
}

QString HelpTopicModel::topicAt(int row) const
{
    return row >= 0 && row < m_topics.size() ? m_topics.at(row).topic : QString();
}

} // namespace qpm::ui
