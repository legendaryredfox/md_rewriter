#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QVector>

// Wire protocol between the desktop app and the md_rewriter firmware.
// Pure functions and tables, no GUI and no I/O, so everything here is unit-testable.
// Must stay byte-compatible with firmware 1.0 (see docs/SPEC.md, section 3).
namespace Protocol {

constexpr int kBlockSize = 1024;       // payload of one write block
constexpr int kBlocksPerBank = 64;     // 64 x 1 KiB per bank, addressed by (bank, index)

// First byte of a command packet. The next four bytes are always AA 55 AA BB.
enum Command : quint8 {
    DumpRom = 0x0A,
    WriteRomBlock = 0x0B,
    Connect = 0x0C,
    Detect = 0x0D,
    EraseAll = 0x0E,
    ClearBuffers = 0x0F,
    DumpSram = 0x1A,
    WriteSramBlock = 0x1B,
    EraseBySize = 0x1E,
    EraseSector = 0x2E
};

// [cmd] AA 55 AA BB [args...]
QByteArray buildCommand(Command cmd, const QByteArray &args = QByteArray());

// Command for block number `index` (0-based) of a write. Appends (index % 64, index / 64).
QByteArray buildWriteBlockCommand(Command cmd, quint32 index);

struct RomSize {
    QString label;       // "512K": bytes of ROM, as shown in the UI
    quint32 bytes;
    quint8 code;         // size argument of DumpRom / EraseBySize
    int eraseSectors;    // number of erase steps the firmware reports (one '.' each)
};

struct SramSize {
    QString label;
    quint32 bytes;
    quint8 code;         // size argument of DumpSram
};

// Sizes the firmware can dump and erase by size.
const QVector<RomSize> &romSizes();
// Sizes of save RAM the firmware can dump.
const QVector<SramSize> &sramSizes();

// File sizes accepted for writing: even, 512 bytes to 4 MiB (the old app accepted five fixed sizes).
bool isWritableRomSize(qint64 bytes);
bool isWritableSramSize(qint64 bytes);

// Number of 1 KiB blocks needed for `bytes` (last block is padded).
quint32 blockCount(qint64 bytes);
// Pads `block` with 0xFF (the value the firmware skips when programming) to kBlockSize.
QByteArray padBlock(QByteArray block);

// Classification of one text line sent by the firmware.
enum class LineKind {
    Other,
    ConnectBanner,     // "FlashMaster MD Dumper is connected"
    DumpStart,         // "... DUMP START!!!"  (raw data follows)
    DumpRomFinished,   // "DUMPER ROM FINISH!!!"
    DumpSramFinished,  // "DUMPER RAM FINISH!!!"
    RomBlockAck,       // "ADD:0x.... WRITE OK"
    SramBlockAck,      // "ADD:0x.... WRITE GK"
    BuffersCleared,    // "BUFF IS CLEAR"
    DetectDone,        // detection result line (flash found or not)
    EraseProgress,     // "FLASH ERASE START", "USE TIME n s"
    EraseDone,         // "... ERASE OK!" or "SRAM ERASE FINISH!!!"
    EraseFailed        // "... TIMEOUT" (new firmware only)
};
LineKind classifyLine(const QString &line);

// Number of '.' characters the firmware prints while erasing by size.
int countDots(const QString &text);

// Human-readable explanation for a serial open failure (permissions, busy port, ...).
// `qtErrorCode` is a QSerialPort::SerialPortError; `portName` e.g. "ttyACM0".
QString describeOpenError(const QString &portName, int qtErrorCode, const QString &qtMessage);

} // namespace Protocol

#endif // PROTOCOL_H
