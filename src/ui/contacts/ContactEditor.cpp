#include "ui/contacts/ContactEditor.h"

#include "core/store/ContactStore.h"
#include "ui/contacts/ContactListModel.h"
#include "ui/contacts/ContactTypes.h"
#include "ui/contacts/PhoneImageProvider.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QLocale>

#include <algorithm>

namespace qpm::ui {

namespace {

using core::codec::Contact;
using core::codec::ContactValueKind;

// Largest side of a picture read for cropping.
constexpr int maxCandidateSide = 2048;
constexpr int smallestPhotoSide = 64;
constexpr int firstJpegQuality = 85;
constexpr int lowestJpegQuality = 45;
constexpr int jpegQualityStep = 10;

QString accountTitle(const core::codec::PbAccount &account)
{
    const bool phoneAccount = account.name.isEmpty() || account.name == QLatin1StringView("null");
    if (phoneAccount)
        return ContactEditor::tr("Phone");
    return account.title.isEmpty() ? account.name
                                   : ContactEditor::tr("%1 (%2)").arg(account.title, account.name);
}

bool sameName(const Contact &a, const Contact &b)
{
    return a.prefix == b.prefix && a.givenName == b.givenName && a.middleName == b.middleName &&
           a.familyName == b.familyName && a.suffix == b.suffix;
}

std::optional<QByteArray> encodeJpeg(const QImage &image, int quality)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) ||
        !image.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPEG", quality)) {
        return std::nullopt;
    }
    return bytes;
}

} // namespace

ContactEditor::ContactEditor(core::service::ContactsService *service,
                             std::shared_ptr<PhotoSource> photos, QObject *parent)
    : QObject(parent), m_service(service), m_photos(std::move(photos)),
      m_phones(new ContactValueModel(ContactValueKind::Phone, this)),
      m_emails(new ContactValueModel(ContactValueKind::Email, this)),
      m_addresses(new ContactValueModel(ContactValueKind::Address, this)),
      m_websites(new ContactValueModel(ContactValueKind::Website, this)),
      m_ims(new ContactValueModel(ContactValueKind::Im, this)),
      m_events(new ContactValueModel(ContactValueKind::Event, this))
{
    Q_ASSERT(service);
    for (ContactValueModel *model :
         {m_phones, m_emails, m_addresses, m_websites, m_ims, m_events}) {
        connect(model, &ContactValueModel::edited, this, [this]() {
            Q_EMIT fieldsChanged();
            Q_EMIT canSaveChanged();
        });
    }
}

void ContactEditor::setStore(const std::shared_ptr<core::store::ContactStore> &store)
{
    m_store = store;
    reloadAccounts();
    Q_EMIT canSaveChanged();
}

void ContactEditor::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    Q_EMIT canSaveChanged();
}

void ContactEditor::setText(QString &field, const QString &value)
{
    if (field == value)
        return;
    field = value;
    Q_EMIT fieldsChanged();
    Q_EMIT canSaveChanged();
}

void ContactEditor::setGivenName(const QString &value)
{
    setText(m_contact.givenName, value);
}

void ContactEditor::setMiddleName(const QString &value)
{
    setText(m_contact.middleName, value);
}

void ContactEditor::setFamilyName(const QString &value)
{
    setText(m_contact.familyName, value);
}

void ContactEditor::setPrefix(const QString &value)
{
    setText(m_contact.prefix, value);
}

void ContactEditor::setSuffix(const QString &value)
{
    setText(m_contact.suffix, value);
}

void ContactEditor::setNickname(const QString &value)
{
    setText(m_contact.nickname, value);
}

void ContactEditor::setOrganization(const QString &value)
{
    setText(m_contact.organization, value);
}

void ContactEditor::setDepartment(const QString &value)
{
    setText(m_contact.department, value);
}

void ContactEditor::setJobTitle(const QString &value)
{
    setText(m_contact.title, value);
}

void ContactEditor::setNote(const QString &value)
{
    setText(m_contact.note, value);
}

void ContactEditor::setBirthday(const QString &value)
{
    setText(m_birthday, value);
}

void ContactEditor::setAnniversary(const QString &value)
{
    setText(m_anniversary, value);
}

QString ContactEditor::dateFormat() const
{
    QString format = QLocale().dateFormat(QLocale::ShortFormat);
    // A two-digit year cannot tell 1925 from 2025.
    if (!format.contains(QLatin1StringView("yyyy")))
        format.replace(QLatin1StringView("yy"), QLatin1StringView("yyyy"));
    return format;
}

QDate ContactEditor::parseDate(const QString &text, bool *ok) const
{
    const QString trimmed = text.trimmed();
    *ok = true;
    if (trimmed.isEmpty())
        return {};
    QDate date = QLocale().toDate(trimmed, dateFormat());
    if (!date.isValid())
        date = QDate::fromString(trimmed, Qt::ISODate);
    *ok = date.isValid();
    return date;
}

QString ContactEditor::formatDate(const QDate &date) const
{
    return date.isValid() ? QLocale().toString(date, dateFormat()) : QString();
}

bool ContactEditor::datesValid() const
{
    bool birthdayOk = false;
    bool anniversaryOk = false;
    static_cast<void>(parseDate(m_birthday, &birthdayOk));
    static_cast<void>(parseDate(m_anniversary, &anniversaryOk));
    return birthdayOk && anniversaryOk;
}

QString ContactEditor::dateExample() const
{
    return formatDate(QDate(1990, 12, 31));
}

QStringList ContactEditor::accountNames() const
{
    QStringList names;
    for (const core::codec::PbAccount &account : m_accounts)
        names.append(accountTitle(account));
    return names;
}

void ContactEditor::setAccountIndex(int index)
{
    if (index == m_accountIndex || index < 0 || index >= m_accounts.size())
        return;
    m_accountIndex = index;
    Q_EMIT fieldsChanged();
    Q_EMIT accountsChanged();
}

QString ContactEditor::accountText() const
{
    if (isNew())
        return {};
    for (const core::codec::PbAccount &account : m_metadata.accounts) {
        if (account.type == m_original.accountType && account.name == m_original.accountName)
            return accountTitle(account);
    }
    core::codec::PbAccount unknown;
    unknown.name = m_original.accountName;
    return accountTitle(unknown);
}

QStringList ContactEditor::groupNames() const
{
    QString type = m_original.accountType;
    QString name = m_original.accountName;
    if (isNew() && m_accountIndex < m_accounts.size()) {
        type = m_accounts.at(m_accountIndex).type;
        name = m_accounts.at(m_accountIndex).name;
    }
    QStringList groups;
    for (const core::codec::PbGroup &group : m_metadata.groups) {
        const bool sameAccount = group.accountType == type && group.accountName == name;
        if ((sameAccount || m_metadata.accounts.size() <= 1) && !groups.contains(group.title))
            groups.append(group.title);
    }
    for (const QString &group : m_contact.groups) {
        if (!groups.contains(group))
            groups.append(group);
    }
    groups.sort(Qt::CaseInsensitive);
    return groups;
}

bool ContactEditor::isModified() const
{
    return !(edited() == m_original) || m_photoChanged;
}

bool ContactEditor::canSave() const
{
    return m_connected && !m_busy && !m_store.expired() && datesValid() &&
           core::codec::isSavable(edited());
}

QString ContactEditor::ownVersionText() const
{
    return m_conflict ? ContactTypes::summary(edited()) : QString();
}

QString ContactEditor::phoneVersionText() const
{
    return m_conflict ? ContactTypes::summary(m_conflict->phoneVersion) : QString();
}

Contact ContactEditor::edited() const
{
    Contact contact = m_contact;
    contact.phones = m_phones->values();
    contact.emails = m_emails->values();
    contact.addresses = m_addresses->values();
    contact.websites = m_websites->values();
    contact.ims = m_ims->values();
    contact.events = m_events->values();
    bool ok = false;
    contact.birthday = parseDate(m_birthday, &ok);
    contact.anniversary = parseDate(m_anniversary, &ok);
    if (!sameName(contact, m_original))
        contact.displayName.clear();
    if (isNew() && m_accountIndex < m_accounts.size()) {
        contact.accountType = m_accounts.at(m_accountIndex).type;
        contact.accountName = m_accounts.at(m_accountIndex).name;
    }
    return contact;
}

void ContactEditor::reloadAccounts()
{
    m_metadata = {};
    m_accounts.clear();
    if (const std::shared_ptr<core::store::ContactStore> store = m_store.lock()) {
        const core::Result<core::codec::PbMetadata> metadata = store->metadata();
        if (metadata.hasValue())
            m_metadata = metadata.value();
    }
    for (const core::codec::PbAccount &account : std::as_const(m_metadata.accounts)) {
        if (account.selected || account.isDefault || m_metadata.filterDisabled)
            m_accounts.append(account);
    }
    std::stable_sort(m_accounts.begin(), m_accounts.end(),
                     [](const core::codec::PbAccount &a, const core::codec::PbAccount &b) {
                         return a.isDefault && !b.isDefault;
                     });
    m_accountIndex = 0;
    Q_EMIT accountsChanged();
}

void ContactEditor::show(const Contact &contact)
{
    m_original = contact;
    m_contact = contact;
    m_phones->setValues(contact.phones);
    m_emails->setValues(contact.emails);
    m_addresses->setValues(contact.addresses);
    m_websites->setValues(contact.websites);
    m_ims->setValues(contact.ims);
    m_events->setValues(contact.events);
    m_birthday = formatDate(contact.birthday);
    m_anniversary = formatDate(contact.anniversary);
    m_photoChanged = false;
    m_photoUrl =
        contact.photo.isEmpty()
            ? QString()
            : ContactListModel::photoUrl(
                  contact.luid,
                  QString::fromLatin1(
                      QCryptographicHash::hash(contact.photo, QCryptographicHash::Sha1).toHex()));
    const bool hadConflict = m_conflict.has_value();
    m_conflict.reset();
    dropCandidate();
    Q_EMIT loaded();
    Q_EMIT fieldsChanged();
    Q_EMIT accountsChanged();
    Q_EMIT photoChanged();
    Q_EMIT canSaveChanged();
    if (hadConflict)
        Q_EMIT conflictChanged();
}

void ContactEditor::newContact()
{
    reloadAccounts();
    show(Contact());
}

bool ContactEditor::load(int luid)
{
    const std::shared_ptr<core::store::ContactStore> store = m_store.lock();
    if (!store || luid <= 0) {
        Q_EMIT errorOccurred(ContactTypes::errorText(
            core::Error(core::ErrorCode::NotFound, QStringLiteral("No such contact"))));
        return false;
    }
    const core::Result<Contact> contact = store->contact(quint32(luid));
    if (!contact.hasValue()) {
        Q_EMIT errorOccurred(ContactTypes::errorText(contact.error()));
        return false;
    }
    reloadAccounts();
    show(contact.value());
    return true;
}

void ContactEditor::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
    Q_EMIT canSaveChanged();
}

bool ContactEditor::save()
{
    if (m_busy)
        return false;
    if (!datesValid()) {
        Q_EMIT errorOccurred(tr("Enter dates like %1.").arg(dateExample()));
        return false;
    }
    if (!core::codec::isSavable(edited())) {
        Q_EMIT errorOccurred(ContactTypes::errorText(
            core::Error(core::ErrorCode::InvalidInput, QStringLiteral("Nothing to save"))));
        return false;
    }
    if (!m_connected) {
        Q_EMIT errorOccurred(ContactTypes::errorText(
            core::Error(core::ErrorCode::ConnectionClosed, QStringLiteral("Not connected"))));
        return false;
    }
    startSave(core::service::SaveMode::CheckConflict);
    return true;
}

void ContactEditor::startSave(core::service::SaveMode mode)
{
    setBusy(true);
    m_service->save(edited(), m_photoChanged, mode)
        .then(this, [this](const core::Result<core::service::SaveOutcome> &outcome) {
            setBusy(false);
            if (!outcome.hasValue()) {
                const QString message = ContactTypes::errorText(outcome.error());
                if (!message.isEmpty())
                    Q_EMIT errorOccurred(message);
                return;
            }
            if (outcome.value().conflict) {
                m_conflict = outcome.value();
                Q_EMIT conflictChanged();
                return;
            }
            const int luid = int(outcome.value().luid);
            static_cast<void>(load(luid));
            Q_EMIT saved(luid);
        });
}

bool ContactEditor::keepOwnVersion()
{
    if (!m_conflict || m_busy)
        return false;
    m_conflict.reset();
    Q_EMIT conflictChanged();
    startSave(core::service::SaveMode::Overwrite);
    return true;
}

bool ContactEditor::takePhoneVersion()
{
    if (!m_conflict || m_busy)
        return false;
    const core::Result<void> kept = m_service->keepPhoneVersion(m_conflict->phoneObject);
    if (!kept.hasValue()) {
        Q_EMIT errorOccurred(ContactTypes::errorText(kept.error()));
        return false;
    }
    const int luid = int(m_original.luid);
    static_cast<void>(load(luid));
    Q_EMIT saved(luid);
    return true;
}

void ContactEditor::setGroupSelected(const QString &group, bool selected)
{
    if (group.isEmpty() || m_contact.groups.contains(group) == selected)
        return;
    if (selected)
        m_contact.groups.append(group);
    else
        m_contact.groups.removeAll(group);
    Q_EMIT fieldsChanged();
    Q_EMIT canSaveChanged();
}

bool ContactEditor::addGroup(const QString &group)
{
    const QString trimmed = group.trimmed();
    if (trimmed.isEmpty())
        return false;
    const bool known = groupNames().contains(trimmed);
    setGroupSelected(trimmed, true);
    if (!known)
        Q_EMIT accountsChanged();
    return true;
}

bool ContactEditor::loadPhotoCandidate(const QUrl &url)
{
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a picture file on this computer."));
        return false;
    }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT errorOccurred(tr("The picture cannot be opened. Check that the file exists."));
        return false;
    }
    const core::Result<QImage> image =
        readImageSafely(&file, QSize(maxCandidateSide, maxCandidateSide));
    if (!image.hasValue()) {
        Q_EMIT errorOccurred(tr("This file is no picture QPhoneManager can read. Choose a JPEG or "
                                "PNG picture."));
        return false;
    }
    m_candidate = image.value();
    m_candidateSize = m_candidate.size();
    m_candidateUrl =
        QStringLiteral("image://qpm/candidate/%1").arg(m_photos->setCandidate(m_candidate));
    Q_EMIT candidateChanged();
    return true;
}

bool ContactEditor::applyCrop(int x, int y, int side)
{
    const QRect area = QRect(x, y, side, side).intersected(m_candidate.rect());
    if (m_candidate.isNull() || area.width() < smallestPhotoSide ||
        area.height() < smallestPhotoSide) {
        Q_EMIT errorOccurred(tr("Choose a larger part of the picture."));
        return false;
    }
    const QImage square = m_candidate.copy(area);
    int edge = std::min({maxContactPhotoSide, area.width(), area.height()});
    std::optional<QByteArray> photo;
    QImage scaled;
    while (!photo && edge >= smallestPhotoSide) {
        scaled = square.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        for (int quality = firstJpegQuality; quality >= lowestJpegQuality;
             quality -= jpegQualityStep) {
            const std::optional<QByteArray> encoded = encodeJpeg(scaled, quality);
            if (encoded && encoded->size() <= maxPhotoBytesForWrite) {
                photo = encoded;
                break;
            }
        }
        edge = edge * 3 / 4;
    }
    if (!photo) {
        Q_EMIT errorOccurred(tr("The picture cannot be made small enough for the phone. Choose "
                                "another picture."));
        return false;
    }
    m_contact.photo = *photo;
    m_photoChanged = true;
    m_photoUrl = QStringLiteral("image://qpm/draft/%1").arg(m_photos->setDraft(scaled));
    dropCandidate();
    Q_EMIT photoChanged();
    Q_EMIT fieldsChanged();
    Q_EMIT canSaveChanged();
    return true;
}

void ContactEditor::clearPhoto()
{
    if (m_contact.photo.isEmpty())
        return;
    m_contact.photo.clear();
    m_photoChanged = true;
    m_photoUrl.clear();
    Q_EMIT photoChanged();
    Q_EMIT fieldsChanged();
    Q_EMIT canSaveChanged();
}

void ContactEditor::dropCandidate()
{
    if (m_candidate.isNull() && m_candidateUrl.isEmpty())
        return;
    m_candidate = QImage();
    m_candidateSize = QSize();
    m_candidateUrl.clear();
    m_photos->setCandidate(QImage());
    Q_EMIT candidateChanged();
}

} // namespace qpm::ui
