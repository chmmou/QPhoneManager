#include "ui/messages/ScheduledListModel.h"

#include "ui/PhoneNumberText.h"

#include <QLocale>

namespace qpm::ui {

namespace {

using core::codec::MessageRecipient;
using core::store::StoredMessage;

} // namespace

ScheduledListModel::ScheduledListModel(QObject *parent) : QAbstractListModel(parent) {}

int ScheduledListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_messages.size());
}

QVariant ScheduledListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const StoredMessage &message = m_messages.at(index.row());
    int sent = 0;
    int failed = 0;
    for (const MessageRecipient &recipient : message.recipients) {
        if (recipient.status == MessageRecipient::Status::Sent)
            ++sent;
        else if (recipient.status == MessageRecipient::Status::Failed)
            ++failed;
    }
    const int total = int(message.recipients.size());
    switch (role) {
    case LuidRole:
        return int(message.luid);
    case Qt::DisplayRole:
    case RecipientsTextRole:
        return recipientsText(message);
    case BodyRole:
        return message.body;
    case WhenTextRole: {
        if (!message.date.isValid())
            return QString();
        const QLocale locale;
        const QDateTime local = message.date.toLocalTime();
        return locale.toString(local.date(), QLocale::LongFormat) + u' ' +
               locale.toString(local.time(), QLocale::ShortFormat);
    }
    case StatusTextRole:
        if (failed > 0)
            return tr("Not sent");
        if (sent > 0 && sent < total)
            return tr("Sent to %1 of %2").arg(sent).arg(total);
        if (sent > 0)
            return tr("Sent");
        return tr("Waiting");
    case FailedRole:
        return failed > 0;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> ScheduledListModel::roleNames() const
{
    return {
        {LuidRole, QByteArrayLiteral("luid")},
        {RecipientsTextRole, QByteArrayLiteral("recipientsText")},
        {BodyRole, QByteArrayLiteral("body")},
        {WhenTextRole, QByteArrayLiteral("whenText")},
        {StatusTextRole, QByteArrayLiteral("statusText")},
        {FailedRole, QByteArrayLiteral("failed")},
    };
}

void ScheduledListModel::setMessages(QList<StoredMessage> messages)
{
    beginResetModel();
    m_messages = std::move(messages);
    endResetModel();
    Q_EMIT countChanged();
}

void ScheduledListModel::setNameLookup(NameLookup lookup)
{
    m_names = std::move(lookup);
    if (!m_messages.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_messages.size()) - 1), {RecipientsTextRole});
}

std::optional<StoredMessage> ScheduledListModel::message(quint32 luid) const
{
    for (const StoredMessage &message : m_messages) {
        if (message.luid == luid)
            return message;
    }
    return std::nullopt;
}

int ScheduledListModel::luidAt(int row) const
{
    return row >= 0 && row < m_messages.size() ? int(m_messages.at(row).luid) : 0;
}

QString ScheduledListModel::recipientsText(const StoredMessage &message) const
{
    QStringList names;
    for (const MessageRecipient &recipient : message.recipients) {
        QString name = m_names ? m_names(recipient.number, recipient.name) : QString();
        if (name.isEmpty())
            name = recipient.name.trimmed();
        names.append(name.isEmpty() ? PhoneNumberText::display(recipient.number) : name);
    }
    return names.join(QLatin1StringView(", "));
}

} // namespace qpm::ui
