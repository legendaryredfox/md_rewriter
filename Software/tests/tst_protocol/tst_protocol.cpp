#include <QBuffer>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QtTest>

#include "chipdb.h"
#include "filecompare.h"
#include "protocol.h"
#include "romimage.h"
#include "romverifier.h"
#include "streamparser.h"

using namespace Protocol;

namespace {

QByteArray hex(const char *s) { return QByteArray::fromHex(s); }

// A realistic firmware session: banner, dump start, N data blocks, finish.
QByteArray dumpSession(const QByteArray &image, const QByteArray &startLine = "512K ROM DUMP START!!!\r\n")
{
    QByteArray s = startLine;
    s += image;
    s += "DUMPER ROM FINISH!!!\r\nPUSH SAVE GAME BUTTON!!!\r\n";
    return s;
}

QByteArray randomBytes(int n, quint32 seed)
{
    QRandomGenerator g(seed);
    QByteArray b(n, 0);
    for (int i = 0; i < n; ++i)
        b[i] = static_cast<char>(g.bounded(256));
    return b;
}

struct Capture {
    QByteArray data;
    QStringList lines;
    QString text;
    int complete = 0;
    int outOfSync = 0;
    explicit Capture(StreamParser &p)
    {
        QObject::connect(&p, &StreamParser::dumpData, [this](const QByteArray &d) { data += d; });
        QObject::connect(&p, &StreamParser::lineReceived, [this](const QString &l) { lines << l; });
        QObject::connect(&p, &StreamParser::textReceived, [this](const QString &t) { text += t; });
        QObject::connect(&p, &StreamParser::dumpComplete, [this]() { ++complete; });
        QObject::connect(&p, &StreamParser::outOfSync, [this](const QString &) { ++outOfSync; });
    }
};

} // namespace

class TstProtocol : public QObject
{
    Q_OBJECT
private slots:
    // ---- command encoding: golden bytes, as produced by the old application ----
    void commandsGolden_data()
    {
        QTest::addColumn<QByteArray>("actual");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("connect")  << buildCommand(Connect) << hex("0CAA55AABB");
        QTest::newRow("detect")   << buildCommand(Detect) << hex("0DAA55AABB");
        QTest::newRow("eraseAll") << buildCommand(EraseAll) << hex("0EAA55AABB");
        QTest::newRow("clear")    << buildCommand(ClearBuffers) << hex("0FAA55AABB");
        QTest::newRow("dump512K") << buildCommand(DumpRom, hex("01")) << hex("0AAA55AABB01");
        QTest::newRow("dump1M")   << buildCommand(DumpRom, hex("02")) << hex("0AAA55AABB02");
        QTest::newRow("dump4M")   << buildCommand(DumpRom, hex("04")) << hex("0AAA55AABB04");
        QTest::newRow("sram32K")  << buildCommand(DumpSram, hex("01")) << hex("1AAA55AABB01");
        QTest::newRow("sram8K")   << buildCommand(DumpSram, hex("02")) << hex("1AAA55AABB02");
        QTest::newRow("erase1M")  << buildCommand(EraseBySize, hex("02")) << hex("1EAA55AABB02");
        QTest::newRow("block0")   << buildWriteBlockCommand(WriteRomBlock, 0) << hex("0BAA55AABB0000");
        QTest::newRow("block63")  << buildWriteBlockCommand(WriteRomBlock, 63) << hex("0BAA55AABB3F00");
        QTest::newRow("block64")  << buildWriteBlockCommand(WriteRomBlock, 64) << hex("0BAA55AABB0001");
        QTest::newRow("block4095")<< buildWriteBlockCommand(WriteRomBlock, 4095) << hex("0BAA55AABB3F3F");
        QTest::newRow("sramBlk")  << buildWriteBlockCommand(WriteSramBlock, 5) << hex("1BAA55AABB0500");
    }
    void commandsGolden()
    {
        QFETCH(QByteArray, actual);
        QFETCH(QByteArray, expected);
        QCOMPARE(actual, expected);
    }

    void sizeTables()
    {
        QCOMPARE(romSizes().size(), 4);
        QCOMPARE(romSizes()[0].bytes, 0x80000u);
        QCOMPARE(romSizes()[3].code, quint8(4));
        // erase steps: 8 small steps then 0x8000 steps (matches the firmware loop)
        QCOMPARE(romSizes()[0].eraseSectors, 15);
        QCOMPARE(romSizes()[1].eraseSectors, 23);
        QCOMPARE(romSizes()[2].eraseSectors, 39);
        QCOMPARE(romSizes()[3].eraseSectors, 71);
        QCOMPARE(sramSizes()[0].code, quint8(0x01)); // 32K
        QCOMPARE(sramSizes()[1].code, quint8(0x02)); // 8K
        QCOMPARE(extendedRomSize().code, quint8(0x05));
        QCOMPARE(extendedRomSize().bytes, quint32(8 * 1024 * 1024));
        QVERIFY(isWritableRomSize(0x300000));
        QVERIFY(isWritableRomSize(0x40000));      // 256 KiB
        QVERIFY(isWritableRomSize(0x180000));     // 1.5 MiB
        QVERIFY(isWritableRomSize(0x400000));
        QVERIFY(!isWritableRomSize(0x300001));    // odd
        QVERIFY(isWritableRomSize(0x400002));     // above 4 MiB: allowed here, the session wants firmware 1.1
        QVERIFY(isWritableRomSize(0x800000));
        QVERIFY(!isWritableRomSize(0x800002));    // over 8 MiB
        QVERIFY(!isWritableRomSize(0x1FE));       // smaller than vector table + header
        QVERIFY(!isWritableRomSize(0));
        QVERIFY(isWritableSramSize(0x2000));
        QVERIFY(isWritableSramSize(0x8000));
        QVERIFY(!isWritableSramSize(0x4000));
    }

    void blocks()
    {
        QCOMPARE(blockCount(0), 0u);
        QCOMPARE(blockCount(1), 1u);
        QCOMPARE(blockCount(1024), 1u);
        QCOMPARE(blockCount(1025), 2u);
        QCOMPARE(blockCount(0x80000), 512u);
        const QByteArray p = padBlock(QByteArray("abc"));
        QCOMPARE(p.size(), 1024);
        QCOMPARE(p.left(3), QByteArray("abc"));
        QCOMPARE(p.at(3), char(0xFF));
        QCOMPARE(padBlock(QByteArray(1024, 'x')).size(), 1024);
    }

    void classify_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<int>("kind");
        auto row = [](const char *n, const char *l, LineKind k) { QTest::newRow(n) << QString::fromLatin1(l) << int(k); };
        row("banner", "FlashMaster MD Dumper is connected", LineKind::ConnectBanner);
        row("start512", "512K ROM DUMP START!!!", LineKind::DumpStart);
        row("start32", "32K RAM DUMP START!!!", LineKind::DumpStart);
        row("start8", "8K ROM DUMP START!!!", LineKind::DumpStart);
        row("finishRom", "DUMPER ROM FINISH!!!", LineKind::DumpRomFinished);
        row("finishRam", "DUMPER RAM FINISH!!!", LineKind::DumpSramFinished);
        row("ackRom", "ADD:0x400 WRITE OK", LineKind::RomBlockAck);
        row("ackSram", "ADD:0x0 WRITE GK", LineKind::SramBlockAck);
        row("clear", "BUFF IS CLEAR", LineKind::BuffersCleared);
        row("detectOk", "MX29LV640EB MD FLASH CART", LineKind::DetectDone);
        row("detectNo", "NO FIND NGP CARD", LineKind::DetectDone);
        row("eraseOk", "512K ERASE OK!", LineKind::EraseDone);
        row("sectorOk", "SECTORADD:0x0 ERASE OK!", LineKind::EraseDone);
        row("sramErase", "SRAM ERASE FINISH!!!", LineKind::EraseDone);
        row("flashFinishIsNotDone", "FLASH ERASE FINISH!!!", LineKind::Other);
        row("timeout", "FLASH ERASE TIMEOUT!!!", LineKind::EraseFailed);
        row("fwVersion", "FWVER:1.1.0-dev", LineKind::FirmwareVersion);
        row("fwVersionTrailingCr", "FWVER:1.1.0\r", LineKind::FirmwareVersion);
        row("useTime", "USE TIME 12 s", LineKind::EraseProgress);
        row("start", "FLASH ERASE START", LineKind::EraseProgress);
        row("eraseing", "512K ERASEING", LineKind::Other);
        row("push", "PUSH SAVE GAME BUTTON!!!", LineKind::Other);
        row("ackWithoutAddr", "WRITE OK", LineKind::Other);
    }
    void classify()
    {
        QFETCH(QString, line);
        QFETCH(int, kind);
        QCOMPARE(int(classifyLine(line)), kind);
    }

    // ---- stream parser ----
    void parserWholeStream()
    {
        const QByteArray image = randomBytes(512 * 1024, 1);
        StreamParser p;
        Capture c(p);
        p.expectDump(image.size());
        p.feed(dumpSession(image));
        QCOMPARE(c.data, image);
        QCOMPARE(c.complete, 1);
        QCOMPARE(c.outOfSync, 0);
        QVERIFY(c.lines.contains(QStringLiteral("DUMPER ROM FINISH!!!")));
        QVERIFY(c.lines.contains(QStringLiteral("PUSH SAVE GAME BUTTON!!!")));
        QVERIFY(c.text.startsWith(QStringLiteral("512K ROM DUMP START!!!\r\n")));
    }

    // The bug of the old app: dump data was told from text by chunk size. Here every possible
    // split of the stream must give the same result.
    void parserEverySplitPoint()
    {
        // Small image with adversarial content: text markers and the command header inside data.
        QByteArray image = randomBytes(3 * 1024, 2);
        image.replace(100, 6, QByteArray("OK\r\nGK\r\n"), 6);
        image.replace(500, 4, hex("AA55AABB"));
        image.replace(1000, 22, QByteArray("DUMPER ROM FINISH!!!\r\n"), 22);
        const QByteArray stream = dumpSession(image);
        for (int cut = 1; cut < stream.size(); ++cut) {
            StreamParser p;
            Capture c(p);
            p.expectDump(image.size());
            p.feed(stream.left(cut));
            p.feed(stream.mid(cut));
            if (c.data != image || c.complete != 1 || c.outOfSync != 0)
                QFAIL(qPrintable(QStringLiteral("split at %1").arg(cut)));
        }
    }

    void parserOneByteAtATime()
    {
        const QByteArray image = randomBytes(2048, 3);
        const QByteArray stream = dumpSession(image, "1M ROM DUMP START!!!\r\n");
        StreamParser p;
        Capture c(p);
        p.expectDump(image.size());
        for (char ch : stream)
            p.feed(QByteArray(1, ch));
        QCOMPARE(c.data, image);
        QCOMPARE(c.outOfSync, 0);
    }

    void parserRandomChunks()
    {
        const QByteArray image = randomBytes(64 * 1024, 4);
        const QByteArray stream = dumpSession(image);
        for (quint32 seed = 1; seed <= 50; ++seed) {
            QRandomGenerator g(seed);
            StreamParser p;
            Capture c(p);
            p.expectDump(image.size());
            int pos = 0;
            while (pos < stream.size()) {
                const int n = 1 + static_cast<int>(g.bounded(3000));
                p.feed(stream.mid(pos, n));
                pos += n;
            }
            QCOMPARE(c.data, image);
            QCOMPARE(c.complete, 1);
        }
    }

    void parserTextWhenNotArmed()
    {
        StreamParser p;
        Capture c(p);
        p.feed("512K ROM DUMP START!!!\r\nhello\r\n");
        QVERIFY(c.data.isEmpty());
        QCOMPARE(c.lines.size(), 2);
    }

    void parserDetectsLostBlock()
    {
        // Old firmware could drop a 1 KiB block: FINISH text then arrives inside the data window.
        const QByteArray image = randomBytes(4096, 5);
        const QByteArray shortened = image.left(4096 - 1024);
        StreamParser p;
        Capture c(p);
        p.expectDump(image.size());
        p.feed(dumpSession(shortened));
        QVERIFY2(c.outOfSync > 0 || c.complete == 0, "a lost block must not look like a good dump");
    }

    void parserEraseDots()
    {
        StreamParser p;
        Capture c(p);
        p.feed("512K ERASEING\r\n.....\r\n512K ERASE OK!\r\n");
        QCOMPARE(c.lines.size(), 3); // "512K ERASEING", ".....", "512K ERASE OK!"
        QCOMPARE(c.text.count('.'), 5);
    }

    void parserLongGarbageLineIsBounded()
    {
        StreamParser p;
        Capture c(p);
        p.feed(QByteArray(100000, 'x'));
        p.feed("\r\n");
        QCOMPARE(c.lines.size(), 1);
        QVERIFY(c.lines.first().size() <= 4096);
    }

    // ---- file comparer ----
    void compareIdentical()
    {
        QByteArray a = randomBytes(200000, 6), b = a;
        QBuffer ba(&a), bb(&b);
        ba.open(QIODevice::ReadOnly); bb.open(QIODevice::ReadOnly);
        FileComparer fc(&ba, &bb);
        while (!fc.step(4096)) {}
        QCOMPARE(fc.differenceCount(), 0ull);
        QVERIFY(!fc.failed());
    }

    void compareDifferences_data()
    {
        QTest::addColumn<int>("n");
        for (int n : {1, 20, 21, 255, 256, 300, 70000})
            QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void compareDifferences()
    {
        QFETCH(int, n);
        QByteArray a(100000, 'a'), b(100000, 'a');
        for (int i = 0; i < n; ++i)
            b[i] = 'b';
        QBuffer ba(&a), bb(&b);
        ba.open(QIODevice::ReadOnly); bb.open(QIODevice::ReadOnly);
        FileComparer fc(&ba, &bb, 20);
        while (!fc.step(1000)) {}
        QCOMPARE(fc.differenceCount(), quint64(n)); // no 8-bit counter overflow
        QCOMPARE(fc.firstDifferences().size(), qMin(n, 20));
        QCOMPARE(fc.firstDifferences().first(), 0ull);
    }

    void compareDifferentLengths()
    {
        QByteArray a(1000, 'a'), b(1010, 'a');
        QBuffer ba(&a), bb(&b);
        ba.open(QIODevice::ReadOnly); bb.open(QIODevice::ReadOnly);
        FileComparer fc(&ba, &bb);
        while (!fc.step(300)) {}
        QCOMPARE(fc.differenceCount(), 10ull);
        QCOMPARE(fc.firstDifferences().first(), 1000ull);
    }

    void compareNotOpenFails()
    {
        QByteArray a(10, 'a');
        QBuffer ba(&a), bb(&a);
        FileComparer fc(&ba, &bb);
        QVERIFY(fc.failed());
        QVERIFY(fc.finished());
    }

    // ---------------------------------------------------------------- RomImage

    static QByteArray fakeRom(int size, bool header = true)
    {
        QByteArray r(size, '\xFF');
        if (header) {
            r.replace(0x100, 16, "SEGA MEGA DRIVE ");
            const quint32 end = static_cast<quint32>(size - 1);
            r[0x1A4] = static_cast<char>(end >> 24);
            r[0x1A5] = static_cast<char>(end >> 16);
            r[0x1A6] = static_cast<char>(end >> 8);
            r[0x1A7] = static_cast<char>(end);
            const quint16 sum = RomImage::computeChecksum(r, size);
            r[0x18E] = static_cast<char>(sum >> 8);
            r[0x18F] = static_cast<char>(sum);
        }
        return r;
    }

    // Builds an SMD file out of a plain ROM (the inverse of the conversion).
    static QByteArray toSmd(const QByteArray &plain)
    {
        QByteArray smd(512, '\0');
        smd[0] = static_cast<char>(plain.size() / 16384);
        smd[1] = 3;
        smd[8] = static_cast<char>(0xAA);
        smd[9] = static_cast<char>(0xBB);
        for (int b = 0; b < plain.size() / 16384; ++b) {
            QByteArray blk(16384, '\0');
            for (int i = 0; i < 8192; ++i) {
                blk[i] = plain.at(b * 16384 + 2 * i + 1);
                blk[8192 + i] = plain.at(b * 16384 + 2 * i);
            }
            smd += blk;
        }
        return smd;
    }

    void romImagePlainPassesThrough()
    {
        const QByteArray rom = fakeRom(0x80000);
        const auto r = RomImage::prepare(rom);
        QVERIFY(r.ok);
        QCOMPARE(r.data, rom);
        QVERIFY(r.infos.isEmpty());
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));
    }

    void romImageNonStandardSizeIsAcceptedWithNote()
    {
        for (int size : {0x40000, 0xC0000, 0x180000, 0x280000, 0x300000}) {
            const auto r = RomImage::prepare(fakeRom(size));
            QVERIFY2(r.ok, qPrintable(r.error));
            QCOMPARE(r.data.size(), size);
            QVERIFY2(!r.infos.isEmpty(), qPrintable(QString::number(size)));
        }
    }

    void romImageRejections()
    {
        QVERIFY(!RomImage::prepare(QByteArray()).ok);
        QVERIFY(!RomImage::prepare(QByteArray(0x100, 'x')).ok);                       // too small
        QVERIFY(RomImage::prepare(QByteArray(0x80001, 'x')).error.contains("odd"));    // odd size
        QVERIFY(RomImage::prepare(QByteArray(0x800002, 'x')).error.contains("8 MiB")); // too big
    }

    void romImageCoveringSize()
    {
        QCOMPARE(RomImage::coveringReadSize(0x40000), qint64(0x80000));
        QCOMPARE(RomImage::coveringReadSize(0x80000), qint64(0x80000));
        QCOMPARE(RomImage::coveringReadSize(0x80002), qint64(0x100000));
        QCOMPARE(RomImage::coveringReadSize(0x300000), qint64(0x400000));
        QCOMPARE(RomImage::coveringReadSize(0x400000), qint64(0x400000));
        QCOMPARE(RomImage::coveringReadSize(0x400002), qint64(0x800000));
        QCOMPARE(RomImage::coveringReadSize(0x800000), qint64(0x800000));
        QCOMPARE(RomImage::coveringReadSize(0x800002), qint64(0));
    }

    void romImageSmdIsConverted()
    {
        const QByteArray plain = fakeRom(0x40000);
        const QByteArray smd = toSmd(plain);
        QCOMPARE(smd.size(), 0x40000 + 512);
        QVERIFY(RomImage::looksLikeSmd(smd));
        QVERIFY(!RomImage::looksLikeSmd(plain));
        const auto r = RomImage::prepare(smd);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.data, plain);
        QVERIFY(r.infos.join(' ').contains("Super Magic Drive"));
        QVERIFY(r.warnings.isEmpty());
    }

    void romImageSmdNeedsTheMagicBytes()
    {
        QByteArray smd = toSmd(fakeRom(0x8000));
        smd[8] = 0;
        QVERIFY(!RomImage::looksLikeSmd(smd));
        QVERIFY(RomImage::convertSmd(smd).isEmpty());
    }

    void romImageHeaderWarnings()
    {
        QByteArray rom = fakeRom(0x80000);
        QVERIFY(RomImage::headerWarnings(rom).isEmpty());

        QByteArray noSega = rom;
        noSega.replace(0x100, 4, "XXXX");
        QVERIFY(RomImage::headerWarnings(noSega).join(' ').contains("SEGA"));

        QByteArray badSum = rom;
        badSum[0x300] = static_cast<char>(badSum.at(0x300) ^ 0x55);
        QVERIFY(RomImage::headerWarnings(badSum).join(' ').contains("checksum"));

        QByteArray zeroSum = badSum;
        zeroSum[0x18E] = 0;
        zeroSum[0x18F] = 0;
        QVERIFY2(RomImage::headerWarnings(zeroSum).isEmpty(), "a checksum of 0 means 'not used'");

        const QByteArray truncated = rom.left(0x40000);   // header still says 512 KiB
        QVERIFY(RomImage::headerWarnings(truncated).join(' ').contains("truncated"));
    }

    // ---------------------------------------------------------------- ChipDb

    void chipDbKnownIds_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("name");
        QTest::newRow("macronix") << "FLASHID:C2CB" << "MX29LV640EB";
        QTest::newRow("st bottom") << "FLASHID:20DF" << "M29W640DB";
        QTest::newRow("st top") << "FLASHID:20DE" << "M29W640DT";
        QTest::newRow("trailing spaces") << "  FLASHID:C2CB \r" << "MX29LV640EB";
    }
    void chipDbKnownIds()
    {
        QFETCH(QString, line);
        QFETCH(QString, name);
        const auto m = ChipDb::identify(line);
        QVERIFY(m.parsed);
        QVERIFY(m.chip);
        QVERIFY2(QString::fromLatin1(m.chip->name).contains(name), m.chip->name);
    }

    void chipDbUnknownAndMalformed()
    {
        auto m = ChipDb::identify("FLASHID:FFFF");
        QVERIFY(m.parsed);
        QVERIFY(!m.chip);
        QCOMPARE(m.idText, QString("FFFF"));
        QVERIFY(!ChipDb::identify("MX29LV640EB MD FLASH CART").parsed);
        QVERIFY(!ChipDb::identify("FLASHID:").parsed);
        QVERIFY(!ChipDb::identify("FLASHID:ZZZZ").parsed);
        QVERIFY(!ChipDb::identify("FLASHID:12345").parsed);
    }

    // The firmware prints "%X%X": values under 0x10 lose their leading zero.
    void chipDbUnpaddedIds()
    {
        auto m = ChipDb::identify("FLASHID:17E");   // could be 1/7E or 17/E
        QVERIFY(m.parsed);
        QCOMPARE(m.manufacturer, quint8(0x1));
        QCOMPARE(m.device, quint8(0x7E));
        m = ChipDb::identify("FLASHID:00");
        QVERIFY(m.parsed);
        QVERIFY(!m.chip);
    }

    void chipDbTableIsSane()
    {
        int n = 0;
        const ChipDb::Chip *t = ChipDb::chips(&n);
        QVERIFY(n >= 5);
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                QVERIFY(t[i].manufacturer != t[j].manufacturer || t[i].device != t[j].device);
        QVERIFY(t[0].confirmed);   // the chip firmware 1.0 itself recognises
        QCOMPARE(t[0].manufacturer, quint8(0xC2));
        QCOMPARE(t[0].device, quint8(0xCB));
    }

    // The capacity lives in the table: every chip must have one, and the ROM size limit must follow it.
    void chipDbCapacity()
    {
        int n = 0;
        const ChipDb::Chip *t = ChipDb::chips(&n);
        for (int i = 0; i < n; ++i)
            QVERIFY2(t[i].capacity >= 512u * 1024u && t[i].capacity <= 8u * 1024u * 1024u, t[i].name);
        QCOMPARE(ChipDb::capacityBytes(ChipDb::identify("FLASHID:C2CB")), 8u * 1024u * 1024u);   // 64 Mbit
        QCOMPARE(ChipDb::capacityBytes(ChipDb::identify("FLASHID:20CA")), 4u * 1024u * 1024u);   // 32 Mbit
        QCOMPARE(ChipDb::capacityBytes(ChipDb::identify("FLASHID:8970")), 512u * 1024u);         // 4 Mbit Intel
        QCOMPARE(ChipDb::capacityBytes(ChipDb::identify("FLASHID:17E")), 8u * 1024u * 1024u);    // unpadded 01/7E
        QCOMPARE(ChipDb::capacityBytes(ChipDb::identify("FLASHID:FFFF")), 0u);                   // not in the table
        QCOMPARE(ChipDb::capacityBytes(ChipDb::Match()), 0u);
    }

    // ---------------------------------------------------------------- RomVerifier

    void verifierMatchesAndIgnoresTail()
    {
        QByteArray ref = randomBytes(5000, 1);
        QBuffer in(&ref);
        in.open(QIODevice::ReadOnly);
        RomVerifier v(&in);
        QByteArray dump = ref + QByteArray(3000, '\xFF');   // dumped size is larger than the file
        for (int off = 0; off < dump.size(); off += 700)
            QCOMPARE(v.write(dump.mid(off, 700)), qint64(dump.mid(off, 700).size()));
        QVERIFY(v.ok());
        QCOMPARE(v.compared(), qint64(5000));
    }

    void verifierReportsFirstDifference()
    {
        QByteArray ref = randomBytes(4096, 2);
        QBuffer in(&ref);
        in.open(QIODevice::ReadOnly);
        RomVerifier v(&in);
        QByteArray dump = ref;
        dump[1000] = static_cast<char>(dump.at(1000) ^ 0x10);
        dump[3000] = static_cast<char>(dump.at(3000) ^ 0x01);
        v.write(dump.left(1500));
        v.write(dump.mid(1500));
        QVERIFY(!v.ok());
        QCOMPARE(v.firstOffset(), qint64(1000));
        QCOMPARE(v.mismatches(), qint64(2));
        QCOMPARE(v.expectedByte(), static_cast<quint8>(ref.at(1000)));
        QCOMPARE(v.actualByte(), static_cast<quint8>(dump.at(1000)));
    }

    void openErrors()
    {
        QVERIFY(describeOpenError("ttyACM0", 1 /*DeviceNotFound*/, "x").contains("ttyACM0"));
        QVERIFY(describeOpenError("ttyACM0", 2 /*Permission*/, "x").contains("dialout"));
    }
};

QTEST_APPLESS_MAIN(TstProtocol)
#include "tst_protocol.moc"
