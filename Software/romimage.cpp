#include "romimage.h"

#include <QCoreApplication>

namespace RomImage {

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0)
        return QCoreApplication::translate("RomImage", "%1 MiB").arg(bytes / (1024 * 1024));
    if (bytes % 1024 == 0)
        return QCoreApplication::translate("RomImage", "%1 KiB").arg(bytes / 1024);
    return QCoreApplication::translate("RomImage", "%1 bytes").arg(bytes);
}

quint32 be32(const QByteArray &d, int at)
{
    return (static_cast<quint32>(static_cast<quint8>(d.at(at))) << 24)
         | (static_cast<quint32>(static_cast<quint8>(d.at(at + 1))) << 16)
         | (static_cast<quint32>(static_cast<quint8>(d.at(at + 2))) << 8)
         |  static_cast<quint32>(static_cast<quint8>(d.at(at + 3)));
}

quint16 be16(const QByteArray &d, int at)
{
    return static_cast<quint16>((static_cast<quint8>(d.at(at)) << 8) | static_cast<quint8>(d.at(at + 1)));
}

} // namespace

bool looksLikeSmd(const QByteArray &file)
{
    const qint64 n = file.size();
    if (n <= kSmdHeaderBytes || (n - kSmdHeaderBytes) % kSmdBlockBytes != 0)
        return false;
    return static_cast<quint8>(file.at(8)) == 0xAA && static_cast<quint8>(file.at(9)) == 0xBB;
}

QByteArray convertSmd(const QByteArray &file)
{
    if (!looksLikeSmd(file))
        return QByteArray();
    const qint64 blocks = (file.size() - kSmdHeaderBytes) / kSmdBlockBytes;
    QByteArray out(static_cast<int>(blocks * kSmdBlockBytes), '\0');
    const int half = static_cast<int>(kSmdBlockBytes / 2);
    for (qint64 b = 0; b < blocks; ++b) {
        const char *src = file.constData() + kSmdHeaderBytes + b * kSmdBlockBytes;
        char *dst = out.data() + b * kSmdBlockBytes;
        for (int i = 0; i < half; ++i) {
            dst[2 * i + 1] = src[i];          // first half: odd bytes
            dst[2 * i] = src[half + i];       // second half: even bytes
        }
    }
    return out;
}

quint16 computeChecksum(const QByteArray &image, qint64 endExclusive)
{
    quint32 sum = 0;
    const qint64 end = qMin<qint64>(endExclusive, image.size()) & ~qint64(1);
    for (qint64 i = 0x200; i < end; i += 2)
        sum += be16(image, static_cast<int>(i));
    return static_cast<quint16>(sum & 0xFFFF);
}

QStringList headerWarnings(const QByteArray &image)
{
    QStringList w;
    if (image.size() < kMinRomBytes)
        return w;
    if (image.mid(0x100, 4) != "SEGA" && image.mid(0x101, 4) != "SEGA") {
        w << QCoreApplication::translate("RomImage", "There is no \"SEGA\" at offset 0x100. This does not look like a Mega Drive ROM "
                "(or it is a file in another format). It will be written anyway.");
        return w;   // the other header fields mean nothing then
    }
    const quint32 romEnd = be32(image, 0x1A4);
    const qint64 declared = static_cast<qint64>(romEnd) + 1;
    if (romEnd >= 0x1FF && declared > image.size()) {
        w << QCoreApplication::translate("RomImage", "The header says the ROM is %1 but the file has only %2: the dump may be truncated.")
                 .arg(humanSize(declared), humanSize(image.size()));
        return w;
    }
    if (romEnd >= 0x1FF) {
        const quint16 stored = be16(image, 0x18E);
        const quint16 computed = computeChecksum(image, declared);
        if (stored != 0 && stored != computed) {
            w << QCoreApplication::translate("RomImage", "The header checksum (0x%1) does not match the data (0x%2). Hacks and translations "
                    "often have a wrong checksum; the game may still work.")
                     .arg(QString::number(stored, 16).rightJustified(4, QLatin1Char('0')).toUpper(),
                          QString::number(computed, 16).rightJustified(4, QLatin1Char('0')).toUpper());
        }
    }
    return w;
}

qint64 coveringReadSize(qint64 bytes)
{
    static const qint64 sizes[] = { 0x080000, 0x100000, 0x200000, 0x400000, 0x800000 };
    for (qint64 s : sizes) {
        if (bytes <= s)
            return s;
    }
    return 0;
}

Result prepare(const QByteArray &file)
{
    Result r;
    QByteArray image = file;
    if (looksLikeSmd(file)) {
        image = convertSmd(file);
        r.infos << QCoreApplication::translate("RomImage", "This is a Super Magic Drive (.smd) file. It was converted to a plain ROM before writing.");
    }

    if (image.size() % 2 != 0) {
        r.error = QCoreApplication::translate("RomImage", "The file has an odd number of bytes (%1). A Mega Drive ROM always has an even size.").arg(image.size());
        return r;
    }
    if (image.size() < kMinRomBytes) {
        r.error = QCoreApplication::translate("RomImage", "The file is too small (%1 bytes) to be a Mega Drive ROM.").arg(image.size());
        return r;
    }
    if (image.size() > kMaxRomBytes) {
        r.error = QCoreApplication::translate("RomImage", "The ROM is %1. The largest size this device can write and read back is 8 MiB "
                     "(the whole chip of a 64 Mbit cartridge; games above that need a bank-switching mapper the cartridge does not have).")
                      .arg(humanSize(image.size()));
        return r;
    }

    const qint64 cover = coveringReadSize(image.size());
    if (image.size() != cover) {
        r.infos << QCoreApplication::translate("RomImage", "The ROM is %1, not a standard size. It is written as it is; the rest of the chip up to %2 "
                      "stays blank (0xFF).").arg(humanSize(image.size()), humanSize(cover));
    }

    r.data = image;
    r.warnings = headerWarnings(image);
    r.ok = true;
    return r;
}

} // namespace RomImage
