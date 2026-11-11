#include "ui/calls/CallListModel.h"

#include "core/store/ContactStore.h"

#include <QLocale>

namespace qpm::ui {

namespace {

using core::codec::CallDirection;
using core::codec::CallEntry;

constexpr qint64 secondsPerMinute = 60;
constexpr qint64 secondsPerHour = 60 * 60;
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

CallListModel::CallListModel(QObject *parent) : QAbstractListModel(parent) {}

int CallListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant CallListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const CallEntry &entry = m_all.at(m_rows.at(index.row()));
    switch (role) {
    case IdRole:
        return int(entry.id);
    case NumberRole:
        return entry.number;
    case NameRole: {
        const QString contact = contactNameFor(entry.number);
        return contact.isEmpty() ? entry.name : contact;
    }
    case Qt::DisplayRole:
    case TitleRole:
        return titleOf(entry);
    case KindRole:
        return int(kindOf(entry.direction));
    case KindTextRole:
        return kindText(entry.direction);
    case StartRole:
        return entry.start;
    case TimeTextRole:
        return QLocale().toString(entry.start.time(), QLocale::ShortFormat);
    case DurationRole:
        return entry.durationSeconds;
    case DurationTextRole:
        return entry.direction == CallDirection::Missed ? QString()
                                                        : durationText(entry.durationSeconds);
    case SectionRole:
        return dayText(entry.start.date());
    case InContactsRole:
        return !contactNameFor(entry.number).isEmpty();
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> CallListModel::roleNames() const
{
    return {
        {IdRole, QByteArrayLiteral("callId")},
        {NumberRole, QByteArrayLiteral("number")},
        {NameRole, QByteArrayLiteral("name")},
        {TitleRole, QByteArrayLiteral("title")},
        {KindRole, QByteArrayLiteral("kind")},
        {KindTextRole, QByteArrayLiteral("kindText")},
        {StartRole, QByteArrayLiteral("start")},
        {TimeTextRole, QByteArrayLiteral("timeText")},
        {DurationRole, QByteArrayLiteral("duration")},
        {DurationTextRole, QByteArrayLiteral("durationText")},
        {SectionRole, QByteArrayLiteral("section")},
        {InContactsRole, QByteArrayLiteral("inContacts")},
    };
}

void CallListModel::setKindFilter(Kind kind)
{
    if (m_kindFilter == kind)
        return;
    m_kindFilter = kind;
    Q_EMIT kindFilterChanged();
    refilter();
}

void CallListModel::setCalls(QList<CallEntry> calls)
{
    m_all = std::move(calls);
    refilter();
}

void CallListModel::setContactNames(QHash<QString, QString> names)
{
    m_contactNames = std::move(names);
    // The search looks at the names too, so the rows may change.
    refilter();
}

void CallListModel::setSearchText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_searchText)
        return;
    m_searchText = trimmed;
    const QString digits = digitsOf(trimmed);
    m_searchDigits =
        !hasLetter(trimmed) && digits.size() >= minimumSearchDigits ? digits : QString();
    refilter();
}

int CallListModel::rowOf(int id) const
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (int(m_all.at(m_rows.at(row)).id) == id)
            return int(row);
    }
    return -1;
}

int CallListModel::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return 0;
    return int(m_all.at(m_rows.at(row)).id);
}

QList<quint32> CallListModel::shownIds() const
{
    QList<quint32> ids;
    ids.reserve(m_rows.size());
    for (const qsizetype row : m_rows)
        ids.append(m_all.at(row).id);
    return ids;
}

QList<CallEntry> CallListModel::shownEntries() const
{
    QList<CallEntry> entries;
    entries.reserve(m_rows.size());
    for (const qsizetype row : m_rows)
        entries.append(m_all.at(row));
    return entries;
}

std::optional<CallEntry> CallListModel::entry(quint32 id) const
{
    for (const CallEntry &entry : m_all) {
        if (entry.id == id)
            return entry;
    }
    return std::nullopt;
}

QString CallListModel::contactNameFor(const QString &number) const
{
    const QString key = core::store::ContactStore::numberKey(number);
    return key.isEmpty() ? QString() : m_contactNames.value(key);
}

QString CallListModel::titleOf(const CallEntry &entry) const
{
    const QString contact = contactNameFor(entry.number);
    if (!contact.isEmpty())
        return contact;
    if (!entry.name.trimmed().isEmpty())
        return entry.name;
    if (!entry.number.isEmpty())
        return entry.number;
    return tr("Unknown number");
}

CallListModel::Kind CallListModel::kindOf(CallDirection direction)
{
    switch (direction) {
    case CallDirection::Incoming:
        return Kind::Incoming;
    case CallDirection::Missed:
        return Kind::Missed;
    case CallDirection::Outgoing:
        break;
    }
    return Kind::Outgoing;
}

QString CallListModel::kindText(CallDirection direction)
{
    switch (direction) {
    case CallDirection::Incoming:
        return tr("Incoming call");
    case CallDirection::Missed:
        return tr("Missed call");
    case CallDirection::Outgoing:
        break;
    }
    return tr("Outgoing call");
}

QString CallListModel::durationText(qint64 seconds)
{
    const qint64 hours = seconds / secondsPerHour;
    const qint64 minutes = (seconds % secondsPerHour) / secondsPerMinute;
    const qint64 rest = seconds % secondsPerMinute;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(rest, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(rest, 2, 10, QLatin1Char('0'));
}

QString CallListModel::dayText(const QDate &day)
{
    const QDate today = QDate::currentDate();
    if (day == today)
        return tr("Today");
    if (day == today.addDays(-1))
        return tr("Yesterday");
    return QLocale().toString(day, QLocale::LongFormat);
}

bool CallListModel::matches(const CallEntry &entry) const
{
    if (m_kindFilter != Kind::All && kindOf(entry.direction) != m_kindFilter)
        return false;
    if (m_searchText.isEmpty())
        return true;
    if (!m_searchDigits.isEmpty() && digitsOf(entry.number).contains(m_searchDigits))
        return true;
    return titleOf(entry).contains(m_searchText, Qt::CaseInsensitive) ||
           entry.name.contains(m_searchText, Qt::CaseInsensitive) ||
           entry.number.contains(m_searchText, Qt::CaseInsensitive);
}

void CallListModel::refilter()
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
