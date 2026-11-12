#include "ui/messages/MessageComposer.h"

#include "core/service/ContactsService.h"
#include "core/service/MessagesService.h"
#include "ui/PhoneNumberText.h"
#include "ui/messages/MessagesArea.h"

#include <QLocale>

namespace qpm::ui {

namespace {

using core::Result;

constexpr qint64 minimumLeadSeconds = 60;
constexpr int defaultHour = 9;

constexpr int twoDigitBaseYear = 2000;

// The short date of the locale, with a four-digit year: a two-digit year
// would read back as one of the last century.
QString dateFormat()
{
    QString format = QLocale().dateFormat(QLocale::ShortFormat);
    if (!format.contains(QLatin1StringView("yyyy")))
        format.replace(QLatin1StringView("yy"), QLatin1StringView("yyyy"));
    return format;
}

QString dateText(const QDate &date)
{
    return QLocale().toString(date, dateFormat());
}

QDate readDate(const QString &text)
{
    const QString trimmed = text.trimmed();
    const QLocale locale;
    QDate date = locale.toDate(trimmed, dateFormat(), twoDigitBaseYear);
    if (!date.isValid())
        date = locale.toDate(trimmed, QLocale::ShortFormat, twoDigitBaseYear);
    if (!date.isValid())
        date = locale.toDate(trimmed, QLocale::LongFormat, twoDigitBaseYear);
    if (!date.isValid())
        date = QDate::fromString(trimmed, Qt::ISODate);
    return date;
}

QTime readTime(const QString &text)
{
    const QString trimmed = text.trimmed();
    const QLocale locale;
    QTime time = locale.toTime(trimmed, QLocale::ShortFormat);
    if (!time.isValid())
        time = QTime::fromString(trimmed, QStringLiteral("H:mm"));
    return time;
}

} // namespace

MessageComposer::MessageComposer(core::service::MessagesService *service, QObject *parent)
    : QObject(parent), m_service(service)
{
    Q_ASSERT(service);
    reset();
}

QStringList MessageComposer::recipientLabels() const
{
    QStringList labels;
    for (const QString &number : m_numbers) {
        const QString name = m_names ? m_names(number) : QString();
        const QString shown = PhoneNumberText::display(number);
        labels.append(name.isEmpty() ? shown : tr("%1 (%2)").arg(name, shown));
    }
    return labels;
}

void MessageComposer::setRecipientText(const QString &text)
{
    if (text == m_recipientText)
        return;
    m_recipientText = text;
    Q_EMIT recipientTextChanged();
}

void MessageComposer::setText(const QString &text)
{
    if (text == m_text)
        return;
    const bool could = canSubmit();
    m_text = text;
    m_length = core::codec::smsLength(text);
    Q_EMIT textChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
}

QString MessageComposer::remainingText() const
{
    return tr("%n character(s) left", nullptr, m_length.remaining);
}

QString MessageComposer::partsText() const
{
    return m_length.parts == 0 ? QString() : tr("%n message(s)", nullptr, m_length.parts);
}

QStringList MessageComposer::simChoices() const
{
    return QStringList{tr("Default SIM card")} + m_simLabels;
}

void MessageComposer::setSimIndex(int index)
{
    const int bounded = index >= 0 && index <= m_simLabels.size() ? index : 0;
    if (bounded == m_simIndex)
        return;
    m_simIndex = bounded;
    Q_EMIT simIndexChanged();
}

void MessageComposer::setScheduled(bool scheduled)
{
    if (scheduled == m_scheduled)
        return;
    const bool could = canSubmit();
    m_scheduled = scheduled;
    Q_EMIT scheduleChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
}

void MessageComposer::setScheduleDate(const QString &date)
{
    if (date == m_scheduleDate)
        return;
    const bool could = canSubmit();
    m_scheduleDate = date;
    Q_EMIT scheduleChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
}

void MessageComposer::setScheduleTime(const QString &time)
{
    if (time == m_scheduleTime)
        return;
    const bool could = canSubmit();
    m_scheduleTime = time;
    Q_EMIT scheduleChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
}

QDateTime MessageComposer::scheduledAt() const
{
    const QDate date = readDate(m_scheduleDate);
    const QTime time = readTime(m_scheduleTime);
    if (!date.isValid() || !time.isValid())
        return {};
    return QDateTime(date, time).toUTC();
}

QString MessageComposer::scheduleProblem() const
{
    if (!m_scheduled)
        return {};
    if (!readDate(m_scheduleDate).isValid())
        return tr("Enter a date like %1.").arg(dateText(QDate::currentDate().addDays(1)));
    if (!readTime(m_scheduleTime).isValid())
        return tr("Enter a time like %1.")
            .arg(QLocale().toString(QTime(defaultHour, 0), QLocale::ShortFormat));
    if (scheduledAt() < QDateTime::currentDateTimeUtc().addSecs(minimumLeadSeconds))
        return tr("Choose a time at least one minute ahead.");
    return {};
}

bool MessageComposer::canSubmit() const
{
    return m_connected && !m_busy && !m_numbers.isEmpty() && !m_text.trimmed().isEmpty() &&
           scheduleProblem().isEmpty();
}

void MessageComposer::setNameLookup(NameLookup lookup)
{
    m_names = std::move(lookup);
    Q_EMIT recipientsChanged();
}

void MessageComposer::setScheduledLookup(ScheduledLookup lookup)
{
    m_scheduledLookup = std::move(lookup);
}

void MessageComposer::setSimCards(const QStringList &labels, const QList<int> &simSlots)
{
    if (labels == m_simLabels && simSlots == m_simSlots)
        return;
    m_simLabels = labels;
    m_simSlots = simSlots;
    Q_EMIT simChoicesChanged();
    if (m_simIndex > m_simLabels.size()) {
        m_simIndex = 0;
        Q_EMIT simIndexChanged();
    }
}

void MessageComposer::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    const bool could = canSubmit();
    m_connected = connected;
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
}

int MessageComposer::simSlot() const
{
    return m_simIndex > 0 && m_simIndex <= m_simSlots.size() ? m_simSlots.at(m_simIndex - 1) : -1;
}

void MessageComposer::reset()
{
    m_numbers.clear();
    m_recipientText.clear();
    m_text.clear();
    m_length = core::codec::smsLength(QString());
    m_simIndex = 0;
    m_scheduled = false;
    const QDateTime tomorrow(QDate::currentDate().addDays(1), QTime(defaultHour, 0));
    m_scheduleDate = dateText(tomorrow.date());
    m_scheduleTime = QLocale().toString(tomorrow.time(), QLocale::ShortFormat);
    m_editingLuid = 0;
}

void MessageComposer::startNew(const QStringList &numbers)
{
    const bool could = canSubmit();
    reset();
    for (const QString &number : numbers) {
        const QString dialable = core::service::ContactsService::dialableNumber(number);
        if (!dialable.isEmpty() && !m_numbers.contains(dialable))
            m_numbers.append(dialable);
    }
    Q_EMIT recipientsChanged();
    Q_EMIT recipientTextChanged();
    Q_EMIT textChanged();
    Q_EMIT simIndexChanged();
    Q_EMIT scheduleChanged();
    Q_EMIT editingLuidChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
}

bool MessageComposer::startEditing(int luid)
{
    QStringList numbers;
    QString text;
    QDateTime when;
    int slot = -1;
    if (luid <= 0 || !m_scheduledLookup ||
        !m_scheduledLookup(quint32(luid), numbers, text, when, slot))
        return false;
    startNew(numbers);
    const bool could = canSubmit();
    m_editingLuid = quint32(luid);
    m_text = text;
    m_length = core::codec::smsLength(text);
    m_scheduled = true;
    const QDateTime local = when.toLocalTime();
    m_scheduleDate = dateText(local.date());
    m_scheduleTime = QLocale().toString(local.time(), QLocale::ShortFormat);
    m_simIndex = int(m_simSlots.indexOf(slot)) + 1;
    Q_EMIT textChanged();
    Q_EMIT scheduleChanged();
    Q_EMIT simIndexChanged();
    Q_EMIT editingLuidChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
    return true;
}

bool MessageComposer::addRecipient()
{
    const QString dialable = core::service::ContactsService::dialableNumber(m_recipientText);
    if (dialable.isEmpty()) {
        Q_EMIT errorOccurred(tr("This is no phone number a message can go to."));
        return false;
    }
    if (m_numbers.size() >= core::service::MessagesService::maxRecipients) {
        Q_EMIT errorOccurred(tr("A message can go to at most %1 recipients.")
                                 .arg(core::service::MessagesService::maxRecipients));
        return false;
    }
    const bool could = canSubmit();
    if (!m_numbers.contains(dialable)) {
        m_numbers.append(dialable);
        Q_EMIT recipientsChanged();
    }
    m_recipientText.clear();
    Q_EMIT recipientTextChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
    return true;
}

void MessageComposer::removeRecipient(int index)
{
    if (index < 0 || index >= m_numbers.size())
        return;
    const bool could = canSubmit();
    m_numbers.removeAt(index);
    Q_EMIT recipientsChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
}

void MessageComposer::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    const bool could = canSubmit();
    m_busy = busy;
    Q_EMIT busyChanged();
    if (could != canSubmit())
        Q_EMIT canSubmitChanged();
}

bool MessageComposer::send()
{
    if (!canSubmit() || m_scheduled)
        return false;
    setBusy(true);
    const int count = int(m_numbers.size());
    m_service->send(m_numbers, m_text, simSlot())
        .then(this, [this, count](const Result<core::service::SendReport> &report) {
            setBusy(false);
            if (!report.hasValue()) {
                const QString message =
                    MessagesArea::errorText(report.error(), MessagesArea::Action::Send);
                if (!message.isEmpty())
                    Q_EMIT errorOccurred(message);
                return;
            }
            if (report.value().failed > 0) {
                const QString reason =
                    MessagesArea::errorText(report.value().firstError, MessagesArea::Action::Send);
                if (report.value().sent > 0) {
                    Q_EMIT errorOccurred(tr("The message went to %1 of %2 recipients. %3")
                                             .arg(report.value().sent)
                                             .arg(count)
                                             .arg(reason));
                } else {
                    Q_EMIT errorOccurred(reason);
                }
                return;
            }
            Q_EMIT infoMessage(
                tr("Message sent to %n recipient(s).", nullptr, report.value().sent));
            startNew({});
            Q_EMIT finished();
        });
    return true;
}

bool MessageComposer::schedule()
{
    if (!canSubmit() || !m_scheduled)
        return false;
    core::codec::Message message;
    message.luid = m_editingLuid;
    message.kind = core::codec::MessageKind::Scheduled;
    message.box = core::codec::MessageBox::Scheduled;
    message.date = scheduledAt();
    message.body = m_text;
    message.simSlot = simSlot();
    for (const QString &number : std::as_const(m_numbers)) {
        core::codec::MessageRecipient recipient;
        recipient.number = number;
        recipient.name = m_names ? m_names(number) : QString();
        message.recipients.append(recipient);
    }
    setBusy(true);
    const bool editing = m_editingLuid != 0;
    m_service->saveScheduled(message).then(this, [this, editing](const Result<quint32> &saved) {
        setBusy(false);
        if (!saved.hasValue()) {
            const QString text =
                MessagesArea::errorText(saved.error(), MessagesArea::Action::Schedule);
            if (!text.isEmpty())
                Q_EMIT errorOccurred(text);
            return;
        }
        Q_EMIT infoMessage(editing ? tr("The scheduled message is changed.")
                                   : tr("The message is scheduled. The phone sends it at the "
                                        "time you chose."));
        startNew({});
        Q_EMIT finished();
    });
    return true;
}

} // namespace qpm::ui
