#include "flashid.h"
#include <stddef.h>

/* Flash chips that take the AMD/JEDEC command set used by this firmware (NFW-1).
 * "confirmed": checked against a datasheet. Layout is only set where the codes (or the datasheet
 * for that exact ID) say so; everything else stays UNKNOWN so the erase loop keeps the 1.0 steps. */
typedef struct {
	uint8_t manufacturer;
	uint8_t device;
	const char *name;
	flash_layout_t layout;
} flash_chip_t;

static const flash_chip_t flash_chips[] = {
	{ 0xC2, 0xCB, "MX29LV640EB", FLASH_LAYOUT_BOTTOM_BOOT },      /* 1.0 chip: confirmed by the released firmware */
	{ 0xC2, 0xC9, "MX29LV640ET", FLASH_LAYOUT_UNKNOWN },          /* not confirmed against a datasheet */
	{ 0x20, 0xDF, "M29W640DB",   FLASH_LAYOUT_BOTTOM_BOOT },      /* confirmed (ST datasheet) */
	{ 0x20, 0xDE, "M29W640DT",   FLASH_LAYOUT_NO_BOTTOM_BOOT },   /* confirmed (ST datasheet) */
	{ 0x20, 0x7E, "M29W640G",    FLASH_LAYOUT_UNKNOWN },          /* not confirmed against a datasheet */
};

/* S29GL064N (Cypress/Infineon datasheet 001-98525, table "Autoselect Codes"): manufacturer 01,
 * cycle 1 low byte 7E. Cycle 2 is 0C (models 03/04, boot sector) or 10 (01/02/V1/V2, uniform).
 * For 03/04, cycle 3 is 00 (04, bottom boot) or 01 (03, top boot). Other chips of the GL family
 * share 01/7E with other cycle 2 values and are not accepted here. */
static int identify_s29gl064n(const uint8_t id[4], flash_info_t *out)
{
	if(id[0] != 0x01U || id[1] != 0x7EU)
		return 0;
	if(id[2] == 0x0CU)
		{
			out->name = "S29GL064N";
			if(id[3] == 0x00U)
				out->layout = FLASH_LAYOUT_BOTTOM_BOOT;
			else if(id[3] == 0x01U)
				out->layout = FLASH_LAYOUT_NO_BOTTOM_BOOT;
			else
				out->layout = FLASH_LAYOUT_UNKNOWN;
			return 1;
		}
	if(id[2] == 0x10U)
		{
			out->name = "S29GL064N";
			out->layout = FLASH_LAYOUT_NO_BOTTOM_BOOT;
			return 1;
		}
	return 0;
}

int flash_identify(const uint8_t id[4], flash_info_t *out)
{
	size_t i;
	if(identify_s29gl064n(id, out))
		return 1;
	for(i = 0; i < (sizeof(flash_chips) / sizeof(flash_chips[0])); i++)
		{
			if((flash_chips[i].manufacturer == id[0]) && (flash_chips[i].device == id[1]))
				{
					out->name = flash_chips[i].name;
					out->layout = flash_chips[i].layout;
					return 1;
				}
		}
	return 0;
}
