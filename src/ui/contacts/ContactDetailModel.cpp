#include "ui/contacts/ContactDetailModel.h"

#include "ui/contacts/ContactTypes.h"

#include <QLocale>

namespace qpm::ui {

namespace {

using core::codec::ContactValue;
using core::codec::ContactValueKind;

QString addressText(const ContactValue &entry)
{
    namespace part = core::codec::addressPart;
    const auto at = [&entry](int index) { return entry.address.value(index).trimmed(); };
    QStringList lines;
    for (const QString &line : {at(part::poBox), at(part::extended), at(part::street)}) {
        if (!line.isEmpty())
            lines.append(line);
    }
    const QString city = QStringList({at(part::postcode), at(part::city)}).join(u' ').trimmed();
    if (!city.isEmpty())
        lines.append(city);
    for (const QString &line : {at(part::region), at(part::country)}) {
        if (!line.isEmpty())
            lines.append(line);
    }
    return lines.join(u'\n');
}

QString dateText(const QString &isoText)
{
    const QDate date = QDate::fromString(isoText, Qt::ISODate);
    return date.isValid() ? QLocale().toString(date, QLocale::LongFormat) : isoText;
}

} // namespace

ContactDetailModel::ContactDetailModel(QObject *parent) : QAbstractListModel(parent) {}

int ContactDetailModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ContactDetailModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case SectionRole:
        return row.section;
    case LabelRole:
        return row.label;
    case Qt::DisplayRole:
    case ValueRole:
        return row.value;
    case ActionRole:
        return row.action;
    case ActionValueRole:
        return row.actionValue;
    case FirstInSectionRole:
        return index.row() == 0 || m_rows.at(index.row() - 1).section != row.section;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> ContactDetailModel::roleNames() const
{
    return {
        {SectionRole, QByteArrayLiteral("section")},
        {LabelRole, QByteArrayLiteral("label")},
        {ValueRole, QByteArrayLiteral("value")},
        {ActionRole, QByteArrayLiteral("action")},
        {ActionValueRole, QByteArrayLiteral("actionValue")},
        {FirstInSectionRole, QByteArrayLiteral("firstInSection")},
    };
}

QString ContactDetailModel::displayName() const
{
    return core::codec::displayNameOf(m_contact);
}

QString ContactDetailModel::work() const
{
    QStringList parts;
    for (const QString &part : {m_contact.title, m_contact.organization, m_contact.department}) {
        if (!part.trimmed().isEmpty())
            parts.append(part.trimmed());
    }
    return parts.join(QLatin1StringView(", "));
}

QString ContactDetailModel::initials() const
{
    QString result;
    const QStringList words = displayName().split(u' ', Qt::SkipEmptyParts);
    for (const QString &word : words) {
        if (word.at(0).isLetterOrNumber())
            result.append(word.at(0).toUpper());
        if (result.size() == 2)
            break;
    }
    return result;
}

QString ContactDetailModel::groups() const
{
    return m_contact.groups.join(QLatin1StringView(", "));
}

void ContactDetailModel::setContact(const core::codec::Contact &contact, const QString &photoUrl,
                                    const QString &accountTitle)
{
    beginResetModel();
    m_contact = contact;
    m_photoUrl = photoUrl;
    m_hasContact = true;
    const bool ownAccount =
        contact.accountName.isEmpty() || contact.accountName == QLatin1StringView("null");
    m_account = ownAccount ? QString()
                           : (accountTitle.isEmpty()
                                  ? contact.accountName
                                  : tr("%1 (%2)").arg(accountTitle, contact.accountName));
    m_rows.clear();
    if (!contact.nickname.isEmpty())
        m_rows.append({tr("Name"), tr("Nickname"), contact.nickname, QString(), QString()});
    for (const ContactValue &phone : contact.phones) {
        if (phone.isEmpty())
            continue;
        m_rows.append({ContactTypes::title(ContactValueKind::Phone),
                       ContactTypes::label(ContactValueKind::Phone, phone.type, phone.label),
                       phone.value, QStringLiteral("call"), phone.value});
    }
    for (const ContactValue &email : contact.emails) {
        if (email.isEmpty())
            continue;
        m_rows.append({ContactTypes::title(ContactValueKind::Email),
                       ContactTypes::label(ContactValueKind::Email, email.type, email.label),
                       email.value, QStringLiteral("mail"), email.value});
    }
    for (const ContactValue &address : contact.addresses) {
        if (address.isEmpty())
            continue;
        m_rows.append({ContactTypes::title(ContactValueKind::Address),
                       ContactTypes::label(ContactValueKind::Address, address.type, address.label),
                       addressText(address), QString(), QString()});
    }
    for (const ContactValue &website : contact.websites) {
        if (website.isEmpty())
            continue;
        m_rows.append({ContactTypes::title(ContactValueKind::Website),
                       ContactTypes::label(ContactValueKind::Website, website.type, website.label),
                       website.value, QStringLiteral("link"), website.value});
    }
    for (const ContactValue &im : contact.ims) {
        if (im.isEmpty())
            continue;
        m_rows.append({ContactTypes::title(ContactValueKind::Im),
                       ContactTypes::label(ContactValueKind::Im, im.type, im.label), im.value,
                       QString(), QString()});
    }
    const QString dates = ContactTypes::title(ContactValueKind::Event);
    if (contact.birthday.isValid()) {
        m_rows.append({dates, tr("Birthday"),
                       QLocale().toString(contact.birthday, QLocale::LongFormat), QString(),
                       QString()});
    }
    if (contact.anniversary.isValid()) {
        m_rows.append({dates, tr("Anniversary"),
                       QLocale().toString(contact.anniversary, QLocale::LongFormat), QString(),
                       QString()});
    }
    for (const ContactValue &event : contact.events) {
        if (event.isEmpty())
            continue;
        m_rows.append({dates, ContactTypes::label(ContactValueKind::Event, event.type, event.label),
                       dateText(event.value), QString(), QString()});
    }
    if (!contact.ringtone.isEmpty()) {
        // The phone sends "<uri>;<title>"; only the title means something to people.
        const QString title = contact.ringtone.section(u';', 1).trimmed();
        m_rows.append({tr("More"), tr("Ringtone"), title.isEmpty() ? contact.ringtone : title,
                       QString(), QString()});
    }
    endResetModel();
    Q_EMIT contactChanged();
}

void ContactDetailModel::clear()
{
    beginResetModel();
    m_rows.clear();
    m_contact = {};
    m_photoUrl.clear();
    m_account.clear();
    m_hasContact = false;
    endResetModel();
    Q_EMIT contactChanged();
}

} // namespace qpm::ui
