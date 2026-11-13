#include "fakefirmware.h"

#include <QtGlobal>

namespace {
const char kHdr[] = "\xAA\x55\xAA\xBB";
}

FakeFirmware::FakeFirmware(QObject *parent)
    : QIODevice(parent)
{
    open(QIODevice::ReadWrite);
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &FakeFirmware::pump);
}

void FakeFirmware::setFragmentation(int maxChunk, quint32 seed)
{
    m_maxChunk = maxChunk;
    m_rng.seed(seed);
}

qint64 FakeFirmware::bytesAvailable() const
{
    return m_ready.size() + QIODevice::bytesAvailable();
}

qint64 FakeFirmware::readData(char *data, qint64 maxlen)
{
    const qint64 n = qMin<qint64>(maxlen, m_ready.size());
    memcpy(data, m_ready.constData(), static_cast<size_t>(n));
    m_ready.remove(0, static_cast<int>(n));
    return n;
}

qint64 FakeFirmware::writeData(const char *data, qint64 len)
{
    if (!isOpen())
        return -1;
    if (m_deaf && m_deafClock.elapsed() < deafAfterDetectMs) {
        droppedBytes += static_cast<int>(len);
        return len;
    }
    m_rx.append(data, static_cast<int>(len));
    process();
    return len;
}

void FakeFirmware::inject(const QByteArray &bytes)
{
    replyRaw(bytes);
}

void FakeFirmware::unplug()
{
    m_pending.clear();
    m_ready.clear();
    close();
}

void FakeFirmware::reply(const QString &text)
{
    replyRaw(text.toLatin1());
}

void FakeFirmware::replyRaw(const QByteArray &bytes)
{
    if (silent)
        return;
    if (holdAfterReplies >= 0 && m_replyCount >= holdAfterReplies) {
        m_held.append(bytes);
        return;
    }
    ++m_replyCount;
    m_pending.append(bytes);
    if (!m_timer.isActive())
        m_timer.start();
}

void FakeFirmware::resume()
{
    holdAfterReplies = -1;
    const QByteArray held = m_held;
    m_held.clear();
    if (!held.isEmpty())
        replyRaw(held);
}

void FakeFirmware::pump()
{
    if (m_pending.isEmpty() || !isOpen())
        return;
    int n = m_pending.size();
    if (m_maxChunk > 0)
        n = qMin(n, 1 + static_cast<int>(m_rng.bounded(static_cast<quint32>(m_maxChunk))));
    m_ready.append(m_pending.left(n));
    m_pending.remove(0, n);
    emit readyRead();
    if (!m_pending.isEmpty())
        m_timer.start();
}

// Mirrors CDC_Receive_FS + the main loop: a write block is 1024 data bytes + a 7-byte command.
void FakeFirmware::process()
{
    for (;;) {
        if (m_rx.size() < 5)
            return;
        const bool shortCmd = m_rx.mid(1, 4) == QByteArray(kHdr, 4);
        if (shortCmd) {
            const quint8 c = static_cast<quint8>(m_rx.at(0));
            int len = 5;
            if (c == 0x0A || c == 0x1A || c == 0x1E || c == 0x3A) len = 6;
            if (c == 0x2E) len = 8;
            if (c == 0x0B || c == 0x1B) {
                errors << QStringLiteral("write command without 1024 data bytes in front");
                m_rx.clear();
                return;
            }
            if (m_rx.size() < len)
                return;
            const QByteArray cmd = m_rx.left(len);
            m_rx.remove(0, len);
            commands << cmd;
            handle(cmd, QByteArray());
            continue;
        }
        // Otherwise it must be a data block followed by its command.
        if (m_rx.size() < 1024 + 7)
            return;
        const QByteArray tail = m_rx.mid(1024, 7);
        const quint8 c = static_cast<quint8>(tail.at(0));
        if (tail.mid(1, 4) != QByteArray(kHdr, 4) || (c != 0x0B && c != 0x1B)) {
            errors << QStringLiteral("unexpected bytes: not a command and not a block+command");
            m_rx.clear();
            return;
        }
        const QByteArray block = m_rx.left(1024);
        m_rx.remove(0, 1024 + 7);
        commands << tail;
        handle(tail, block);
    }
}

void FakeFirmware::dump(const QByteArray &image, bool isRom, int sizeCode)
{
    int blocks = image.size() / 1024;
    if (isRom) {
        static const char *names[] = { "512K", "512K", "1M", "2M", "4M", "8M" };
        // Code 5 is the 8 MiB dump of the new firmware; firmware 1.0 falls into its "else" (512K).
        const int idx = (sizeCode >= 1 && sizeCode <= 4) ? sizeCode : (sizeCode == 5 && !fwVersion.isEmpty() ? 5 : 1);
        blocks = (idx == 1 ? 512 : idx == 2 ? 1024 : idx == 3 ? 2048 : idx == 4 ? 4096 : 8192);
        reply(QStringLiteral("%1 ROM DUMP START!!!\r\n").arg(QLatin1String(names[idx])));
    } else {
        blocks = (sizeCode == 0x01) ? 32 : 8;
        reply(QStringLiteral("%1 RAM DUMP START!!!\r\n").arg(sizeCode == 0x01 ? "32K" : "8K"));
    }
    m_dumpBlocksSent = 0;
    for (int j = 0; j < blocks; ++j) {
        if (stopAfterDumpBlocks >= 0 && j >= stopAfterDumpBlocks)
            return;
        if (j != dropDumpBlock)
            replyRaw(image.mid(j * 1024, 1024).leftJustified(1024, '\xFF', true));
        ++m_dumpBlocksSent;
    }
    reply(isRom ? QStringLiteral("DUMPER ROM FINISH!!!\r\n") : QStringLiteral("DUMPER RAM FINISH!!!\r\n"));
    if (isRom)
        reply(QStringLiteral("PUSH SAVE GAME BUTTON!!!\r\n"));
}

void FakeFirmware::handle(const QByteArray &cmd, const QByteArray &blockData)
{
    const quint8 c = static_cast<quint8>(cmd.at(0));
    switch (c) {
    case 0x0C:
        if (ignoreConnects > 0) {
            --ignoreConnects;
            break;
        }
        if (sendBanner)
            reply(QStringLiteral("FlashMaster MD Dumper is connected\r\n"));
        if (sendBanner && !fwVersion.isEmpty())
            reply(QStringLiteral("FWVER:%1\r\n").arg(fwVersion));
        break;
    case 0x0D:
        reply(QStringLiteral("-- MD CART ID --\r\nFLASHID:%1\r\n%2\r\n").arg(flashId, chipLine));
        if (deafAfterDetectMs > 0) {
            m_deaf = true;
            m_deafClock.start();
        }
        break;
    case 0x0A:
        dump(rom, true, static_cast<quint8>(cmd.at(5)));
        break;
    case 0x1A:
        dump(sram, false, static_cast<quint8>(cmd.at(5)));
        break;
    case 0x3A: {
        // Firmware 1.1 only: pages x 512 KiB through the SSF2 mapper. Firmware 1.0 ignores the command.
        const int pages = static_cast<quint8>(cmd.at(5));
        if (fwVersion.isEmpty() || pages < 1 || pages > 64)
            break;
        reply(QStringLiteral("SSF2 ROM DUMP START!!!\r\n"));
        for (int j = 0; j < pages * 512; ++j)
            replyRaw(rom.mid(j * 1024, 1024).leftJustified(1024, '\xFF', true));
        reply(QStringLiteral("DUMPER ROM FINISH!!!\r\n"));
        reply(QStringLiteral("PUSH SAVE GAME BUTTON!!!\r\n"));
        break;
    }
    case 0x0B:
    case 0x1B: {
        const quint32 idx = static_cast<quint8>(cmd.at(5));
        const quint32 bank = static_cast<quint8>(cmd.at(6));
        QByteArray &target = (c == 0x0B) ? rom : sram;
        const int off = static_cast<int>((bank * 64 + idx) * 1024);
        if (target.size() < off + 1024)
            target.resize(off + 1024);
        if (c == 0x0B) {
            // Flash programming: a 0xFFFF word is not programmed, others overwrite.
            for (int i = 0; i < 1024; i += 2) {
                if (static_cast<quint8>(blockData.at(i)) == 0xFF && static_cast<quint8>(blockData.at(i + 1)) == 0xFF)
                    continue;
                target[off + i] = blockData.at(i);
                target[off + i + 1] = blockData.at(i + 1);
            }
        } else {
            target.replace(off, 1024, blockData);
        }
        if (c == 0x0B && corruptRomOffset >= off && corruptRomOffset < off + 1024)
            target[corruptRomOffset] = static_cast<char>(target.at(corruptRomOffset) ^ 0x01);
        ++blocksWritten;
        const unsigned long addw = (c == 0x0B) ? (bank * 64 * 512 + idx * 512) : (bank * 64 * 1024 + idx * 1024);
        const QString hex = QString::number(addw, 16).toUpper();
        if (c == 0x0B && failRomWriteAtBlock >= 0 && static_cast<int>(bank * 64 + idx) == failRomWriteAtBlock) {
            reply(QStringLiteral("ADD:0x%1 WRITE TIMEOUT\r\n").arg(hex));
            break;
        }
        reply(QStringLiteral("ADD:0x%1 %2\r\n").arg(hex, c == 0x0B ? QStringLiteral("WRITE OK") : QStringLiteral("WRITE GK")));
        break;
    }
    case 0x0E:
        reply(QStringLiteral("-- MD CART ERASE --\r\nFLASH ERASE START\r\n"));
        if (eraseFails) {
            reply(QStringLiteral("USE TIME 0 s\r\nFLASH ERASE TIMEOUT!!!\r\n"));
        } else {
            reply(QStringLiteral("USE TIME 0 s\r\nUSE TIME 1 s\r\nFLASH ERASE FINISH!!!\r\nSRAM ERASE START\r\nSRAM ERASE FINISH!!!\r\n"));
        }
        break;
    case 0x1E: {
        const int code = static_cast<quint8>(cmd.at(5));
        if (code == 5) {
            // Every firmware: code 5 erases the whole flash chip (no dots, "USE TIME" lines instead).
            reply(QStringLiteral("-- MD CART ERASE --\r\nFLASH ERASE START\r\n"));
            if (eraseFails)
                reply(QStringLiteral("USE TIME 0 s\r\nFLASH ERASE TIMEOUT!!!\r\n"));
            else
                reply(QStringLiteral("USE TIME 0 s\r\nUSE TIME 1 s\r\nFLASH ERASE FINISH!!!\r\n\r\n8M ERASE OK!\r\n"));
            break;
        }
        static const char *names[] = { "512K", "512K", "1M", "2M", "4M" };
        const int idx = (code >= 1 && code <= 4) ? code : 1;
        const int steps = 8 + ((idx == 1 ? 0x40000 : idx == 2 ? 0x80000 : idx == 3 ? 0x100000 : 0x200000) / 0x8000) - 1;
        reply(QStringLiteral("%1 ERASEING\r\n").arg(QLatin1String(names[idx])));
        if (eraseFails) {
            reply(QStringLiteral("\r\nERASE TIMEOUT!\r\n"));
        } else {
            for (int i = 0; i < steps; ++i)
                reply(QStringLiteral("."));
            reply(QStringLiteral("\r\n%1 ERASE OK!\r\n").arg(QLatin1String(names[idx])));
        }
        break;
    }
    case 0x0F:
        reply(QStringLiteral("BUFF IS CLEAR\r\n"));
        break;
    default:
        errors << QStringLiteral("unknown command 0x%1").arg(c, 2, 16, QLatin1Char('0'));
        break;
    }
}
