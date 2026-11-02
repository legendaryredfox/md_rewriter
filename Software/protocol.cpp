#include "protocol.h"

#include <QCoreApplication>
#include <QSerialPort>

namespace Protocol {

QByteArray buildCommand(Command cmd, const QByteArray &args)
{
    QByteArray packet;
    packet.reserve(5 + args.size());
    packet.append(static_cast<char>(cmd));
    packet.append(static_cast<char>(0xAA));
    packet.append(static_cast<char>(0x55));
    packet.append(static_cast<char>(0xAA));
    packet.append(static_cast<char>(0xBB));
    packet.append(args);
    return packet;
}

QByteArray buildWriteBlockCommand(Command cmd, quint32 index)
{
    QByteArray args;
    args.append(static_cast<char>(index % kBlocksPerBank));
    args.append(static_cast<char>((index / kBlocksPerBank) & 0xFF));
    return buildCommand(cmd, args);
}

namespace {

// The firmware erases 8 steps of 0x1000 below 0x8000, then steps of 0x8000.
int eraseSteps(quint32 sectorWords)
{
    return 8 + static_cast<int>(sectorWords / 0x8000) - 1;
}

} // namespace

const QVector<RomSize> &romSizes()
{
    static const QVector<RomSize> sizes = {
        { QStringLiteral("512K"), 0x080000, 0x01, eraseSteps(0x040000) },
        { QStringLiteral("1M"),   0x100000, 0x02, eraseSteps(0x080000) },
        { QStringLiteral("2M"),   0x200000, 0x03, eraseSteps(0x100000) },
        { QStringLiteral("4M"),   0x400000, 0x04, eraseSteps(0x200000) },
    };
    return sizes;
}

const QVector<SramSize> &sramSizes()
{
    // Note the firmware codes: 0x01 = 32K, 0x02 = 8K.
    static const QVector<SramSize> sizes = {
        { QStringLiteral("32K"), 0x8000, 0x01 },
        { QStringLiteral("8K"),  0x2000, 0x02 },
    };
    return sizes;
}

bool isWritableRomSize(qint64 bytes)
{
    // The firmware programs 1 KiB blocks, skips 0xFFFF words and does not care about the size.
    return bytes >= 0x200 && bytes <= 0x400000 && bytes % 2 == 0;
}

bool isWritableSramSize(qint64 bytes)
{
    return bytes == 0x2000 || bytes == 0x8000;
}

quint32 blockCount(qint64 bytes)
{
    if (bytes <= 0)
        return 0;
    return static_cast<quint32>((bytes + kBlockSize - 1) / kBlockSize);
}

QByteArray padBlock(QByteArray block)
{
    if (block.size() < kBlockSize)
        block.append(QByteArray(kBlockSize - block.size(), static_cast<char>(0xFF)));
    return block;
}

LineKind classifyLine(const QString &line)
{
    const QString l = line.trimmed();
    if (l.contains(QLatin1String("MD Dumper is connected")))
        return LineKind::ConnectBanner;
    if (l.contains(QLatin1String("DUMP START!!!")))
        return LineKind::DumpStart;
    if (l.contains(QLatin1String("DUMPER ROM FINISH")))
        return LineKind::DumpRomFinished;
    if (l.contains(QLatin1String("DUMPER RAM FINISH")))
        return LineKind::DumpSramFinished;
    if (l.startsWith(QLatin1String("ADD:0x")) && l.endsWith(QLatin1String("WRITE OK")))
        return LineKind::RomBlockAck;
    if (l.startsWith(QLatin1String("ADD:0x")) && l.endsWith(QLatin1String("WRITE GK")))
        return LineKind::SramBlockAck;
    if (l.contains(QLatin1String("BUFF IS CLEAR")))
        return LineKind::BuffersCleared;
    if (l.contains(QLatin1String("MD FLASH CART")) || l.contains(QLatin1String("NO FIND NGP CARD")))
        return LineKind::DetectDone;
    if (l.contains(QLatin1String("TIMEOUT")))
        return LineKind::EraseFailed;
    if (l.contains(QLatin1String("ERASE OK!")) || l.contains(QLatin1String("SRAM ERASE FINISH")))
        return LineKind::EraseDone;
    if (l.contains(QLatin1String("FLASH ERASE START")) || (l.startsWith(QLatin1String("USE TIME")) && l.endsWith(QLatin1String(" s"))))
        return LineKind::EraseProgress;
    return LineKind::Other;
}

int countDots(const QString &text)
{
    return static_cast<int>(text.count(QLatin1Char('.')));
}

QString describeOpenError(const QString &portName, int qtErrorCode, const QString &qtMessage)
{
    const auto code = static_cast<QSerialPort::SerialPortError>(qtErrorCode);
    switch (code) {
    case QSerialPort::PermissionError:
        return QCoreApplication::translate("Protocol",
            "Cannot open %1: permission denied, or the port is already in use by another program. "
            "Close other programs that use it (for example ModemManager). On Linux, add your user "
            "to the \"dialout\" group (sudo usermod -aG dialout $USER, then log in again) or install "
            "the udev rule shipped with this application.").arg(portName);
    case QSerialPort::DeviceNotFoundError:
        return QCoreApplication::translate("Protocol",
            "Cannot open %1: the device is not there any more. Replug the cable and press Refresh.")
            .arg(portName);
    case QSerialPort::OpenError:
        return QCoreApplication::translate("Protocol",
            "Cannot open %1: the port is already open.").arg(portName);
    default:
        return QCoreApplication::translate("Protocol", "Cannot open %1: %2")
            .arg(portName, qtMessage);
    }
}

} // namespace Protocol
