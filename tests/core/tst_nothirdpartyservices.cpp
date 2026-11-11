#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTest>

namespace {

// The QR code the phone scans carries this address; the phone, not this
// program, opens it. It is the one place the maker's host may appear.
QString allowedQrUrl()
{
    return QStringLiteral("https://www.fjsoft.at/mpeclient.php?sid=");
}

// How often the maker's host appears outside the QR address.
qsizetype manufacturerHosts(QString text)
{
    text.remove(allowedQrUrl());
    return text.count(QStringLiteral("fjsoft.at"), Qt::CaseInsensitive);
}

// Classes that reach a server on the internet. The phone is reached over
// QTcpSocket, QUdpSocket and Bluetooth only.
QStringList networkApis(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\b(QNetworkAccessManager|QNetworkRequest|QNetworkReply|"
                       "QRestAccessManager|QHttpMultiPart|QWebSocket|QDnsLookup|QHostInfo|"
                       "QSslSocket|XMLHttpRequest)\\b"));
    QStringList found;
    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    while (it.hasNext())
        found.append(it.next().captured(1));
    return found;
}

struct SourceFile
{
    QString path;
    QString text;
};

QList<SourceFile> sourceFiles()
{
    QList<SourceFile> files;
    const QString root = QStringLiteral(QPM_SOURCE_DIR);
    for (const QString &folder : {QStringLiteral("/src"), QStringLiteral("/qml")}) {
        QDirIterator it(root + folder,
                        {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml"),
                         QStringLiteral("*.js")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile file(it.next());
            if (file.open(QIODevice::ReadOnly))
                files.append({file.fileName(), QString::fromUtf8(file.readAll())});
            else
                files.append({file.fileName(), QString()});
        }
    }
    return files;
}

} // namespace

class TestNoThirdPartyServices : public QObject
{
    Q_OBJECT

public:
    explicit TestNoThirdPartyServices(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void manufacturerHostIsFoundOutsideTheQrUrl_data();
    void manufacturerHostIsFoundOutsideTheQrUrl();
    void networkApisAreFound_data();
    void networkApisAreFound();
    void sourcesNameNoManufacturerHostButTheQrUrl();
    void sourcesUseNoNetworkApiBeyondThePhone();
};

void TestNoThirdPartyServices::manufacturerHostIsFoundOutsideTheQrUrl_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<qsizetype>("expected");

    QTest::newRow("address service") << QStringLiteral("\"http://www.fjsoft.at/getlocalip.php\"")
                                     << qsizetype(1);
    QTest::newRow("upper case") << QStringLiteral("WWW.FJSOFT.AT") << qsizetype(1);
    QTest::newRow("qr url") << QStringLiteral("\"https://www.fjsoft.at/mpeclient.php?sid=%1\"")
                            << qsizetype(0);
    QTest::newRow("qr url and update check")
        << QStringLiteral("https://www.fjsoft.at/mpeclient.php?sid= https://fjsoft.at/version")
        << qsizetype(1);
    QTest::newRow("app package name") << QStringLiteral("com.fjsoft.myphoneexplorer.client")
                                      << qsizetype(0);
}

void TestNoThirdPartyServices::manufacturerHostIsFoundOutsideTheQrUrl()
{
    QFETCH(QString, text);
    QFETCH(qsizetype, expected);

    QCOMPARE(manufacturerHosts(text), expected);
}

void TestNoThirdPartyServices::networkApisAreFound_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("http client") << QStringLiteral("QNetworkAccessManager manager;")
                                 << QStringList{QStringLiteral("QNetworkAccessManager")};
    QTest::newRow("qml request") << QStringLiteral("const request = new XMLHttpRequest();")
                                 << QStringList{QStringLiteral("XMLHttpRequest")};
    QTest::newRow("name lookup") << QStringLiteral("QHostInfo::lookupHost(name, this, slot);")
                                 << QStringList{QStringLiteral("QHostInfo")};
    QTest::newRow("phone socket") << QStringLiteral("QTcpSocket socket; QUdpSocket udp;")
                                  << QStringList();
}

void TestNoThirdPartyServices::networkApisAreFound()
{
    QFETCH(QString, text);
    QFETCH(QStringList, expected);

    QCOMPARE(networkApis(text), expected);
}

void TestNoThirdPartyServices::sourcesNameNoManufacturerHostButTheQrUrl()
{
    const QList<SourceFile> files = sourceFiles();

    QVERIFY2(files.size() > 20, qPrintable(QString::number(files.size())));
    for (const SourceFile &file : files) {
        QVERIFY2(!file.text.isEmpty(), qPrintable(file.path));
        QVERIFY2(manufacturerHosts(file.text) == 0, qPrintable(file.path));
    }
}

void TestNoThirdPartyServices::sourcesUseNoNetworkApiBeyondThePhone()
{
    const QList<SourceFile> files = sourceFiles();

    QVERIFY(!files.isEmpty());
    for (const SourceFile &file : files) {
        const QStringList found = networkApis(file.text);
        QVERIFY2(found.isEmpty(), qPrintable(file.path + u' ' + found.join(u',')));
    }
}

QTEST_GUILESS_MAIN(TestNoThirdPartyServices)

#include "tst_nothirdpartyservices.moc"
