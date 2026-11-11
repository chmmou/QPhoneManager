#include "ui/contacts/ContactsArea.h"

#include "core/codec/VCardInterchange.h"
#include "core/store/ProfileStore.h"
#include "ui/contacts/ContactTypes.h"
#include "ui/contacts/PhoneImageProvider.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrentRun>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::Contact;

QString accountTitleOf(const core::codec::PbAccount &account)
{
    const bool phoneAccount = account.name.isEmpty() || account.name == QLatin1StringView("null");
    if (phoneAccount)
        return ContactsArea::tr("Phone");
    return account.title.isEmpty() ? account.name
                                   : ContactsArea::tr("%1 (%2)").arg(account.title, account.name);
}

Result<QList<Contact>> readContactFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot open the file"));
    if (file.size() > core::codec::maxInterchangeBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("File of %1 bytes is too large").arg(file.size()));
    }
    const QByteArray data = file.read(core::codec::maxInterchangeBytes + 1);
    // The content decides the format, whatever the file is called.
    if (data.toUpper().contains("BEGIN:VCARD"))
        return core::codec::readVCards(data);
    return core::codec::readContactsCsv(data);
}

Result<int> writeContactFile(const QString &path, const QList<Contact> &contacts, bool asTable,
                             bool german)
{
    const QByteArray data =
        asTable
            ? core::codec::writeContactsCsv(contacts, german ? core::codec::CsvLanguage::German
                                                             : core::codec::CsvLanguage::English)
            : core::codec::writeVCards(contacts);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot write the file"));
    return int(contacts.size());
}

} // namespace

ContactsArea::ContactsArea(core::protocol::PhoneConnection *connection,
                           std::shared_ptr<PhotoSource> photos, QObject *parent)
    : QObject(parent), m_service(new core::service::ContactsService(connection, this)),
      m_photos(std::move(photos)), m_list(new ContactListModel(this)),
      m_detail(new ContactDetailModel(this)),
      m_editor(new ContactEditor(m_service, m_photos, this)),
      m_duplicates(new DuplicateListModel(this))
{
    connect(m_service, &core::service::ContactsService::contactsChanged, this,
            &ContactsArea::reload);
    connect(m_editor, &ContactEditor::errorOccurred, this, &ContactsArea::errorOccurred);
    connect(m_editor, &ContactEditor::saved, this, [this](int luid) { setCurrentLuid(luid); });
}

void ContactsArea::setCurrentLuid(int luid)
{
    if (luid == m_currentLuid)
        return;
    m_currentLuid = luid;
    showCurrent();
    Q_EMIT currentLuidChanged();
}

void ContactsArea::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    applySearch();
    Q_EMIT searchTextChanged();
}

QStringList ContactsArea::groupNames() const
{
    QStringList names;
    for (const core::codec::PbGroup &group : m_metadata.groups) {
        if (!names.contains(group.title))
            names.append(group.title);
    }
    return names;
}

QStringList ContactsArea::accountNames() const
{
    QStringList names;
    for (const core::codec::PbAccount &account : m_metadata.accounts)
        names.append(accountTitleOf(account));
    return names;
}

QStringList ContactsArea::accountKeys() const
{
    QStringList keys;
    for (const core::codec::PbAccount &account : m_metadata.accounts)
        keys.append(account.type + u';' + account.name);
    return keys;
}

int ContactsArea::defaultAccountIndex() const
{
    for (qsizetype i = 0; i < m_metadata.accounts.size(); ++i) {
        if (m_metadata.accounts.at(i).isDefault)
            return int(i);
    }
    return -1;
}

void ContactsArea::setProfileStore(core::store::ProfileStore *profile)
{
    m_store = profile ? std::make_shared<core::store::ContactStore>(profile) : nullptr;
    m_photos->setBlobDirectory(
        profile ? QDir(profile->directory()).filePath(QStringLiteral("blobs")) : QString());
    m_service->setStore(m_store);
    m_editor->setStore(m_store);
    m_editor->setConnected(canEdit());
    m_currentLuid = 0;
    m_duplicates->setEntries({});
    reload();
    Q_EMIT currentLuidChanged();
    Q_EMIT hasPhoneChanged();
    Q_EMIT canEditChanged();
}

void ContactsArea::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    m_editor->setConnected(canEdit());
    Q_EMIT canEditChanged();
}

QFuture<Result<core::store::ContactChanges>> ContactsArea::sync()
{
    return m_service->sync();
}

void ContactsArea::reload()
{
    if (!m_store) {
        m_metadata = {};
        m_list->setContacts({});
        m_detail->clear();
        Q_EMIT metadataChanged();
        return;
    }
    const Result<QList<core::store::ContactSummary>> summaries = m_store->summaries();
    if (!summaries.hasValue())
        report(summaries.error());
    m_list->setContacts(summaries.hasValue() ? summaries.value()
                                             : QList<core::store::ContactSummary>());
    const Result<core::codec::PbMetadata> metadata = m_store->metadata();
    m_metadata = metadata.hasValue() ? metadata.value() : core::codec::PbMetadata();
    applySearch();
    showCurrent();
    Q_EMIT metadataChanged();
}

void ContactsArea::showCurrent()
{
    if (!m_store || m_currentLuid <= 0) {
        m_detail->clear();
        return;
    }
    const Result<Contact> contact = m_store->contact(quint32(m_currentLuid));
    if (!contact.hasValue()) {
        m_detail->clear();
        return;
    }
    QString photoUrl;
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->luidAt(row) == m_currentLuid) {
            photoUrl = m_list->data(m_list->index(row), ContactListModel::PhotoUrlRole).toString();
            break;
        }
    }
    QString title;
    for (const core::codec::PbAccount &account : std::as_const(m_metadata.accounts)) {
        if (account.type == contact.value().accountType &&
            account.name == contact.value().accountName) {
            title = account.title;
        }
    }
    m_detail->setContact(contact.value(), photoUrl, title);
}

void ContactsArea::applySearch()
{
    if (!m_store || m_searchText.trimmed().isEmpty()) {
        m_list->setSearchMatches(std::nullopt);
        return;
    }
    const Result<QSet<quint32>> matches = m_store->search(m_searchText);
    if (!matches.hasValue()) {
        report(matches.error());
        m_list->setSearchMatches(std::nullopt);
        return;
    }
    m_list->setSearchMatches(matches.value());
}

void ContactsArea::report(const Error &error)
{
    const QString message = ContactTypes::errorText(error);
    if (!message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void ContactsArea::beginWork()
{
    if (m_busy++ == 0)
        Q_EMIT busyChanged();
}

void ContactsArea::endWork()
{
    if (m_busy > 0 && --m_busy == 0)
        Q_EMIT busyChanged();
}

bool ContactsArea::removeContacts(const QList<int> &luids)
{
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    QList<quint32> wanted;
    for (const int luid : luids) {
        if (luid > 0)
            wanted.append(quint32(luid));
    }
    if (wanted.isEmpty())
        return false;
    beginWork();
    m_service->remove(wanted).then(this, [this, wanted](const Result<void> &removed) {
        endWork();
        if (!removed.hasValue()) {
            report(removed.error());
            return;
        }
        if (wanted.contains(quint32(m_currentLuid)))
            setCurrentLuid(0);
    });
    return true;
}

bool ContactsArea::exportContacts(const QUrl &url, bool asTable, const QList<int> &luids)
{
    if (!m_store) {
        report(Error(ErrorCode::InvalidInput, QStringLiteral("No phone chosen")));
        return false;
    }
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    QSet<quint32> wanted;
    if (luids.isEmpty()) {
        const QList<quint32> shown = m_list->shownLuids();
        wanted = QSet<quint32>(shown.cbegin(), shown.cend());
    } else {
        for (const int luid : luids)
            wanted.insert(quint32(luid));
    }
    const Result<QList<Contact>> all = m_store->allContacts();
    if (!all.hasValue()) {
        report(all.error());
        return false;
    }
    QList<Contact> contacts;
    for (const Contact &contact : all.value()) {
        if (wanted.contains(contact.luid))
            contacts.append(contact);
    }
    const bool german = QLocale().language() == QLocale::German;
    const QString path = url.toLocalFile();
    beginWork();
    QtConcurrent::run([path, contacts, asTable, german]() {
        return writeContactFile(path, contacts, asTable, german);
    }).then(this, [this](const Result<int> &written) {
        endWork();
        if (!written.hasValue()) {
            Q_EMIT errorOccurred(tr("The file cannot be written. Choose another folder."));
            return;
        }
        Q_EMIT infoMessage(tr("%n contact(s) exported.", nullptr, written.value()));
    });
    return true;
}

bool ContactsArea::importContacts(const QUrl &url)
{
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    const QString path = url.toLocalFile();
    beginWork();
    QtConcurrent::run([path]() {
        return readContactFile(path);
    }).then(this, [this](const Result<QList<Contact>> &read) {
        if (!read.hasValue()) {
            endWork();
            Q_EMIT errorOccurred(
                read.error().code() == ErrorCode::IoFailure
                    ? tr("The file cannot be opened. Check that it exists.")
                    : tr("The file holds no contacts QPhoneManager can read. Choose a vCard "
                         "file or a table saved as CSV."));
            return;
        }
        if (read.value().isEmpty()) {
            endWork();
            Q_EMIT errorOccurred(tr("The file holds no contacts with a name, number, mail "
                                    "address or company."));
            return;
        }
        m_service->importContacts(read.value())
            .then(this, [this](const Result<core::service::ImportOutcome> &imported) {
                endWork();
                if (!imported.hasValue()) {
                    report(imported.error());
                    return;
                }
                QString message = tr("%n contact(s) imported.", nullptr, imported.value().created);
                if (imported.value().skipped > 0) {
                    message += u' ' + tr("%n contact(s) were left out, because the phone "
                                         "does not take them.",
                                         nullptr, imported.value().skipped);
                }
                Q_EMIT infoMessage(message);
            });
    });
    return true;
}

bool ContactsArea::call(const QString &number)
{
    if (core::service::ContactsService::dialableNumber(number).isEmpty()) {
        Q_EMIT errorOccurred(tr("This is no phone number the phone can call."));
        return false;
    }
    if (!m_connected) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    m_service->dial(number).then(this, [this, number](const Result<void> &dialled) {
        if (!dialled.hasValue()) {
            report(dialled.error());
            return;
        }
        Q_EMIT infoMessage(tr("The phone calls %1.").arg(number));
    });
    return true;
}

bool ContactsArea::writeMessage(const QString &number)
{
    const QString dialable = core::service::ContactsService::dialableNumber(number);
    if (dialable.isEmpty()) {
        Q_EMIT errorOccurred(tr("This is no phone number a message can go to."));
        return false;
    }
    Q_EMIT messageRequested(dialable);
    return true;
}

bool ContactsArea::findDuplicates()
{
    if (!m_store) {
        report(Error(ErrorCode::InvalidInput, QStringLiteral("No phone chosen")));
        return false;
    }
    const Result<QList<Contact>> all = m_store->allContacts();
    if (!all.hasValue()) {
        report(all.error());
        return false;
    }
    const QList<Contact> contacts = all.value();
    beginWork();
    QtConcurrent::run([contacts]() {
        return core::service::ContactsService::findDuplicates(contacts);
    }).then(this, [this](const QList<core::service::DuplicatePair> &pairs) {
        endWork();
        QList<DuplicateListModel::Entry> entries;
        for (const core::service::DuplicatePair &pair : pairs)
            entries.append(
                {pair, m_list->nameOf(int(pair.first)), m_list->nameOf(int(pair.second))});
        m_duplicates->setEntries(entries);
        Q_EMIT duplicatesFound(int(entries.size()));
    });
    return true;
}

bool ContactsArea::mergeDuplicate(int row)
{
    const core::service::DuplicatePair pair = m_duplicates->pairAt(row);
    if (pair.first == 0 || pair.second == 0)
        return false;
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    beginWork();
    m_service->merge(pair.first, pair.second)
        .then(this, [this, row, pair](const Result<quint32> &merged) {
            endWork();
            if (!merged.hasValue()) {
                report(merged.error());
                return;
            }
            m_duplicates->removeContacts(row, {pair.second});
            if (m_currentLuid == int(pair.second))
                setCurrentLuid(int(merged.value()));
            Q_EMIT infoMessage(tr("The two contacts are merged."));
        });
    return true;
}

void ContactsArea::ignoreDuplicate(int row)
{
    m_duplicates->removeContacts(row, {});
}

bool ContactsArea::setDefaultAccount(int index)
{
    if (index < 0 || index >= m_metadata.accounts.size())
        return false;
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    const core::codec::PbAccount account = m_metadata.accounts.at(index);
    beginWork();
    m_service->setDefaultAccount(account.type, account.name)
        .then(this, [this, account](const Result<void> &done) {
            endWork();
            if (!done.hasValue()) {
                report(done.error());
                return;
            }
            Q_EMIT infoMessage(tr("New contacts now go to %1.").arg(accountTitleOf(account)));
        });
    return true;
}

} // namespace qpm::ui
