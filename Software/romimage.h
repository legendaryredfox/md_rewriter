#ifndef ROMIMAGE_H
#define ROMIMAGE_H

#include <QByteArray>
#include <QString>
#include <QStringList>

// Prepares a ROM file for writing to a cartridge. Pure functions, no GUI and no I/O.
//
// Nothing here changes the wire protocol: the firmware programs 1 KiB blocks and skips
// 0xFFFF words, so a ROM of any even size is written as it is and the rest of the chip
// stays blank (0xFF).
namespace RomImage {

constexpr qint64 kMaxRomBytes = 4 * 1024 * 1024;   // largest ROM the firmware can read back
constexpr qint64 kSmdHeaderBytes = 512;
constexpr qint64 kSmdBlockBytes = 16 * 1024;
constexpr qint64 kMinRomBytes = 0x200;             // vector table + header

struct Result {
    bool ok = false;
    QString error;           // set when !ok
    QByteArray data;         // plain binary image, as it will be written
    QStringList infos;       // what was changed or noted (SMD converted, size not standard, ...)
    QStringList warnings;    // header problems; never block the write
};

// Looks at the bytes of a ROM file (.bin, .md, .gen, .smd) and returns the image to write.
Result prepare(const QByteArray &file);

// Super Magic Drive copier format: 512-byte header, then 16 KiB blocks in which the odd bytes
// come first and the even bytes second. Header bytes 8 and 9 are AA BB.
bool looksLikeSmd(const QByteArray &file);
// Converts an SMD file to a plain image. Returns an empty array when `file` is not SMD.
QByteArray convertSmd(const QByteArray &file);

// Header sanity checks on a plain image ("SEGA" at 0x100, ROM end, checksum). Warnings only.
QStringList headerWarnings(const QByteArray &image);

// Checksum the header stores at 0x18E: sum of the 16-bit words from 0x200 to the ROM end.
quint16 computeChecksum(const QByteArray &image, qint64 endExclusive);

// Smallest size the firmware can read back (512 KiB, 1 MiB, 2 MiB, 4 MiB) that holds `bytes`.
// Returns 0 when `bytes` is larger than 4 MiB.
qint64 coveringReadSize(qint64 bytes);

} // namespace RomImage

#endif // ROMIMAGE_H
