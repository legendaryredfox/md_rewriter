#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#include "clirunner.h"
#include "fakefirmware.h"

namespace {

QByteArray randomBytes(int n, quint32 seed)
{
    QRandomGenerator g(seed);
    QByteArray b(n, 0);
    for (int i = 0; i < n; ++i)
        b[i] = static_cast<char>(g.bounded(256));
    return b;
}

struct Run {
    int code;
    QString out;
    QString err;
    QJsonObject json() const { return QJsonDocument::fromJson(out.toUtf8()).object(); }
};

Run cli(const QStringList &args, QIODevice *dev = nullptr)
{
    QString o, e;
    QTextStream out(&o), err(&e);
    CliRunner r(out, err);
    const int code = r.run(args, dev);
    return { code, o, e };
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

} // namespace

class TstCli : public QObject
{
    Q_OBJECT
private slots:
    void usageErrors()
    {
        QCOMPARE(cli({}).code, int(CliRunner::Usage));
        QCOMPARE(cli({"nonsense"}).code, int(CliRunner::Usage));
        QCOMPARE(cli({"--bogus", "chips"}).code, int(CliRunner::Usage));
        QCOMPARE(cli({"read-rom", "--out", "x"}).code, int(CliRunner::Usage));     // no size
        QCOMPARE(cli({"erase", "13M", "--yes"}).code, int(CliRunner::Usage));
    }

    void chipsJson()
    {
        const Run r = cli({"chips", "--json"});
        QCOMPARE(r.code, 0);
        QVERIFY(r.json().value("chips").toArray().size() >= 8);
    }

    void destructiveNeedsYes()
    {
        QTemporaryDir d;
        writeFile(d.filePath("a.bin"), randomBytes(0x1000, 1));
        QCOMPARE(cli({"write-rom", d.filePath("a.bin")}).code, int(CliRunner::NeedsConfirm));
        QCOMPARE(cli({"erase", "all"}).code, int(CliRunner::NeedsConfirm));
        QCOMPARE(cli({"write-rom", d.filePath("a.bin"), "--dry-run"}).code, 0);
    }

    void compareFiles()
    {
        QTemporaryDir d;
        writeFile(d.filePath("a"), randomBytes(1000, 1));
        writeFile(d.filePath("b"), randomBytes(1000, 1));
        writeFile(d.filePath("c"), randomBytes(1000, 2));
        QCOMPARE(cli({"compare", d.filePath("a"), d.filePath("b")}).code, 0);
        QCOMPARE(cli({"compare", d.filePath("a"), d.filePath("c"), "--json"}).code, int(CliRunner::Failed));
        QCOMPARE(cli({"compare", d.filePath("a"), d.filePath("missing")}).code, int(CliRunner::FileError));
    }

    void readRomRefusesToOverwrite()
    {
        QTemporaryDir d;
        writeFile(d.filePath("out.bin"), "keep");
        FakeFirmware fw;
        const Run r = cli({"read-rom", "--size", "512K", "--out", d.filePath("out.bin")}, &fw);
        QCOMPARE(r.code, int(CliRunner::NeedsConfirm));
        QVERIFY(fw.commands.isEmpty());   // never touched the device
        QFile f(d.filePath("out.bin"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("keep"));
    }

    void readDetectWriteVerify()
    {
        QTemporaryDir d;
        FakeFirmware fw;
        fw.rom = randomBytes(0x80000, 7);
        const QString out = d.filePath("dump.bin");

        Run r = cli({"read-rom", "--size", "512K", "--out", out, "--json", "-q"}, &fw);
        QCOMPARE(r.code, 0);
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), fw.rom);

        r = cli({"detect", "--json"}, &fw);
        QVERIFY2(r.code == 0, qPrintable(r.err + r.out));
        QCOMPARE(r.json().value("flashId").toString(), QString("C2CB"));

        QCOMPARE(cli({"erase", "all", "--yes", "-q"}, &fw).code, 0);
        fw.rom.fill(char(0xFF));   // the fake does not model the erased state
        const QByteArray image = randomBytes(0x20000, 9);
        writeFile(d.filePath("game.bin"), image);
        r = cli({"write-rom", d.filePath("game.bin"), "--yes", "--json", "-q"}, &fw);
        QVERIFY2(r.code == 0, qPrintable(r.err + r.out));
        QVERIFY(r.json().value("verified").toBool());
        QCOMPARE(fw.rom.left(image.size()), image);

        r = cli({"verify", d.filePath("game.bin")}, &fw);
        QCOMPARE(r.code, 0);
        writeFile(d.filePath("other.bin"), randomBytes(0x20000, 10));
        r = cli({"verify", d.filePath("other.bin")}, &fw);
        QCOMPARE(r.code, int(CliRunner::Failed));
    }

    void badWriteIsReported()
    {
        QTemporaryDir d;
        FakeFirmware fw;
        fw.rom = QByteArray(0x80000, char(0xFF));
        fw.corruptRomOffset = 0x100;
        writeFile(d.filePath("game.bin"), randomBytes(0x4000, 3));
        const Run r = cli({"write-rom", d.filePath("game.bin"), "--yes", "-q"}, &fw);
        QCOMPARE(r.code, int(CliRunner::Failed));
    }

    void silentDeviceIsNoDevice()
    {
        FakeFirmware fw;
        fw.silent = true;
        const Run r = cli({"info", "--timeout", "1"}, &fw);
        QCOMPARE(r.code, int(CliRunner::NoDevice));
    }
};

QTEST_MAIN(TstCli)
#include "tst_cli.moc"
