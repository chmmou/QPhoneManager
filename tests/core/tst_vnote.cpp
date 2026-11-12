#include "core/codec/VNote.h"

#include <QTest>
#include <QTimeZone>

using namespace qpm::core;
using namespace qpm::core::codec;

class TestVNote : public QObject
{
    Q_OBJECT

public:
    explicit TestVNote(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void noteIsReadAsThePhoneSendsIt();
    void noteIsWrittenWithCategoryColours();
    void noteRoundTripsThroughThePhoneForm();
    void emptyNoteHasNoBodyLine();
    void objectWithoutNoteIsRejected();
    void noteCategoriesAreReadFromTheMetadata();
    void foreignMetadataIsRejected();
};

void TestVNote::noteIsReadAsThePhoneSendsIt()
{
    const QByteArray data = "BEGIN:VNOTE\r\nBODY;ENCODING=QUOTED-PRINTABLE;CHARSET=UTF-8:Einkauf=0D=0A"
                            "- Äpfel=0D=0A- Brot =3D 2 Euro\r\nLAST-MODIFIED:20250101T120000Z\r\n"
                            "CATEGORIES:Haushalt;Ideen\\; Pläne\r\nX-IRMC-LUID:000000000007\r\nEND:VNOTE";

    const Result<Note> note = parsePhoneNote(data);

    QVERIFY(note.hasValue());
    QCOMPARE(note.value().luid, 7U);
    QCOMPARE(note.value().body, QStringLiteral("Einkauf\r\n- Äpfel\r\n- Brot = 2 Euro"));
    QCOMPARE(note.value().lastModified,
             QDateTime(QDate(2025, 1, 1), QTime(12, 0), QTimeZone::UTC));
    QCOMPARE(note.value().categories,
             QStringList({QStringLiteral("Haushalt"), QStringLiteral("Ideen; Pläne")}));
}

void TestVNote::noteIsWrittenWithCategoryColours()
{
    Note note;
    note.body = QStringLiteral("Zeile 1\nZeile 2");
    note.categories = {QStringLiteral("Ideen")};

    const QByteArray colored = encodePhoneNote(note, {0x00AA00U});
    const QByteArray plain = encodePhoneNote(note, {std::nullopt});

    QVERIFY(colored.startsWith("BEGIN:VNOTE\r\n"));
    QVERIFY(colored.endsWith("END:VNOTE\r\n"));
    QVERIFY(colored.contains("BODY;ENCODING=QUOTED-PRINTABLE:Zeile 1=0AZeile 2\r\n"));
    QVERIFY(colored.contains("CATEGORIES;COLOR=00AA00:Ideen\r\n"));
    QVERIFY(plain.contains("CATEGORIES:Ideen\r\n"));
    QVERIFY(!colored.contains("X-IRMC-LUID"));
    QVERIFY(!colored.contains("LAST-MODIFIED"));
}

void TestVNote::noteRoundTripsThroughThePhoneForm()
{
    Note note;
    note.luid = 12;
    note.body = QStringLiteral("Grüße = Hallo\nzweite Zeile");
    note.lastModified = QDateTime(QDate(2026, 11, 11), QTime(9, 30), QTimeZone::UTC);
    note.categories = {QStringLiteral("Privat"), QStringLiteral("A;B")};

    const Result<Note> back = parsePhoneNote(encodeNoteAsPhoneSendsIt(note));

    QVERIFY(back.hasValue());
    QCOMPARE(back.value(), note);
    QVERIFY(sameNoteContent(back.value(), note));
    Note changed = note;
    changed.body += QStringLiteral("!");
    QVERIFY(!sameNoteContent(changed, note));
}

void TestVNote::emptyNoteHasNoBodyLine()
{
    const QByteArray data = encodePhoneNote(Note(), {});

    QCOMPARE(data, QByteArray("BEGIN:VNOTE\r\nEND:VNOTE\r\n"));
    QVERIFY(parsePhoneNote(data).hasValue());
    QVERIFY(parsePhoneNote(data).value().body.isEmpty());
}

void TestVNote::objectWithoutNoteIsRejected()
{
    QCOMPARE(parsePhoneNote("BEGIN:VCARD\r\nEND:VCARD").error().code(), ErrorCode::ProtocolError);
}

void TestVNote::noteCategoriesAreReadFromTheMetadata()
{
    const QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">"
                           "<note_categories>\r\n<category name=\"Ideen\" color=\"FF8800\" id=\"1\"/>\r\n"
                           "<category name=\"Kein\" color=\"000000\" id=\"0\"/>\r\n"
                           "</note_categories>\r\n</metadata>";
    const QByteArray empty = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<metadata version=\"1.0\">"
                             "<note_categories>\r\n</note_categories>\r\n</metadata>";

    const Result<QList<OrganizerCategory>> categories = parseNoteMetadata(xml);

    QVERIFY(categories.hasValue());
    QCOMPARE(categories.value(),
             QList<OrganizerCategory>({{1, QStringLiteral("Ideen"), 0xFF8800U}}));
    QVERIFY(parseNoteMetadata(empty).hasValue());
    QVERIFY(parseNoteMetadata(empty).value().isEmpty());
}

void TestVNote::foreignMetadataIsRejected()
{
    QCOMPARE(parseNoteMetadata("<notes/>").error().code(), ErrorCode::ProtocolError);
    QCOMPARE(parseNoteMetadata("kein XML").error().code(), ErrorCode::InvalidInput);
}

QTEST_GUILESS_MAIN(TestVNote)

#include "tst_vnote.moc"
