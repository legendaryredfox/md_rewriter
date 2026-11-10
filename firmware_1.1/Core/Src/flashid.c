#include "flashid.h"
#include <stddef.h>

/* Flash chips that take the AMD/JEDEC command set used by this firmware (NFW-1). The layout field
 * says how the 30h erase command behaves at the start of the chip (small sectors or not).
 * "confirmed": checked against a datasheet. Layout is only set where the codes (or the datasheet
 * for that exact ID) say so; everything else stays UNKNOWN so the erase loop keeps the 1.0 steps. */
typedef struct {
	uint8_t manufacturer;
	uint8_t device;
	const char *name;
	flash_layout_t layout;
	flash_cmdset_t cmdset;          /* AMD_555 unless the chip needs another set */
	uint32_t words;                 /* chip size in words, for TOP_BOOT */
	uint8_t erase_cmd;              /* 0 = 30h */
} flash_chip_t;

static const flash_chip_t flash_chips[] = {
	{ 0xC2, 0xCB, "MX29LV640EB", FLASH_LAYOUT_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0 },      /* 1.0 chip: confirmed by the released firmware */
	{ 0xC2, 0xC9, "MX29LV640ET", FLASH_LAYOUT_UNKNOWN, FLASH_CMDSET_AMD_555, 0, 0 },          /* not confirmed against a datasheet */
	{ 0x20, 0xDF, "M29W640DB",   FLASH_LAYOUT_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0 },      /* confirmed (ST datasheet) */
	{ 0x20, 0xDE, "M29W640DT",   FLASH_LAYOUT_TOP_BOOT, FLASH_CMDSET_AMD_555, 0x400000U, 0 },   /* confirmed (ST datasheet) */
	{ 0x20, 0x7E, "M29W640G",    FLASH_LAYOUT_UNKNOWN, FLASH_CMDSET_AMD_555, 0, 0 },          /* not confirmed against a datasheet */
	/* SST39VF6401B / 6402B (Microchip DS20005008B): same unlock addresses (555h/2AAh) as AMD, ID BFh
	 * + 236Dh / 236Ch. Uniform 32 KWord blocks erased with 30h (the sector erase of the AMD
	 * sequence), so there are no small sectors at the start: NO_BOTTOM_BOOT. No write buffer. */
	{ 0xBF, 0x6D, "SST39VF6401B", FLASH_LAYOUT_NO_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0 },  /* confirmed (datasheet) */
	{ 0xBF, 0x6C, "SST39VF6402B", FLASH_LAYOUT_NO_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0 },  /* confirmed (datasheet) */
	/* 32 Mbit parts of KY / FlashKit MD carts (docs/KY_SUPPORT.md). All take the AMD 555h/2AAh
	 * sequence the firmware already uses. IDs come from MD_Dumper flashlist.csv and ST/Macronix
	 * datasheets as remembered: not confirmed on a real chip, so a wrong match can only name a chip. */
	{ 0x20, 0xCA, "M29W320DT",   FLASH_LAYOUT_TOP_BOOT, FLASH_CMDSET_AMD_555, 0x200000U, 0 },   /* ST, top boot (not confirmed) */
	{ 0x20, 0xCB, "M29W320DB",   FLASH_LAYOUT_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0 },      /* ST, bottom boot (not confirmed) */
	{ 0x20, 0x56, "M29W320ET",   FLASH_LAYOUT_TOP_BOOT, FLASH_CMDSET_AMD_555, 0x200000U, 0 },   /* ST, top boot (not confirmed) */
	{ 0x20, 0x57, "M29W320EB",   FLASH_LAYOUT_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0 },      /* ST, bottom boot (not confirmed) */
	{ 0xC2, 0xA7, "MX29LV320T",  FLASH_LAYOUT_TOP_BOOT, FLASH_CMDSET_AMD_555, 0x200000U, 0 },   /* Macronix, top boot (not confirmed) */
	{ 0xC2, 0xA8, "MX29LV320B",  FLASH_LAYOUT_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0 },      /* Macronix, bottom boot (not confirmed) */
	/* SST39VF3201 / 3202 (Microchip DS20005011): ID BF + 235B / 235A, unlock 555h/2AAh. Uniform; 30h erases
	 * a 2 KWord sector, the 32 KWord block needs 50h. From MD_Dumper flashlist.csv and the datasheet: not
	 * confirmed on a chip. */
	{ 0xBF, 0x5B, "SST39VF3201", FLASH_LAYOUT_NO_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0x50 },
	{ 0xBF, 0x5A, "SST39VF3202", FLASH_LAYOUT_NO_BOTTOM_BOOT, FLASH_CMDSET_AMD_555, 0, 0x50 },
	/* MX29L3211: unlock at 5555h/2AAAh (FlashKit-MD-Plus Device.cs flash29l*), ID C2 F9. Layout unknown. */
	{ 0xC2, 0xF9, "MX29L3211",   FLASH_LAYOUT_UNKNOWN, FLASH_CMDSET_AMD_5555, 0, 0 },
	/* Intel 28F400 (FlashKit-MD-Plus flash28*): ID 89 + 4470 (top boot) / 4471 (bottom boot). The
	 * block map is in MD.c. Intel command set. Not confirmed on a chip. */
	{ 0x89, 0x70, "PA28F400T",   FLASH_LAYOUT_NO_BOTTOM_BOOT, FLASH_CMDSET_INTEL, 0, 0 },
	{ 0x89, 0x71, "PA28F400B",   FLASH_LAYOUT_BOTTOM_BOOT, FLASH_CMDSET_INTEL, 0, 0 },
};

/* S29GL064N (Cypress/Infineon datasheet 001-98525, table "Autoselect Codes"): manufacturer 01,
 * cycle 1 low byte 7E. Cycle 2 is 0C (models 03/04, boot sector) or 10 (01/02/V1/V2, uniform).
 * For 03/04, cycle 3 is 00 (04, bottom boot) or 01 (03, top boot). The datasheet documents the
 * 16-word Write Buffer Programming command (25h/29h) for the whole family. Other chips of the GL family
 * share 01/7E with other cycle 2 values and are not accepted here. */
static int identify_s29gl064n(const uint8_t id[4], flash_info_t *out)
{
	if(id[0] != 0x01U || id[1] != 0x7EU)
		return 0;
	if(id[2] == 0x0CU)
		{
			out->name = "S29GL064N";
			out->write_buffer = 1;
			out->cmdset = FLASH_CMDSET_AMD_555;
			out->words = 0;
			out->erase_cmd = 0x30U;
			if(id[3] == 0x00U)
				out->layout = FLASH_LAYOUT_BOTTOM_BOOT;
			else if(id[3] == 0x01U)
				{
					out->layout = FLASH_LAYOUT_TOP_BOOT;
					out->words = 0x400000U;
				}
			else
				out->layout = FLASH_LAYOUT_UNKNOWN;
			return 1;
		}
	if(id[2] == 0x10U)
		{
			out->name = "S29GL064N";
			out->write_buffer = 1;
			out->cmdset = FLASH_CMDSET_AMD_555;
			out->words = 0;
			out->erase_cmd = 0x30U;
			out->layout = FLASH_LAYOUT_NO_BOTTOM_BOOT;
			return 1;
		}
	return 0;
}

int flash_cfi_decode(const uint8_t qry[3], uint8_t cmdset_lo, uint8_t size_exp, flash_cfi_t *out)
{
	if((qry[0] != 0x51U) || (qry[1] != 0x52U) || (qry[2] != 0x59U))
		return 0;
	if((size_exp < 17U) || (size_exp > 27U))
		return 0;
	out->bytes = (uint32_t)1U << size_exp;
	out->cmdset = cmdset_lo;
	return 1;
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
					out->write_buffer = 0;
					out->cmdset = flash_chips[i].cmdset;
					out->words = flash_chips[i].words;
					out->erase_cmd = flash_chips[i].erase_cmd ? flash_chips[i].erase_cmd : 0x30U;
					return 1;
				}
		}
	return 0;
}
