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

    void writeRomRejectsBadSize_data()
    {
        QTest::addColumn<int>("size");
        QTest::newRow("empty") << 0;
        QTest::newRow("tiny") << 1000;
        QTest::newRow("oneOver") << 0x80001;
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
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QTRY_VERIFY_WITH_TIMEOUT(out.size() > 10000, 5000);
        h.s.cancel();
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].cancelled);
        QVERIFY(!h.done[0].ok);
        QVERIFY(out.size() < 0x80000);
        // The device finished its dump in the background; the next operation must still work.
        QVERIFY(h.s.detect());
        QVERIFY(h.waitDone(2));
        QVERIFY(h.done[1].ok);
    }

    void cancelWrite()
    {
        Harness h;
        h.fw.rom = QByteArray(0x80000, '\xFF');
        h.fw.setFragmentation(50, 4);
        QByteArray image = randomBytes(0x80000, 61);
        QBuffer in(&image); in.open(QIODevice::ReadOnly);
        QVERIFY(h.s.writeRom(&in));
        QTRY_VERIFY_WITH_TIMEOUT(h.fw.blocksWritten > 5, 5000);
        h.s.cancel();
        QVERIFY(h.waitDone());
        QVERIFY(h.done[0].cancelled);
        QVERIFY(h.fw.blocksWritten < 512);
        QCOMPARE(h.fw.commands.last(), QByteArray::fromHex("0FAA55AABB"));
        QVERIFY(h.hasMessage(Sev::Warning, "partial image"));
        QVERIFY(h.fw.errors.isEmpty());
    }

    void deviceLostMidRead()
    {
        Harness h;
        h.fw.rom = cartRom(71);
        h.fw.setFragmentation(500, 4);
        QBuffer out; out.open(QIODevice::WriteOnly);
        QVERIFY(h.s.readRom(Protocol::romSizes()[0], &out));
        QTRY_VERIFY_WITH_TIMEOUT(out.size() > 5000, 5000);
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
