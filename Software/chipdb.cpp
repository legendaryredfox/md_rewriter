#include "chipdb.h"

#include <QVector>

namespace ChipDb {

namespace {

// Word-mode ID codes (low byte of each word).
//  - C2/CB: the chip firmware 1.0 recognises (MX29LV640EB, bottom boot).
//  - 20/DE and 20/DF: M29W640DT and M29W640DB, confirmed in the ST datasheet.
//  - 01/7E: S29GL064N, confirmed in the Cypress datasheet 001-98525. The low byte 7E is shared with
//    other GL parts, so the second ID cycle (not read by the firmware) would be needed to be sure.
//    Seen on the bench (2026-11-13, firmware 1.0, PCB Ver1.4 cartridge S29GL064N90TFI03): FLASHID:17E.
//    The two cartridges tried (KY and a second S29GL064N one) are 2 MiB + save RAM boards: the flash is selected only
//    below 2 MiB, so erasing or writing above that fails (docs/HWTEST.md). That is the board limit, 2 MiB.
//  - BF/6D and BF/6C: SST39VF6401B and 6402B, confirmed in the Microchip datasheet DS20005008B (same
//    555h/2AAh unlock as AMD, so the commands of the firmware apply).
//  - the others come from memory and are marked as not confirmed.
const Chip kChips[] = {
    { 0xC2, 0xCB, "MX29LV640EB (Macronix, 64 Mbit, bottom boot)", true, CommandSet::Amd555, 8 * 1024 * 1024 },
    { 0xC2, 0xC9, "MX29LV640ET (Macronix, 64 Mbit, top boot)", false, CommandSet::Amd555, 8 * 1024 * 1024 },
    { 0x20, 0xDF, "M29W640DB (ST, 64 Mbit, bottom boot)", true, CommandSet::Amd555, 8 * 1024 * 1024 },
    { 0x20, 0xDE, "M29W640DT (ST, 64 Mbit, top boot)", true, CommandSet::Amd555, 8 * 1024 * 1024 },
    { 0x20, 0x7E, "M29W640G family (ST/Numonyx, 64 Mbit)", false, CommandSet::Amd555, 8 * 1024 * 1024 },
    { 0xBF, 0x6D, "SST39VF6401B (Microchip/SST, 64 Mbit, uniform 32 KWord blocks)", true, CommandSet::Amd555, 8 * 1024 * 1024 },
    { 0xBF, 0x6C, "SST39VF6402B (Microchip/SST, 64 Mbit, uniform 32 KWord blocks)", true, CommandSet::Amd555, 8 * 1024 * 1024 },
    // 32 Mbit parts of KY / FlashKit MD carts (docs/KY_SUPPORT.md). IDs from MD_Dumper flashlist.csv.
    { 0x20, 0xCA, "M29W320DT (ST, 32 Mbit, top boot)", false, CommandSet::Amd555, 4 * 1024 * 1024 },
    { 0x20, 0xCB, "M29W320DB (ST, 32 Mbit, bottom boot)", false, CommandSet::Amd555, 4 * 1024 * 1024 },
    { 0x20, 0x56, "M29W320ET (ST, 32 Mbit, top boot)", false, CommandSet::Amd555, 4 * 1024 * 1024 },
    { 0x20, 0x57, "M29W320EB (ST, 32 Mbit, bottom boot)", false, CommandSet::Amd555, 4 * 1024 * 1024 },
    { 0xC2, 0xA7, "MX29LV320T (Macronix, 32 Mbit, top boot)", false, CommandSet::Amd555, 4 * 1024 * 1024 },
    { 0xC2, 0xA8, "MX29LV320B (Macronix, 32 Mbit, bottom boot)", false, CommandSet::Amd555, 4 * 1024 * 1024 },
    // Other command sets (new firmware only, docs/KY_SUPPORT.md). Firmware 1.0 cannot write them.
    { 0xBF, 0x5B, "SST39VF3201 (Microchip/SST, 32 Mbit, uniform, block erase 50h)", false, CommandSet::Amd555Block50, 4 * 1024 * 1024 },
    { 0xBF, 0x5A, "SST39VF3202 (Microchip/SST, 32 Mbit, uniform, block erase 50h)", false, CommandSet::Amd555Block50, 4 * 1024 * 1024 },
    { 0xC2, 0xF9, "MX29L3211 (Macronix, 32 Mbit, unlock at 5555h)", false, CommandSet::Amd5555, 4 * 1024 * 1024 },
    { 0x89, 0x70, "PA28F400T (Intel, 4 Mbit, top boot)", false, CommandSet::Intel, 512 * 1024 },
    { 0x89, 0x71, "PA28F400B (Intel, 4 Mbit, bottom boot)", false, CommandSet::Intel, 512 * 1024 },
    { 0x01, 0x7E, "S29GL064N (Spansion/Cypress, 64 Mbit; other GL-N chips share this ID)", true, CommandSet::Amd555, 8 * 1024 * 1024, 2 * 1024 * 1024 },
};

bool hex(const QString &s, int from, int len, quint8 *out)
{
    bool ok = false;
    const uint v = s.mid(from, len).toUInt(&ok, 16);
    if (!ok || v > 0xFF)
        return false;
    *out = static_cast<quint8>(v);
    return true;
}

const Chip *lookup(quint8 m, quint8 d)
{
    for (const Chip &c : kChips) {
        if (c.manufacturer == m && c.device == d)
            return &c;
    }
    return nullptr;
}

} // namespace

const Chip *chips(int *count)
{
    if (count)
        *count = static_cast<int>(sizeof(kChips) / sizeof(kChips[0]));
    return kChips;
}

Match identify(const QString &line)
{
    Match r;
    const QString trimmed = line.trimmed();
    const QString tag = QStringLiteral("FLASHID:");
    if (!trimmed.startsWith(tag))
        return r;
    const QString id = trimmed.mid(tag.size()).trimmed().toUpper();
    if (id.size() < 2 || id.size() > 4)
        return r;
    r.idText = id;

    // Candidates for the split into (manufacturer, device). The firmware prints both values
    // with %X, so the text can be 2, 3 or 4 digits long.
    struct Split { int mLen; int dLen; };
    QVector<Split> splits;
    switch (id.size()) {
    case 2: splits << Split{1, 1}; break;
    case 3: splits << Split{1, 2} << Split{2, 1}; break;
    default: splits << Split{2, 2}; break;
    }

    bool haveFirst = false;
    for (const Split &sp : splits) {
        quint8 m = 0, d = 0;
        if (!hex(id, 0, sp.mLen, &m) || !hex(id, sp.mLen, sp.dLen, &d))
            continue;
        if (!haveFirst) {
            r.parsed = true;
            r.manufacturer = m;
            r.device = d;
            haveFirst = true;
        }
        if (const Chip *c = lookup(m, d)) {
            r.manufacturer = m;
            r.device = d;
            r.chip = c;
            return r;
        }
    }
    return r;
}

quint32 capacityBytes(const Match &match)
{
    return match.chip ? match.chip->capacity : 0;
}

quint32 boardLimitBytes(const Match &match)
{
    return match.chip ? match.chip->boardLimit : 0;
}

} // namespace ChipDb
