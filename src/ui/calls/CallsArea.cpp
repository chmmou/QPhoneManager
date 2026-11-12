#include "ui/calls/CallsArea.h"

#include "core/Logging.h"
#include "core/service/ContactsService.h"
#include "core/store/ContactStore.h"
#include "core/store/ProfileStore.h"
#include "ui/PhoneNumberText.h"

#include <QLocale>
#include <QSaveFile>
#include <QtConcurrentRun>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::CallEntry;

QString errorText(const Error &error)
{
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::ConnectionClosed:
    case ErrorCode::Timeout:
        return CallsArea::tr("The phone is not connected. Connect it, then try again.");
    case ErrorCode::PermissionDenied:
    case ErrorCode::AccessDenied:
        return CallsArea::tr("The app on the phone may not access the call log. Allow it in the "
                             "Android settings.");
    case ErrorCode::NotFound:
        return CallsArea::tr("This entry is no longer on the phone. Sync to update the list.");
    case ErrorCode::LimitExceeded:
        return CallsArea::tr("The call log on the phone is too long to read. Delete old calls on "
                             "the phone, then sync again.");
    case ErrorCode::IoFailure:
        return CallsArea::tr("The call log cannot be saved on this computer. Check the storage "
                             "folder in the settings.");
    case ErrorCode::NotSupported:
        return CallsArea::tr("The phone does not support this. Update MyPhoneExplorer Client on "
                             "the phone.");
    case ErrorCode::InvalidInput:
        return CallsArea::tr("Choose a phone first.");
    case ErrorCode::WrongPin:
    case ErrorCode::AlreadyConnected:
    case ErrorCode::WifiDisabled:
    case ErrorCode::Rejected:
    case ErrorCode::ProtocolError:
        break;
    }
    return CallsArea::tr("The phone did not accept the request. Sync, then try again.");
}

QString callErrorText(const Error &error)
{
    switch (error.code()) {
    case ErrorCode::PermissionDenied:
    case ErrorCode::AccessDenied:
        return CallsArea::tr("The app on the phone may not place calls. Allow it in the Android "
                             "settings.");
    case ErrorCode::Rejected:
        return CallsArea::tr("The phone did not place the call. Check the number, then try "
                             "again.");
    default:
        break;
    }
    return errorText(error);
}

QString removeErrorText(const Error &error)
{
    if (error.code() == ErrorCode::PermissionDenied || error.code() == ErrorCode::AccessDenied) {
        return CallsArea::tr("The app on the phone may not change the call log. Delete the call "
                             "on the phone itself.");
    }
    return errorText(error);
}

Result<int> writeCallFile(const QString &path, const QList<CallEntry> &entries, bool german)
{
    const QByteArray data = core::codec::writeCallsCsv(
        entries, german ? core::codec::CsvLanguage::German : core::codec::CsvLanguage::English);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot write the file"));
    return int(entries.size());
}

} // namespace

CallsArea::CallsArea(core::protocol::PhoneConnection *connection,
                     core::service::ContactsService *dialer, QObject *parent)
    : QObject(parent), m_service(new core::service::CallLogService(connection, this)),
      m_dialer(dialer), m_list(new CallListModel(this))
{
    Q_ASSERT(dialer);
    connect(m_service, &core::service::CallLogService::callsChanged, this, &CallsArea::reload);
}

void CallsArea::setCurrentId(int id)
{
    if (id == m_currentId)
        return;
    m_currentId = id;
    showCurrent();
    Q_EMIT currentIdChanged();
}

void CallsArea::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    m_list->setSearchText(text);
    Q_EMIT searchTextChanged();
}

QString CallsArea::currentTitle() const
{
    return m_current ? m_list->titleOf(*m_current) : QString();
}

QString CallsArea::currentNumber() const
{
    return m_current ? PhoneNumberText::display(m_current->number) : QString();
}

CallListModel::Kind CallsArea::currentKind() const
{
    return m_current ? CallListModel::kindOf(m_current->direction) : CallListModel::Kind::All;
}

QString CallsArea::currentKindText() const
{
    return m_current ? CallListModel::kindText(m_current->direction) : QString();
}

QString CallsArea::currentTimeText() const
{
    if (!m_current)
        return {};
    const QLocale locale;
    return locale.toString(m_current->start.date(), QLocale::LongFormat) + u' ' +
           locale.toString(m_current->start.time(), QLocale::ShortFormat);
}

QString CallsArea::currentDurationText() const
{
    if (!m_current || m_current->direction == core::codec::CallDirection::Missed)
        return {};
    return CallListModel::durationText(m_current->durationSeconds);
}

QString CallsArea::currentSimText() const
{
    if (!m_current || m_current->simSlot < 0)
        return {};
    return tr("SIM %1").arg(m_current->simSlot + 1);
}

bool CallsArea::currentInContacts() const
{
    return m_current && !m_list->contactNameFor(m_current->number).isEmpty();
}

bool CallsArea::currentCallable() const
{
    return m_current &&
           !core::service::ContactsService::dialableNumber(m_current->number).isEmpty();
}

void CallsArea::setProfileStore(core::store::ProfileStore *profile)
{
    m_store = profile ? std::make_shared<core::store::CallStore>(profile) : nullptr;
    m_contacts = profile ? std::make_shared<core::store::ContactStore>(profile) : nullptr;
    m_service->setStore(m_store);
    m_currentId = 0;
    reload();
    Q_EMIT currentIdChanged();
    Q_EMIT hasPhoneChanged();
    Q_EMIT canEditChanged();
}

void CallsArea::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    Q_EMIT canEditChanged();
}

void CallsArea::refreshNames()
{
    if (!m_contacts) {
        m_list->setContactNames({});
        showCurrent();
        return;
    }
    const Result<QHash<QString, QString>> names = m_contacts->namesByNumber();
    if (!names.hasValue()) {
        // The list stays usable with the names the phone keeps with each call.
        qCWarning(core::lcUi) << "Contact names for the call log not read:"
                              << names.error().message();
    }
    m_list->setContactNames(names.hasValue() ? names.value() : QHash<QString, QString>());
    showCurrent();
}

QFuture<Result<core::store::CallChanges>> CallsArea::sync()
{
    return m_service->sync();
}

void CallsArea::reload()
{
    if (!m_store) {
        m_list->setCalls({});
        refreshNames();
        return;
    }
    const Result<QList<CallEntry>> calls = m_store->calls();
    if (!calls.hasValue())
        report(calls.error());
    m_list->setCalls(calls.hasValue() ? calls.value() : QList<CallEntry>());
    refreshNames();
}

void CallsArea::showCurrent()
{
    m_current = m_currentId > 0 ? m_list->entry(quint32(m_currentId)) : std::nullopt;
    Q_EMIT currentChanged();
}

void CallsArea::report(const Error &error)
{
    const QString message = errorText(error);
    if (!message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void CallsArea::beginWork()
{
    if (m_busy++ == 0)
        Q_EMIT busyChanged();
}

void CallsArea::endWork()
{
    if (m_busy > 0 && --m_busy == 0)
        Q_EMIT busyChanged();
}

bool CallsArea::removeCalls(const QList<int> &ids)
{
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    QList<quint32> wanted;
    for (const int id : ids) {
        if (id > 0)
            wanted.append(quint32(id));
    }
    if (wanted.isEmpty())
        return false;
    beginWork();
    m_service->remove(wanted).then(this, [this, wanted](const Result<void> &removed) {
        endWork();
        if (m_currentId > 0 && !m_list->entry(quint32(m_currentId)))
            setCurrentId(0);
        if (!removed.hasValue()) {
            const QString message = removeErrorText(removed.error());
            if (!message.isEmpty())
                Q_EMIT errorOccurred(message);
            return;
        }
        Q_EMIT infoMessage(tr("%n call(s) deleted.", nullptr, int(wanted.size())));
    });
    return true;
}

QList<int> CallsArea::shownIds() const
{
    const QList<quint32> shown = m_list->shownIds();
    QList<int> ids;
    ids.reserve(shown.size());
    for (const quint32 id : shown)
        ids.append(int(id));
    return ids;
}

bool CallsArea::exportCalls(const QUrl &url)
{
    if (!m_store) {
        report(Error(ErrorCode::InvalidInput, QStringLiteral("No phone chosen")));
        return false;
    }
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    QList<CallEntry> entries = m_list->shownEntries();
    for (CallEntry &entry : entries) {
        const QString contact = m_list->contactNameFor(entry.number);
        if (!contact.isEmpty())
            entry.name = contact;
    }
    const bool german = QLocale().language() == QLocale::German;
    const QString path = url.toLocalFile();
    beginWork();
    QtConcurrent::run([path, entries, german]() {
        return writeCallFile(path, entries, german);
    }).then(this, [this](const Result<int> &written) {
        endWork();
        if (!written.hasValue()) {
            Q_EMIT errorOccurred(tr("The file cannot be written. Choose another folder."));
            return;
        }
        Q_EMIT infoMessage(tr("%n call(s) exported.", nullptr, written.value()));
    });
    return true;
}

bool CallsArea::call(const QString &number)
{
    if (core::service::ContactsService::dialableNumber(number).isEmpty()) {
        Q_EMIT errorOccurred(tr("This is no phone number the phone can call."));
        return false;
    }
    if (!m_connected) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    m_dialer->dial(number).then(this, [this, number](const Result<void> &dialled) {
        if (!dialled.hasValue()) {
            const QString message = callErrorText(dialled.error());
            if (!message.isEmpty())
                Q_EMIT errorOccurred(message);
            return;
        }
        Q_EMIT infoMessage(tr("The phone calls %1.").arg(number));
    });
    return true;
}

bool CallsArea::writeMessage(const QString &number)
{
    const QString dialable = core::service::ContactsService::dialableNumber(number);
    if (dialable.isEmpty()) {
        Q_EMIT errorOccurred(tr("This is no phone number a message can go to."));
        return false;
    }
    Q_EMIT messageRequested(dialable);
    return true;
}

bool CallsArea::addCurrentToContacts()
{
    if (!m_current)
        return false;
    if (core::service::ContactsService::dialableNumber(m_current->number).isEmpty()) {
        Q_EMIT errorOccurred(tr("This entry has no number that can be saved."));
        return false;
    }
    if (!m_connected) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    Q_EMIT newContactRequested(m_current->name.trimmed(), m_current->number.trimmed());
    return true;
}

} // namespace qpm::ui
