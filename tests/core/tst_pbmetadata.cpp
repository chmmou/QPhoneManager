#include "core/codec/PbMetadata.h"

#include <QTest>

using namespace qpm::core;
using namespace qpm::core::codec;

namespace {

// The client's own example from the protocol notes, with placeholder values.
QByteArray clientMetadata()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
           "<metadata version=\"1.0\">\r\n"
           "<accounts filter_disabled=\"0\">\r\n"
           "<account type=\"null\" name=\"null\"  default=\"1\" title=\"Phone\"/>\r\n"
           "<account selected=\"1\" type=\"com.google\" name=\"user@example.org\"  "
           "title=\"Google\"/>\r\n"
           "</accounts>\r\n"
           "<groups>\r\n"
           "<group title=\"Coworkers\" sourceid=\"f\" account_name=\"user@example.org\" "
           "account_type=\"com.google\"/>\r\n"
           "<group title=\"Familie &amp; Freunde\" sourceid=\"6\" "
           "account_name=\"user@example.org\" "
           "account_type=\"com.google\"/>\r\n"
           "</groups>\r\n"
           "<ringtones>\r\n"
           "<ringtonecount>24</ringtonecount>\r\n"
           "<ringtone id=\"107\" default_ringtone=\"1\" title=\"Tune\"/>\r\n"
           "<ringtone id=\"94\" title=\"Arrival\"/>\r\n"
           "<ringtonemediacount>0</ringtonemediacount>\r\n"
           "<ringtone row=\"1\" error=\"Index 0 requested, with a size of 0\"/>\r\n"
           "</ringtones>\r\n"
           "</metadata>";
}

} // namespace

class TestPbMetadata : public QObject
{
    Q_OBJECT

public:
    explicit TestPbMetadata(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void clientMetadataIsRead();
    void accountsAreWrittenForFilterAndDefault();
    void writtenMetadataReadsBack();
    void entityDeclarationIsRejected();
    void otherRootIsRejected();
};

void TestPbMetadata::clientMetadataIsRead()
{
    const Result<PbMetadata> read = parsePbMetadata(clientMetadata());

    QVERIFY(read.hasValue());
    const PbMetadata &metadata = read.value();
    QVERIFY(!metadata.filterDisabled);
    QCOMPARE(metadata.accounts.size(), 2);
    QCOMPARE(metadata.accounts.at(0), (PbAccount{QStringLiteral("null"), QStringLiteral("null"),
                                                 QStringLiteral("Phone"), false, true}));
    QCOMPARE(metadata.accounts.at(1),
             (PbAccount{QStringLiteral("com.google"), QStringLiteral("user@example.org"),
                        QStringLiteral("Google"), true, false}));
    QCOMPARE(metadata.groups.size(), 2);
    QCOMPARE(metadata.groups.at(1).title, QStringLiteral("Familie & Freunde"));
    QCOMPARE(metadata.groups.at(0).accountType, QStringLiteral("com.google"));
    QCOMPARE(metadata.ringtones.size(), 2);
    QVERIFY(metadata.ringtones.at(0).isDefault);
    QCOMPARE(metadata.ringtones.at(1).title, QStringLiteral("Arrival"));
}

void TestPbMetadata::accountsAreWrittenForFilterAndDefault()
{
    PbMetadata metadata;
    metadata.accounts = {
        {QStringLiteral("null"), QStringLiteral("null"), QStringLiteral("Phone"), true, false},
        {QStringLiteral("com.google"), QStringLiteral("a&b@example.org"), QStringLiteral("Google"),
         true, true},
    };
    metadata.groups = {{QStringLiteral("Ignored"), QStringLiteral("1"), QString(), QString()}};

    const QByteArray written = encodePbMetadata(metadata);

    QCOMPARE(written,
             QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                        "<metadata version=\"1.0\">\r\n"
                        "<accounts filter_disabled=\"0\">\r\n"
                        "<account selected=\"1\" type=\"null\" name=\"null\"  title=\"Phone\"/>\r\n"
                        "<account selected=\"1\" type=\"com.google\" name=\"a&amp;b@example.org\""
                        "  default=\"1\"  title=\"Google\"/>\r\n"
                        "</accounts>\r\n"
                        "</metadata>"));
}

void TestPbMetadata::writtenMetadataReadsBack()
{
    const PbMetadata original = parsePbMetadata(clientMetadata()).value();

    const Result<PbMetadata> read = parsePbMetadata(encodePbMetadata(original));

    QVERIFY(read.hasValue());
    QCOMPARE(read.value().accounts, original.accounts);
    QVERIFY(read.value().groups.isEmpty());
}

void TestPbMetadata::entityDeclarationIsRejected()
{
    const Result<PbMetadata> read = parsePbMetadata(
        "<?xml version=\"1.0\"?><!DOCTYPE m [<!ENTITY x \"y\">]><metadata>&x;</metadata>");

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::InvalidInput);
}

void TestPbMetadata::otherRootIsRejected()
{
    const Result<PbMetadata> read = parsePbMetadata("<calendars/>");

    QVERIFY(!read.hasValue());
    QCOMPARE(read.error().code(), ErrorCode::ProtocolError);
}

QTEST_GUILESS_MAIN(TestPbMetadata)

#include "tst_pbmetadata.moc"
