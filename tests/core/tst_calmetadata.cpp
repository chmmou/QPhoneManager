#include "core/codec/CalMetadata.h"
#include "core/codec/SafeXml.h"

#include <QTest>

Q_DECLARE_METATYPE(qpm::core::ErrorCode)

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

QByteArray phoneMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">\r\n"
           "<calendars filter_disabled=\"0\">\r\n"
           "<calendar selected=\"1\" default=\"1\" url=\"user@example.invalid@com.google/NULL\" "
           "title=\"Feiertage &amp; Ferien\" color=\"FFFAD165\" hidden=\"0\" "
           "timezone=\"Europe/Berlin\" access_level=\"200\"/>\r\n"
           "<calendar selected=\"0\" url=\"user@example.invalid@com.google/NULL\" "
           "title=\"Calendar B\" color=\"FFD06B64\" hidden=\"1\" timezone=\"Europe/Berlin\" "
           "access_level=\"700\"/>\r\n"
           "</calendars>\r\n<tasksupport>1</tasksupport>\r\n<task_categories>\r\n"
           "<category name=\"Arbeit\" color=\"FF0000\" id=\"1\"/>\r\n"
           "<category name=\"\" color=\"00FF00\" id=\"2\"/>\r\n"
           "</task_categories>\r\n</metadata>";
}

} // namespace

class TestCalMetadata : public QObject
{
    Q_OBJECT

public:
    explicit TestCalMetadata(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void calendarsAndTaskCategoriesAreRead();
    void calendarListThePhoneNeverOpenedIsEmpty();
    void selectionIsWrittenForEveryCalendar();
    void categoryChangesAreWrittenForTasksAndNotes();
    void unsafeOrForeignDocumentsAreRejected_data();
    void unsafeOrForeignDocumentsAreRejected();
};

void TestCalMetadata::calendarsAndTaskCategoriesAreRead()
{
    const Result<CalMetadata> metadata = parseCalMetadata(phoneMetadata());

    QVERIFY(metadata.hasValue());
    QVERIFY(!metadata.value().filterDisabled);
    QVERIFY(metadata.value().taskSupport);
    QCOMPARE(metadata.value().calendars.size(), 2);
    const CalendarInfo &first = metadata.value().calendars.at(0);
    QCOMPARE(first.title, QStringLiteral("Feiertage & Ferien"));
    QCOMPARE(first.url, QStringLiteral("user@example.invalid@com.google/NULL"));
    QCOMPARE(first.color, 0xFFFAD165U);
    QCOMPARE(first.timezone, QStringLiteral("Europe/Berlin"));
    QVERIFY(first.selected);
    QVERIFY(first.isDefault);
    QVERIFY(!first.isWritable());
    const CalendarInfo &second = metadata.value().calendars.at(1);
    QVERIFY(!second.selected);
    QVERIFY(second.hidden);
    QVERIFY(second.isWritable());
    // A category without a name is no category.
    QCOMPARE(metadata.value().taskCategories,
             QList<OrganizerCategory>({{1, QStringLiteral("Arbeit"), 0xFF0000U}}));
}

void TestCalMetadata::calendarListThePhoneNeverOpenedIsEmpty()
{
    const QByteArray broken = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata "
                              "version=\"1.0\">\r\n</calendars>\r\n<tasksupport>1</tasksupport>\r\n"
                              "<task_categories>\r\n</task_categories>\r\n</metadata>";

    const Result<CalMetadata> metadata = parseCalMetadata(broken);

    QVERIFY(metadata.hasValue());
    QVERIFY(metadata.value().calendars.isEmpty());
    QVERIFY(metadata.value().taskSupport);
}

void TestCalMetadata::selectionIsWrittenForEveryCalendar()
{
    CalendarInfo work;
    work.url = QStringLiteral("anna@example.org@com.google/NULL");
    work.title = QStringLiteral("Arbeit \"Team\"");
    work.color = 0xFF9FC6E7U;
    work.selected = true;
    work.isDefault = true;
    CalendarInfo home = work;
    home.title = QStringLiteral("Privat");
    home.isDefault = false;
    home.selected = false;

    const QByteArray document = encodeCalendarSelection({work, home});
    const Result<XmlElement> parsed = parseSafeXml(document);

    QVERIFY(parsed.hasValue());
    const XmlElement &calendars = parsed.value().children.constFirst();
    QCOMPARE(calendars.name, QStringLiteral("calendars"));
    QCOMPARE(calendars.attribute(QStringLiteral("filter_disabled")), QStringLiteral("0"));
    QCOMPARE(calendars.children.size(), 2);
    QCOMPARE(calendars.children.at(0).attribute(QStringLiteral("title")), work.title);
    QCOMPARE(calendars.children.at(0).attribute(QStringLiteral("default")), QStringLiteral("1"));
    QCOMPARE(calendars.children.at(0).attribute(QStringLiteral("selected")), QStringLiteral("1"));
    QCOMPARE(calendars.children.at(1).attribute(QStringLiteral("selected")), QStringLiteral("0"));
    QVERIFY(calendars.children.at(1).attribute(QStringLiteral("default")).isEmpty());
}

void TestCalMetadata::categoryChangesAreWrittenForTasksAndNotes()
{
    const QList<CategoryChange> changes = {
        {CategoryChange::Kind::Add, {0, QStringLiteral("Einkauf & Co"), 0x00AA00U}},
        {CategoryChange::Kind::Edit, {2, QStringLiteral("Haus"), 0x123456U}},
        {CategoryChange::Kind::Delete, {3, QStringLiteral("Alt"), 0}},
    };

    const Result<XmlElement> tasks = parseSafeXml(encodeCategoryChanges(changes, false));
    const Result<XmlElement> notes = parseSafeXml(encodeCategoryChanges(changes, true));

    QVERIFY(tasks.hasValue());
    QVERIFY(notes.hasValue());
    QCOMPARE(tasks.value().children.constFirst().name, QStringLiteral("task_categories"));
    QCOMPARE(notes.value().children.constFirst().name, QStringLiteral("note_categories"));
    const QList<XmlElement> &entries = tasks.value().children.constFirst().children;
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).attribute(QStringLiteral("flag")), QStringLiteral("add"));
    QCOMPARE(entries.at(0).attribute(QStringLiteral("name")), QStringLiteral("Einkauf & Co"));
    QCOMPARE(entries.at(0).attribute(QStringLiteral("color")), QStringLiteral("00AA00"));
    QVERIFY(entries.at(0).attribute(QStringLiteral("id")).isEmpty());
    QCOMPARE(entries.at(1).attribute(QStringLiteral("flag")), QStringLiteral("edit"));
    QCOMPARE(entries.at(1).attribute(QStringLiteral("id")), QStringLiteral("2"));
    QCOMPARE(entries.at(2).attribute(QStringLiteral("flag")), QStringLiteral("delete"));
    QCOMPARE(entries.at(2).attribute(QStringLiteral("id")), QStringLiteral("3"));
}

void TestCalMetadata::unsafeOrForeignDocumentsAreRejected_data()
{
    QTest::addColumn<QByteArray>("xml");
    QTest::addColumn<ErrorCode>("code");

    QTest::newRow("entity") << QByteArray("<?xml version=\"1.0\"?><!DOCTYPE m [<!ENTITY x "
                                          "\"y\">]><metadata>&x;</metadata>")
                            << ErrorCode::InvalidInput;
    QTest::newRow("other root") << QByteArray("<accounts/>") << ErrorCode::ProtocolError;
    QTest::newRow("no XML") << QByteArray("Total-Records:3") << ErrorCode::InvalidInput;
}

void TestCalMetadata::unsafeOrForeignDocumentsAreRejected()
{
    QFETCH(QByteArray, xml);
    QFETCH(ErrorCode, code);

    QCOMPARE(parseCalMetadata(xml).error().code(), code);
}

QTEST_GUILESS_MAIN(TestCalMetadata)

#include "tst_calmetadata.moc"
