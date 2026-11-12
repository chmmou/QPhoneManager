#include "ui/messages/ConversationListModel.h"

#include "core/store/ContactStore.h"
#include "ui/PhoneNumberText.h"

#include <QLocale>
#include <QStringList>

namespace qpm::ui {

namespace {

using core::store::ConversationSummary;

constexpr qsizetype minimumSearchDigits = 3;

QString digitsOf(const QString &text)
{
    QString digits;
    for (const QChar c : text) {
        if (c.isDigit())
            digits.append(c);
    }
    return digits;
}

bool hasLetter(const QString &text)
{
    for (const QChar c : text) {
        if (c.isLetter())
            return true;
    }
    return false;
}

} // namespace

ConversationListModel::ConversationListModel(QObject *parent) : QAbstractListModel(parent) {}

int ConversationListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ConversationListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const ConversationSummary &conversation = m_all.at(m_rows.at(index.row()));
    switch (role) {
    case ThreadKeyRole:
        return conversation.threadKey;
    case Qt::DisplayRole:
    case TitleRole:
        return titleFor(conversation.address);
    case AddressRole:
        return conversation.address;
    case SnippetRole:
        return conversation.snippet;
    case DateRole:
        return conversation.lastDate;
    case DateTextRole:
        return dateText(conversation.lastDate);
    case UnreadRole:
        return conversation.unread;
    case CountRole:
        return conversation.count;
    case ArchivedRole:
        return conversation.archived;
    case HasAttachmentsRole:
        return conversation.lastHasAttachments;
    case InitialsRole: {
        QString initials;
        const QStringList words =
            contactNameFor(conversation.address).split(u' ', Qt::SkipEmptyParts);
        for (const QString &word : words) {
            if (word.at(0).isLetter())
                initials.append(word.at(0).toUpper());
            if (initials.size() == 2)
                break;
        }
        return initials.isEmpty() ? QStringLiteral("#") : initials;
    }
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> ConversationListModel::roleNames() const
{
    return {
        {ThreadKeyRole, QByteArrayLiteral("threadKey")},
        {TitleRole, QByteArrayLiteral("title")},
        {AddressRole, QByteArrayLiteral("address")},
        {SnippetRole, QByteArrayLiteral("snippet")},
        {DateRole, QByteArrayLiteral("date")},
        {DateTextRole, QByteArrayLiteral("dateText")},
        {UnreadRole, QByteArrayLiteral("unread")},
        {CountRole, QByteArrayLiteral("messageCount")},
        {ArchivedRole, QByteArrayLiteral("archived")},
        {HasAttachmentsRole, QByteArrayLiteral("hasAttachments")},
        {InitialsRole, QByteArrayLiteral("initials")},
    };
}

void ConversationListModel::setConversations(QList<ConversationSummary> conversations)
{
    m_all = std::move(conversations);
    refilter();
}

void ConversationListModel::setContactNames(QHash<QString, QString> names)
{
    m_contactNames = std::move(names);
    // The search looks at the names too, so the rows may change.
    refilter();
}

void ConversationListModel::setSearch(const QString &text, QSet<QString> matches)
{
    m_searchText = text.trimmed();
    const QString digits = digitsOf(m_searchText);
    m_searchDigits =
        !hasLetter(m_searchText) && digits.size() >= minimumSearchDigits ? digits : QString();
    m_matches = std::move(matches);
    refilter();
}

int ConversationListModel::rowOf(const QString &threadKey) const
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_all.at(m_rows.at(row)).threadKey == threadKey)
            return int(row);
    }
    return -1;
}

QString ConversationListModel::threadKeyAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_all.at(m_rows.at(row)).threadKey;
}

const ConversationSummary *ConversationListModel::find(const QString &threadKey) const
{
    for (const ConversationSummary &conversation : m_all) {
        if (conversation.threadKey == threadKey)
            return &conversation;
    }
    return nullptr;
}

QString ConversationListModel::contactNameFor(const QString &address) const
{
    const QString key = core::store::ContactStore::numberKey(address);
    return key.isEmpty() ? QString() : m_contactNames.value(key);
}

QString ConversationListModel::titleFor(const QString &address) const
{
    const QString contact = contactNameFor(address);
    if (!contact.isEmpty())
        return contact;
    if (!address.trimmed().isEmpty())
        return PhoneNumberText::display(address);
    return tr("Unknown sender");
}

QString ConversationListModel::dateText(const QDateTime &utc)
{
    if (!utc.isValid())
        return {};
    const QDateTime local = utc.toLocalTime();
    const QDate today = QDate::currentDate();
    const QLocale locale;
    if (local.date() == today)
        return locale.toString(local.time(), QLocale::ShortFormat);
    if (local.date() == today.addDays(-1))
        return tr("Yesterday");
    return locale.toString(local.date(), QLocale::ShortFormat);
}

bool ConversationListModel::matches(const ConversationSummary &conversation) const
{
    if (m_searchText.isEmpty())
        return true;
    if (m_matches.contains(conversation.threadKey))
        return true;
    if (!m_searchDigits.isEmpty() && digitsOf(conversation.address).contains(m_searchDigits))
        return true;
    return titleFor(conversation.address).contains(m_searchText, Qt::CaseInsensitive) ||
           conversation.address.contains(m_searchText, Qt::CaseInsensitive);
}

void ConversationListModel::refilter()
{
    beginResetModel();
    m_rows.clear();
    for (qsizetype i = 0; i < m_all.size(); ++i) {
        if (matches(m_all.at(i)))
            m_rows.append(i);
    }
    endResetModel();
    // The total may have changed too, so the signal goes out every time.
    Q_EMIT countChanged();
}

} // namespace qpm::ui
