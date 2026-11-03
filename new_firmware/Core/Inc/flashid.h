#ifndef __FLASHID_H
#define __FLASHID_H

/* Pure logic (no HAL, no registers): turns the autoselect codes of a flash chip into a name and
 * a sector layout. Compiled for the MCU and, in tests/, for the PC. */

#include <stdint.h>

/* Where the small (8 KiB) boot sectors are, as far as the codes tell.
 * UNKNOWN means "do not rely on it": callers must then behave as 1.0 did (bottom boot). */
typedef enum {
	FLASH_LAYOUT_UNKNOWN = 0,
	FLASH_LAYOUT_BOTTOM_BOOT,       /* eight 8 KiB sectors at the start of the chip */
	FLASH_LAYOUT_NO_BOTTOM_BOOT     /* top boot or uniform: the first 64 KiB is one sector */
} flash_layout_t;

typedef struct {
	const char *name;
	flash_layout_t layout;
} flash_info_t;

/* id[0] = manufacturer, id[1] = device (cycle 1), id[2] = cycle 2 (word 0x0E), id[3] = cycle 3
 * (word 0x0F). Only the low byte of each word is available (8-bit bus read).
 * Returns 1 and fills *out when the chip is in the table, else 0 and *out is untouched. */
int flash_identify(const uint8_t id[4], flash_info_t *out);

#endif
