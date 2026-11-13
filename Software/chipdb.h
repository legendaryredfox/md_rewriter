#ifndef CHIPDB_H
#define CHIPDB_H

#include <QString>

// Flash chips the app can name from the "FLASHID:xxxx" line of the firmware.
//
// Firmware 1.0 reads the low byte of the first two words of the chip's ID area and prints them
// with "%X%X", without zero padding, so "FLASHID:20DF" is manufacturer 0x20, device 0xDF and
// a value below 0x10 shortens the text. It only knows the Macronix chip; for any other chip it
// prints "NO FIND NGP CARD". That text is not an error: ROM reading never depends on the ID, and
// writing only needs a chip that takes the AMD/JEDEC commands (0x555/0x2AA unlock).
namespace ChipDb {

// Command set the chip needs for writing. Firmware 1.0 only speaks Amd555.
// Amd555Block50 is the 555h sequence with 50h as the erase code (SST39VF320x): firmware 1.0 sends 30h,
// which erases only 2 KWord there, so it cannot write these chips.
enum class CommandSet { Amd555, Amd5555, Intel, Amd555Block50 };

struct Chip {
    quint8 manufacturer;
    quint8 device;
    const char *name;
    bool confirmed;   // false: ID taken from memory, not checked against a datasheet
    CommandSet commandSet = CommandSet::Amd555;
    quint32 capacity = 0;   // bytes the chip holds; used to refuse a ROM that does not fit
    quint32 boardLimit = 0; // bytes of ROM the cartridges seen with this ID can use (the board selects the flash only below it); 0 = no limit known
};

struct Match {
    bool parsed = false;      // the line had a readable ID
    QString idText;           // "C2CB" as the firmware printed it
    quint8 manufacturer = 0;
    quint8 device = 0;
    const Chip *chip = nullptr;   // null when the ID is not in the table
};

// `line` is a firmware line, normally "FLASHID:C2CB". Any other text gives parsed == false.
Match identify(const QString &line);
// Known byte capacity of a flash chip; zero when this ID is not listed.
quint32 capacityBytes(const Match &match);
// Bytes of ROM that the cartridge boards known with this ID can use, which can be less than the chip holds:
// the KY / FlashKit boards with an S29GL064N select the flash only below 2 MiB (docs/HWTEST.md). Zero when no limit is known.
quint32 boardLimitBytes(const Match &match);

// The whole table, for tests and for the guide.
const Chip *chips(int *count);

} // namespace ChipDb

#endif // CHIPDB_H
