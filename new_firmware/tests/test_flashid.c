/* Host test for Core/Src/flashid.c: gcc -ICore/Inc tests/test_flashid.c Core/Src/flashid.c */
#include <stdio.h>
#include <string.h>
#include "flashid.h"

static int failures;

static void expect(const char *what, const uint8_t id[4], int known, const char *name, flash_layout_t layout)
{
	flash_info_t info = { "untouched", FLASH_LAYOUT_UNKNOWN, 0 };
	const int got = flash_identify(id, &info);
	int ok = (got == known);
	if(ok && known)
		ok = (strcmp(info.name, name) == 0) && (info.layout == layout);
	if(ok && !known)
		ok = (strcmp(info.name, "untouched") == 0);
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if(!ok)
		failures++;
}

int main(void)
{
	const uint8_t mx[4]     = { 0xC2, 0xCB, 0xFF, 0xFF };   /* cycle 2/3 do not matter for the 1.0 chip */
	const uint8_t mx0[4]    = { 0xC2, 0xCB, 0x00, 0x00 };
	const uint8_t mxet[4]   = { 0xC2, 0xC9, 0x00, 0x00 };
	const uint8_t dt[4]     = { 0x20, 0xDE, 0x00, 0x00 };
	const uint8_t db[4]     = { 0x20, 0xDF, 0x00, 0x00 };
	const uint8_t g[4]      = { 0x20, 0x7E, 0x10, 0x00 };
	const uint8_t gl03[4]   = { 0x01, 0x7E, 0x0C, 0x01 };   /* S29GL064N90TFI03: top boot (the chip on the cartridge in the photo) */
	const uint8_t gl04[4]   = { 0x01, 0x7E, 0x0C, 0x00 };
	const uint8_t gl01[4]   = { 0x01, 0x7E, 0x10, 0x01 };   /* uniform sectors */
	const uint8_t glodd[4]  = { 0x01, 0x7E, 0x0C, 0x55 };   /* cycle 3 unreadable: do not guess */
	const uint8_t gl128[4]  = { 0x01, 0x7E, 0x21, 0x01 };   /* another GL part sharing 01/7E */
	const uint8_t sst1[4]   = { 0xBF, 0x6D, 0x00, 0x00 };
	const uint8_t sst2[4]   = { 0xBF, 0x6C, 0x00, 0x00 };
	const uint8_t none[4]   = { 0xFF, 0xFF, 0xFF, 0xFF };
	const uint8_t rom[4]    = { 0x00, 0xFF, 0x00, 0x00 };

	expect("MX29LV640EB", mx, 1, "MX29LV640EB", FLASH_LAYOUT_BOTTOM_BOOT);
	expect("MX29LV640EB, other cycles", mx0, 1, "MX29LV640EB", FLASH_LAYOUT_BOTTOM_BOOT);
	expect("MX29LV640ET layout stays unknown", mxet, 1, "MX29LV640ET", FLASH_LAYOUT_UNKNOWN);
	expect("M29W640DT", dt, 1, "M29W640DT", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("M29W640DB", db, 1, "M29W640DB", FLASH_LAYOUT_BOTTOM_BOOT);
	expect("M29W640G layout stays unknown", g, 1, "M29W640G", FLASH_LAYOUT_UNKNOWN);
	{
		flash_info_t i = { "", FLASH_LAYOUT_UNKNOWN, 0 };
		int ok = flash_identify(gl03, &i) && i.write_buffer == 1;
		ok = ok && flash_identify(gl01, &i) && i.write_buffer == 1;
		ok = ok && flash_identify(mx, &i) && i.write_buffer == 0;
		ok = ok && flash_identify(dt, &i) && i.write_buffer == 0;
		printf("%s write buffer only for the S29GL064N\n", ok ? "PASS" : "FAIL");
		if(!ok) failures++;
	}
	expect("S29GL064N model 03 (top boot)", gl03, 1, "S29GL064N", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("S29GL064N model 04 (bottom boot)", gl04, 1, "S29GL064N", FLASH_LAYOUT_BOTTOM_BOOT);
	expect("S29GL064N uniform", gl01, 1, "S29GL064N", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("S29GL064N with odd cycle 3 is unknown layout", glodd, 1, "S29GL064N", FLASH_LAYOUT_UNKNOWN);
	expect("SST39VF6401B", sst1, 1, "SST39VF6401B", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("SST39VF6402B", sst2, 1, "SST39VF6402B", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("other 01/7E chip is not named", gl128, 0, NULL, FLASH_LAYOUT_UNKNOWN);
	expect("empty bus", none, 0, NULL, FLASH_LAYOUT_UNKNOWN);
	expect("mask ROM data", rom, 0, NULL, FLASH_LAYOUT_UNKNOWN);

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
