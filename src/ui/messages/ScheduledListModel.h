#pragma once

#include "core/store/MessageStore.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <optional>

namespace qpm::ui {

/**
 * The scheduled messages of the current phone, earliest first, with their
 * recipients by contact name where there is one. Filled by MessagesArea.
 */
class ScheduledListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ScheduledListModel)
    QML_UNCREATABLE("Provided by MessagesArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        LuidRole = Qt::UserRole + 1,
        RecipientsTextRole,
        BodyRole,
        /// Date and time to send, in the long form of the current locale.
        WhenTextRole,
        /// "Waiting", "Sent", "Sent to 1 of 2" or "Not sent".
        StatusTextRole,
        FailedRole,
    };

    /**
     * The contact's name for a number, or an empty string. `storedName` is the
     * name the message carries for the number; where contacts share the
     * number, it tells which one is meant.
     */
    using NameLookup = std::function<QString(const QString &number, const QString &storedName)>;

    explicit ScheduledListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }

    void setMessages(QList<core::store::StoredMessage> messages);
    void setNameLookup(NameLookup lookup);
    [[nodiscard]] std::optional<core::store::StoredMessage> message(quint32 luid) const;
    /// The message in `row`, or 0.
    [[nodiscard]] Q_INVOKABLE int luidAt(int row) const;
    /// The recipients as names where known, else numbers, joined for one line.
    [[nodiscard]] QString recipientsText(const core::store::StoredMessage &message) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<core::store::StoredMessage> m_messages;
    NameLookup m_names;
};

} // namespace qpm::ui
