#pragma once

#include "core/store/MessageStore.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The conversations of the current phone, newest first, narrowed by the
 * search of the area. Each shows the name of the stored contact with the
 * number, else the number or sender name. Filled by MessagesArea.
 */
class ConversationListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ConversationListModel)
    QML_UNCREATABLE("Provided by MessagesArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Conversations stored for the phone, before the search.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged FINAL)

public:
    enum Role {
        ThreadKeyRole = Qt::UserRole + 1,
        /// The contact's name, else the number, else a text for an unknown sender.
        TitleRole,
        AddressRole,
        SnippetRole,
        DateRole,
        /// The time today, else the date in the short form of the current locale.
        DateTextRole,
        UnreadRole,
        CountRole,
        /// Messages kept on this computer.
        ArchivedRole,
        HasAttachmentsRole,
        /// Up to two letters of the contact's name; "#" without a contact.
        InitialsRole,
    };

    explicit ConversationListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] int totalCount() const { return int(m_all.size()); }

    /// Replaces every conversation, which must come newest first; the search stays.
    void setConversations(QList<core::store::ConversationSummary> conversations);
    /// Contact names by ContactStore::numberKey().
    void setContactNames(QHash<QString, QString> names);
    /**
     * Shows only conversations whose name or number holds `text`, or which
     * are in `matches`, the conversations whose messages match. An empty
     * text shows all.
     */
    void setSearch(const QString &text, QSet<QString> matches);

    /// The row of the conversation `threadKey`, or -1 when it is not shown.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &threadKey) const;
    /// The conversation in `row`, or an empty string.
    [[nodiscard]] Q_INVOKABLE QString threadKeyAt(int row) const;
    /// The stored conversation `threadKey`, also when the search hides it.
    [[nodiscard]] const core::store::ConversationSummary *find(const QString &threadKey) const;
    /// The contact's name for `address`; empty when no stored contact has it.
    [[nodiscard]] QString contactNameFor(const QString &address) const;
    /// What the list shows as the title of a conversation with `address`.
    [[nodiscard]] QString titleFor(const QString &address) const;
    /// The time today, else "Yesterday", else the date in short form.
    [[nodiscard]] static QString dateText(const QDateTime &utc);

Q_SIGNALS:
    void countChanged();

private:
    void refilter();
    [[nodiscard]] bool matches(const core::store::ConversationSummary &conversation) const;

    QList<core::store::ConversationSummary> m_all;
    QList<qsizetype> m_rows;
    QHash<QString, QString> m_contactNames;
    QString m_searchText;
    QString m_searchDigits;
    QSet<QString> m_matches;
};

} // namespace qpm::ui
