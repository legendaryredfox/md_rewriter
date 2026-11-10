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
	FLASH_LAYOUT_NO_BOTTOM_BOOT,    /* uniform: the first 64 KiB is one sector */
	FLASH_LAYOUT_TOP_BOOT           /* top boot: first 64 KiB is one sector, the last 64 KiB is eight 8 KiB sectors (needs `words`) */
} flash_layout_t;

/* Command set of the chip. AMD_555 is what 1.0 used and what every chip of the table had until
 * 2026-11-04. AMD_5555 is the same sequence with unlock addresses 5555h/2AAAh (MX29L3211).
 * INTEL is the Intel/Sharp set (28F400): FFh reset, 90h ID, 40h program, 20h+D0h block erase,
 * status register bit 7 = ready. Zero must stay AMD_555: table rows that do not name one get it. */
typedef enum {
	FLASH_CMDSET_AMD_555 = 0,
	FLASH_CMDSET_AMD_5555,
	FLASH_CMDSET_INTEL
} flash_cmdset_t;

typedef struct {
	const char *name;
	flash_layout_t layout;
	int write_buffer;               /* 1: takes the 16-word Write Buffer Programming command (datasheet-confirmed) */
	flash_cmdset_t cmdset;
	uint32_t words;                 /* chip size in 16-bit words, 0 = unknown (only TOP_BOOT needs it) */
	uint8_t erase_cmd;              /* last cycle of the erase: 30h (sector), 50h on SST39VF320x (32 KWord block; 30h erases only 2 KWord) */
} flash_info_t;

/* id[0] = manufacturer, id[1] = device (cycle 1), id[2] = cycle 2 (word 0x0E), id[3] = cycle 3
 * (word 0x0F). Only the low byte of each word is available (8-bit bus read).
 * Returns 1 and fills *out when the chip is in the table, else 0 and *out is untouched. */
int flash_identify(const uint8_t id[4], flash_info_t *out);

#endif
