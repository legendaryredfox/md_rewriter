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
enum class CommandSet { Amd555, Amd5555, Intel };

struct Chip {
    quint8 manufacturer;
    quint8 device;
    const char *name;
    bool confirmed;   // false: ID taken from memory, not checked against a datasheet
    CommandSet commandSet = CommandSet::Amd555;
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

// The whole table, for tests and for the guide.
const Chip *chips(int *count);

} // namespace ChipDb

#endif // CHIPDB_H
