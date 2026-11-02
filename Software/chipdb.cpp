#include "chipdb.h"

#include <QVector>

namespace ChipDb {

namespace {

// Word-mode ID codes (low byte of each word).
//  - C2/CB: the chip firmware 1.0 recognises (MX29LV640EB, bottom boot).
//  - 20/DE and 20/DF: M29W640DT and M29W640DB, confirmed in the ST datasheet.
//  - the others come from memory and are marked as not confirmed.
const Chip kChips[] = {
    { 0xC2, 0xCB, "MX29LV640EB (Macronix, 64 Mbit, bottom boot)", true },
    { 0xC2, 0xC9, "MX29LV640ET (Macronix, 64 Mbit, top boot)", false },
    { 0x20, 0xDF, "M29W640DB (ST, 64 Mbit, bottom boot)", true },
    { 0x20, 0xDE, "M29W640DT (ST, 64 Mbit, top boot)", true },
    { 0x20, 0x7E, "M29W640G family (ST/Numonyx, 64 Mbit)", false },
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

} // namespace ChipDb
