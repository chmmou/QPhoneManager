#include "ui/AppController.h"
#include "ui/Settings.h"
#include "ui/calls/CallListModel.h"
#include "ui/calls/CallsArea.h"
#include "ui/contacts/ContactEditor.h"
#include "ui/contacts/ContactValueModel.h"
#include "ui/contacts/ContactsArea.h"

#include "core/codec/CsvTable.h"
#include "core/codec/PhoneTime.h"
#include "core/codec/PhoneVCard.h"
#include "platform/SecretStore.h"

#include "FakePhone.h"

#include <QFile>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::test::FakeCall;
using qpm::test::FakePhone;
using qpm::ui::AppController;
using qpm::ui::CallListModel;
using qpm::ui::CallsArea;
using qpm::ui::ContactEditor;
using qpm::ui::Settings;

namespace {

constexpr int timeoutMs = 15000;

QDateTime today(int hour, int minute)
{
    return QDateTime(QDate::currentDate(), QTime(hour, minute));
}

QDateTime yesterday(int hour, int minute)
{
    return QDateTime(QDate::currentDate().addDays(-1), QTime(hour, minute));
}

FakeCall fakeCall(quint32 id, const QString &storage, const QDateTime &time, const QString &number,
                  const QString &name = {}, qint64 duration = 0)
{
    FakeCall call;
    call.id = id;
    call.storage = storage;
    call.time = formatCallLogTime(time);
    call.number = number;
    call.name = name;
    call.duration = duration;
    return call;
}

QList<FakeCall> exampleLog()
{
    FakeCall secondSim =
        fakeCall(4, QStringLiteral("RC"), QDateTime(QDate(2026, 1, 15), QTime(9, 0)),
                 QStringLiteral("040 2000006"), QString(), 3723);
    secondSim.addressType = QStringLiteral("SIM1");
    return {
        fakeCall(1, QStringLiteral("DC"), today(0, 0).addSecs(1), QStringLiteral("0151 1234567"),
                 QStringLiteral("Erika"), 65),
        fakeCall(2, QStringLiteral("MC"), yesterday(18, 30), QStringLiteral("+49 30 2000002"),
                 QStringLiteral("Bernd Albrecht")),
        fakeCall(3, QStringLiteral("MC"), yesterday(8, 15), QString()),
        secondSim,
    };
}

Contact erika()
{
    Contact contact;
    contact.luid = 1;
    contact.familyName = QStringLiteral("Mustermann");
    contact.givenName = QStringLiteral("Erika");
    contact.displayName = QStringLiteral("Erika Mustermann");
    ContactValue phone;
    phone.dataId = 11;
    phone.type = phoneType::mobile;
    phone.value = QStringLiteral("+49 151 1234567");
    contact.phones = {phone};
    return contact;
}

QString role(const QAbstractItemModel *model, int row, const QByteArray &name)
{
    return model->data(model->index(row, 0), model->roleNames().key(name)).toString();
}

QStringList titles(const QAbstractItemModel *model)
{
    QStringList result;
    for (int row = 0; row < model->rowCount(); ++row)
        result.append(role(model, row, "title"));
    return result;
}

} // namespace

class TestCalls : public QObject
{
    Q_OBJECT

public:
    explicit TestCalls(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void listShowsCallsNewestFirstUnderTheirDay();
    void contactNameWinsOverTheNameThePhoneKeeps();
    void kindFilterAndSearchNarrowTheList();
    void detailShowsTheEntryChosen();
    void namesFollowTheContacts();
    void deletedCallsAreGoneOnThePhoneAndInTheList();
    void refusedDeletionSaysToDeleteOnThePhone();
    void shownCallsAreExportedAsATable();
    void callSendsTheNumberAndWrongNumbersStayHere();
    void messageAndNewContactGetTheCheckedNumber();
    void newContactFormIsFilledFromTheCall();
    void deletingAndCallingNeedAConnection();
    void syncAllReportsContactsAndCalls();

private:
    [[nodiscard]] CallsArea *area() const { return m_controller->calls(); }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<AppController> m_controller;
};

void TestCalls::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
}

void TestCalls::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setDataDirectory(m_dir->filePath(QStringLiteral("profiles")));
    m_settings->setAdbPath(m_dir->filePath(QStringLiteral("no-adb")));
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("pb"), {{1, encodeAsPhoneSendsIt(erika())}});
    m_phone->setCalls(exampleLog());

    AppController::Environment environment;
    environment.listenPort = 0;
    environment.listenAddress = QHostAddress(QHostAddress::LocalHost);
    environment.searchNetwork = false;
    environment.searchBluetooth = false;
    m_controller = std::make_unique<AppController>(m_settings.get(), nullptr, environment);
    QVERIFY(m_controller->connectAddress(QStringLiteral("127.0.0.1:%1").arg(m_phone->port())));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->isConnected(), timeoutMs);
    QVERIFY(area()->hasPhone());
    QVERIFY(area()->canEdit());
    const QFuture<Result<store::ContactChanges>> contacts = m_controller->contacts()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(contacts.isFinished(), timeoutMs);
    QVERIFY(contacts.result().hasValue());
    const QFuture<Result<store::CallChanges>> calls = area()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(calls.isFinished(), timeoutMs);
    QVERIFY(calls.result().hasValue());
    QCOMPARE(area()->list()->totalCount(), 4);
}

void TestCalls::cleanup()
{
    m_controller.reset();
    m_phone.reset();
    m_settings.reset();
    m_dir.reset();
}

void TestCalls::listShowsCallsNewestFirstUnderTheirDay()
{
    const CallListModel *list = area()->list();

    QCOMPARE(list->count(), 4);
    QCOMPARE(titles(list),
             (QStringList{QStringLiteral("Erika Mustermann"), QStringLiteral("Bernd Albrecht"),
                          QStringLiteral("Unknown number"), QStringLiteral("040 2000006")}));
    QCOMPARE(role(list, 0, "section"), QStringLiteral("Today"));
    QCOMPARE(role(list, 1, "section"), QStringLiteral("Yesterday"));
    QCOMPARE(role(list, 3, "section"), QLocale().toString(QDate(2026, 1, 15), QLocale::LongFormat));
    QCOMPARE(role(list, 0, "kindText"), QStringLiteral("Outgoing call"));
    QCOMPARE(role(list, 1, "kindText"), QStringLiteral("Missed call"));
    QCOMPARE(role(list, 3, "kindText"), QStringLiteral("Incoming call"));
    QCOMPARE(role(list, 0, "durationText"), QStringLiteral("1:05"));
    QCOMPARE(role(list, 1, "durationText"), QString());
    QCOMPARE(role(list, 3, "durationText"), QStringLiteral("1:02:03"));
    QCOMPARE(role(list, 1, "timeText"), QLocale().toString(QTime(18, 30), QLocale::ShortFormat));
    QCOMPARE(list->data(list->index(1), list->roleNames().key("kind")).toInt(),
             int(CallListModel::Kind::Missed));
    QCOMPARE(list->idAt(0), 1);
    QCOMPARE(list->rowOf(4), 3);
}

void TestCalls::contactNameWinsOverTheNameThePhoneKeeps()
{
    const CallListModel *list = area()->list();

    QCOMPARE(role(list, 0, "name"), QStringLiteral("Erika Mustermann"));
    QCOMPARE(list->data(list->index(0), list->roleNames().key("inContacts")).toBool(), true);
    QCOMPARE(role(list, 1, "name"), QStringLiteral("Bernd Albrecht"));
    QCOMPARE(list->data(list->index(1), list->roleNames().key("inContacts")).toBool(), false);
    QCOMPARE(list->contactNameFor(QStringLiteral("+49 (151) 123-4567")),
             QStringLiteral("Erika Mustermann"));
    QVERIFY(list->contactNameFor(QStringLiteral("4567")).isEmpty());
}

void TestCalls::kindFilterAndSearchNarrowTheList()
{
    CallListModel *list = area()->list();
    QSignalSpy filterChanged(list, &CallListModel::kindFilterChanged);

    list->setKindFilter(CallListModel::Kind::Missed);
    QCOMPARE(titles(list),
             (QStringList{QStringLiteral("Bernd Albrecht"), QStringLiteral("Unknown number")}));
    QCOMPARE(filterChanged.count(), 1);

    list->setKindFilter(CallListModel::Kind::Incoming);
    QCOMPARE(titles(list), (QStringList{QStringLiteral("040 2000006")}));
    list->setKindFilter(CallListModel::Kind::Outgoing);
    QCOMPARE(list->count(), 1);

    list->setKindFilter(CallListModel::Kind::All);
    area()->setSearchText(QStringLiteral("muster"));
    QCOMPARE(titles(list), (QStringList{QStringLiteral("Erika Mustermann")}));
    area()->setSearchText(QStringLiteral("302000"));
    QCOMPARE(titles(list), (QStringList{QStringLiteral("Bernd Albrecht")}));
    area()->setSearchText(QStringLiteral("nobody"));
    QCOMPARE(list->count(), 0);
    QCOMPARE(list->totalCount(), 4);
    QCOMPARE(area()->shownIds(), QList<int>());
    area()->setSearchText(QString());
    QCOMPARE(area()->shownIds(), (QList<int>{1, 2, 3, 4}));
}

void TestCalls::detailShowsTheEntryChosen()
{
    QVERIFY(!area()->hasCurrent());
    QSignalSpy changed(area(), &CallsArea::currentChanged);

    area()->setCurrentId(4);

    QVERIFY(changed.count() >= 1);
    QVERIFY(area()->hasCurrent());
    QCOMPARE(area()->currentTitle(), QStringLiteral("040 2000006"));
    QCOMPARE(area()->currentNumber(), QStringLiteral("040 2000006"));
    QCOMPARE(area()->currentKind(), CallListModel::Kind::Incoming);
    QCOMPARE(area()->currentKindText(), QStringLiteral("Incoming call"));
    QCOMPARE(area()->currentDurationText(), QStringLiteral("1:02:03"));
    QCOMPARE(area()->currentSimText(), QStringLiteral("SIM 2"));
    QVERIFY(area()->currentTimeText().contains(
        QLocale().toString(QDate(2026, 1, 15), QLocale::LongFormat)));
    QVERIFY(!area()->currentInContacts());
    QVERIFY(area()->currentCallable());

    area()->setCurrentId(3);
    QCOMPARE(area()->currentTitle(), QStringLiteral("Unknown number"));
    QCOMPARE(area()->currentDurationText(), QString());
    QVERIFY(area()->currentSimText().isEmpty());
    QVERIFY(!area()->currentCallable());

    area()->setCurrentId(99);
    QVERIFY(!area()->hasCurrent());
}

void TestCalls::namesFollowTheContacts()
{
    Contact bernd;
    bernd.luid = 2;
    bernd.givenName = QStringLiteral("Bernd");
    bernd.familyName = QStringLiteral("Brandt");
    bernd.displayName = QStringLiteral("Bernd Brandt");
    ContactValue phone;
    phone.dataId = 21;
    phone.type = phoneType::work;
    phone.value = QStringLiteral("030 2000002");
    bernd.phones = {phone};
    m_phone->addObject(QStringLiteral("pb"), 2, encodeAsPhoneSendsIt(bernd));

    const QFuture<Result<store::ContactChanges>> pulled = m_controller->contacts()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(pulled.isFinished(), timeoutMs);
    QVERIFY(pulled.result().hasValue());

    QCOMPARE(role(area()->list(), 1, "title"), QStringLiteral("Bernd Brandt"));
}

void TestCalls::deletedCallsAreGoneOnThePhoneAndInTheList()
{
    QSignalSpy info(area(), &CallsArea::infoMessage);
    area()->setCurrentId(2);

    QVERIFY(area()->removeCalls({2, 3}));

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QVERIFY(info.first().first().toString().startsWith(QStringLiteral("2 call")));
    QCOMPARE(area()->list()->totalCount(), 2);
    QCOMPARE(m_phone->calls().size(), 2);
    QCOMPARE(area()->currentId(), 0);
    QVERIFY(!area()->hasCurrent());
    QVERIFY(!area()->removeCalls({0, -4}));
}

void TestCalls::refusedDeletionSaysToDeleteOnThePhone()
{
    m_phone->config().callLogReadOnly = true;
    QSignalSpy failed(area(), &CallsArea::errorOccurred);
    QSignalSpy info(area(), &CallsArea::infoMessage);

    QVERIFY(area()->removeCalls({2}));

    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, timeoutMs);
    QCOMPARE(failed.first().first().toString(),
             QStringLiteral("The app on the phone may not change the call log. Delete the call "
                            "on the phone itself."));
    QCOMPARE(info.count(), 0);
    QCOMPARE(area()->list()->totalCount(), 4);
}

void TestCalls::shownCallsAreExportedAsATable()
{
    QSignalSpy info(area(), &CallsArea::infoMessage);
    const QString path = m_dir->filePath(QStringLiteral("anrufe.csv"));
    area()->list()->setKindFilter(CallListModel::Kind::Missed);

    QVERIFY(area()->exportCalls(QUrl::fromLocalFile(path)));

    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QVERIFY(info.first().first().toString().startsWith(QStringLiteral("2 call")));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const Result<CsvRows> rows = readCsv(file.readAll());
    QVERIFY(rows.hasValue());
    QCOMPARE(rows.value().size(), 3);
    QCOMPARE(rows.value().at(0).at(0), QStringLiteral("Type"));
    QCOMPARE(rows.value().at(1).at(0), QStringLiteral("Missed"));
    QCOMPARE(rows.value().at(1).at(1), QStringLiteral("Bernd Albrecht"));
    QCOMPARE(rows.value().at(2).at(2), QString());

    QSignalSpy failed(area(), &CallsArea::errorOccurred);
    QVERIFY(!area()->exportCalls(QUrl(QStringLiteral("https://example.org/calls.csv"))));
    QCOMPARE(failed.count(), 1);
    QVERIFY(area()->exportCalls(
        QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("missing/folder/calls.csv")))));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, timeoutMs);
}

void TestCalls::callSendsTheNumberAndWrongNumbersStayHere()
{
    QSignalSpy info(area(), &CallsArea::infoMessage);
    QSignalSpy failed(area(), &CallsArea::errorOccurred);
    m_phone->clearLog();

    QVERIFY(area()->call(QStringLiteral("+49 30 2000002")));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("ATD+49302000002;")));

    QVERIFY(!area()->call(QStringLiteral("030;ATA")));
    QVERIFY(!area()->call(QString()));
    QCOMPARE(failed.count(), 2);
    const QStringList commands = m_phone->receivedCommands();
    for (const QString &command : commands)
        QVERIFY(!command.contains(QStringLiteral("ATA")));
}

void TestCalls::messageAndNewContactGetTheCheckedNumber()
{
    QSignalSpy message(area(), &CallsArea::messageRequested);
    QSignalSpy contact(area(), &CallsArea::newContactRequested);
    QSignalSpy failed(area(), &CallsArea::errorOccurred);

    QVERIFY(area()->writeMessage(QStringLiteral("+49 (30) 2000002")));
    QVERIFY(!area()->writeMessage(QString()));
    QVERIFY(!area()->addCurrentToContacts());
    area()->setCurrentId(2);
    QVERIFY(area()->addCurrentToContacts());
    area()->setCurrentId(3);
    QVERIFY(!area()->addCurrentToContacts());

    QCOMPARE(message.count(), 1);
    QCOMPARE(message.first().first().toString(), QStringLiteral("+49302000002"));
    QCOMPARE(contact.count(), 1);
    QCOMPARE(contact.first().at(0).toString(), QStringLiteral("Bernd Albrecht"));
    QCOMPARE(contact.first().at(1).toString(), QStringLiteral("+49 30 2000002"));
    QCOMPARE(failed.count(), 2);
}

void TestCalls::newContactFormIsFilledFromTheCall()
{
    qpm::ui::ContactsArea *contacts = m_controller->contacts();
    QSignalSpy started(contacts, &qpm::ui::ContactsArea::newContactStarted);
    ContactEditor *editor = contacts->editor();

    QVERIFY(contacts->startNewContact(QStringLiteral("Bernd  van Albrecht"),
                                      QStringLiteral("+49 30 2000002")));

    QCOMPARE(started.count(), 1);
    QVERIFY(editor->isNew());
    QCOMPARE(editor->givenName(), QStringLiteral("Bernd van"));
    QCOMPARE(editor->familyName(), QStringLiteral("Albrecht"));
    QCOMPARE(editor->phones()->values().size(), 1);
    QCOMPARE(editor->phones()->values().at(0).value, QStringLiteral("+49 30 2000002"));
    QCOMPARE(editor->phones()->values().at(0).type, phoneType::mobile);
    QVERIFY(editor->canSave());

    QVERIFY(contacts->startNewContact(QString(), QStringLiteral("0301")));
    QVERIFY(editor->givenName().isEmpty());
    QVERIFY(editor->familyName().isEmpty());
}

void TestCalls::deletingAndCallingNeedAConnection()
{
    m_controller->disconnectPhone();
    QSignalSpy failed(area(), &CallsArea::errorOccurred);
    area()->setCurrentId(2);

    QVERIFY(!area()->canEdit());
    QVERIFY(area()->hasPhone());
    QVERIFY(!area()->removeCalls({2}));
    QVERIFY(!area()->call(QStringLiteral("030 2000002")));
    QVERIFY(!area()->addCurrentToContacts());
    QVERIFY(!m_controller->contacts()->startNewContact(QString(), QStringLiteral("0301")));
    QCOMPARE(failed.count(), 3);
    QCOMPARE(area()->list()->totalCount(), 4);
    QCOMPARE(m_phone->calls().size(), 4);
}

void TestCalls::syncAllReportsContactsAndCalls()
{
    QList<FakeCall> log = exampleLog();
    log.removeAt(3);
    log.append(fakeCall(5, QStringLiteral("RC"), today(0, 0).addSecs(2),
                        QStringLiteral("0170 2000003"), QString(), 20));
    m_phone->setCalls(log);
    QSignalSpy finished(m_controller.get(), &AppController::syncFinished);

    m_controller->syncAll();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeoutMs);
    QCOMPARE(finished.first().first().toString(),
             QStringLiteral("Contacts: 0 new, 0 changed, 0 deleted\n"
                            "Calls: 1 new, 0 changed, 1 deleted\n"
                            "Messages: 0 new, 0 changed, 0 deleted\n"
                            "Calendar and tasks: 0 new, 0 changed, 0 deleted\n"
                            "Notes: 0 new, 0 changed, 0 deleted"));
    QCOMPARE(area()->list()->totalCount(), 4);
    QCOMPARE(role(area()->list(), 0, "title"), QStringLiteral("0170 2000003"));
}

QTEST_MAIN(TestCalls)

#include "tst_calls.moc"
