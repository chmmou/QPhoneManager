#include "core/backup/BackupArchive.h"

#include "core/backup/ZipStore.h"
#include "core/codec/CsvTable.h"
#include "core/codec/ICalendar.h"
#include "core/codec/Limits.h"
#include "core/codec/MessageExport.h"
#include "core/sync/IrmcSync.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <cmath>
#include <limits>

namespace qpm::core::backup {

namespace {

using sync::IrmcDomain;
using sync::IrmcSync;
using sync::RawObject;

constexpr QLatin1StringView manifestName("manifest.json");
constexpr QLatin1StringView contactsStandardName("contacts/contacts.vcf");
constexpr QLatin1StringView contactsPhoneName("contacts/phone.vcf");
constexpr QLatin1StringView eventsStandardName("calendar/events.ics");
constexpr QLatin1StringView eventsPhoneName("calendar/phone.vcs");
constexpr QLatin1StringView tasksStandardName("tasks/tasks.ics");
constexpr QLatin1StringView tasksPhoneName("tasks/phone.vcs");
constexpr QLatin1StringView notesName("notes/notes.vnt");
constexpr QLatin1StringView callsName("calls/calls.csv");
constexpr QLatin1StringView messagesName("messages/messages.xml");
constexpr QLatin1StringView attachmentFolder("messages/attachments/%1/");

// A part name longer than this in UTF-8 is replaced; together with the
// characters partFileName() replaces, unpacking does not fail on the name.
constexpr qsizetype maxPartNameBytes = 200;

Error invalid(const QString &what)
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Not a readable backup: %1").arg(what));
}

struct Entry
{
    QString name;
    std::function<QByteArray()> make;
};

template <typename T, typename Encode> QByteArray joined(const QList<T> &items, Encode encode)
{
    QByteArray out;
    for (const T &item : items) {
        out += encode(item);
        if (!out.endsWith('\n'))
            out += "\r\n";
    }
    return out;
}

// The name a part of a message gets inside its folder; never a path. Control
// and format characters and those Windows refuses in a file name become "_".
QString partFileName(const QString &name, qsizetype position, QSet<QString> &used)
{
    static constexpr QStringView refused = u"<>:\"/\\|?*";
    const QList<uint> codePoints = name.toUcs4();
    QString safe;
    safe.reserve(name.size());
    for (const uint c : codePoints) {
        if (isControlOrFormat(c) || (c < 0x80 && refused.contains(QChar(char16_t(c))))) {
            safe.append(QChar(u'_'));
        } else if (QChar::requiresSurrogates(c)) {
            safe.append(QChar(QChar::highSurrogate(c)));
            safe.append(QChar(QChar::lowSurrogate(c)));
        } else {
            safe.append(QChar(char16_t(c)));
        }
    }
    safe = safe.trimmed();
    if (safe.isEmpty() || safe == QLatin1StringView(".") || safe == QLatin1StringView("..") ||
        safe.toUtf8().size() > maxPartNameBytes) {
        safe = QStringLiteral("part-%1").arg(position + 1);
    }
    QString candidate = safe;
    for (qsizetype prefix = position + 1; used.contains(candidate); ++prefix)
        candidate = QStringLiteral("%1-%2").arg(prefix).arg(safe);
    used.insert(candidate);
    return candidate;
}

QByteArray manifestJson(const BackupInfo &info)
{
    QJsonObject sections;
    for (auto it = info.counts.cbegin(); it != info.counts.cend(); ++it)
        sections.insert(sectionKey(it.key()), it.value());
    QJsonObject root;
    root.insert(QStringLiteral("format"), QString(backupFormatName));
    root.insert(QStringLiteral("formatVersion"), info.formatVersion);
    root.insert(QStringLiteral("createdAt"), info.createdAt.toUTC().toString(Qt::ISODate));
    root.insert(QStringLiteral("app"), info.app);
    root.insert(QStringLiteral("deviceId"), info.deviceId);
    root.insert(QStringLiteral("deviceName"), info.deviceName);
    root.insert(QStringLiteral("sections"), sections);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<int> wholeNumber(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (number != std::floor(number) || number < 0 || number > std::numeric_limits<int>::max())
        return std::nullopt;
    return int(number);
}

Result<BackupInfo> parseManifest(const QByteArray &bytes)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return invalid(QStringLiteral("the manifest is not a JSON object"));
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != backupFormatName)
        return invalid(QStringLiteral("the manifest names another format"));
    const std::optional<int> version = wholeNumber(root.value(QStringLiteral("formatVersion")));
    if (!version || *version < 1)
        return invalid(QStringLiteral("the manifest has no format version"));
    if (*version > backupFormatVersion) {
        return Error(ErrorCode::NotSupported, QStringLiteral("Backup format %1 is newer than %2")
                                                  .arg(*version)
                                                  .arg(backupFormatVersion));
    }

    BackupInfo info;
    info.formatVersion = *version;
    info.createdAt =
        QDateTime::fromString(root.value(QStringLiteral("createdAt")).toString(), Qt::ISODate)
            .toUTC();
    if (!info.createdAt.isValid())
        return invalid(QStringLiteral("the manifest has no time"));
    info.app = root.value(QStringLiteral("app")).toString();
    info.deviceId = root.value(QStringLiteral("deviceId")).toString();
    info.deviceName = root.value(QStringLiteral("deviceName")).toString();
    const QJsonValue sections = root.value(QStringLiteral("sections"));
    if (!sections.isObject())
        return invalid(QStringLiteral("the manifest lists no sections"));
    const QJsonObject listed = sections.toObject();
    for (auto it = listed.constBegin(); it != listed.constEnd(); ++it) {
        // A section added by a later program of the same format version is passed over.
        const std::optional<BackupSection> section = sectionFromKey(it.key());
        if (!section)
            continue;
        const std::optional<int> count = wholeNumber(it.value());
        if (!count || *count > limits::maxSyncObjects)
            return invalid(QStringLiteral("the count of a section is not a number"));
        info.counts.insert(*section, *count);
    }
    return info;
}

// A reader's own failure keeps its code when it is a limit; anything else
// means the file is damaged.
Error sectionError(const Error &error, BackupSection section)
{
    if (error.code() == ErrorCode::LimitExceeded || error.code() == ErrorCode::IoFailure)
        return error;
    return invalid(QStringLiteral("section %1: %2").arg(sectionKey(section), error.message()));
}

template <typename T, typename Parse>
Result<QList<T>> parseObjects(const QByteArray &dump, IrmcDomain domain, Parse parse)
{
    const Result<QList<RawObject>> objects = IrmcSync::splitObjects(dump, domain);
    if (!objects.hasValue())
        return objects.error();
    QList<T> items;
    items.reserve(objects.value().size());
    for (const RawObject &object : objects.value()) {
        const Result<T> item = parse(object.data);
        if (!item.hasValue())
            return item.error();
        items.append(item.value());
    }
    return items;
}

Result<int> readSection(ZipReader &reader, BackupSection section, BackupData &data)
{
    const auto entry = [&reader](const QString &name) -> Result<QByteArray> {
        if (!reader.contains(name))
            return invalid(QStringLiteral("%1 is missing").arg(name));
        return reader.read(name);
    };
    switch (section) {
    case BackupSection::Contacts: {
        const Result<QByteArray> bytes = entry(contactsPhoneName);
        if (!bytes.hasValue())
            return bytes.error();
        const Result<QList<codec::Contact>> contacts = parseObjects<codec::Contact>(
            bytes.value(), IrmcDomain::Contacts,
            [](const QByteArray &one) { return codec::parsePhoneVCard(one); });
        if (!contacts.hasValue())
            return contacts.error();
        data.contacts = contacts.value();
        return int(data.contacts.size());
    }
    case BackupSection::Calendar: {
        const Result<QByteArray> bytes = entry(eventsPhoneName);
        if (!bytes.hasValue())
            return bytes.error();
        const Result<QList<codec::CalendarEvent>> events = parseObjects<codec::CalendarEvent>(
            bytes.value(), IrmcDomain::Calendar,
            [](const QByteArray &one) { return codec::parsePhoneEvent(one); });
        if (!events.hasValue())
            return events.error();
        data.events = events.value();
        return int(data.events.size());
    }
    case BackupSection::Tasks: {
        const Result<QByteArray> bytes = entry(tasksPhoneName);
        if (!bytes.hasValue())
            return bytes.error();
        const Result<QList<codec::CalendarTask>> tasks = parseObjects<codec::CalendarTask>(
            bytes.value(), IrmcDomain::Calendar,
            [](const QByteArray &one) { return codec::parsePhoneTask(one); });
        if (!tasks.hasValue())
            return tasks.error();
        data.tasks = tasks.value();
        return int(data.tasks.size());
    }
    case BackupSection::Notes: {
        const Result<QByteArray> bytes = entry(notesName);
        if (!bytes.hasValue())
            return bytes.error();
        const Result<QList<codec::Note>> notes =
            parseObjects<codec::Note>(bytes.value(), IrmcDomain::Notes, [](const QByteArray &one) {
                return codec::parsePhoneNote(one);
            });
        if (!notes.hasValue())
            return notes.error();
        data.notes = notes.value();
        return int(data.notes.size());
    }
    case BackupSection::Calls: {
        const Result<QByteArray> bytes = entry(callsName);
        if (!bytes.hasValue())
            return bytes.error();
        const Result<codec::CsvRows> rows = codec::readCsv(bytes.value());
        if (!rows.hasValue())
            return rows.error();
        if (rows.value().isEmpty())
            return invalid(QStringLiteral("the call table has no header"));
        return int(rows.value().size() - 1);
    }
    case BackupSection::Messages: {
        const Result<QByteArray> bytes = entry(messagesName);
        if (!bytes.hasValue())
            return bytes.error();
        const Result<QList<codec::Message>> messages = codec::readMessagesXml(bytes.value());
        if (!messages.hasValue())
            return messages.error();
        data.messages = messages.value();
        return int(data.messages.size());
    }
    }
    return invalid(QStringLiteral("unknown section"));
}

} // namespace

QString sectionKey(BackupSection section)
{
    switch (section) {
    case BackupSection::Contacts:
        return QStringLiteral("contacts");
    case BackupSection::Calendar:
        return QStringLiteral("calendar");
    case BackupSection::Tasks:
        return QStringLiteral("tasks");
    case BackupSection::Notes:
        return QStringLiteral("notes");
    case BackupSection::Calls:
        return QStringLiteral("calls");
    case BackupSection::Messages:
        break;
    }
    return QStringLiteral("messages");
}

std::optional<BackupSection> sectionFromKey(QStringView key)
{
    for (const BackupSection section : allBackupSections) {
        if (key == sectionKey(section))
            return section;
    }
    return std::nullopt;
}

Result<BackupInfo> writeBackupArchive(const QString &path, const BackupInfo &info,
                                      const BackupData &data, const QList<BackupSection> &sections,
                                      codec::CsvLanguage language, const BackupStep &step)
{
    BackupInfo written = info;
    written.formatVersion = backupFormatVersion;
    written.counts.clear();
    QList<Entry> entries;
    for (const BackupSection section : allBackupSections) {
        if (!sections.contains(section))
            continue;
        switch (section) {
        case BackupSection::Contacts:
            written.counts.insert(section, int(data.contacts.size()));
            entries.append(
                {contactsStandardName, [&data]() { return codec::writeVCards(data.contacts); }});
            entries.append({contactsPhoneName, [&data]() {
                                return joined(data.contacts, &codec::encodeAsPhoneSendsIt);
                            }});
            break;
        case BackupSection::Calendar:
            written.counts.insert(section, int(data.events.size()));
            entries.append(
                {eventsStandardName, [&data]() { return codec::writeICalendar(data.events, {}); }});
            entries.append({eventsPhoneName, [&data]() {
                                return joined(data.events, &codec::encodeEventAsPhoneSendsIt);
                            }});
            break;
        case BackupSection::Tasks:
            written.counts.insert(section, int(data.tasks.size()));
            entries.append(
                {tasksStandardName, [&data]() { return codec::writeICalendar({}, data.tasks); }});
            entries.append({tasksPhoneName, [&data]() {
                                return joined(data.tasks, &codec::encodeTaskAsPhoneSendsIt);
                            }});
            break;
        case BackupSection::Notes:
            written.counts.insert(section, int(data.notes.size()));
            entries.append({notesName, [&data]() {
                                return joined(data.notes, &codec::encodeNoteAsPhoneSendsIt);
                            }});
            break;
        case BackupSection::Calls:
            written.counts.insert(section, int(data.calls.size()));
            entries.append({callsName, [&data, language]() {
                                return codec::writeCallsCsv(data.calls, language);
                            }});
            break;
        case BackupSection::Messages: {
            written.counts.insert(section, int(data.messages.size()));
            entries.append({messagesName, [&data]() {
                                QList<codec::ExportedMessage> exported;
                                exported.reserve(data.messages.size());
                                for (const codec::Message &message : data.messages)
                                    exported.append({message, QString(), false});
                                return codec::writeMessagesXml(exported);
                            }});
            for (qsizetype index = 0; index < data.messages.size(); ++index) {
                const codec::Message &message = data.messages.at(index);
                const quint32 folder = message.luid != 0 ? message.luid : quint32(index);
                QSet<QString> used;
                for (qsizetype part = 0; part < message.attachments.size(); ++part) {
                    const codec::MessageAttachment &attachment = message.attachments.at(part);
                    if (attachment.onPhoneOnly || attachment.data.isEmpty())
                        continue;
                    const QString name = QString(attachmentFolder).arg(folder) +
                                         partFileName(attachment.name, part, used);
                    entries.append({name, [&attachment]() { return attachment.data; }});
                }
            }
            break;
        }
        }
    }

    const int total = int(entries.size()) + 1;
    ZipWriter writer(path, written.createdAt.isValid() ? written.createdAt.toLocalTime()
                                                       : QDateTime::currentDateTime());
    if (const Result<void> opened = writer.open(); !opened.hasValue())
        return opened.error();
    const Error cancelled(ErrorCode::Cancelled, QStringLiteral("The backup was cancelled"));
    if (step && !step(0, total))
        return cancelled;
    if (const Result<void> added = writer.add(manifestName, manifestJson(written));
        !added.hasValue()) {
        return added.error();
    }
    for (qsizetype index = 0; index < entries.size(); ++index) {
        if (step && !step(int(index) + 1, total))
            return cancelled;
        const Entry &entry = entries.at(index);
        if (const Result<void> added = writer.add(entry.name, entry.make()); !added.hasValue())
            return added.error();
    }
    if (step && !step(total, total))
        return cancelled;
    if (const Result<void> committed = writer.commit(); !committed.hasValue())
        return committed.error();
    return written;
}

Result<Backup> readBackupArchive(const QString &path, const BackupStep &step)
{
    ZipReader reader(path);
    if (const Result<void> opened = reader.open(); !opened.hasValue()) {
        // This program never compresses or encrypts a backup, so such an
        // archive is something else; NotSupported stays for a newer format.
        if (opened.error().code() == ErrorCode::NotSupported)
            return invalid(opened.error().message());
        return opened.error();
    }
    if (!reader.contains(manifestName))
        return invalid(QStringLiteral("there is no manifest"));
    for (const ZipEntryInfo &entry : reader.entries()) {
        if (entry.name == manifestName && entry.size > maxManifestBytes) {
            return Error(ErrorCode::LimitExceeded,
                         QStringLiteral("Manifest of %1 bytes is too large").arg(entry.size));
        }
    }
    const Result<QByteArray> manifest = reader.read(manifestName);
    if (!manifest.hasValue())
        return manifest.error();
    const Result<BackupInfo> info = parseManifest(manifest.value());
    if (!info.hasValue())
        return info.error();

    Backup backup;
    backup.info = info.value();
    const int total = int(backup.info.counts.size()) + 1;
    int done = 1;
    for (auto it = backup.info.counts.cbegin(); it != backup.info.counts.cend(); ++it) {
        if (step && !step(done, total))
            return Error(ErrorCode::Cancelled, QStringLiteral("Reading the backup was cancelled"));
        const Result<int> count = readSection(reader, it.key(), backup.data);
        if (!count.hasValue())
            return sectionError(count.error(), it.key());
        if (count.value() != it.value()) {
            return invalid(QStringLiteral("section %1 holds %2 entries, the manifest says %3")
                               .arg(sectionKey(it.key()))
                               .arg(count.value())
                               .arg(it.value()));
        }
        ++done;
    }
    if (step && !step(total, total))
        return Error(ErrorCode::Cancelled, QStringLiteral("Reading the backup was cancelled"));
    return backup;
}

} // namespace qpm::core::backup
