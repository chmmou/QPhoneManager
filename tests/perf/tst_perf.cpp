#include "ui/AppController.h"
#include "ui/Settings.h"
#include "ui/apps/AppListModel.h"
#include "ui/contacts/ContactListModel.h"
#include "ui/contacts/ContactsArea.h"
#include "ui/contacts/PhoneImageProvider.h"
#include "ui/messages/ConversationListModel.h"
#include "ui/messages/MessageListModel.h"
#include "ui/messages/MessagesArea.h"
#include "ui/organizer/CalendarArea.h"
#include "ui/organizer/CalendarDaysModel.h"
#include "ui/organizer/CalendarModel.h"

#include "core/codec/PhoneVCal.h"
#include "core/codec/PhoneVCard.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/DeviceStatusService.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"
#include "platform/SecretStore.h"

#include "FakePhone.h"

#include <QElapsedTimer>
#include <QLocale>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <algorithm>
#include <functional>
#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::DeviceStatusService;
using qpm::core::store::ProfileStore;
using qpm::test::FakeApp;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::ui::AppController;
using qpm::ui::AppListModel;
using qpm::ui::CalendarArea;
using qpm::ui::ContactsArea;
using qpm::ui::MessagesArea;
using qpm::ui::PhotoSource;
using qpm::ui::Settings;

namespace {

// With these amounts, a sync without changes of 1000 contacts has to take
// less than 10 s, and each list has to open from the local store in less
// than 2 s. For scrolling and searching this test sets its own limits: one
// frame at 60 Hz to fetch the rows of a screen, and 200 ms to answer a
// keystroke or a page change.
constexpr int contactCount = 5000;
constexpr int messageCount = 50000;
constexpr int eventCount = 10000;
constexpr int appCount = 1000;
constexpr int syncContactCount = 1000;

constexpr double syncLimitMs = 10000;
constexpr double openLimitMs = 2000;
constexpr double viewportLimitMs = 16;
constexpr double answerLimitMs = 200;

// Rows a tall list shows at once.
constexpr int viewportRows = 40;
constexpr int viewportPositions = 25;

// Each measurement runs once to fill the caches and is then repeated; the
// median of the repetitions counts.
constexpr int warmUpRuns = 1;
constexpr int measuredRuns = 5;

// One message in ten belongs to one long conversation, the rest is spread
// over the others.
constexpr int bigThreadMessages = messageCount / 10;
constexpr int otherThreads = 900;

constexpr int syncTimeoutMs = 240000;
constexpr int timeoutMs = 60000;

constexpr QLatin1StringView bigPhoneId("8f3c2a10-0000-4000-8000-0000000000a1");
constexpr QLatin1StringView syncPhoneId("8f3c2a10-0000-4000-8000-0000000000a2");
constexpr QLatin1StringView accountUrl("anna@example.org@com.google/NULL");

const QStringList &givenNames()
{
    static const QStringList list = {
        QStringLiteral("Anna"),   QStringLiteral("Bernd"),  QStringLiteral("Clara"),
        QStringLiteral("Dieter"), QStringLiteral("Emma"),   QStringLiteral("Felix"),
        QStringLiteral("Greta"),  QStringLiteral("Hans"),   QStringLiteral("Ida"),
        QStringLiteral("Jörg"),   QStringLiteral("Karin"),  QStringLiteral("Lukas"),
        QStringLiteral("Marie"),  QStringLiteral("Niklas"), QStringLiteral("Olga"),
        QStringLiteral("Paul"),   QStringLiteral("Quirin"), QStringLiteral("Rosa"),
        QStringLiteral("Stefan"), QStringLiteral("Tanja"),  QStringLiteral("Ulrich"),
        QStringLiteral("Vera"),   QStringLiteral("Walter"), QStringLiteral("Xenia"),
        QStringLiteral("Yusuf"),  QStringLiteral("Zoë"),    QStringLiteral("Ahmet"),
        QStringLiteral("Björn"),  QStringLiteral("Chiara"), QStringLiteral("Dörte"),
        QStringLiteral("Elif"),   QStringLiteral("Frauke"), QStringLiteral("Günther"),
        QStringLiteral("Helga"),  QStringLiteral("Igor"),   QStringLiteral("Jana"),
        QStringLiteral("Kemal"),  QStringLiteral("Lena"),   QStringLiteral("Mats"),
        QStringLiteral("Nora"),
    };
    return list;
}

const QStringList &familyNames()
{
    static const QStringList list = {
        QStringLiteral("Müller"),   QStringLiteral("Schmidt"),    QStringLiteral("Schneider"),
        QStringLiteral("Fischer"),  QStringLiteral("Weber"),      QStringLiteral("Meyer"),
        QStringLiteral("Wagner"),   QStringLiteral("Becker"),     QStringLiteral("Schulz"),
        QStringLiteral("Hoffmann"), QStringLiteral("Schäfer"),    QStringLiteral("Koch"),
        QStringLiteral("Bauer"),    QStringLiteral("Richter"),    QStringLiteral("Klein"),
        QStringLiteral("Wolf"),     QStringLiteral("Schröder"),   QStringLiteral("Neumann"),
        QStringLiteral("Schwarz"),  QStringLiteral("Zimmermann"), QStringLiteral("Braun"),
        QStringLiteral("Krüger"),   QStringLiteral("Hofmann"),    QStringLiteral("Hartmann"),
        QStringLiteral("Lange"),
    };
    return list;
}

const QStringList &words()
{
    static const QStringList list = {
        QStringLiteral("Hallo"),   QStringLiteral("Termin"),  QStringLiteral("morgen"),
        QStringLiteral("heute"),   QStringLiteral("Abend"),   QStringLiteral("Essen"),
        QStringLiteral("Bahnhof"), QStringLiteral("Paket"),   QStringLiteral("später"),
        QStringLiteral("gerne"),   QStringLiteral("danke"),   QStringLiteral("Büro"),
        QStringLiteral("Kinder"),  QStringLiteral("Schule"),  QStringLiteral("Wochenende"),
        QStringLiteral("Urlaub"),  QStringLiteral("Projekt"), QStringLiteral("Besprechung"),
        QStringLiteral("Arzt"),    QStringLiteral("Kaffee"),
    };
    return list;
}

QString contactNumber(int index)
{
    return QStringLiteral("+49 151 %1").arg(1000000 + index);
}

QString displayNameOf(int index)
{
    return givenNames().at(index % givenNames().size()) + u' ' +
           familyNames().at((index / givenNames().size()) % familyNames().size());
}

QString textOf(int seed, int length)
{
    QStringList chosen;
    for (int i = 0; i < length; ++i)
        chosen.append(words().at((seed * 7 + i * 13) % words().size()));
    return chosen.join(u' ');
}

QByteArray contactMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<accounts filter_disabled=\"0\">\r\n"
           "<account selected=\"1\" type=\"null\" name=\"null\"  default=\"1\" "
           "title=\"Phone\"/>\r\n"
           "<account selected=\"1\" type=\"com.google\" name=\"anna@example.org\"  "
           "title=\"Google\"/>\r\n</accounts>\r\n<groups>\r\n"
           "<group title=\"Familie\" sourceid=\"1\" account_name=\"anna@example.org\" "
           "account_type=\"com.google\"/>\r\n"
           "<group title=\"Arbeit\" sourceid=\"2\" account_name=\"anna@example.org\" "
           "account_type=\"com.google\"/>\r\n</groups>\r\n</metadata>";
}

QByteArray calendarMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<calendars filter_disabled=\"0\">\r\n"
           "<calendar selected=\"1\" default=\"1\" url=\"anna@example.org@com.google/NULL\" "
           "title=\"Familie\" color=\"FF33B679\" hidden=\"0\" timezone=\"Europe/Berlin\" "
           "access_level=\"700\"/>\r\n"
           "<calendar selected=\"1\" url=\"anna@example.org@com.google/NULL\" title=\"Arbeit\" "
           "color=\"FF4285F4\" hidden=\"0\" timezone=\"Europe/Berlin\" access_level=\"700\"/>\r\n"
           "</calendars>\r\n<tasksupport>1</tasksupport>\r\n</metadata>";
}

QList<FakeSyncObject> contacts(int count)
{
    QList<FakeSyncObject> objects;
    objects.reserve(count);
    quint64 dataId = 1;
    for (int i = 0; i < count; ++i) {
        Contact contact;
        contact.luid = quint32(i + 1);
        contact.givenName = givenNames().at(i % givenNames().size());
        contact.familyName = familyNames().at((i / givenNames().size()) % familyNames().size());
        contact.displayName = displayNameOf(i);
        ContactValue mobile;
        mobile.dataId = dataId++;
        mobile.type = phoneType::mobile;
        mobile.value = contactNumber(i);
        contact.phones.append(mobile);
        if (i % 3 == 0) {
            ContactValue home;
            home.dataId = dataId++;
            home.type = phoneType::home;
            home.value = QStringLiteral("030 %1").arg(200000 + i);
            contact.phones.append(home);
            ContactValue mail;
            mail.dataId = dataId++;
            mail.type = emailType::home;
            mail.value = QStringLiteral("person%1@example.org").arg(i);
            contact.emails.append(mail);
        }
        if (i % 5 == 0)
            contact.organization = QStringLiteral("Beispiel %1 GmbH").arg(i % 50);
        if (i % 4 == 0) {
            contact.accountType = QStringLiteral("com.google");
            contact.accountName = QStringLiteral("anna@example.org");
            contact.groups = {i % 8 == 0 ? QStringLiteral("Familie") : QStringLiteral("Arbeit")};
        }
        objects.append({contact.luid, encodeAsPhoneSendsIt(contact)});
    }
    return objects;
}

QString threadNumber(int thread)
{
    // Most conversations are with a stored contact, some with a number alone.
    if (thread % 10 < 7)
        return contactNumber(thread * 5);
    return QStringLiteral("+49 170 %1").arg(9000000 + thread);
}

QByteArray sms(quint32 luid, bool incoming, bool read, const QString &number, const QString &text,
               const QDateTime &when)
{
    QByteArray data = "BEGIN:VMESSAGE\r\nTYP:SMS;" + QByteArray(incoming ? "IN" : "OUT") + "\r\n";
    data += incoming ? "BOX:INBOX\r\nREAD:" + QByteArray(read ? "1" : "0") + "\r\n"
                     : QByteArray("BOX:SENT\r\n");
    data += "DATE:" + when.toUTC().toString(QStringLiteral("yyyyMMddTHHmmssZ")).toLatin1() + "\r\n";
    data += "NUMBER:" + number.toUtf8() + "\r\n";
    data += "BODY;CHARSET=UTF-8:" + text.toUtf8() + "\r\n";
    data += "X-IRMC-LUID:" + sync::IrmcSync::luidText(luid).toLatin1() + "\r\nEND:VMESSAGE\r\n";
    return data;
}

QList<FakeSyncObject> messages(const QDateTime &now)
{
    QList<FakeSyncObject> objects;
    objects.reserve(messageCount);
    const QDateTime first = now.addDays(-730);
    const qint64 step = first.secsTo(now) / messageCount;
    for (int i = 0; i < messageCount; ++i) {
        const int thread = i % 10 == 0 ? 0 : 1 + (i - i / 10 - 1) % otherThreads;
        const bool incoming = i % 2 == 0;
        const bool read = i < messageCount - 30;
        const auto luid = quint32(i + 1);
        objects.append({luid, sms(luid, incoming, read, threadNumber(thread), textOf(i, 4 + i % 12),
                                  first.addSecs(step * i))});
    }
    return objects;
}

QList<FakeSyncObject> events(const QDate &today)
{
    QList<FakeSyncObject> objects;
    objects.reserve(eventCount);
    // Four years, two and a half of them before today.
    const QDate first = today.addMonths(-30);
    constexpr int spanDays = 1461;
    for (int i = 0; i < eventCount; ++i) {
        CalendarEvent event;
        event.luid = 0x10000000u | quint32(i + 1);
        event.summary = textOf(i, 2);
        if (i % 7 == 0)
            event.location = QStringLiteral("Raum %1").arg(i % 40);
        event.calendarUrl = accountUrl;
        event.calendarTitle = i % 3 == 0 ? QStringLiteral("Arbeit") : QStringLiteral("Familie");
        const QDate day = first.addDays(qint64(i) * spanDays / eventCount);
        if (i % 20 == 0) {
            event.allDay = true;
            event.start = QDateTime(day, QTime(0, 0), QTimeZone::UTC);
            event.end = event.start.addDays(1);
        } else {
            event.start = QDateTime(day, QTime(7 + i % 11, 0), QTimeZone::UTC);
            event.end = event.start.addSecs(3600);
        }
        if (i % 500 == 0)
            event.rrule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO");
        objects.append({event.luid, encodeEventAsPhoneSendsIt(event)});
    }
    return objects;
}

QList<FakeApp> apps()
{
    QList<FakeApp> list;
    list.reserve(appCount);
    for (int i = 0; i < appCount; ++i) {
        FakeApp app;
        app.package = QStringLiteral("org.example.app%1").arg(i);
        app.label = QStringLiteral("%1 %2").arg(words().at(i % words().size())).arg(i);
        app.version = QStringLiteral("%1.%2").arg(i % 9 + 1).arg(i % 13);
        app.userApp = i % 5 != 0;
        list.append(app);
    }
    return list;
}

double toMs(qint64 nanoseconds)
{
    return double(nanoseconds) / 1e6;
}

struct Stats
{
    double median = 0;
    double min = 0;
    double max = 0;
};

Stats statsOf(QList<double> samples)
{
    Stats stats;
    if (samples.isEmpty())
        return stats;
    std::sort(samples.begin(), samples.end());
    const qsizetype middle = samples.size() / 2;
    stats.median = samples.size() % 2 == 1 ? samples.at(middle)
                                           : (samples.at(middle - 1) + samples.at(middle)) / 2;
    stats.min = samples.constFirst();
    stats.max = samples.constLast();
    return stats;
}

// Runs `once` to warm up and then measuredRuns times; `once` returns its own
// time in nanoseconds, so that preparation stays outside the measurement.
Stats measure(const std::function<qint64()> &once)
{
    for (int run = 0; run < warmUpRuns; ++run)
        static_cast<void>(once());
    QList<double> samples;
    for (int run = 0; run < measuredRuns; ++run)
        samples.append(toMs(once()));
    return statsOf(samples);
}

void report(const QString &name, const Stats &stats, double limitMs)
{
    qInfo().noquote() << QStringLiteral("perf | %1 | median %2 ms | min %3 ms | max %4 ms | "
                                        "limit %5 ms")
                             .arg(name)
                             .arg(stats.median, 0, 'f', 2)
                             .arg(stats.min, 0, 'f', 2)
                             .arg(stats.max, 0, 'f', 2)
                             .arg(limitMs, 0, 'f', 0);
}

int touched = 0;

// Reads every role of `rows` rows from `first` on, as the delegates of a
// view do.
void fetchRows(const QAbstractItemModel *model, const QList<int> &roles, int first, int rows)
{
    const int last = std::min(first + rows, model->rowCount());
    for (int row = first; row < last; ++row) {
        const QModelIndex index = model->index(row, 0);
        for (const int role : roles)
            touched += model->data(index, role).isValid() ? 1 : 0;
    }
}

void fetchFirstRows(const QAbstractItemModel *model)
{
    fetchRows(model, model->roleNames().keys(), 0, viewportRows);
}

// Starts of the viewports: the top, the bottom and spots in between that are
// the same in every run.
QList<int> viewportStarts(int rowCount)
{
    const int lastStart = std::max(0, rowCount - viewportRows);
    QList<int> starts = {0, lastStart};
    QRandomGenerator random(2026);
    for (int i = 0; i < viewportPositions; ++i)
        starts.append(lastStart > 0 ? int(random.bounded(lastStart + 1)) : 0);
    return starts;
}

// The slowest viewport of the model, each one measured by its median.
Stats measureViewports(const QAbstractItemModel *model)
{
    const QList<int> roles = model->roleNames().keys();
    const QList<int> starts = viewportStarts(model->rowCount());
    QList<QList<double>> samples(starts.size());
    for (int run = 0; run < warmUpRuns + measuredRuns; ++run) {
        for (qsizetype i = 0; i < starts.size(); ++i) {
            QElapsedTimer timer;
            timer.start();
            fetchRows(model, roles, starts.at(i), viewportRows);
            if (run >= warmUpRuns)
                samples[i].append(toMs(timer.nsecsElapsed()));
        }
    }
    QList<double> medians;
    for (const QList<double> &position : std::as_const(samples))
        medians.append(statsOf(position).median);
    return statsOf(medians);
}

// Times `answer` for each of `inputs` in turn, as typing or paging does, and
// returns the stats of the slowest input by its median.
Stats measureAnswers(const QStringList &inputs, const std::function<void(const QString &)> &answer)
{
    QList<QList<double>> samples(inputs.size());
    for (int run = 0; run < warmUpRuns + measuredRuns; ++run) {
        for (qsizetype i = 0; i < inputs.size(); ++i) {
            QElapsedTimer timer;
            timer.start();
            answer(inputs.at(i));
            if (run >= warmUpRuns)
                samples[i].append(toMs(timer.nsecsElapsed()));
        }
    }
    Stats slowest;
    for (const QList<double> &input : std::as_const(samples)) {
        const Stats stats = statsOf(input);
        if (stats.median >= slowest.median)
            slowest = stats;
    }
    return slowest;
}

// Opens the profile store and hands it to `show`, as choosing a phone does,
// and returns the time until the first rows of `model` are read.
qint64 openOnce(const QString &profiles, const QString &deviceId,
                const std::function<void(ProfileStore *)> &show, const QAbstractItemModel *model)
{
    ProfileStore store(profiles, deviceId);
    QElapsedTimer timer;
    timer.start();
    if (!store.open().hasValue())
        return -1;
    show(&store);
    fetchFirstRows(model);
    const qint64 elapsed = timer.nsecsElapsed();
    show(nullptr);
    return elapsed;
}

bool syncedWithoutChanges(const QString &summary)
{
    const QStringList lines = summary.split(u'\n');
    return lines.size() == 5 && std::all_of(lines.cbegin(), lines.cend(), [](const QString &line) {
               return line.endsWith(QLatin1StringView(": 0 new, 0 changed, 0 deleted"));
           });
}

AppController::Environment testEnvironment()
{
    AppController::Environment environment;
    environment.listenPort = 0;
    environment.listenAddress = QHostAddress(QHostAddress::LocalHost);
    environment.searchNetwork = false;
    environment.searchBluetooth = false;
    return environment;
}

// The time from the start until a signal arrives, shared with the slot that
// stops it.
struct Clock
{
    QElapsedTimer timer;
    qint64 stoppedAt = -1;
    QString text;
};

// Runs syncAll() and returns its time until syncFinished(), or -1 when it
// does not finish in time.
qint64 timeSyncAll(AppController *controller, QString *summary)
{
    const auto clock = std::make_shared<Clock>();
    const QMetaObject::Connection connection = QObject::connect(
        controller, &AppController::syncFinished, controller, [clock](const QString &text) {
            clock->stoppedAt = clock->timer.nsecsElapsed();
            clock->text = text;
        });
    clock->timer.start();
    controller->syncAll();
    const bool finished =
        QTest::qWaitFor([clock]() { return clock->stoppedAt >= 0; }, syncTimeoutMs);
    QObject::disconnect(connection);
    *summary = clock->text;
    return finished ? clock->stoppedAt : -1;
}

// The areas of the window without a controller, so that each list can be
// opened on its own.
struct LoneAreas
{
    PhoneConnection connection;
    DeviceStatusService status{&connection};
    std::shared_ptr<PhotoSource> photos = std::make_shared<PhotoSource>();
};

} // namespace

/**
 * Measures the time limits for syncing, opening, scrolling and searching on
 * the real models and areas the window uses, with generated data on the fake
 * phone. Runs only when QPM_TEST_PERF is 1, since its result depends on the
 * computer and the build.
 */
class TestPerf : public QObject
{
    Q_OBJECT

public:
    explicit TestPerf(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void syncWithoutChangesOfAThousandContactsStaysUnderTenSeconds();
    void everyListOpensFromTheLocalStoreUnderTwoSeconds();
    void rowsOfAScreenAreReadWithinOneFrame();
    void searchAndPagingAnswerWithinTheLimit();

private:
    [[nodiscard]] QString profiles() const { return m_dir->filePath(QStringLiteral("profiles")); }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<AppController> m_controller;
    QString m_bigThread;
};

void TestPerf::initTestCase()
{
    if (qEnvironmentVariable("QPM_TEST_PERF") != QLatin1StringView("1"))
        QSKIP("QPM_TEST_PERF is not 1");
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    // One line per sync and area would bury the measurements.
    QLoggingCategory::setFilterRules(QStringLiteral("qpm.*.info=false"));

    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setDataDirectory(profiles());
    m_settings->setAdbPath(m_dir->filePath(QStringLiteral("no-adb")));

    QElapsedTimer generation;
    generation.start();
    m_phone = std::make_unique<FakePhone>();
    m_phone->config().deviceId = QString(bigPhoneId);
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("pb"), contacts(contactCount));
    m_phone->setMetadata(QStringLiteral("pb"), contactMetadata());
    m_phone->setObjects(QStringLiteral("msg"), messages(QDateTime::currentDateTimeUtc()));
    m_phone->setObjects(QStringLiteral("cal"), events(QDate::currentDate()));
    m_phone->setMetadata(QStringLiteral("cal"), calendarMetadata());
    m_phone->setApps(apps());
    qInfo().noquote() << QStringLiteral("perf | data generated in %1 ms")
                             .arg(toMs(generation.nsecsElapsed()), 0, 'f', 0);

    m_controller = std::make_unique<AppController>(m_settings.get(), nullptr, testEnvironment());
    QVERIFY(m_controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port())));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->isConnected(), timeoutMs);
    QString summary;
    const qint64 firstLoad = timeSyncAll(m_controller.get(), &summary);
    QVERIFY2(firstLoad >= 0, "The first load did not finish");
    qInfo().noquote() << QStringLiteral("perf | first load of all areas in %1 ms")
                             .arg(toMs(firstLoad), 0, 'f', 0);
    QVERIFY2(!summary.contains(QLatin1StringView("not synced")), qPrintable(summary));

    QCOMPARE(m_controller->contacts()->list()->totalCount(), contactCount);
    QCOMPARE(m_controller->messages()->conversations()->count(), otherThreads + 1);
    const qpm::ui::ConversationListModel *conversations = m_controller->messages()->conversations();
    for (int row = 0; row < conversations->count(); ++row) {
        const QModelIndex index = conversations->index(row);
        if (conversations->data(index, qpm::ui::ConversationListModel::CountRole).toInt() ==
            bigThreadMessages) {
            m_bigThread = conversations->threadKeyAt(row);
        }
    }
    QVERIFY(!m_bigThread.isEmpty());
}

void TestPerf::cleanupTestCase()
{
    m_controller.reset();
    m_phone.reset();
    m_settings.reset();
    m_dir.reset();
}

void TestPerf::syncWithoutChangesOfAThousandContactsStaysUnderTenSeconds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    settings.setDataDirectory(dir.filePath(QStringLiteral("profiles")));
    settings.setAdbPath(dir.filePath(QStringLiteral("no-adb")));
    FakePhone phone;
    phone.config().deviceId = QString(syncPhoneId);
    QVERIFY(phone.listen());
    phone.setObjects(QStringLiteral("pb"), contacts(syncContactCount));
    phone.setMetadata(QStringLiteral("pb"), contactMetadata());
    AppController controller(&settings, nullptr, testEnvironment());
    QVERIFY(controller.connectAddress(QStringLiteral("127.0.0.1:%1").arg(phone.port())));
    QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), timeoutMs);
    QString summary;
    QVERIFY(timeSyncAll(&controller, &summary) >= 0);
    QCOMPARE(controller.contacts()->list()->totalCount(), syncContactCount);

    bool allUnchanged = true;
    const Stats stats = measure([&controller, &summary, &allUnchanged]() {
        const qint64 elapsed = timeSyncAll(&controller, &summary);
        allUnchanged = allUnchanged && elapsed >= 0 && syncedWithoutChanges(summary);
        return elapsed;
    });
    report(QStringLiteral("sync without changes, 1000 contacts"), stats, syncLimitMs);
    QVERIFY2(allUnchanged, qPrintable(summary));
    QVERIFY(stats.median < syncLimitMs);

    // The same on the large data set, for the record; the limit is set for
    // 1000 contacts.
    const Stats large = measure([this, &summary, &allUnchanged]() {
        const qint64 elapsed = timeSyncAll(m_controller.get(), &summary);
        allUnchanged = allUnchanged && elapsed >= 0 && syncedWithoutChanges(summary);
        return elapsed;
    });
    report(QStringLiteral("sync without changes, full data set"), large, syncLimitMs);
    QVERIFY2(allUnchanged, qPrintable(summary));
}

void TestPerf::everyListOpensFromTheLocalStoreUnderTwoSeconds()
{
    LoneAreas lone;
    ContactsArea contacts(&lone.connection, lone.photos);
    MessagesArea messages(&lone.connection, &lone.status);
    CalendarArea calendar(&lone.connection);

    const Stats contactsOpen = measure([this, &contacts]() {
        return openOnce(
            profiles(), QString(bigPhoneId),
            [&contacts](ProfileStore *store) { contacts.setProfileStore(store); }, contacts.list());
    });
    report(QStringLiteral("open contacts"), contactsOpen, openLimitMs);

    const Stats messagesOpen = measure([this, &messages]() {
        return openOnce(
            profiles(), QString(bigPhoneId),
            [&messages](ProfileStore *store) { messages.setProfileStore(store); },
            messages.conversations());
    });
    report(QStringLiteral("open conversations"), messagesOpen, openLimitMs);

    const Stats calendarOpen = measure([this, &calendar]() {
        return openOnce(
            profiles(), QString(bigPhoneId),
            [&calendar](ProfileStore *store) {
                calendar.setProfileStore(store);
                if (store)
                    fetchFirstRows(calendar.days());
            },
            calendar.list());
    });
    report(QStringLiteral("open calendar month"), calendarOpen, openLimitMs);

    // The window shows one conversation at a time; the longest is opened.
    MessagesArea *shown = m_controller->messages();
    const Stats conversationOpen = measure([shown, this]() {
        shown->setCurrentThread(QString());
        QElapsedTimer timer;
        timer.start();
        shown->setCurrentThread(m_bigThread);
        fetchFirstRows(shown->messages());
        return timer.nsecsElapsed();
    });
    report(QStringLiteral("open longest conversation"), conversationOpen, openLimitMs);
    QCOMPARE(shown->messages()->count(), bigThreadMessages);

    // Choosing the phone loads every area at once, so this is what the window waits for.
    const Stats selection = measure([this]() {
        Settings settings(m_dir->filePath(QStringLiteral("select.ini")));
        settings.setDataDirectory(profiles());
        AppController controller(&settings, nullptr, testEnvironment());
        QElapsedTimer timer;
        timer.start();
        controller.selectDevice(QString(bigPhoneId));
        fetchFirstRows(controller.contacts()->list());
        fetchFirstRows(controller.messages()->conversations());
        fetchFirstRows(controller.calendar()->days());
        fetchFirstRows(controller.calendar()->list());
        const qint64 elapsed = timer.nsecsElapsed();
        return controller.contacts()->list()->totalCount() == contactCount ? elapsed : -1;
    });
    report(QStringLiteral("choose the phone, all areas"), selection, openLimitMs);

    // Apps are not kept on this computer; their list is read from the phone
    // whenever it is shown.
    AppListModel *appList = m_controller->apps();
    appList->setFilter(AppListModel::Filter::All);
    const Stats appsOpen = measure([appList]() {
        const auto clock = std::make_shared<Clock>();
        const QMetaObject::Connection connection =
            QObject::connect(appList, &AppListModel::countChanged, appList, [appList, clock]() {
                if (appList->totalCount() == appCount && clock->stoppedAt < 0) {
                    fetchFirstRows(appList);
                    clock->stoppedAt = clock->timer.nsecsElapsed();
                }
            });
        clock->timer.start();
        static_cast<void>(appList->refresh());
        const bool shown = QTest::qWaitFor([clock]() { return clock->stoppedAt >= 0; }, timeoutMs);
        QObject::disconnect(connection);
        return shown ? clock->stoppedAt : qint64(-1);
    });
    report(QStringLiteral("open apps from the phone"), appsOpen, openLimitMs);

    QVERIFY(contactsOpen.min >= 0 && messagesOpen.min >= 0 && calendarOpen.min >= 0);
    QVERIFY(selection.min >= 0 && appsOpen.min >= 0);
    QCOMPARE(appList->count(), appCount);
    QVERIFY(contactsOpen.median < openLimitMs);
    QVERIFY(messagesOpen.median < openLimitMs);
    QVERIFY(calendarOpen.median < openLimitMs);
    QVERIFY(conversationOpen.median < openLimitMs);
    QVERIFY(selection.median < openLimitMs);
    QVERIFY(appsOpen.median < openLimitMs);
}

void TestPerf::rowsOfAScreenAreReadWithinOneFrame()
{
    MessagesArea *messageArea = m_controller->messages();
    messageArea->setSearchText(QString());
    messageArea->setCurrentThread(m_bigThread);
    CalendarArea *calendar = m_controller->calendar();
    calendar->setSearchText(QString());
    calendar->setViewMode(CalendarArea::ViewMode::List);
    AppListModel *appList = m_controller->apps();
    appList->setSearchText(QString());
    appList->setFilter(AppListModel::Filter::All);
    m_controller->contacts()->setSearchText(QString());

    const QList<std::pair<QString, const QAbstractItemModel *>> lists = {
        {QStringLiteral("contacts"), m_controller->contacts()->list()},
        {QStringLiteral("conversations"), messageArea->conversations()},
        {QStringLiteral("messages of the longest conversation"), messageArea->messages()},
        {QStringLiteral("calendar month as list"), calendar->list()},
        {QStringLiteral("apps"), appList},
    };
    QCOMPARE(m_controller->contacts()->list()->rowCount(), contactCount);
    QCOMPARE(messageArea->messages()->rowCount(), bigThreadMessages);
    QCOMPARE(appList->rowCount(), appCount);
    QVERIFY(calendar->list()->rowCount() > viewportRows);

    bool allInTime = true;
    const auto measureList = [&allInTime](const QString &name, const QAbstractItemModel *model) {
        const Stats stats = measureViewports(model);
        report(QStringLiteral("rows of a screen, %1 (%2 rows), max is the slowest spot")
                   .arg(name)
                   .arg(model->rowCount()),
               stats, viewportLimitMs);
        allInTime = allInTime && stats.max < viewportLimitMs;
    };
    for (const auto &[name, model] : lists)
        measureList(name, model);
    // The grid of days exists in the month view only.
    calendar->setViewMode(CalendarArea::ViewMode::Month);
    QVERIFY(calendar->days()->rowCount() > 0);
    measureList(QStringLiteral("calendar month grid"), calendar->days());

    QVERIFY(touched > 0);
    QVERIFY(allInTime);
}

void TestPerf::searchAndPagingAnswerWithinTheLimit()
{
    ContactsArea *contacts = m_controller->contacts();
    const Stats contactSearch =
        measureAnswers({QStringLiteral("m"), QStringLiteral("mü"), QStringLiteral("müll"),
                        QStringLiteral("müller"), QStringLiteral("anna müller"),
                        QStringLiteral("0151 100"), QString()},
                       [contacts](const QString &text) {
                           contacts->setSearchText(text);
                           fetchFirstRows(contacts->list());
                       });
    report(QStringLiteral("search contacts, slowest keystroke"), contactSearch, answerLimitMs);
    QCOMPARE(contacts->list()->rowCount(), contactCount);

    MessagesArea *messageArea = m_controller->messages();
    const Stats messageSearch =
        measureAnswers({QStringLiteral("t"), QStringLiteral("te"), QStringLiteral("termin"),
                        QStringLiteral("termin morgen"), QStringLiteral("0170"),
                        QStringLiteral("anna"), QString()},
                       [messageArea](const QString &text) {
                           messageArea->setSearchText(text);
                           fetchFirstRows(messageArea->conversations());
                       });
    report(QStringLiteral("search messages, slowest keystroke"), messageSearch, answerLimitMs);
    QCOMPARE(messageArea->conversations()->rowCount(), otherThreads + 1);

    CalendarArea *calendar = m_controller->calendar();
    calendar->setViewMode(CalendarArea::ViewMode::List);
    const Stats calendarSearch =
        measureAnswers({QStringLiteral("p"), QStringLiteral("pro"), QStringLiteral("projekt"),
                        QStringLiteral("raum"), QString()},
                       [calendar](const QString &text) {
                           calendar->setSearchText(text);
                           fetchFirstRows(calendar->list());
                       });
    report(QStringLiteral("search calendar month, slowest keystroke"), calendarSearch,
           answerLimitMs);

    AppListModel *appList = m_controller->apps();
    appList->setFilter(AppListModel::Filter::All);
    const Stats appSearch =
        measureAnswers({QStringLiteral("k"), QStringLiteral("ka"), QStringLiteral("kaffee"),
                        QStringLiteral("example"), QString()},
                       [appList](const QString &text) {
                           appList->setSearchText(text);
                           fetchFirstRows(appList);
                       });
    report(QStringLiteral("search apps, slowest keystroke"), appSearch, answerLimitMs);
    QCOMPARE(appList->rowCount(), appCount);

    // A page of the calendar is the next or the previous month.
    QStringList moves;
    for (int i = 0; i < 6; ++i)
        moves.append(i < 3 ? QStringLiteral("next") : QStringLiteral("previous"));
    Stats paging;
    for (const CalendarArea::ViewMode mode :
         {CalendarArea::ViewMode::Month, CalendarArea::ViewMode::List}) {
        calendar->setViewMode(mode);
        const Stats stats = measureAnswers(moves, [calendar](const QString &move) {
            if (move == QLatin1StringView("next"))
                calendar->next();
            else
                calendar->previous();
            fetchFirstRows(calendar->days());
            fetchFirstRows(calendar->list());
        });
        report(QStringLiteral("calendar page, %1 view, slowest step")
                   .arg(mode == CalendarArea::ViewMode::Month ? QStringLiteral("month")
                                                              : QStringLiteral("list")),
               stats, answerLimitMs);
        if (stats.median > paging.median)
            paging = stats;
    }

    QVERIFY(contactSearch.median < answerLimitMs);
    QVERIFY(messageSearch.median < answerLimitMs);
    QVERIFY(calendarSearch.median < answerLimitMs);
    QVERIFY(appSearch.median < answerLimitMs);
    QVERIFY(paging.median < answerLimitMs);
}

QTEST_MAIN(TestPerf)

#include "tst_perf.moc"
