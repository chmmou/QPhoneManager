#include "core/service/NotesService.h"

#include "core/protocol/ObexClient.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/OrganizerSession.h"

#include <QPointer>

namespace qpm::core::service {

namespace {

using codec::Note;
using protocol::ObexClient;
using store::OrganizerChanges;
using store::OrganizerStore;
using sync::IrmcDomain;
using sync::RawObject;

constexpr IrmcDomain notes = IrmcDomain::Notes;

} // namespace

NotesService::NotesService(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
}

void NotesService::setStore(const std::shared_ptr<OrganizerStore> &store)
{
    m_store = store;
}

QFuture<Result<OrganizerChanges>> NotesService::sync()
{
    if (m_store.expired())
        return organizer::failed<OrganizerChanges>(organizer::noStore());
    if (!m_connection->session())
        return organizer::failed<OrganizerChanges>(organizer::notConnected());
    return m_connection->withObex<OrganizerChanges>(
        ObexClient::Target::IrmcSync, [this](ObexClient &obex) {
            auto pending = organizer::promised<OrganizerChanges>();
            const organizer::Done<OrganizerChanges> done = pending.second;
            organizer::pull(obex, notes, m_store, this,
                            [this, done](const Result<OrganizerChanges> &pulled) {
                                if (pulled.hasValue())
                                    Q_EMIT notesChanged();
                                done(pulled);
                            });
            return pending.first;
        });
}

QFuture<Result<NoteSaveOutcome>> NotesService::save(const Note &edited, SaveMode mode)
{
    const std::shared_ptr<OrganizerStore> store = m_store.lock();
    if (!store)
        return organizer::failed<NoteSaveOutcome>(organizer::noStore());
    if (edited.body.trimmed().isEmpty()) {
        return organizer::failed<NoteSaveOutcome>(
            Error(ErrorCode::InvalidInput, QStringLiteral("A note needs text")));
    }
    if (!m_connection->session())
        return organizer::failed<NoteSaveOutcome>(organizer::notConnected());
    const Result<QList<codec::OrganizerCategory>> known = store->noteCategories();
    const QByteArray body = codec::encodePhoneNote(
        edited, organizer::categoryColors(edited.categories,
                                          known.hasValue() ? known.value()
                                                           : QList<codec::OrganizerCategory>()));
    std::optional<Note> original;
    if (edited.luid != 0) {
        const Result<Note> stored = store->note(edited.luid);
        if (!stored.hasValue())
            return organizer::failed<NoteSaveOutcome>(stored.error());
        original = stored.value();
    }
    return m_connection->withObex<NoteSaveOutcome>(
        ObexClient::Target::IrmcSync, [this, original, body, mode](ObexClient &obex) {
            auto pending = organizer::promised<NoteSaveOutcome>();
            const organizer::Done<NoteSaveOutcome> done = pending.second;
            QPointer<ObexClient> client = &obex;
            // The phone adds categories it did not know yet, so its list is read again.
            const auto keep = [this, client, done](const Result<RawObject> &fresh) {
                if (!fresh.hasValue()) {
                    done(fresh.error());
                    return;
                }
                const std::shared_ptr<OrganizerStore> current = m_store.lock();
                if (!current) {
                    done(organizer::storeGone());
                    return;
                }
                if (const Result<void> kept = current->storeObject(notes, fresh.value());
                    !kept.hasValue()) {
                    done(kept.error());
                    return;
                }
                if (!client) {
                    done(organizer::clientGone());
                    return;
                }
                const quint32 luid = fresh.value().luid;
                organizer::readMetadata(*client, notes, m_store, this,
                                        [this, done, luid](const Result<void> &listed) {
                                            if (!listed.hasValue()) {
                                                done(listed.error());
                                                return;
                                            }
                                            Q_EMIT notesChanged();
                                            NoteSaveOutcome outcome;
                                            outcome.luid = luid;
                                            done(outcome);
                                        });
            };
            organizer::loadArea(obex, notes, this, [=, this](const Result<void> &loaded) {
                if (!loaded.hasValue() || !client) {
                    done(loaded.hasValue() ? organizer::clientGone() : loaded.error());
                    return;
                }
                if (!original) {
                    organizer::createObject(*client, notes, body, this, keep);
                    return;
                }
                const quint32 luid = original->luid;
                organizer::readObject(*client, notes, luid, this, [=, this](const Result<RawObject> &read) {
                    if (!read.hasValue()) {
                        done(read.error());
                        return;
                    }
                    const Result<Note> phone = codec::parsePhoneNote(read.value().data);
                    if (!phone.hasValue()) {
                        done(phone.error());
                        return;
                    }
                    if (mode == SaveMode::CheckConflict &&
                        !codec::sameNoteContent(phone.value(), *original)) {
                        NoteSaveOutcome outcome;
                        outcome.conflict = true;
                        outcome.phoneVersion = phone.value();
                        outcome.phoneVersion.luid = luid;
                        outcome.phoneObject = read.value();
                        done(outcome);
                        return;
                    }
                    if (!client) {
                        done(organizer::clientGone());
                        return;
                    }
                    organizer::replaceObject(*client, notes, luid, body, this, keep);
                });
            });
            return pending.first;
        });
}

Result<void> NotesService::keepPhoneVersion(const RawObject &object)
{
    const std::shared_ptr<OrganizerStore> store = m_store.lock();
    if (!store)
        return organizer::noStore();
    const Result<void> stored = store->storeObject(notes, object);
    if (stored.hasValue())
        Q_EMIT notesChanged();
    return stored;
}

QFuture<Result<void>> NotesService::remove(const QList<quint32> &luids)
{
    if (m_store.expired())
        return organizer::failed<void>(organizer::noStore());
    if (!m_connection->session())
        return organizer::failed<void>(organizer::notConnected());
    if (luids.isEmpty())
        return QtFuture::makeReadyValueFuture(Result<void>());
    return m_connection->withObex<void>(ObexClient::Target::IrmcSync, [this, luids](ObexClient &obex) {
        auto pending = organizer::promised<void>();
        const organizer::Done<void> done = pending.second;
        QPointer<ObexClient> client = &obex;
        organizer::loadArea(obex, notes, this, [this, client, luids, done](const Result<void> &loaded) {
            if (!loaded.hasValue() || !client) {
                done(loaded.hasValue() ? Result<void>(organizer::clientGone()) : loaded);
                return;
            }
            organizer::removeObjects(*client, notes, luids, m_store, this,
                                     [this]() { Q_EMIT notesChanged(); }, done);
        });
        return pending.first;
    });
}

QFuture<Result<void>> NotesService::changeCategories(const QList<codec::CategoryChange> &changes)
{
    if (m_store.expired())
        return organizer::failed<void>(organizer::noStore());
    if (!m_connection->session())
        return organizer::failed<void>(organizer::notConnected());
    const QByteArray document = codec::encodeCategoryChanges(changes, true);
    return m_connection->withObex<void>(
        ObexClient::Target::IrmcSync, [this, document](ObexClient &obex) {
            auto pending = organizer::promised<void>();
            const organizer::Done<void> done = pending.second;
            organizer::writeMetadata(obex, notes, document, m_store, this,
                                     [this, done](const Result<void> &written) {
                                         if (written.hasValue())
                                             Q_EMIT notesChanged();
                                         done(written);
                                     });
            return pending.first;
        });
}

} // namespace qpm::core::service
