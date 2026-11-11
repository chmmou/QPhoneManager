#include "core/service/ContactsService.h"

#include "core/Logging.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/PhoneConnection.h"
#include "core/sync/IrmcSync.h"

#include <QHash>
#include <QPointer>
#include <QPromise>
#include <QSet>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>

namespace qpm::core::service {

namespace {

using codec::Contact;
using codec::ContactValue;
using codec::ContactValueKind;
using protocol::ObexClient;
using protocol::ObexResponse;
using store::ContactChanges;
using store::ContactStore;
using sync::IrmcDomain;
using sync::RawObject;

constexpr std::chrono::seconds objectTimeout{20};
constexpr qsizetype numberKeyDigits = 9;
constexpr qsizetype minimumNumberDigits = 5;
constexpr qsizetype minimumDialDigits = 3;
constexpr qsizetype maximumDialDigits = 32;
constexpr qsizetype nameNeighbours = 10;
constexpr qsizetype minimumSimilarNameLength = 6;
constexpr qsizetype longNameLength = 12;

QString objectPath(quint32 luid)
{
    return QStringLiteral("telecom/pb/luid/%1.vcf").arg(sync::IrmcSync::luidText(luid));
}

QString createPath()
{
    return QStringLiteral("telecom/pb/luid/.vcf");
}

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

Error noStore()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("No phone is chosen"));
}

Error storeGone()
{
    return Error(ErrorCode::Cancelled, QStringLiteral("The phone was switched meanwhile"));
}

Error notConnected()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected"));
}

Error clientGone()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The OBEX session ended"));
}

// Forwards to the store while it exists; IrmcSync may outlive a switch of
// the phone, and then must not touch the store of another phone.
class GuardedStore : public sync::SyncStoreAdapter
{
public:
    explicit GuardedStore(std::weak_ptr<ContactStore> store) : m_store(std::move(store)) {}

    [[nodiscard]] std::optional<quint64> changeCounter(IrmcDomain domain) const override
    {
        const std::shared_ptr<ContactStore> store = m_store.lock();
        return store ? store->changeCounter(domain) : std::nullopt;
    }

    [[nodiscard]] std::optional<QByteArray> objectHash(IrmcDomain domain,
                                                       quint32 luid) const override
    {
        const std::shared_ptr<ContactStore> store = m_store.lock();
        return store ? store->objectHash(domain, luid) : std::nullopt;
    }

    [[nodiscard]] Result<void> apply(IrmcDomain domain, const sync::SyncDelta &delta) override
    {
        const std::shared_ptr<ContactStore> store = m_store.lock();
        if (!store)
            return storeGone();
        return store->apply(domain, delta);
    }

    [[nodiscard]] ContactChanges lastChanges() const
    {
        const std::shared_ptr<ContactStore> store = m_store.lock();
        return store ? store->lastChanges() : ContactChanges();
    }

private:
    std::weak_ptr<ContactStore> m_store;
};

Result<RawObject> objectFrom(const Result<ObexResponse> &response, quint32 luid)
{
    if (!response.hasValue())
        return response.error();
    const Result<QList<RawObject>> objects =
        sync::IrmcSync::splitObjects(response.value().body, IrmcDomain::Contacts);
    if (!objects.hasValue())
        return objects.error();
    if (objects.value().isEmpty()) {
        return Error(
            ErrorCode::ProtocolError,
            QStringLiteral("Contact %1 came without content").arg(sync::IrmcSync::luidText(luid)));
    }
    RawObject object = objects.value().constFirst();
    object.luid = luid;
    return object;
}

QString digitsOf(const QString &text)
{
    QString digits;
    for (const QChar c : text) {
        if (c.isDigit())
            digits.append(c);
    }
    return digits;
}

QString numberKey(const QString &number)
{
    QString digits = digitsOf(number);
    while (digits.startsWith(u'0'))
        digits.remove(0, 1);
    if (digits.size() < minimumNumberDigits)
        return {};
    return digits.right(numberKeyDigits);
}

QString foldedText(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            out.append(c.toLower());
    }
    return out;
}

QString nameKey(const Contact &contact)
{
    QStringList words;
    const QStringList allWords =
        foldedText(codec::displayNameOf(contact)).split(QChar(u' '), Qt::SkipEmptyParts);
    for (const QString &word : allWords) {
        QString letters;
        for (const QChar c : word) {
            if (c.isLetterOrNumber())
                letters.append(c);
        }
        if (!letters.isEmpty())
            words.append(letters);
    }
    words.sort();
    return words.join(u' ');
}

int editDistance(const QString &a, const QString &b)
{
    QList<int> previous(b.size() + 1);
    QList<int> current(b.size() + 1);
    for (qsizetype j = 0; j <= b.size(); ++j)
        previous[j] = int(j);
    for (qsizetype i = 1; i <= a.size(); ++i) {
        current[0] = int(i);
        for (qsizetype j = 1; j <= b.size(); ++j) {
            const int substitution = previous.at(j - 1) + (a.at(i - 1) == b.at(j - 1) ? 0 : 1);
            current[j] = std::min({previous.at(j) + 1, current.at(j - 1) + 1, substitution});
        }
        std::swap(previous, current);
    }
    return previous.at(b.size());
}

bool sameValue(ContactValueKind kind, const ContactValue &a, const ContactValue &b)
{
    switch (kind) {
    case ContactValueKind::Phone: {
        const QString keyA = numberKey(a.value);
        return keyA.isEmpty() ? digitsOf(a.value) == digitsOf(b.value) : keyA == numberKey(b.value);
    }
    case ContactValueKind::Email:
        return a.value.trimmed().compare(b.value.trimmed(), Qt::CaseInsensitive) == 0;
    case ContactValueKind::Address:
        return a.address == b.address;
    case ContactValueKind::Website:
    case ContactValueKind::Im:
    case ContactValueKind::Event:
        break;
    }
    return a.value.trimmed() == b.value.trimmed() && a.type == b.type;
}

void fillIfEmpty(QString &target, const QString &source)
{
    if (target.trimmed().isEmpty())
        target = source;
}

Error dialRefused(const protocol::AtReply &reply)
{
    switch (reply.status) {
    case protocol::AtReply::Status::Ok:
        break;
    case protocol::AtReply::Status::Timeout:
        return Error(ErrorCode::Timeout,
                     QStringLiteral("The phone did not answer the call request"));
    case protocol::AtReply::Status::Closed:
        return notConnected();
    case protocol::AtReply::Status::PermissionDenied:
        return Error(ErrorCode::PermissionDenied, QStringLiteral("The phone may not place calls"));
    case protocol::AtReply::Status::NotSupported:
        return Error(ErrorCode::NotSupported, QStringLiteral("The phone cannot place calls"));
    default:
        return Error(ErrorCode::Rejected, QStringLiteral("The phone refused the call"));
    }
    return {};
}

} // namespace

ContactsService::ContactsService(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
}

void ContactsService::setStore(const std::shared_ptr<ContactStore> &store)
{
    m_store = store;
}

QFuture<Result<ContactChanges>> ContactsService::pullInSession(ObexClient &obex)
{
    auto adapter = std::make_shared<GuardedStore>(m_store);
    auto *pull = new sync::IrmcSync(IrmcDomain::Contacts, adapter.get(), this);
    return pull->pull(obex).then(
        this,
        [this, pull, adapter](const Result<sync::SyncDelta> &delta) -> Result<ContactChanges> {
            pull->deleteLater();
            if (!delta.hasValue())
                return delta.error();
            Q_EMIT contactsChanged();
            return adapter->lastChanges();
        });
}

QFuture<Result<ContactChanges>> ContactsService::sync()
{
    if (m_store.expired())
        return failed<ContactChanges>(noStore());
    if (!m_connection->session())
        return failed<ContactChanges>(notConnected());
    return m_connection->withObex<ContactChanges>(
        ObexClient::Target::IrmcSync, [this](ObexClient &obex) { return pullInSession(obex); });
}

QFuture<Result<SaveOutcome>> ContactsService::save(const Contact &edited, bool photoChanged,
                                                   SaveMode mode)
{
    const std::shared_ptr<ContactStore> store = m_store.lock();
    if (!store)
        return failed<SaveOutcome>(noStore());
    if (!codec::isSavable(edited)) {
        return failed<SaveOutcome>(
            Error(ErrorCode::InvalidInput,
                  QStringLiteral("A contact needs a name, a number, a mail address or a company")));
    }
    if (!m_connection->session())
        return failed<SaveOutcome>(notConnected());

    if (edited.luid == 0) {
        const Result<QByteArray> body = codec::encodeNewContact(edited);
        if (!body.hasValue())
            return failed<SaveOutcome>(body.error());
        const QByteArray data = body.value();
        return m_connection->withObex<SaveOutcome>(
            ObexClient::Target::IrmcSync, [this, data](ObexClient &obex) {
                auto promise = std::make_shared<QPromise<Result<SaveOutcome>>>();
                promise->start();
                QPointer<ObexClient> client = &obex;
                const auto finish = [promise](const Result<SaveOutcome> &result) {
                    promise->addResult(result);
                    promise->finish();
                };
                obex.put(createPath(), data)
                    .then(this, [this, client, finish](const Result<ObexResponse> &created) {
                        if (!created.hasValue()) {
                            finish(created.error());
                            return;
                        }
                        const std::optional<quint32> luid =
                            ObexClient::createdLuid(created.value().appParams);
                        if (!luid || *luid == 0) {
                            finish(Error(ErrorCode::ProtocolError,
                                         QStringLiteral("The phone did not say which contact "
                                                        "it created")));
                            return;
                        }
                        if (!client) {
                            finish(clientGone());
                            return;
                        }
                        client->get(objectPath(*luid), {}, objectTimeout)
                            .then(this, [this, finish, luid](const Result<ObexResponse> &read) {
                                const Result<RawObject> object = objectFrom(read, *luid);
                                if (!object.hasValue()) {
                                    finish(object.error());
                                    return;
                                }
                                const std::shared_ptr<ContactStore> current = m_store.lock();
                                if (!current) {
                                    finish(storeGone());
                                    return;
                                }
                                if (const Result<void> stored =
                                        current->storeObject(object.value());
                                    !stored.hasValue()) {
                                    finish(stored.error());
                                    return;
                                }
                                Q_EMIT contactsChanged();
                                SaveOutcome outcome;
                                outcome.luid = *luid;
                                finish(outcome);
                            });
                    });
                return promise->future();
            });
    }

    const Result<Contact> stored = store->contact(edited.luid);
    if (!stored.hasValue())
        return failed<SaveOutcome>(stored.error());
    const Contact original = stored.value();
    return m_connection->withObex<SaveOutcome>(ObexClient::Target::IrmcSync, [this, edited,
                                                                              original,
                                                                              photoChanged, mode](
                                                                                 ObexClient &obex) {
        auto promise = std::make_shared<QPromise<Result<SaveOutcome>>>();
        promise->start();
        QPointer<ObexClient> client = &obex;
        const auto finish = [promise](const Result<SaveOutcome> &result) {
            promise->addResult(result);
            promise->finish();
        };
        const quint32 luid = edited.luid;
        obex.get(objectPath(luid), {}, objectTimeout)
            .then(this, [this, client, finish, edited, original, photoChanged, mode,
                         luid](const Result<ObexResponse> &read) {
                const Result<RawObject> object = objectFrom(read, luid);
                if (!object.hasValue()) {
                    finish(object.error());
                    return;
                }
                const Result<Contact> parsed = codec::parsePhoneVCard(object.value().data);
                if (!parsed.hasValue()) {
                    finish(parsed.error());
                    return;
                }
                Contact phone = parsed.value();
                phone.luid = luid;
                if (mode == SaveMode::CheckConflict && !codec::sameContent(phone, original)) {
                    SaveOutcome outcome;
                    outcome.conflict = true;
                    outcome.phoneVersion = phone;
                    outcome.phoneObject = object.value();
                    finish(outcome);
                    return;
                }
                Contact target = edited;
                if (!photoChanged)
                    target.photo = phone.photo;
                const Result<QByteArray> body = codec::encodeContactUpdate(phone, target);
                if (!body.hasValue()) {
                    finish(body.error());
                    return;
                }
                if (!client) {
                    finish(clientGone());
                    return;
                }
                client->put(objectPath(luid), body.value())
                    .then(this, [this, client, finish, luid](const Result<ObexResponse> &put) {
                        if (!put.hasValue()) {
                            finish(put.error());
                            return;
                        }
                        if (!client) {
                            finish(clientGone());
                            return;
                        }
                        client->get(objectPath(luid), {}, objectTimeout)
                            .then(this, [this, finish, luid](const Result<ObexResponse> &again) {
                                const Result<RawObject> fresh = objectFrom(again, luid);
                                if (!fresh.hasValue()) {
                                    finish(fresh.error());
                                    return;
                                }
                                const std::shared_ptr<ContactStore> current = m_store.lock();
                                if (!current) {
                                    finish(storeGone());
                                    return;
                                }
                                if (const Result<void> kept = current->storeObject(fresh.value());
                                    !kept.hasValue()) {
                                    finish(kept.error());
                                    return;
                                }
                                Q_EMIT contactsChanged();
                                SaveOutcome outcome;
                                outcome.luid = luid;
                                finish(outcome);
                            });
                    });
            });
        return promise->future();
    });
}

Result<void> ContactsService::keepPhoneVersion(const RawObject &object)
{
    const std::shared_ptr<ContactStore> store = m_store.lock();
    if (!store)
        return noStore();
    const Result<void> stored = store->storeObject(object);
    if (stored.hasValue())
        Q_EMIT contactsChanged();
    return stored;
}

QFuture<Result<void>> ContactsService::remove(const QList<quint32> &luids)
{
    if (m_store.expired())
        return failed<void>(noStore());
    if (!m_connection->session())
        return failed<void>(notConnected());
    if (luids.isEmpty())
        return QtFuture::makeReadyValueFuture(Result<void>());
    return m_connection->withObex<void>(
        ObexClient::Target::IrmcSync, [this, luids](ObexClient &obex) {
            struct Job
            {
                QPromise<Result<void>> promise;
                QPointer<ObexClient> client;
                QList<quint32> pending;
                std::function<void()> next;
            };
            auto job = std::make_shared<Job>();
            job->promise.start();
            job->client = &obex;
            job->pending = luids;
            const auto finish = [](const std::shared_ptr<Job> &done, const Result<void> &result) {
                done->promise.addResult(result);
                done->promise.finish();
            };
            // The callback refers to the job only weakly, so that the job is
            // freed once the last request has answered.
            job->next = [this, weak = std::weak_ptr<Job>(job), finish]() {
                const std::shared_ptr<Job> current = weak.lock();
                if (!current)
                    return;
                if (current->pending.isEmpty()) {
                    finish(current, {});
                    return;
                }
                if (!current->client) {
                    finish(current, clientGone());
                    return;
                }
                const quint32 luid = current->pending.takeFirst();
                current->client->remove(objectPath(luid))
                    .then(this, [this, current, finish, luid](const Result<ObexResponse> &removed) {
                        if (!removed.hasValue() && removed.error().code() != ErrorCode::NotFound) {
                            finish(current, removed.error());
                            return;
                        }
                        const std::shared_ptr<ContactStore> store = m_store.lock();
                        if (!store) {
                            finish(current, storeGone());
                            return;
                        }
                        if (const Result<void> gone = store->removeObject(luid); !gone.hasValue()) {
                            finish(current, gone);
                            return;
                        }
                        Q_EMIT contactsChanged();
                        if (current->next)
                            current->next();
                    });
            };
            job->next();
            return job->promise.future();
        });
}

QFuture<Result<ImportOutcome>> ContactsService::importContacts(const QList<Contact> &contacts)
{
    if (m_store.expired())
        return failed<ImportOutcome>(noStore());
    if (!m_connection->session())
        return failed<ImportOutcome>(notConnected());

    QList<QByteArray> bodies;
    ImportOutcome counts;
    for (Contact contact : contacts) {
        // Imported contacts go to the default account of the phone.
        contact.luid = 0;
        contact.accountType.clear();
        contact.accountName.clear();
        const Result<QByteArray> body = codec::encodeNewContact(contact);
        if (body.hasValue())
            bodies.append(body.value());
        else
            ++counts.skipped;
    }
    return m_connection->withObex<ImportOutcome>(
        ObexClient::Target::IrmcSync, [this, bodies, counts](ObexClient &obex) {
            struct Job
            {
                QPromise<Result<ImportOutcome>> promise;
                QPointer<ObexClient> client;
                QList<QByteArray> pending;
                ImportOutcome outcome;
                std::function<void()> next;
            };
            auto job = std::make_shared<Job>();
            job->promise.start();
            job->client = &obex;
            job->pending = bodies;
            job->outcome = counts;
            const auto finish = [](const std::shared_ptr<Job> &done,
                                   const Result<ImportOutcome> &result) {
                done->promise.addResult(result);
                done->promise.finish();
            };
            job->next = [this, weak = std::weak_ptr<Job>(job), finish]() {
                const std::shared_ptr<Job> current = weak.lock();
                if (!current)
                    return;
                if (!current->client) {
                    finish(current, clientGone());
                    return;
                }
                if (current->pending.isEmpty()) {
                    pullInSession(*current->client)
                        .then(this, [current, finish](const Result<ContactChanges> &pulled) {
                            if (!pulled.hasValue())
                                finish(current, pulled.error());
                            else
                                finish(current, current->outcome);
                        });
                    return;
                }
                const QByteArray body = current->pending.takeFirst();
                current->client->put(createPath(), body)
                    .then(this, [current, finish](const Result<ObexResponse> &created) {
                        if (created.hasValue()) {
                            ++current->outcome.created;
                        } else if (created.error().code() == ErrorCode::LimitExceeded ||
                                   created.error().code() == ErrorCode::Rejected) {
                            ++current->outcome.skipped;
                        } else {
                            finish(current, created.error());
                            return;
                        }
                        if (current->next)
                            current->next();
                    });
            };
            job->next();
            return job->promise.future();
        });
}

QFuture<Result<quint32>> ContactsService::merge(quint32 keep, quint32 drop)
{
    const std::shared_ptr<ContactStore> store = m_store.lock();
    if (!store)
        return failed<quint32>(noStore());
    if (keep == drop) {
        return failed<quint32>(Error(ErrorCode::InvalidInput,
                                     QStringLiteral("A contact cannot be merged with itself")));
    }
    const Result<Contact> first = store->contact(keep);
    if (!first.hasValue())
        return failed<quint32>(first.error());
    const Result<Contact> second = store->contact(drop);
    if (!second.hasValue())
        return failed<quint32>(second.error());
    const Contact joined = merged(first.value(), second.value());
    const bool photoChanged = joined.photo != first.value().photo;

    auto promise = std::make_shared<QPromise<Result<quint32>>>();
    promise->start();
    save(joined, photoChanged, SaveMode::Overwrite)
        .then(this, [this, promise, drop](const Result<SaveOutcome> &saved) {
            if (!saved.hasValue()) {
                promise->addResult(saved.error());
                promise->finish();
                return;
            }
            const quint32 luid = saved.value().luid;
            remove({drop}).then(this, [promise, luid](const Result<void> &removed) {
                promise->addResult(removed.hasValue() ? Result<quint32>(luid)
                                                      : Result<quint32>(removed.error()));
                promise->finish();
            });
        });
    return promise->future();
}

QFuture<Result<void>> ContactsService::setDefaultAccount(const QString &type, const QString &name)
{
    const std::shared_ptr<ContactStore> store = m_store.lock();
    if (!store)
        return failed<void>(noStore());
    if (!m_connection->session())
        return failed<void>(notConnected());
    const Result<codec::PbMetadata> stored = store->metadata();
    if (!stored.hasValue())
        return failed<void>(stored.error());
    codec::PbMetadata metadata = stored.value();
    bool found = false;
    for (codec::PbAccount &account : metadata.accounts) {
        const bool match = account.type == type && account.name == name;
        account.isDefault = match;
        if (match) {
            account.selected = true;
            found = true;
        }
    }
    if (!found)
        return failed<void>(Error(ErrorCode::NotFound, QStringLiteral("Unknown account")));
    // The phone keeps a filter only when it is switched on.
    metadata.filterDisabled = false;
    const QByteArray document = codec::encodePbMetadata(metadata);
    return m_connection->withObex<void>(ObexClient::Target::IrmcSync, [this,
                                                                       document](ObexClient &obex) {
        auto promise = std::make_shared<QPromise<Result<void>>>();
        promise->start();
        QPointer<ObexClient> client = &obex;
        obex.put(QStringLiteral("telecom/pb/metadata.xml"), document)
            .then(this, [this, promise, client](const Result<ObexResponse> &put) {
                if (!put.hasValue() || !client) {
                    promise->addResult(put.hasValue() ? Result<void>(clientGone())
                                                      : Result<void>(put.error()));
                    promise->finish();
                    return;
                }
                pullInSession(*client).then(this, [promise](const Result<ContactChanges> &pulled) {
                    promise->addResult(pulled.hasValue() ? Result<void>()
                                                         : Result<void>(pulled.error()));
                    promise->finish();
                });
            });
        return promise->future();
    });
}

QString ContactsService::dialableNumber(const QString &number)
{
    QString out;
    for (const QChar c : number) {
        if (c.isSpace() || c == u'-' || c == u'/' || c == u'(' || c == u')' || c == u'.')
            continue;
        const bool allowed =
            (c >= u'0' && c <= u'9') || c == u'*' || c == u'#' || (c == u'+' && out.isEmpty());
        if (!allowed)
            return {};
        out.append(c);
    }
    if (out.size() < minimumDialDigits || out.size() > maximumDialDigits)
        return {};
    return out;
}

QFuture<Result<void>> ContactsService::dial(const QString &number)
{
    const QString dialable = dialableNumber(number);
    if (dialable.isEmpty()) {
        return failed<void>(Error(ErrorCode::InvalidInput, QStringLiteral("Not a phone number")));
    }
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<void>(notConnected());
    return session->send(QStringLiteral("ATD%1;").arg(dialable))
        .then(this, [](const protocol::AtReply &reply) -> Result<void> {
            const Error error = dialRefused(reply);
            if (error.isError())
                return error;
            return {};
        });
}

QList<DuplicatePair> ContactsService::findDuplicates(const QList<Contact> &contacts)
{
    QList<DuplicatePair> pairs;
    QSet<std::pair<quint32, quint32>> seen;
    const auto addPair = [&pairs, &seen](quint32 a, quint32 b, DuplicatePair::Reason reason) {
        if (a == b)
            return;
        const std::pair<quint32, quint32> key = {std::min(a, b), std::max(a, b)};
        if (seen.contains(key))
            return;
        seen.insert(key);
        pairs.append({key.first, key.second, reason});
    };

    QHash<QString, quint32> byNumber;
    QHash<QString, quint32> byEmail;
    QHash<QString, quint32> byName;
    QList<std::pair<QString, quint32>> names;
    for (const Contact &contact : contacts) {
        QSet<QString> ownNumbers;
        for (const ContactValue &phone : contact.phones) {
            const QString key = numberKey(phone.value);
            if (key.isEmpty() || ownNumbers.contains(key))
                continue;
            ownNumbers.insert(key);
            if (const auto other = byNumber.constFind(key); other != byNumber.cend())
                addPair(other.value(), contact.luid, DuplicatePair::Reason::Number);
            else
                byNumber.insert(key, contact.luid);
        }
        QSet<QString> ownEmails;
        for (const ContactValue &email : contact.emails) {
            const QString key = email.value.trimmed().toLower();
            if (key.isEmpty() || ownEmails.contains(key))
                continue;
            ownEmails.insert(key);
            if (const auto other = byEmail.constFind(key); other != byEmail.cend())
                addPair(other.value(), contact.luid, DuplicatePair::Reason::Email);
            else
                byEmail.insert(key, contact.luid);
        }
        const QString key = nameKey(contact);
        if (key.isEmpty())
            continue;
        if (const auto other = byName.constFind(key); other != byName.cend())
            addPair(other.value(), contact.luid, DuplicatePair::Reason::Name);
        else
            byName.insert(key, contact.luid);
        names.append({key, contact.luid});
    }

    // Names one or two letters apart, looked for among neighbours in sorted
    // order; comparing every pair would not scale to thousands of contacts.
    std::sort(names.begin(), names.end());
    for (qsizetype i = 0; i < names.size(); ++i) {
        const QString &a = names.at(i).first;
        if (a.size() < minimumSimilarNameLength)
            continue;
        for (qsizetype j = i + 1; j < names.size() && j <= i + nameNeighbours; ++j) {
            const QString &b = names.at(j).first;
            if (b.size() < minimumSimilarNameLength || a == b)
                continue;
            const int allowed = std::min(a.size(), b.size()) >= longNameLength ? 2 : 1;
            if (std::abs(a.size() - b.size()) <= allowed && editDistance(a, b) <= allowed)
                addPair(names.at(i).second, names.at(j).second, DuplicatePair::Reason::Name);
        }
    }
    return pairs;
}

Contact ContactsService::merged(const Contact &keep, const Contact &drop)
{
    Contact result = keep;
    fillIfEmpty(result.familyName, drop.familyName);
    fillIfEmpty(result.givenName, drop.givenName);
    fillIfEmpty(result.middleName, drop.middleName);
    fillIfEmpty(result.prefix, drop.prefix);
    fillIfEmpty(result.suffix, drop.suffix);
    fillIfEmpty(result.nickname, drop.nickname);
    fillIfEmpty(result.organization, drop.organization);
    fillIfEmpty(result.department, drop.department);
    fillIfEmpty(result.title, drop.title);
    fillIfEmpty(result.ringtone, drop.ringtone);
    if (result.note.trimmed().isEmpty())
        result.note = drop.note;
    else if (!drop.note.trimmed().isEmpty() && drop.note != result.note)
        result.note += QStringLiteral("\n") + drop.note;
    if (!result.birthday.isValid())
        result.birthday = drop.birthday;
    if (!result.anniversary.isValid())
        result.anniversary = drop.anniversary;
    for (const ContactValueKind kind : codec::allContactValueKinds) {
        QList<ContactValue> &target = result.values(kind);
        for (const ContactValue &entry : drop.values(kind)) {
            if (entry.isEmpty())
                continue;
            const bool present = std::any_of(
                target.cbegin(), target.cend(),
                [kind, &entry](const ContactValue &own) { return sameValue(kind, own, entry); });
            if (present)
                continue;
            ContactValue copy = entry;
            copy.dataId = 0;
            copy.primary = false;
            target.append(copy);
        }
    }
    for (const QString &group : drop.groups) {
        if (!result.groups.contains(group))
            result.groups.append(group);
    }
    if (result.photo.isEmpty() && drop.photo.size() <= codec::maxPhotoBytesToSend)
        result.photo = drop.photo;
    return result;
}

} // namespace qpm::core::service
