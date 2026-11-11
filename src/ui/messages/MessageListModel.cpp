#include "ui/messages/MessageListModel.h"

#include "ui/calls/CallListModel.h"

#include <QLocale>
#include <QVariantMap>

namespace qpm::ui {

namespace {

using core::codec::MessageBox;
using core::codec::MessageKind;
using core::store::StoredAttachment;
using core::store::StoredMessage;

QVariantList attachmentList(const StoredMessage &message)
{
    QVariantList list;
    for (const StoredAttachment &attachment : message.attachments) {
        const bool picture =
            attachment.mimeType.startsWith(QLatin1StringView("image/"), Qt::CaseInsensitive) &&
            !attachment.blob.isEmpty();
        list.append(QVariantMap{
            {QStringLiteral("position"), attachment.position},
            {QStringLiteral("name"),
             attachment.name.isEmpty() ? attachment.mimeType : attachment.name},
            {QStringLiteral("mimeType"), attachment.mimeType},
            {QStringLiteral("sizeText"), MessageListModel::sizeText(attachment.size)},
            {QStringLiteral("available"), !attachment.blob.isEmpty()},
            {QStringLiteral("imageUrl"),
             picture ? QStringLiteral("image://qpm/part/") + attachment.blob : QString()},
        });
    }
    return list;
}

} // namespace

MessageListModel::MessageListModel(QObject *parent) : QAbstractListModel(parent) {}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_messages.size());
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const StoredMessage &message = m_messages.at(index.row());
    switch (role) {
    case LuidRole:
        return int(message.luid);
    case OutgoingRole:
        return !message.incoming;
    case Qt::DisplayRole:
    case BodyRole:
        return message.body;
    case SubjectRole:
        return message.subject;
    case DateRole:
        return message.date;
    case TimeTextRole:
        return message.date.isValid()
                   ? QLocale().toString(message.date.toLocalTime().time(), QLocale::ShortFormat)
                   : QString();
    case SectionRole:
        return message.date.isValid() ? CallListModel::dayText(message.date.toLocalTime().date())
                                      : QString();
    case StatusTextRole:
        return statusText(message.box);
    case ReadRole:
        return message.read;
    case ArchivedRole:
        return message.archived;
    case OnPhoneRole:
        return message.isOnPhone();
    case IsMmsRole:
        return message.kind == MessageKind::Mms;
    case SimTextRole:
        return message.simSlot >= 0 ? tr("SIM %1").arg(message.simSlot + 1) : QString();
    case AttachmentsRole:
        return attachmentList(message);
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {
        {LuidRole, QByteArrayLiteral("luid")},
        {OutgoingRole, QByteArrayLiteral("outgoing")},
        {BodyRole, QByteArrayLiteral("body")},
        {SubjectRole, QByteArrayLiteral("subject")},
        {DateRole, QByteArrayLiteral("date")},
        {TimeTextRole, QByteArrayLiteral("timeText")},
        {SectionRole, QByteArrayLiteral("section")},
        {StatusTextRole, QByteArrayLiteral("statusText")},
        {ReadRole, QByteArrayLiteral("read")},
        {ArchivedRole, QByteArrayLiteral("archived")},
        {OnPhoneRole, QByteArrayLiteral("onPhone")},
        {IsMmsRole, QByteArrayLiteral("isMms")},
        {SimTextRole, QByteArrayLiteral("simText")},
        {AttachmentsRole, QByteArrayLiteral("attachments")},
    };
}

void MessageListModel::setMessages(QList<StoredMessage> messages)
{
    beginResetModel();
    m_messages = std::move(messages);
    endResetModel();
    Q_EMIT countChanged();
}

std::optional<StoredMessage> MessageListModel::message(quint32 luid) const
{
    for (const StoredMessage &message : m_messages) {
        if (message.luid == luid)
            return message;
    }
    return std::nullopt;
}

QList<quint32> MessageListModel::unreadLuids() const
{
    QList<quint32> luids;
    for (const StoredMessage &message : m_messages) {
        if (message.incoming && !message.read)
            luids.append(message.luid);
    }
    return luids;
}

int MessageListModel::luidAt(int row) const
{
    return row >= 0 && row < m_messages.size() ? int(m_messages.at(row).luid) : 0;
}

bool MessageListModel::archivedAt(int row) const
{
    return row >= 0 && row < m_messages.size() && m_messages.at(row).archived;
}

bool MessageListModel::unreadAt(int row) const
{
    return row >= 0 && row < m_messages.size() && m_messages.at(row).incoming &&
           !m_messages.at(row).read;
}

int MessageListModel::savableAttachmentsAt(int row) const
{
    if (row < 0 || row >= m_messages.size())
        return 0;
    int savable = 0;
    for (const StoredAttachment &attachment : m_messages.at(row).attachments) {
        if (!attachment.blob.isEmpty())
            ++savable;
    }
    return savable;
}

QString MessageListModel::statusText(MessageBox box)
{
    switch (box) {
    case MessageBox::Draft:
        return tr("Draft");
    case MessageBox::Outbox:
    case MessageBox::Queued:
        return tr("Waiting to be sent");
    case MessageBox::Failed:
        return tr("Not sent");
    case MessageBox::Inbox:
    case MessageBox::Sent:
    case MessageBox::Scheduled:
    case MessageBox::Other:
        break;
    }
    return {};
}

QString MessageListModel::sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace qpm::ui
