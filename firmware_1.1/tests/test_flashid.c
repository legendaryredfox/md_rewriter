/* Host test for Core/Src/flashid.c: gcc -ICore/Inc tests/test_flashid.c Core/Src/flashid.c */
#include <stdio.h>
#include <string.h>
#include "flashid.h"

static int failures;

static void expect(const char *what, const uint8_t id[4], int known, const char *name, flash_layout_t layout)
{
	flash_info_t info = { "untouched", FLASH_LAYOUT_UNKNOWN, 0, FLASH_CMDSET_AMD_555, 0, 0 };
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
	const uint8_t sst32a[4] = { 0xBF, 0x5B, 0x00, 0x00 };
	const uint8_t sst32b[4] = { 0xBF, 0x5A, 0x00, 0x00 };
	const uint8_t none[4]   = { 0xFF, 0xFF, 0xFF, 0xFF };
	const uint8_t rom[4]    = { 0x00, 0xFF, 0x00, 0x00 };

	expect("MX29LV640EB", mx, 1, "MX29LV640EB", FLASH_LAYOUT_BOTTOM_BOOT);
	expect("MX29LV640EB, other cycles", mx0, 1, "MX29LV640EB", FLASH_LAYOUT_BOTTOM_BOOT);
	{
		const uint8_t m29w320eb[4] = { 0x20, 0x57, 0x00, 0x00 };
		const uint8_t mx3211[4]    = { 0xC2, 0xF9, 0x00, 0x00 };
		expect("M29W320EB (KY cart)", m29w320eb, 1, "M29W320EB", FLASH_LAYOUT_BOTTOM_BOOT);
		flash_info_t ci = { "untouched", FLASH_LAYOUT_UNKNOWN, 0, FLASH_CMDSET_AMD_555, 0, 0 };
		const uint8_t pa28b[4] = { 0x89, 0x71, 0x00, 0x00 };
		const uint8_t pa28t[4] = { 0x89, 0x70, 0x00, 0x00 };
		expect("MX29L3211 (KY cart)", mx3211, 1, "MX29L3211", FLASH_LAYOUT_UNKNOWN);
		expect("PA28F400B", pa28b, 1, "PA28F400B", FLASH_LAYOUT_BOTTOM_BOOT);
		expect("PA28F400T", pa28t, 1, "PA28F400T", FLASH_LAYOUT_NO_BOTTOM_BOOT);
		{
			int ok = flash_identify(mx3211, &ci) && ci.cmdset == FLASH_CMDSET_AMD_5555;
			ok = ok && flash_identify(pa28b, &ci) && ci.cmdset == FLASH_CMDSET_INTEL;
			ok = ok && flash_identify(m29w320eb, &ci) && ci.cmdset == FLASH_CMDSET_AMD_555;
			printf("%s command set per chip\n", ok ? "PASS" : "FAIL");
			if(!ok) failures++;
		}
	}
	expect("MX29LV640ET layout stays unknown", mxet, 1, "MX29LV640ET", FLASH_LAYOUT_UNKNOWN);
	expect("M29W640DT", dt, 1, "M29W640DT", FLASH_LAYOUT_TOP_BOOT);
	expect("M29W640DB", db, 1, "M29W640DB", FLASH_LAYOUT_BOTTOM_BOOT);
	expect("M29W640G layout stays unknown", g, 1, "M29W640G", FLASH_LAYOUT_UNKNOWN);
	{
		flash_info_t i = { "", FLASH_LAYOUT_UNKNOWN, 0, FLASH_CMDSET_AMD_555, 0, 0 };
		int ok = flash_identify(gl03, &i) && i.write_buffer == 1;
		ok = ok && flash_identify(gl01, &i) && i.write_buffer == 1;
		ok = ok && flash_identify(mx, &i) && i.write_buffer == 0;
		ok = ok && flash_identify(dt, &i) && i.write_buffer == 0;
		printf("%s write buffer only for the S29GL064N\n", ok ? "PASS" : "FAIL");
		if(!ok) failures++;
	}
	expect("S29GL064N model 03 (top boot)", gl03, 1, "S29GL064N", FLASH_LAYOUT_TOP_BOOT);
	expect("S29GL064N model 04 (bottom boot)", gl04, 1, "S29GL064N", FLASH_LAYOUT_BOTTOM_BOOT);
	expect("S29GL064N uniform", gl01, 1, "S29GL064N", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("S29GL064N with odd cycle 3 is unknown layout", glodd, 1, "S29GL064N", FLASH_LAYOUT_UNKNOWN);
	expect("SST39VF6401B", sst1, 1, "SST39VF6401B", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("SST39VF6402B", sst2, 1, "SST39VF6402B", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("SST39VF3201", sst32a, 1, "SST39VF3201", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	expect("SST39VF3202", sst32b, 1, "SST39VF3202", FLASH_LAYOUT_NO_BOTTOM_BOOT);
	{
		flash_info_t i1, i2, i3;
		int ok = flash_identify(sst32a, &i1) && flash_identify(sst1, &i2) && flash_identify(gl03, &i3);
		ok = ok && i1.erase_cmd == 0x50 && i2.erase_cmd == 0x30 && i3.erase_cmd == 0x30;
		printf("%s erase command: 50h on SST39VF320x, 30h elsewhere\n", ok ? "PASS" : "FAIL");
		if(!ok) failures++;
	}
	expect("other 01/7E chip is not named", gl128, 0, NULL, FLASH_LAYOUT_UNKNOWN);
	expect("empty bus", none, 0, NULL, FLASH_LAYOUT_UNKNOWN);
	expect("mask ROM data", rom, 0, NULL, FLASH_LAYOUT_UNKNOWN);

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
