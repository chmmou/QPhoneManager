#include "core/service/CalendarService.h"

#include "core/protocol/ObexClient.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/OrganizerSession.h"

#include <QPointer>

#include <functional>

namespace qpm::core::service {

namespace {

using codec::CalendarEvent;
using codec::CalendarInfo;
using codec::CalendarTask;
using protocol::ObexClient;
using protocol::ObexResponse;
using store::OrganizerChanges;
using store::OrganizerStore;
using sync::IrmcDomain;
using sync::RawObject;

constexpr IrmcDomain calendar = IrmcDomain::Calendar;

std::optional<CalendarInfo> findCalendar(const QList<CalendarInfo> &calendars, const QString &url,
                                         const QString &title)
{
    for (const CalendarInfo &info : calendars) {
        if (info.url == url && info.title == title)
            return info;
    }
    return std::nullopt;
}

bool skippable(const Error &error)
{
    switch (error.code()) {
    case ErrorCode::Rejected:
    case ErrorCode::LimitExceeded:
    case ErrorCode::ProtocolError:
    case ErrorCode::InvalidInput:
        return true;
    default:
        return false;
    }
}

} // namespace

CalendarService::CalendarService(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
}

void CalendarService::setStore(const std::shared_ptr<OrganizerStore> &store)
{
    m_store = store;
}

QFuture<Result<OrganizerChanges>> CalendarService::sync()
{
    if (m_store.expired())
        return organizer::failed<OrganizerChanges>(organizer::noStore());
    if (!m_connection->session())
        return organizer::failed<OrganizerChanges>(organizer::notConnected());
    return m_connection->withObex<OrganizerChanges>(
        ObexClient::Target::IrmcSync, [this](ObexClient &obex) {
            auto pending = organizer::promised<OrganizerChanges>();
            const organizer::Done<OrganizerChanges> done = pending.second;
            organizer::pull(obex, calendar, m_store, this,
                            [this, done](const Result<OrganizerChanges> &pulled) {
                                if (pulled.hasValue())
                                    Q_EMIT calendarChanged();
                                done(pulled);
                            });
            return pending.first;
        });
}

QFuture<Result<EventSaveOutcome>> CalendarService::saveEvent(const CalendarEvent &edited,
                                                             SaveMode mode)
{
    const std::shared_ptr<OrganizerStore> store = m_store.lock();
    if (!store)
        return organizer::failed<EventSaveOutcome>(organizer::noStore());
    if (!edited.start.isValid() || edited.summary.trimmed().isEmpty()) {
        return organizer::failed<EventSaveOutcome>(
            Error(ErrorCode::InvalidInput, QStringLiteral("An event needs a title and a start")));
    }
    if (!m_connection->session())
        return organizer::failed<EventSaveOutcome>(organizer::notConnected());

    if (edited.luid == 0) {
        const Result<codec::CalMetadata> metadata = store->calendarMetadata();
        if (!metadata.hasValue())
            return organizer::failed<EventSaveOutcome>(metadata.error());
        const std::optional<CalendarInfo> target =
            findCalendar(metadata.value().calendars, edited.calendarUrl, edited.calendarTitle);
        if (!target || !target->isWritable()) {
            return organizer::failed<EventSaveOutcome>(
                Error(ErrorCode::InvalidInput,
                      QStringLiteral("The calendar of a new event must be one the phone may "
                                     "write to")));
        }
        if (target->selected || metadata.value().filterDisabled)
            return createEvent(edited, *target);

        // The phone takes a changed choice of calendars only in a new session.
        QList<CalendarInfo> selection = metadata.value().calendars;
        for (CalendarInfo &info : selection) {
            if (info.url == target->url && info.title == target->title)
                info.selected = true;
        }
        auto pending = organizer::promised<EventSaveOutcome>();
        const organizer::Done<EventSaveOutcome> done = pending.second;
        const QByteArray document = codec::encodeCalendarSelection(selection);
        m_connection
            ->withObex<bool>(ObexClient::Target::IrmcSync,
                             [this, document](ObexClient &obex) {
                                 auto inner = organizer::promised<bool>();
                                 const organizer::Done<bool> finish = inner.second;
                                 organizer::writeMetadata(
                                     obex, calendar, document, m_store, this,
                                     [finish](const Result<void> &written) {
                                         finish(written.hasValue() ? Result<bool>(true)
                                                                   : Result<bool>(written.error()));
                                     });
                                 return inner.first;
                             })
            .then(this, [this, done, edited, target](const Result<bool> &selected) {
                if (!selected.hasValue()) {
                    done(selected.error());
                    return;
                }
                Q_EMIT calendarChanged();
                createEvent(edited, *target).then(
                    this, [done](const Result<EventSaveOutcome> &created) { done(created); });
            });
        return pending.first;
    }

    const Result<CalendarEvent> stored = store->event(edited.luid);
    if (!stored.hasValue())
        return organizer::failed<EventSaveOutcome>(stored.error());
    const CalendarEvent original = stored.value();
    const QByteArray body = codec::encodePhoneEvent(edited, true);
    return m_connection->withObex<EventSaveOutcome>(
        ObexClient::Target::IrmcSync, [this, original, body, mode](ObexClient &obex) {
            auto pending = organizer::promised<EventSaveOutcome>();
            const organizer::Done<EventSaveOutcome> done = pending.second;
            QPointer<ObexClient> client = &obex;
            const quint32 luid = original.luid;
            organizer::loadArea(obex, calendar, this, [=, this](const Result<void> &loaded) {
                if (!loaded.hasValue() || !client) {
                    done(loaded.hasValue() ? organizer::clientGone() : loaded.error());
                    return;
                }
                organizer::readObject(*client, calendar, luid, this, [=, this](const Result<RawObject> &read) {
                    if (!read.hasValue()) {
                        done(read.error());
                        return;
                    }
                    const Result<CalendarEvent> phone = codec::parsePhoneEvent(read.value().data);
                    if (!phone.hasValue()) {
                        done(phone.error());
                        return;
                    }
                    if (mode == SaveMode::CheckConflict &&
                        !codec::sameEventContent(phone.value(), original)) {
                        EventSaveOutcome outcome;
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
                    organizer::replaceObject(*client, calendar, luid, body, this,
                                             [this, done, luid](const Result<RawObject> &fresh) {
                        if (!fresh.hasValue()) {
                            done(fresh.error());
                            return;
                        }
                        const std::shared_ptr<OrganizerStore> current = m_store.lock();
                        if (!current) {
                            done(organizer::storeGone());
                            return;
                        }
                        if (const Result<void> kept = current->storeObject(calendar, fresh.value());
                            !kept.hasValue()) {
                            done(kept.error());
                            return;
                        }
                        Q_EMIT calendarChanged();
                        EventSaveOutcome outcome;
                        outcome.luid = luid;
                        done(outcome);
                    });
                });
            });
            return pending.first;
        });
}

QFuture<Result<EventSaveOutcome>> CalendarService::createEvent(const CalendarEvent &edited,
                                                               const CalendarInfo &target)
{
    CalendarEvent event = edited;
    event.calendarUrl = target.url;
    event.calendarTitle = target.title;
    const QByteArray body = codec::encodePhoneEvent(event, false);
    return m_connection->withObex<EventSaveOutcome>(
        ObexClient::Target::IrmcSync, [this, body](ObexClient &obex) {
            auto pending = organizer::promised<EventSaveOutcome>();
            const organizer::Done<EventSaveOutcome> done = pending.second;
            QPointer<ObexClient> client = &obex;
            organizer::loadArea(obex, calendar, this, [this, client, body, done](const Result<void> &loaded) {
                if (!loaded.hasValue() || !client) {
                    done(loaded.hasValue() ? organizer::clientGone() : loaded.error());
                    return;
                }
                organizer::createObject(*client, calendar, body, this,
                                        [this, done](const Result<RawObject> &created) {
                    if (!created.hasValue()) {
                        done(created.error());
                        return;
                    }
                    const std::shared_ptr<OrganizerStore> current = m_store.lock();
                    if (!current) {
                        done(organizer::storeGone());
                        return;
                    }
                    if (const Result<void> kept = current->storeObject(calendar, created.value());
                        !kept.hasValue()) {
                        done(kept.error());
                        return;
                    }
                    Q_EMIT calendarChanged();
                    EventSaveOutcome outcome;
                    outcome.luid = created.value().luid;
                    done(outcome);
                });
            });
            return pending.first;
        });
}

QFuture<Result<TaskSaveOutcome>> CalendarService::saveTask(const CalendarTask &edited, SaveMode mode)
{
    const std::shared_ptr<OrganizerStore> store = m_store.lock();
    if (!store)
        return organizer::failed<TaskSaveOutcome>(organizer::noStore());
    if (edited.summary.trimmed().isEmpty()) {
        return organizer::failed<TaskSaveOutcome>(
            Error(ErrorCode::InvalidInput, QStringLiteral("A task needs a title")));
    }
    if (!m_connection->session())
        return organizer::failed<TaskSaveOutcome>(organizer::notConnected());
    const Result<codec::CalMetadata> metadata = store->calendarMetadata();
    const QList<codec::OrganizerCategory> known =
        metadata.hasValue() ? metadata.value().taskCategories : QList<codec::OrganizerCategory>();
    const QByteArray body =
        codec::encodePhoneTask(edited, organizer::categoryColors(edited.categories, known));

    std::optional<CalendarTask> original;
    if (edited.luid != 0) {
        const Result<CalendarTask> stored = store->task(edited.luid);
        if (!stored.hasValue())
            return organizer::failed<TaskSaveOutcome>(stored.error());
        original = stored.value();
    }
    return m_connection->withObex<TaskSaveOutcome>(
        ObexClient::Target::IrmcSync, [this, original, body, mode](ObexClient &obex) {
            auto pending = organizer::promised<TaskSaveOutcome>();
            const organizer::Done<TaskSaveOutcome> done = pending.second;
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
                if (const Result<void> kept = current->storeObject(calendar, fresh.value());
                    !kept.hasValue()) {
                    done(kept.error());
                    return;
                }
                if (!client) {
                    done(organizer::clientGone());
                    return;
                }
                const quint32 luid = fresh.value().luid;
                organizer::readMetadata(*client, calendar, m_store, this,
                                        [this, done, luid](const Result<void> &listed) {
                                            if (!listed.hasValue()) {
                                                done(listed.error());
                                                return;
                                            }
                                            Q_EMIT calendarChanged();
                                            TaskSaveOutcome outcome;
                                            outcome.luid = luid;
                                            done(outcome);
                                        });
            };
            organizer::loadArea(obex, calendar, this, [=, this](const Result<void> &loaded) {
                if (!loaded.hasValue() || !client) {
                    done(loaded.hasValue() ? organizer::clientGone() : loaded.error());
                    return;
                }
                if (!original) {
                    organizer::createObject(*client, calendar, body, this, keep);
                    return;
                }
                const quint32 luid = original->luid;
                organizer::readObject(*client, calendar, luid, this, [=, this](const Result<RawObject> &read) {
                    if (!read.hasValue()) {
                        done(read.error());
                        return;
                    }
                    const Result<CalendarTask> phone = codec::parsePhoneTask(read.value().data);
                    if (!phone.hasValue()) {
                        done(phone.error());
                        return;
                    }
                    if (mode == SaveMode::CheckConflict &&
                        !codec::sameTaskContent(phone.value(), *original)) {
                        TaskSaveOutcome outcome;
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
                    organizer::replaceObject(*client, calendar, luid, body, this, keep);
                });
            });
            return pending.first;
        });
}

Result<void> CalendarService::keepPhoneVersion(const RawObject &object)
{
    const std::shared_ptr<OrganizerStore> store = m_store.lock();
    if (!store)
        return organizer::noStore();
    const Result<void> stored = store->storeObject(calendar, object);
    if (stored.hasValue())
        Q_EMIT calendarChanged();
    return stored;
}

QFuture<Result<void>> CalendarService::remove(const QList<quint32> &luids)
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
        organizer::loadArea(obex, calendar, this, [this, client, luids, done](const Result<void> &loaded) {
            if (!loaded.hasValue() || !client) {
                done(loaded.hasValue() ? Result<void>(organizer::clientGone()) : loaded);
                return;
            }
            organizer::removeObjects(*client, calendar, luids, m_store, this,
                                     [this]() { Q_EMIT calendarChanged(); }, done);
        });
        return pending.first;
    });
}

QFuture<Result<void>> CalendarService::changeTaskCategories(
    const QList<codec::CategoryChange> &changes)
{
    if (m_store.expired())
        return organizer::failed<void>(organizer::noStore());
    if (!m_connection->session())
        return organizer::failed<void>(organizer::notConnected());
    const QByteArray document = codec::encodeCategoryChanges(changes, false);
    return m_connection->withObex<void>(
        ObexClient::Target::IrmcSync, [this, document](ObexClient &obex) {
            auto pending = organizer::promised<void>();
            const organizer::Done<void> done = pending.second;
            organizer::writeMetadata(obex, calendar, document, m_store, this,
                                     [this, done](const Result<void> &written) {
                                         if (written.hasValue())
                                             Q_EMIT calendarChanged();
                                         done(written);
                                     });
            return pending.first;
        });
}

QFuture<Result<void>> CalendarService::setCalendarSelection(const QList<CalendarInfo> &calendars)
{
    if (m_store.expired())
        return organizer::failed<void>(organizer::noStore());
    if (!m_connection->session())
        return organizer::failed<void>(organizer::notConnected());
    const QByteArray document = codec::encodeCalendarSelection(calendars);
    auto pending = organizer::promised<void>();
    const organizer::Done<void> done = pending.second;
    m_connection
        ->withObex<void>(ObexClient::Target::IrmcSync,
                         [this, document](ObexClient &obex) {
                             auto inner = organizer::promised<void>();
                             organizer::writeMetadata(obex, calendar, document, m_store, this,
                                                      inner.second);
                             return inner.first;
                         })
        .then(this, [this, done](const Result<void> &written) {
            if (!written.hasValue()) {
                done(written);
                return;
            }
            // The phone filters by the new choice from the next session on.
            sync().then(this, [done](const Result<OrganizerChanges> &pulled) {
                done(pulled.hasValue() ? Result<void>() : Result<void>(pulled.error()));
            });
        });
    return pending.first;
}

QFuture<Result<OrganizerImportOutcome>>
CalendarService::importItems(const QList<CalendarEvent> &events, const QList<CalendarTask> &tasks,
                             const CalendarInfo &target)
{
    const std::shared_ptr<OrganizerStore> store = m_store.lock();
    if (!store)
        return organizer::failed<OrganizerImportOutcome>(organizer::noStore());
    if (!events.isEmpty() && !target.isWritable()) {
        return organizer::failed<OrganizerImportOutcome>(Error(
            ErrorCode::InvalidInput, QStringLiteral("Events go into a calendar the phone may write to")));
    }
    if (!m_connection->session())
        return organizer::failed<OrganizerImportOutcome>(organizer::notConnected());

    OrganizerImportOutcome counts;
    QList<QByteArray> bodies;
    for (CalendarEvent event : events) {
        if (!event.start.isValid() || event.summary.trimmed().isEmpty()) {
            ++counts.skipped;
            continue;
        }
        event.luid = 0;
        event.calendarUrl = target.url;
        event.calendarTitle = target.title;
        bodies.append(codec::encodePhoneEvent(event, false));
    }
    const Result<codec::CalMetadata> metadata = store->calendarMetadata();
    const QList<codec::OrganizerCategory> known =
        metadata.hasValue() ? metadata.value().taskCategories : QList<codec::OrganizerCategory>();
    for (CalendarTask task : tasks) {
        if (task.summary.trimmed().isEmpty()) {
            ++counts.skipped;
            continue;
        }
        task.luid = 0;
        bodies.append(codec::encodePhoneTask(task, organizer::categoryColors(task.categories, known)));
    }
    const bool selectFirst = !events.isEmpty() && !target.selected && metadata.hasValue() &&
                             !metadata.value().filterDisabled;
    QList<CalendarInfo> selection = metadata.hasValue() ? metadata.value().calendars
                                                        : QList<CalendarInfo>();
    for (CalendarInfo &info : selection) {
        if (info.url == target.url && info.title == target.title)
            info.selected = true;
    }

    auto pending = organizer::promised<OrganizerImportOutcome>();
    const organizer::Done<OrganizerImportOutcome> done = pending.second;
    const auto runImport = [this, bodies, counts, done]() {
        m_connection
            ->withObex<OrganizerImportOutcome>(
                ObexClient::Target::IrmcSync,
                [this, bodies, counts](ObexClient &obex) {
                    struct Job
                    {
                        QPointer<ObexClient> client;
                        QList<QByteArray> pending;
                        OrganizerImportOutcome outcome;
                        std::function<void()> next;
                        organizer::Done<OrganizerImportOutcome> finish;
                    };
                    auto job = std::make_shared<Job>();
                    auto inner = organizer::promised<OrganizerImportOutcome>();
                    job->client = &obex;
                    job->pending = bodies;
                    job->outcome = counts;
                    job->finish = inner.second;
                    // The callback holds the job only weakly, so that the job
                    // is freed once the last request has answered.
                    job->next = [this, weak = std::weak_ptr<Job>(job)]() {
                        const std::shared_ptr<Job> current = weak.lock();
                        if (!current)
                            return;
                        if (!current->client) {
                            current->finish(organizer::clientGone());
                            return;
                        }
                        if (current->pending.isEmpty()) {
                            organizer::pull(*current->client, calendar, m_store, this,
                                            [this, current](const Result<OrganizerChanges> &pulled) {
                                                if (!pulled.hasValue()) {
                                                    current->finish(pulled.error());
                                                    return;
                                                }
                                                Q_EMIT calendarChanged();
                                                current->finish(current->outcome);
                                            });
                            return;
                        }
                        const QByteArray body = current->pending.takeFirst();
                        current->client
                            ->put(QStringLiteral("telecom/cal/luid/.vcs"), body)
                            .then(this, [current](const Result<ObexResponse> &created) {
                                if (created.hasValue()) {
                                    ++current->outcome.created;
                                } else if (skippable(created.error())) {
                                    ++current->outcome.skipped;
                                } else {
                                    current->finish(created.error());
                                    return;
                                }
                                if (current->next)
                                    current->next();
                            });
                    };
                    organizer::loadArea(obex, calendar, this, [job](const Result<void> &loaded) {
                        if (!loaded.hasValue()) {
                            job->finish(loaded.error());
                            return;
                        }
                        job->next();
                    });
                    return inner.first;
                })
            .then(this, [done](const Result<OrganizerImportOutcome> &imported) { done(imported); });
    };
    if (!selectFirst) {
        runImport();
        return pending.first;
    }
    const QByteArray document = codec::encodeCalendarSelection(selection);
    m_connection
        ->withObex<void>(ObexClient::Target::IrmcSync,
                         [this, document](ObexClient &obex) {
                             auto inner = organizer::promised<void>();
                             organizer::writeMetadata(obex, calendar, document, m_store, this,
                                                      inner.second);
                             return inner.first;
                         })
        .then(this, [done, runImport](const Result<void> &selected) {
            if (!selected.hasValue()) {
                done(selected.error());
                return;
            }
            runImport();
        });
    return pending.first;
}

} // namespace qpm::core::service
