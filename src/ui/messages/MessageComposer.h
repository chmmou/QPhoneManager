#pragma once

#include "core/codec/SmsLength.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

namespace qpm::core::service {
class MessagesService;
}

namespace qpm::ui {

/**
 * The form for a new text message or a scheduled one: recipients, text with
 * the count of characters and messages, the SIM card and the time to send.
 * Sending and scheduling go through MessagesService; their outcome comes
 * through errorOccurred(), infoMessage() and finished(). Owned by
 * MessagesArea, which also hands in the contact names, the SIM cards and the
 * connection state. No call blocks.
 */
class MessageComposer : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MessageComposer)
    QML_UNCREATABLE("Provided by MessagesArea")

    /// One line per recipient: the contact's name with the number, else the number.
    Q_PROPERTY(QStringList recipients READ recipientLabels NOTIFY recipientsChanged FINAL)
    /// The number being typed before it becomes a recipient.
    Q_PROPERTY(QString recipientText READ recipientText WRITE setRecipientText NOTIFY
                   recipientTextChanged FINAL)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged FINAL)
    /// "120 characters left" and the like.
    Q_PROPERTY(QString remainingText READ remainingText NOTIFY textChanged FINAL)
    /// "2 messages" and the like; empty for an empty text.
    Q_PROPERTY(QString partsText READ partsText NOTIFY textChanged FINAL)
    Q_PROPERTY(int parts READ parts NOTIFY textChanged FINAL)
    /// The text holds a character outside the GSM alphabet, so fewer fit one message.
    Q_PROPERTY(bool unicode READ isUnicode NOTIFY textChanged FINAL)
    /// "Default SIM card" first, then one line per SIM card of the phone.
    Q_PROPERTY(QStringList simChoices READ simChoices NOTIFY simChoicesChanged FINAL)
    Q_PROPERTY(int simIndex READ simIndex WRITE setSimIndex NOTIFY simIndexChanged FINAL)
    /// Send later at the time below instead of now.
    Q_PROPERTY(bool scheduled READ isScheduled WRITE setScheduled NOTIFY scheduleChanged FINAL)
    /// Date and time to send, in the short form of the current locale.
    Q_PROPERTY(
        QString scheduleDate READ scheduleDate WRITE setScheduleDate NOTIFY scheduleChanged FINAL)
    Q_PROPERTY(
        QString scheduleTime READ scheduleTime WRITE setScheduleTime NOTIFY scheduleChanged FINAL)
    /// Why the time cannot be used; empty when it can.
    Q_PROPERTY(QString scheduleProblem READ scheduleProblem NOTIFY scheduleChanged FINAL)
    /// The scheduled message being changed; 0 for a new message.
    Q_PROPERTY(int editingLuid READ editingLuid NOTIFY editingLuidChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// Everything needed is there and the phone is connected.
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY canSubmitChanged FINAL)

public:
    /// The contact's name for a number, or an empty string.
    using NameLookup = std::function<QString(const QString &number)>;

    /// `service` must outlive this object.
    explicit MessageComposer(core::service::MessagesService *service, QObject *parent = nullptr);

    [[nodiscard]] QStringList recipientLabels() const;
    [[nodiscard]] QStringList recipientNumbers() const { return m_numbers; }
    [[nodiscard]] QString recipientText() const { return m_recipientText; }
    void setRecipientText(const QString &text);
    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString &text);
    [[nodiscard]] QString remainingText() const;
    [[nodiscard]] QString partsText() const;
    [[nodiscard]] int parts() const { return m_length.parts; }
    [[nodiscard]] bool isUnicode() const { return m_length.unicode; }
    [[nodiscard]] QStringList simChoices() const;
    [[nodiscard]] int simIndex() const { return m_simIndex; }
    void setSimIndex(int index);
    [[nodiscard]] bool isScheduled() const { return m_scheduled; }
    void setScheduled(bool scheduled);
    [[nodiscard]] QString scheduleDate() const { return m_scheduleDate; }
    void setScheduleDate(const QString &date);
    [[nodiscard]] QString scheduleTime() const { return m_scheduleTime; }
    void setScheduleTime(const QString &time);
    [[nodiscard]] QString scheduleProblem() const;
    [[nodiscard]] int editingLuid() const { return int(m_editingLuid); }
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] bool canSubmit() const;

    void setNameLookup(NameLookup lookup);
    /// The SIM cards as "SIM 1: Name" lines with their slot numbers.
    void setSimCards(const QStringList &labels, const QList<int> &simSlots);
    void setConnected(bool connected);
    /// The time to send as entered, in UTC; invalid when it cannot be read.
    [[nodiscard]] QDateTime scheduledAt() const;

    /// Empties the form for a new message to `numbers`; numbers that are none are left out.
    Q_INVOKABLE void startNew(const QStringList &numbers);
    /// Fills the form with the stored scheduled message `luid`; false when there is none.
    Q_INVOKABLE bool startEditing(int luid);
    /**
     * Adds the number typed in recipientText. False with errorOccurred() when
     * it is no number a message can go to; a number already added is passed over.
     */
    Q_INVOKABLE bool addRecipient();
    Q_INVOKABLE void removeRecipient(int index);
    /// Sends the text now to every recipient; false when the form is not complete.
    Q_INVOKABLE bool send();
    /// Saves the message as a scheduled one on the phone; false when the form is not complete.
    Q_INVOKABLE bool schedule();

    /// Where startEditing() takes the stored message from.
    using ScheduledLookup = std::function<bool(quint32 luid, QStringList &numbers, QString &text,
                                               QDateTime &when, int &simSlot)>;
    void setScheduledLookup(ScheduledLookup lookup);

Q_SIGNALS:
    void recipientsChanged();
    void recipientTextChanged();
    void textChanged();
    void simChoicesChanged();
    void simIndexChanged();
    void scheduleChanged();
    void editingLuidChanged();
    void busyChanged();
    void canSubmitChanged();
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);
    /// The message went out or was scheduled; the form is empty again.
    void finished();

private:
    void setBusy(bool busy);
    void reset();
    [[nodiscard]] int simSlot() const;

    core::service::MessagesService *m_service = nullptr;
    NameLookup m_names;
    ScheduledLookup m_scheduledLookup;
    QStringList m_numbers;
    QString m_recipientText;
    QString m_text;
    core::codec::SmsLength m_length;
    QStringList m_simLabels;
    QList<int> m_simSlots;
    int m_simIndex = 0;
    bool m_scheduled = false;
    QString m_scheduleDate;
    QString m_scheduleTime;
    quint32 m_editingLuid = 0;
    bool m_busy = false;
    bool m_connected = false;
};

} // namespace qpm::ui
