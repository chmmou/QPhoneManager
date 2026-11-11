#pragma once

#include "core/store/MessageStore.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace qpm::ui {

/**
 * The messages of the conversation shown, oldest first, grouped by day.
 * Filled by MessagesArea.
 */
class MessageListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MessageListModel)
    QML_UNCREATABLE("Provided by MessagesArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        LuidRole = Qt::UserRole + 1,
        OutgoingRole,
        BodyRole,
        SubjectRole,
        DateRole,
        /// The time of day, for the bubble under its day.
        TimeTextRole,
        /// The day the messages are listed under: today, yesterday or a date.
        SectionRole,
        /// "Not sent", "Draft" and the like; empty for a message that went out or came in.
        StatusTextRole,
        ReadRole,
        ArchivedRole,
        /// The phone still has the message.
        OnPhoneRole,
        IsMmsRole,
        /// "SIM 1" and so on; empty when the phone does not say.
        SimTextRole,
        /**
         * The parts as a list of maps with position, name, mimeType, sizeText,
         * available (on this computer) and imageUrl (for a picture, else empty).
         */
        AttachmentsRole,
    };

    explicit MessageListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }

    /// Replaces every message, which must come oldest first.
    void setMessages(QList<core::store::StoredMessage> messages);
    [[nodiscard]] const QList<core::store::StoredMessage> &messages() const { return m_messages; }
    /// The message `luid` when it is shown.
    [[nodiscard]] std::optional<core::store::StoredMessage> message(quint32 luid) const;
    /// The LUIDs of the received messages not read yet.
    [[nodiscard]] QList<quint32> unreadLuids() const;

    /// The message in `row`, or 0.
    [[nodiscard]] Q_INVOKABLE int luidAt(int row) const;
    /// Whether the message in `row` is archived; false for a row that does not exist.
    [[nodiscard]] Q_INVOKABLE bool archivedAt(int row) const;
    /// Whether the message in `row` was received and is not read yet.
    [[nodiscard]] Q_INVOKABLE bool unreadAt(int row) const;
    /// How many parts of the message in `row` are on this computer.
    [[nodiscard]] Q_INVOKABLE int savableAttachmentsAt(int row) const;

    /// "Not sent", "Draft" and the like for a message in `box`; empty for inbox and sent.
    [[nodiscard]] static QString statusText(core::codec::MessageBox box);
    /// "12 KB", "3.4 MB": the size in the units of the current locale.
    [[nodiscard]] static QString sizeText(qint64 bytes);

Q_SIGNALS:
    void countChanged();

private:
    QList<core::store::StoredMessage> m_messages;
};

} // namespace qpm::ui
