#include <QBuffer>
#include <QRandomGenerator>
#include <QtTest>

#include "devicesession.h"
#include "fakefirmware.h"

using Op = DeviceSession::Op;
using Sev = DeviceSession::Severity;

namespace {

QByteArray randomBytes(int n, quint32 seed)
{
    QRandomGenerator g(seed);
    QByteArray b(n, 0);
    for (int i = 0; i < n; ++i)
        b[i] = static_cast<char>(g.bounded(256));
    return b;
}

// Everything the GUI would see.
struct Harness {
    FakeFirmware fw;
    DeviceSession s;
    struct Done { Op op; bool ok; bool cancelled; };
    QList<Done> done;
    QList<QPair<Sev, QString>> messages;
    QList<int> progress;
    QString deviceText;
    int startedCount = 0;

    Harness()
    {
        s.setDevice(&fw);
        QObject::connect(&s, &DeviceSession::finished, [this](Op op, bool ok, bool c) { done.append({op, ok, c}); });
        QObject::connect(&s, &DeviceSession::message, [this](Sev v, const QString &t) { messages.append({v, t}); });
        QObject::connect(&s, &DeviceSession::progress, [this](int p) { progress.append(p); });
        QObject::connect(&s, &DeviceSession::deviceText, [this](const QString &t) { deviceText += t; });
        QObject::connect(&s, &DeviceSession::started, [this](Op) { ++startedCount; });
    }
    bool waitDone(int n = 1, int ms = 20000)
    {
        QElapsedTimer t;
        t.start();
        while (done.size() < n && t.elapsed() < ms)
            QTest::qWait(5);
        return done.size() >= n;
    }
    bool hasMessage(Sev v, const QString &part) const
    {
        for (const auto &m : messages)
            if (m.first == v && m.second.contains(part))
                return true;
        return false;
    }
};

QByteArray cartRom(quint32 seed, int size = 0x80000)
{
    QByteArray r = randomBytes(size, seed);
    // adversarial content: markers that the old chunk-size heuristic would misread
    r.replace(1000, 8, QByteArray("OK\r\nGK\r\n"));
    r.replace(2000, 4, QByteArray::fromHex("AA55AABB"));
    r.replace(4096, 22, QByteArray("DUMPER ROM FINISH!!!\r\n"));
    return r;
}

class FailingDevice : public QIODevice
{
public:
    FailingDevice() { open(QIODevice::WriteOnly); }
protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *, qint64) override { return -1; }
};

} // namespace

class TstSession : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qRegisterMetaType<DeviceSession::Op>("DeviceSession::Op");
        qRegisterMetaType<DeviceSession::Severity>("DeviceSession::Severity");
    }

    void connectOk()
    {
        Harness h;
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QCOMPARE(h.fw.commands.value(0), QByteArray::fromHex("0CAA55AABB"));
    }

    void connectWithFirmware1_0HasNoVersionAndNo8M()
    {
        Harness h;
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY(h.s.firmwareVersion().isEmpty());
        QVERIFY(!h.s.canRead8M());
    }

    void connectLearnsTheFirmwareVersion()
    {
        Harness h;
        h.fw.fwVersion = "1.1.0-test";
        QStringList versions;
        QObject::connect(&h.s, &DeviceSession::firmwareVersionChanged, [&](const QString &v) { versions << v; });
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QCOMPARE(h.s.firmwareVersion(), QString("1.1.0-test"));
        QVERIFY(h.s.canRead8M());
        QCOMPARE(versions, QStringList{"1.1.0-test"});
        h.s.resetFirmwareInfo();
        QVERIFY(!h.s.canRead8M());
        QCOMPARE(versions.size(), 2);
    }

    void connectToNewFirmwareDoesNotWaitForTheGracePeriod()
    {
        Harness h;
        h.fw.fwVersion = "1.1.0";
        QElapsedTimer t;
        t.start();
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        QVERIFY2(t.elapsed() < 350, "FWVER ends the connect at once");
    }

    void readRom8MFromNewFirmware()
    {
        Harness h;
        h.fw.fwVersion = "1.1.0";
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        h.fw.rom = randomBytes(0x800000, 50);
        QByteArray out;
        QBuffer buf(&out); buf.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::extendedRomSize(), &buf));
        QVERIFY(h.waitDone(2, 120000));
        QVERIFY(h.done[1].ok);
        QCOMPARE(out.size(), 0x800000);
        QVERIFY(out == h.fw.rom);
        QCOMPARE(h.fw.commands.last(), QByteArray::fromHex("0AAA55AABB05"));
    }

    void readRomSsf2FromNewFirmware()
    {
        Harness h;
        h.fw.fwVersion = "1.1.0";
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        h.fw.rom = randomBytes(0x500000, 53);                   // 5 MiB: 10 pages
        QByteArray out;
        QBuffer buf(&out); buf.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRomSsf2(10, &buf));
        QVERIFY(h.waitDone(2, 120000));
        QVERIFY(h.done[1].ok);
        QVERIFY(out == h.fw.rom);
        QCOMPARE(h.fw.commands.last(), QByteArray::fromHex("3AAA55AABB0A"));
    }

    void readRomSsf2IsRefusedOnFirmware1_0AndBadPages()
    {
        Harness h;
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        QByteArray out;
        QBuffer buf(&out); buf.open(QIODevice::WriteOnly);
        const int before = h.fw.commands.size();
        QVERIFY(!h.s.readRomSsf2(10, &buf));
        QVERIFY(h.hasMessage(Sev::Error, "firmware 1.1"));
        QVERIFY(!h.s.readRomSsf2(0, &buf));
        QVERIFY(!h.s.readRomSsf2(65, &buf));
        QTest::qWait(30);
        QCOMPARE(h.fw.commands.size(), before);                 // 1.0 would ignore the command and time out
    }

    void readRom8MIsRefusedOnFirmware1_0()
    {
        Harness h;
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        QByteArray out;
        QBuffer buf(&out); buf.open(QIODevice::WriteOnly);
        const int before = h.fw.commands.size();
        QVERIFY(!h.s.readRom(Protocol::extendedRomSize(), &buf));
        QVERIFY(h.hasMessage(Sev::Error, "firmware 1.1"));
        QTest::qWait(30);
        QCOMPARE(h.fw.commands.size(), before);   // nothing sent: 1.0 would have dumped 512K
    }

    void erase8MIsAFullChipEraseOnAnyFirmware()
    {
        Harness h;
        QVERIFY(h.s.eraseBySize(Protocol::extendedRomSize()));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QCOMPARE(h.fw.commands.last(), QByteArray::fromHex("1EAA55AABB05"));
        QCOMPARE(h.progress.first(), -1);   // no dots to count
    }

    void connectNoBanner()
    {
        Harness h;
        h.s.setIdleTimeoutMs(150);
        h.fw.sendBanner = false;
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        QVERIFY(!h.done[0].ok);
        QVERIFY(h.hasMessage(Sev::Warning, "did not answer"));
    }

    void detect()
    {
        Harness h;
        QVERIFY(h.s.detect());
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY(h.deviceText.contains("MX29LV640EB MD FLASH CART"));
    }

    void nothingStartsInTheSettleWindowAfterDetect()
    {
        Harness h;
        h.fw.deafAfterDetectMs = 100;
        QVERIFY(h.s.detect());
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY(h.s.busy() == false);   // finished() is only emitted after the settle time
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone(2));
        QCOMPARE(h.fw.droppedBytes, 0);
        QVERIFY(h.done[1].ok);
    }

    void readRomAnyFragmentation_data()
    {
        QTest::addColumn<int>("maxChunk");
        QTest::newRow("whole") << 0;
        QTest::newRow("jumbo") << 4000;
        QTest::newRow("mtu64") << 64;
        QTest::newRow("tiny") << 7;
    }
    void readRomAnyFragmentation()
    {
        QFETCH(int, maxChunk);
        Harness h;
        h.fw.rom = cartRom(11);
        h.fw.setFragmentation(maxChunk, 99);
        QBuffer out;
        out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QVERIFY(h.waitDone());
        QVERIFY2(h.done[0].ok, "read failed");
        QCOMPARE(out.data().size(), h.fw.rom.size());
        QVERIFY(out.data() == h.fw.rom);
        QCOMPARE(h.fw.commands.value(0), QByteArray::fromHex("0AAA55AABB01"));
        QCOMPARE(h.progress.last(), 100);
        // progress never decreases and never exceeds 100
        int prev = -1;
        for (int p : h.progress) { QVERIFY(p >= prev && p <= 100); prev = p; }
        QVERIFY(h.deviceText.contains("PUSH SAVE GAME BUTTON!!!"));
    }

    void readRom1M()
    {
        Harness h;
        h.fw.rom = cartRom(12, 0x100000);
        h.fw.setFragmentation(1500, 5);
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[1], &out));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY(out.data() == h.fw.rom);
    }

    void readRomDroppedBlockFails()
    {
        Harness h;
        h.s.setIdleTimeoutMs(300);
        h.fw.rom = cartRom(13);
        h.fw.dropDumpBlock = 100;
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QVERIFY(h.waitDone());
        QVERIFY2(!h.done[0].ok, "a dump with a lost block must be reported as failed");
    }

    void readRomDeviceStops()
    {
        Harness h;
        h.s.setIdleTimeoutMs(300);
        h.fw.rom = cartRom(14);
        h.fw.stopAfterDumpBlocks = 10;
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QVERIFY(h.waitDone());
        QVERIFY(!h.done[0].ok);
        QVERIFY(h.hasMessage(Sev::Error, "stopped answering"));
    }

    void readSave_data()
    {
        QTest::addColumn<int>("index");
        QTest::newRow("32K") << 0;
        QTest::newRow("8K") << 1;
    }
    void readSave()
    {
        QFETCH(int, index);
        Harness h;
        const auto size = Protocol::sramSizes()[index];
        h.fw.sram = randomBytes(static_cast<int>(size.bytes), 21);
        h.fw.setFragmentation(100, 3);
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readSave(size, &out));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY(out.data() == h.fw.sram);
    }

    void writeRom512K()
    {
        Harness h;
        h.fw.rom = QByteArray(0x80000, '\xFF');
        h.fw.setFragmentation(300, 8);
        QByteArray image = randomBytes(0x80000, 31);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QCOMPARE(h.fw.blocksWritten, 512);
        QVERIFY(h.fw.rom == image);
        QVERIFY2(h.fw.errors.isEmpty(), qPrintable(h.fw.errors.join("; ")));
        QCOMPARE(h.fw.commands.last(), QByteArray::fromHex("0FAA55AABB"));
        QCOMPARE(h.fw.commands.at(1), QByteArray::fromHex("0BAA55AABB0100"));
        QCOMPARE(h.fw.commands.at(64), QByteArray::fromHex("0BAA55AABB0001"));
        QCOMPARE(h.progress.last(), 100);
    }

    void writeRom3M()
    {
        Harness h;
        h.fw.rom = QByteArray(0x300000, '\xFF');
        QByteArray image = randomBytes(0x300000, 32);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY(h.fw.rom == image);
    }

    void writeRomNonStandardSize()
    {
        Harness h;
        h.fw.rom = QByteArray(0x100000, '\xFF');       // an erased 1 MiB cartridge
        QByteArray image = randomBytes(0xC0000, 40);   // 768 KiB
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QCOMPARE(h.fw.blocksWritten, 768);
        QVERIFY(h.fw.rom.left(image.size()) == image);
        QVERIFY(h.fw.rom.mid(image.size()) == QByteArray(0x100000 - image.size(), '\xFF'));   // the rest stays blank
    }

    void writeRomAbove4MiBNeedsFirmware11()
    {
        Harness h;
        QByteArray image = randomBytes(0x500000, 51);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(!h.s.writeRom(&in));                       // firmware 1.0 (no FWVER): refused before anything is sent
        QVERIFY(h.hasMessage(Sev::Error, "firmware 1.1"));
        QCOMPARE(h.fw.blocksWritten, 0);
    }

    void writeRom8MiBWithVerify()
    {
        Harness h;
        h.fw.fwVersion = "1.1.0";
        QVERIFY(h.s.connectDevice());
        QVERIFY(h.waitDone());
        h.done.clear();
        h.fw.rom = QByteArray(0x800000, '\xFF');
        QByteArray image = randomBytes(0x500000, 52);       // 5 MiB: read back as 8M (code 5)
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in, true));
        QVERIFY(h.waitDone(1, 120000));
        QVERIFY2(h.done[0].ok, "write + verify of 5 MiB");
        QCOMPARE(h.fw.blocksWritten, 5 * 1024);
        QVERIFY(h.hasMessage(Sev::Success, "Verified"));
        QVERIFY(h.fw.rom.left(image.size()) == image);
        QVERIFY2(h.fw.errors.isEmpty(), qPrintable(h.fw.errors.join("; ")));
    }

    void writeRomWithVerifyOk()
    {
        Harness h;
        h.fw.rom = QByteArray(0x80000, '\xFF');
        h.fw.setFragmentation(500, 9);
        QByteArray image = randomBytes(0x40000, 41);   // 256 KiB: read back as 512K
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in, true));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QCOMPARE(h.done.size(), 1);                       // one finished() for write + verify
        QCOMPARE(h.done[0].op, Op::WriteRom);
        QCOMPARE(h.fw.blocksWritten, 256);
        QVERIFY(h.hasMessage(Sev::Success, "Game written"));
        QVERIFY(h.hasMessage(Sev::Success, "Verified"));
        QVERIFY2(h.fw.errors.isEmpty(), qPrintable(h.fw.errors.join("; ")));
        // the read-back used the normal dump command, 512K code, right after the clear-buffers command
        QCOMPARE(h.fw.commands.last(), QByteArray::fromHex("0AAA55AABB01"));
        QCOMPARE(h.fw.commands.at(h.fw.commands.size() - 2), QByteArray::fromHex("0FAA55AABB"));
        QCOMPARE(h.progress.last(), 100);
    }

    void writeRomVerifyReadsFourMiBFor3MiB()
    {
        Harness h;
        h.fw.rom = QByteArray(0x300000, '\xFF');
        QByteArray image = randomBytes(0x300000, 42);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in, true));
        QVERIFY(h.waitDone(1, 60000));
        QVERIFY(h.done[0].ok);
        QCOMPARE(h.fw.commands.last(), QByteArray::fromHex("0AAA55AABB04"));
    }

    void writeRomVerifyCatchesABadWrite()
    {
        Harness h;
        h.fw.rom = QByteArray(0x80000, '\xFF');
        h.fw.corruptRomOffset = 0x1234;
        QByteArray image = randomBytes(0x80000, 43);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in, true));
        QVERIFY(h.waitDone());
        QVERIFY(!h.done[0].ok);
        QVERIFY(h.hasMessage(Sev::Success, "Game written"));
        QVERIFY2(h.hasMessage(Sev::Error, "Verification failed"), "must say it failed");
        QVERIFY(h.hasMessage(Sev::Error, "0x1234"));
        QVERIFY(h.hasMessage(Sev::Error, "1 bytes differ"));
    }

    void writeRomStopsOnAWriteTimeoutFromNewFirmware()
    {
        Harness h;
        h.fw.rom = QByteArray(0x80000, '\xFF');
        h.fw.failRomWriteAtBlock = 3;
        QByteArray image = randomBytes(0x80000, 45);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in, true));
        QVERIFY(h.waitDone(1, 5000));                 // well before the 10 s idle timeout
        QVERIFY(!h.done[0].ok);
        QVERIFY(h.hasMessage(Sev::Error, "did not finish programming"));
        QCOMPARE(h.fw.blocksWritten, 4);              // blocks 0..3 reached the device, then it stopped
        QVERIFY2(!h.hasMessage(Sev::Success, "Game written"), "must not claim success");
        QVERIFY(h.s.currentOp() == Op::None);
    }

    void writeRomWithoutVerifyDoesNotReadBack()
    {
        Harness h;
        h.fw.rom = QByteArray(0x80000, '\xFF');
        QByteArray image = randomBytes(0x40000, 44);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in, false));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        for (const QByteArray &c : h.fw.commands)
            QVERIFY(static_cast<quint8>(c.at(0)) != 0x0A);
    }

    void detectNamesChips_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("line");
        QTest::addColumn<int>("severity");
        QTest::addColumn<QString>("expected");
        QTest::newRow("supported") << "C2CB" << "MX29LV640EB MD FLASH CART" << int(Sev::Success) << "Supported";
        QTest::newRow("st chip firmware does not list") << "20DF" << "NO FIND NGP CARD" << int(Sev::Info) << "M29W640DB";
        QTest::newRow("sst chip firmware does not list") << "BF6D" << "NO FIND NGP CARD" << int(Sev::Info) << "SST39VF6401B";
        QTest::newRow("unconfirmed id says so") << "207E" << "NO FIND NGP CARD" << int(Sev::Info) << "not confirmed";
        QTest::newRow("m29w320 same commands") << "2057" << "NO FIND NGP CARD" << int(Sev::Info) << "M29W320EB";
        QTest::newRow("5555h chip, firmware does not list it") << "C2F9" << "NO FIND NGP CARD" << int(Sev::Warning) << "needs firmware 1.1";
        QTest::newRow("intel chip, firmware does not list it") << "8971" << "NO FIND NGP CARD" << int(Sev::Warning) << "needs firmware 1.1";
        QTest::newRow("5555h chip, new firmware lists it") << "C2F9" << "MX29L3211 MD FLASH CART" << int(Sev::Success) << "Supported";
        QTest::newRow("unknown") << "FFFF" << "NO FIND NGP CARD" << int(Sev::Warning) << "Chip not in the list";
        QTest::newRow("unknown chip with CFI, AMD") << "AB34" << "CFI:SIZE=8192KB CMDSET=2\r\nNO FIND NGP CARD" << int(Sev::Info) << "8 MiB, AMD/Fujitsu";
        QTest::newRow("unknown chip with CFI, Intel, 512 KiB") << "AB34" << "CFI:SIZE=512KB CMDSET=1\r\nNO FIND NGP CARD" << int(Sev::Info) << "512 KiB, Intel/Sharp";
        QTest::newRow("unknown chip with CFI is still not in the list") << "AB34" << "CFI:SIZE=8192KB CMDSET=2\r\nNO FIND NGP CARD" << int(Sev::Warning) << "Chip not in the list";
    }
    void detectNamesChips()
    {
        QFETCH(QString, id);
        QFETCH(QString, line);
        QFETCH(int, severity);
        QFETCH(QString, expected);
        Harness h;
        h.fw.flashId = id;
        h.fw.chipLine = line;
        QVERIFY(h.s.detect());
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY2(h.hasMessage(static_cast<Sev>(severity), expected), qPrintable(h.messages.isEmpty() ? QString("no message") : h.messages.last().second));
    }

    void writeRomRejectsBadSize_data()
    {
        QTest::addColumn<int>("size");
        QTest::newRow("empty") << 0;
        QTest::newRow("tiny") << 100;
        QTest::newRow("odd") << 0x80001;
        QTest::newRow("overEightMiB") << 0x800002;
    }
    void writeRomRejectsBadSize()
    {
        QFETCH(int, size);
        Harness h;
        QByteArray image(size, 'x');
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(!h.s.writeRom(&in));
        QVERIFY(h.hasMessage(Sev::Error, "not supported"));
        QTest::qWait(30);
        QVERIFY2(h.fw.commands.isEmpty(), "nothing may be sent for a rejected file");
        QCOMPARE(h.startedCount, 0);
        QVERIFY(!h.s.busy());
    }

    void writeSave_data()
    {
        QTest::addColumn<int>("size");
        QTest::newRow("8K") << 0x2000;
        QTest::newRow("32K") << 0x8000;
    }
    void writeSave()
    {
        QFETCH(int, size);
        Harness h;
        h.fw.sram = QByteArray(size, '\0');
        QByteArray image = randomBytes(size, 41);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeSave(&in));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY(h.fw.sram == image);
        QCOMPARE(h.fw.commands.first().at(0), char(0x1B));
    }

    void eraseBySize()
    {
        Harness h;
        h.fw.setFragmentation(3, 1);
        QVERIFY(h.s.eraseBySize(Protocol::romSizes()[0]));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QCOMPARE(h.fw.commands.first(), QByteArray::fromHex("1EAA55AABB01"));
        QVERIFY(h.progress.contains(99) || h.progress.contains(93));
        QCOMPARE(h.progress.last(), 100);
    }

    void eraseBySizeFails()
    {
        Harness h;
        h.fw.eraseFails = true;
        QVERIFY(h.s.eraseBySize(Protocol::romSizes()[0]));
        QVERIFY(h.waitDone());
        QVERIFY(!h.done[0].ok);
        QVERIFY(h.hasMessage(Sev::Error, "did not finish erasing"));
    }

    void eraseAll()
    {
        Harness h;
        QVERIFY(h.s.eraseAll());
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QVERIFY(h.progress.contains(-1));
        QCOMPARE(h.fw.commands.first(), QByteArray::fromHex("0EAA55AABB"));
    }

    void eraseAllFails()
    {
        Harness h;
        h.fw.eraseFails = true;
        QVERIFY(h.s.eraseAll());
        QVERIFY(h.waitDone());
        QVERIFY(!h.done[0].ok);
    }

    void cannotCancelErase()
    {
        Harness h;
        QVERIFY(h.s.eraseAll());
        QVERIFY(!h.s.canCancel());
        QVERIFY(h.waitDone());
    }

    void cancelReadKeepsStreamInSync()
    {
        Harness h;
        h.fw.rom = cartRom(51);
        h.fw.setFragmentation(2000, 4);
        h.fw.holdAfterReplies = 1 + 20;          // start line + exactly 20 blocks, then the device pauses
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QTRY_VERIFY_WITH_TIMEOUT(out.size() == 20 * 1024, 5000);
        QVERIFY(h.done.isEmpty());
        h.s.cancel();
        h.fw.resume();                           // the device finishes its dump in the background
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].cancelled);
        QVERIFY(!h.done[0].ok);
        QCOMPARE(out.size(), qint64(20 * 1024)); // nothing more was written after the cancel
        // The stream stayed in sync: the next operation works.
        QVERIFY(h.s.detect());
        QVERIFY(h.waitDone(2));
        QVERIFY(h.done[1].ok);
    }

    void cancelWrite()
    {
        Harness h;
        h.fw.rom = QByteArray(0x80000, '\xFF');
        h.fw.setFragmentation(50, 4);
        h.fw.holdAfterReplies = 5;               // acknowledge 5 blocks, then go quiet
        QByteArray image = randomBytes(0x80000, 61);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in));
        QTRY_VERIFY_WITH_TIMEOUT(h.fw.blocksWritten == 6, 5000);   // the 6th block is in flight, unacknowledged
        QVERIFY(h.done.isEmpty());
        h.s.cancel();
        h.fw.resume();
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].cancelled);
        QCOMPARE(h.fw.blocksWritten, 6);         // no further block was sent
        QCOMPARE(h.fw.commands.last(), QByteArray::fromHex("0FAA55AABB"));
        QVERIFY(h.hasMessage(Sev::Warning, "partial image"));
        QVERIFY(h.fw.errors.isEmpty());
    }

    void deviceLostMidRead()
    {
        Harness h;
        h.fw.rom = cartRom(71);
        h.fw.setFragmentation(500, 4);
        h.fw.holdAfterReplies = 1 + 10;
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QTRY_VERIFY_WITH_TIMEOUT(out.size() == 10 * 1024, 5000);
        h.s.deviceLost("cable pulled");
        QVERIFY(h.waitDone());
        QVERIFY(!h.done[0].ok);
        QVERIFY(h.hasMessage(Sev::Error, "cable pulled"));
        QVERIFY(!h.s.busy());
    }

    void secondOperationRefused()
    {
        Harness h;
        QVERIFY(h.s.detect());
        QVERIFY(!h.s.detect());
        QVERIFY(h.hasMessage(Sev::Error, "still running"));
        QVERIFY(h.waitDone());
        QTest::qWait(30);
        QCOMPARE(h.done.size(), 1);
    }

    void notConnectedRefused()
    {
        FakeFirmware fw;
        DeviceSession s;
        s.setDevice(&fw);
        fw.unplug();
        QStringList msgs;
        QObject::connect(&s, &DeviceSession::message, [&](Sev, const QString &t) { msgs << t; });
        QVERIFY(!s.detect());
        QVERIFY(msgs.join(' ').contains("not connected"));
    }

    // The old app connected readyRead on every "Refresh", duplicating each received byte.
    void setDeviceTwiceDoesNotDuplicate()
    {
        Harness h;
        h.s.setDevice(&h.fw);
        h.s.setDevice(&h.fw);
        h.s.setDevice(&h.fw);
        h.fw.rom = cartRom(81);
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].ok);
        QCOMPARE(out.data().size(), h.fw.rom.size());
    }

    void unsolicitedTextWhenIdle()
    {
        Harness h;
        h.fw.inject("hello from device\r\n");
        QTRY_VERIFY(h.deviceText.contains("hello from device"));
        QVERIFY(!h.s.busy());
    }

    void outputWriteFailureFails()
    {
        Harness h;
        h.fw.rom = cartRom(91);
        FailingDevice out;
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QVERIFY(h.waitDone());
        QVERIFY(!h.done[0].ok);
        QVERIFY(h.hasMessage(Sev::Error, "output file"));
    }

    void strayAckKeepsOrdering()
    {
        Harness h;
        h.fw.rom = QByteArray(0x80000, '\xFF');
        QByteArray image = randomBytes(0x80000, 101);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in));
        h.fw.inject("ADD:0x0 WRITE OK\r\n"); // stray extra ack
        QVERIFY(h.waitDone());
        QVERIFY(h.fw.rom == image);
    }
};

QTEST_MAIN(TstSession)
#include "tst_session.moc"
