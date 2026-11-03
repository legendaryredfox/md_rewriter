/* Runs the real Core/Src/MD.c on the PC against the chip model in host_hw.c.
 * Build and run: make test   (in new_firmware/). */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "MD.h"
#include "host_hw.h"

static int failures, checks;

static void check(int ok, const char *what)
{
	checks++;
	if(!ok)
		{
			failures++;
			printf("FAIL %s\n", what);
		}
}

/* The firmware is idle with the data bus as outputs ("write mode") between commands. */
static void idle_bus(void) { write_mode(); }

static void check_clean(const char *what)
{
	char msg[300];
	snprintf(msg, sizeof(msg), "%s: no bus errors (%s)", what, hw_error_text);
	check(hw_errors == 0, msg);
}

/* ---- ID detection ---- */

static void test_detect(const hw_chip_config_t *chip, const char *id_line, const char *name_line, const char *what)
{
	char expect[128];
	hw_reset(chip);
	checkid();
	snprintf(expect, sizeof(expect), "-- MD CART ID --\r\n%s%s", id_line, name_line);
	check(strcmp(hw_cdc_log, expect) == 0, what);
	if(strcmp(hw_cdc_log, expect) != 0)
		printf("  got: %s\n", hw_cdc_log);
	check_clean(what);
}

static void test_detects(void)
{
	hw_chip_config_t none = HW_MX29LV640EB;
	test_detect(&HW_MX29LV640EB, "FLASHID:C2CB\r\n", "MX29LV640EB MD FLASH CART\r\n", "detect MX29LV640EB (line as in 1.0)");
	test_detect(&HW_S29GL064N_03, "FLASHID:017E\r\n", "S29GL064N MD FLASH CART\r\n", "detect S29GL064N model 03");
	test_detect(&HW_S29GL064N_04, "FLASHID:017E\r\n", "S29GL064N MD FLASH CART\r\n", "detect S29GL064N model 04");
	test_detect(&HW_S29GL064N_01, "FLASHID:017E\r\n", "S29GL064N MD FLASH CART\r\n", "detect S29GL064N uniform");
	test_detect(&HW_M29W640DT, "FLASHID:20DE\r\n", "M29W640DT MD FLASH CART\r\n", "detect M29W640DT");
	none.id[0] = 0x0000; none.id[1] = 0x1234;
	test_detect(&none, "FLASHID:0034\r\n", "NO FIND NGP CARD\r\n", "unknown chip is reported, not named");

	/* a mask ROM game: the autoselect command is ignored, the bytes are game data */
	hw_reset(&HW_MX29LV640EB);
	hw_set_rom_only(1);
	hw_set_word(0, 0x00FF);
	hw_set_word(1, 0x1234);
	checkid();
	check(strstr(hw_cdc_log, "NO FIND NGP CARD") != NULL, "mask ROM: not named as flash");
	check_clean("mask ROM detect");
}

/* ---- layout probe ---- */

static void test_layout_probe(void)
{
	hw_reset(&HW_MX29LV640EB);     check(flash_has_bottom_boot_sectors() == 1, "probe: MX29LV640EB has bottom boot sectors");
	hw_reset(&HW_S29GL064N_04);    check(flash_has_bottom_boot_sectors() == 1, "probe: S29GL064N-04 has bottom boot sectors");
	hw_reset(&HW_S29GL064N_03);    check(flash_has_bottom_boot_sectors() == 0, "probe: S29GL064N-03 (top boot) has none");
	hw_reset(&HW_S29GL064N_01);    check(flash_has_bottom_boot_sectors() == 0, "probe: S29GL064N-01 (uniform) has none");
	hw_reset(&HW_M29W640DT);       check(flash_has_bottom_boot_sectors() == 0, "probe: M29W640DT has none");
	hw_reset(&HW_MX29LV640EB);
	hw_set_rom_only(1);
	check(flash_has_bottom_boot_sectors() == 1, "probe: unreadable/unknown keeps the 1.0 steps");
	check_clean("probe");
}

/* ---- program ---- */

static void fill_block(const hw_chip_config_t *chip, uint32_t base, uint32_t words)
{
	uint32_t i;
	int ok = 1;
	(void)chip;
	for(i = 0; i < words; i++)
		{
			const uint16_t v = (uint16_t)(0x1234U + i * 0x0101U);
			if(!flash_program_word(base + i, (uint8_t)(v >> 8), (uint8_t)v)) ok = 0;
		}
	check(ok, "program: every word reported done");
	for(i = 0; i < words; i++)
		{
			const uint16_t v = (uint16_t)(0x1234U + i * 0x0101U);
			if(hw_word(base + i) != v) ok = 0;
		}
	check(ok, "program: chip holds the data");
}

static void test_program(void)
{
	hw_chip_config_t slow;
	uint8_t id;

	for(id = 0; id < 3; id++)
		{
			const hw_chip_config_t *c = id == 0 ? &HW_MX29LV640EB : (id == 1 ? &HW_S29GL064N_03 : &HW_M29W640DT);
			hw_reset(c);
			idle_bus();
			fill_block(c, 0x100, 64);
			check(hw_program_while_busy == 0, "program: no command sent while the chip was busy");
			check_clean("program");
		}

	/* a slow chip (the datasheet maximum is much higher than the typical time) */
	slow = HW_S29GL064N_03;
	slow.program_ns = 400000;
	hw_reset(&slow);
	idle_bus();
	fill_block(&slow, 0x2000, 32);
	check(hw_program_while_busy == 0, "program: slow chip, still no command while busy");
	check_clean("program slow chip");

	/* programming over data that is not erased: 0 to 1 cannot be done, must be reported */
	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	hw_set_word(0x500, 0x00FF);
	check(flash_program_word(0x500, 0x12, 0x34) == 0, "program: 0->1 change is reported as failure");
	check_clean("program failure");
	/* and the chip was reset to read mode: the next write works */
	check(flash_program_word(0x501, 0xAB, 0xCD) == 1 && hw_word(0x501) == 0xABCD, "program: chip usable after a failure");
	check(hw_program_while_busy == 0, "program: failure path sent no command while busy");
}

/* ---- erase ---- */

static void test_erase(void)
{
	uint32_t address, i;
	int ok;

	/* sector erase, bottom boot: an 8 KiB sector (4 Kwords) */
	hw_reset(&HW_MX29LV640EB);
	hw_set_word(0x0FFF, 0); hw_set_word(0x1000, 0); hw_set_word(0x1FFF, 0); hw_set_word(0x2000, 0);
	check(erase_sector(0x1000) == 1, "erase_sector returns done");
	check(hw_word(0x1000) == 0xFFFF && hw_word(0x1FFF) == 0xFFFF, "bottom boot: 4 Kword sector erased");
	check(hw_word(0x0FFF) == 0 && hw_word(0x2000) == 0, "bottom boot: neighbours untouched");
	check_clean("erase sector");

	/* the same address on a top boot chip erases the whole first 32 Kword sector */
	hw_reset(&HW_S29GL064N_03);
	hw_set_word(0x0000, 0); hw_set_word(0x7FFF, 0); hw_set_word(0x8000, 0);
	check(erase_sector(0x1000) == 1, "top boot: erase_sector returns done");
	check(hw_word(0x0000) == 0xFFFF && hw_word(0x7FFF) == 0xFFFF && hw_word(0x8000) == 0, "top boot: one 32 Kword sector");

	/* erase by size 512 KiB (0x40000 words... the 1E command passes words): number of sector erases */
	hw_reset(&HW_MX29LV640EB);
	address = 0;
	check(erase_range(&address, 0x40000) == 1, "erase_range bottom boot done");
	check(hw_sector_erases == 8 + (0x40000 - 0x8000) / 0x8000, "bottom boot: 8 small + 7 large sectors for 0x40000 words");
	check(address == 0x40000, "bottom boot: address ends after the range (printed by SECTORADD text)");
	ok = 1;
	for(i = 0; i < 0x40000; i += 0x1000) { hw_set_word(i, 0); }
	hw_reset(&HW_MX29LV640EB);
	for(i = 0; i < 0x40000; i += 0x400) hw_set_word(i, 0);
	address = 0;
	erase_range(&address, 0x40000);
	for(i = 0; i < 0x40000; i++) if(hw_word(i) != 0xFFFF) { ok = 0; break; }
	check(ok, "bottom boot: whole range erased");
	check_clean("erase_range bottom");

	hw_reset(&HW_S29GL064N_03);
	for(i = 0; i < 0x40000; i += 0x400) hw_set_word(i, 0);
	address = 0;
	check(erase_range(&address, 0x40000) == 1, "erase_range top boot done");
	check(hw_sector_erases == 0x40000 / 0x8000, "top boot: one erase per 32 Kword sector (8, not 15)");
	ok = 1;
	for(i = 0; i < 0x40000; i++) if(hw_word(i) != 0xFFFF) { ok = 0; break; }
	check(ok, "top boot: whole range erased");
	check(address == 0x40000, "top boot: address ends after the range");
	check_clean("erase_range top");

	/* The PC counts dots for its progress bar (8 below 64 KiB, then one per 64 KiB): the same count on every layout */
	{
		const hw_chip_config_t *chips[] = { &HW_MX29LV640EB, &HW_S29GL064N_03, &HW_S29GL064N_01 };
		unsigned n;
		for(n = 0; n < 3; n++)
			{
				hw_reset(chips[n]);
				hw_cdc_clear();
				address = 0;
				erase_range(&address, 0x40000);
				check(strlen(hw_cdc_log) == 15 && strspn(hw_cdc_log, ".") == 15, "erase 512K prints 15 dots on every layout");
			}
	}

	/* 4 MiB = 0x200000 words, every layout */
	{
		const hw_chip_config_t *chips[] = { &HW_MX29LV640EB, &HW_S29GL064N_03, &HW_S29GL064N_04, &HW_S29GL064N_01 };
		unsigned n;
		for(n = 0; n < 4; n++)
			{
				hw_reset(chips[n]);
				for(i = 0; i < 0x200000; i += 0x800) hw_set_word(i, 0);
				address = 0;
				check(erase_range(&address, 0x200000) == 1, "erase 4M done");
				ok = 1;
				for(i = 0; i < 0x200000; i++) if(hw_word(i) != 0xFFFF) { ok = 0; break; }
				check(ok, "erase 4M: every word erased");
				check_clean("erase 4M");
			}
	}

	/* a single sector given by address (1E with arg 0): one erase, address moves on */
	hw_reset(&HW_S29GL064N_03);
	address = 0x18000;
	check(erase_range(&address, 1) == 1 && hw_sector_erases == 1, "single sector form: one erase");

	/* chip erase */
	hw_reset(&HW_S29GL064N_03);
	hw_set_word(0x3FFFFF, 0); hw_set_word(0, 0); hw_set_word(0x12345, 0);
	check(eraseFLASH() == 1, "chip erase returns done");
	check(hw_word(0x3FFFFF) == 0xFFFF && hw_word(0) == 0xFFFF && hw_word(0x12345) == 0xFFFF, "chip erase: all erased");
	check_clean("chip erase");

	/* a chip that never answers: timeout is reported, not a hang */
	{
		hw_chip_config_t stuck = HW_S29GL064N_03;
		stuck.sector_erase_ns = 100000000000ULL;   /* 100 s, beyond ERASE_SECTOR_TIMEOUT_MS */
		hw_reset(&stuck);
		check(erase_sector(0) == 0, "stuck sector erase: timeout reported");
		check(hw_errors == 0, "stuck sector erase: no bus error");
	}
}

/* ---- write then read back through getByte ---- */

static void test_read_back(void)
{
	uint32_t i;
	int ok = 1;
	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	for(i = 0; i < 16; i++)
		flash_program_word(i, (uint8_t)(0xA0 + i), (uint8_t)(0x10 + i));
	read_mode();
	for(i = 0; i < 16; i++)
		if(getByte(i) != (uint8_t)(0x10 + i)) ok = 0;     /* the 8-bit bus read returns the low byte */
	check(ok, "getByte reads the low byte of each programmed word");
	check_clean("read back");
}

/* ---- Write Buffer Programming ---- */

static void make_block(uint8_t *block, unsigned seed)
{
	unsigned i;
	for(i = 0; i < 1024; i++)
		block[i] = (uint8_t)((i * 37U + seed * 11U) ^ (i >> 3));
	/* gaps the PC pads with 0xFF: a whole page, and a page with FF at both ends */
	memset(block + 64, 0xFF, 32);
	memset(block + 160, 0xFF, 4);
	memset(block + 188, 0xFF, 4);
	memset(block + 512, 0xFF, 512);       /* second half of the block blank */
}

static void test_write_buffer(void)
{
	uint8_t block[1024];
	uint32_t base = 0x4000, k;
	int ok = 1, words_written;
	int cycles_word, cycles_page;

	/* the same block through the word path and through the page path: same content, fewer bus cycles */
	make_block(block, 1);
	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	for(k = 0; k < 512; k++)
		if(!(block[2 * k] == 0xFF && block[2 * k + 1] == 0xFF))
			ok &= flash_program_word(base + k, block[2 * k], block[2 * k + 1]);
	check(ok, "word path programs the block");
	cycles_word = hw_write_cycles;
	words_written = hw_program_ops;

	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	for(k = 0; k < 32; k++)
		ok &= flash_program_page(base + k * 16, block + k * 32);
	check(ok, "page path programs the block");
	cycles_page = hw_write_cycles;
	for(k = 0; k < 512; k++)
		if(hw_word(base + k) != (uint16_t)((block[2 * k] << 8) | block[2 * k + 1])) ok = 0;
	check(ok, "page path: chip holds the same data as the PC sent");
	check(hw_buffer_programs > 0 && hw_buffer_programs <= 32, "page path used write buffer operations");
	check(hw_program_while_busy == 0, "page path: no command while the chip was busy");
	check(cycles_page * 2 < cycles_word, "page path needs less than half the bus write cycles");
	printf("  info: block of %d programmed words: %d write cycles with the word path, %d with write buffer (%d operations)\n",
	       words_written, cycles_word, cycles_page, hw_buffer_programs);
	check_clean("write buffer");

	/* a page that is all 0xFFFF costs nothing */
	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	{
		uint8_t blank[32];
		memset(blank, 0xFF, sizeof(blank));
		check(flash_program_page(0x100, blank) == 1 && hw_write_cycles == 0, "blank page: nothing sent to the chip");
	}

	/* a single word at the end of a page (span of one word) */
	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	{
		uint8_t one[32];
		memset(one, 0xFF, sizeof(one));
		one[30] = 0x12; one[31] = 0x34;
		check(flash_program_page(0x200, one) == 1 && hw_word(0x20F) == 0x1234 && hw_word(0x20E) == 0xFFFF, "page with one word at the end");
	}

	/* top boot sector area (4 Kword sectors): the page stays inside its sector */
	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	make_block(block, 3);
	for(k = 0; k < 32; k++)
		ok &= flash_program_page(0x3FF000 + k * 16, block + k * 32);
	check(ok, "pages programmed inside a 4 Kword boot sector");
	check_clean("boot sector pages");

	/* programming over data that was not erased: failure reported, chip reset, usable afterwards */
	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	hw_set_word(0x303, 0x0000);
	{
		uint8_t page[32];
		memset(page, 0xFF, sizeof(page));
		page[6] = 0x12; page[7] = 0x34;       /* word 3 */
		check(flash_program_page(0x300, page) == 0, "write buffer over unerased data is reported");
		check_clean("write buffer failure");
		check(flash_program_word(0x400, 0xAB, 0xCD) == 1 && hw_word(0x400) == 0xABCD, "chip usable after a write buffer failure");
	}

	/* the chip aborts the operation (DQ1): needs the abort reset, reported as failure, usable afterwards */
	hw_reset(&HW_S29GL064N_03);
	idle_bus();
	{
		uint8_t page[32];
		memset(page, 0x55, sizeof(page));
		hw_inject_buffer_abort = 1;
		check(flash_program_page(0x500, page) == 0, "write buffer abort (DQ1) is reported");
		check(flash_program_page(0x510, page) == 1 && hw_word(0x510) == 0x5555, "chip usable after the abort reset sequence");
		check_clean("write buffer abort");
	}

	/* chip gating */
#if FLASH_FAST
	hw_reset(&HW_S29GL064N_03); flash_fast_forget(); check(flash_fast_available() == 1, "FLASH_FAST: S29GL064N is offered the write buffer");
	hw_reset(&HW_MX29LV640EB);  flash_fast_forget(); check(flash_fast_available() == 0, "FLASH_FAST: MX29LV640EB keeps the word program");
	hw_reset(&HW_M29W640DT);    flash_fast_forget(); check(flash_fast_available() == 0, "FLASH_FAST: M29W640DT keeps the word program");
	hw_reset(&HW_S29GL064N_03); flash_fast_forget(); flash_fast_available();
	hw_reset(&HW_MX29LV640EB);
	check(flash_fast_available() == 1, "FLASH_FAST: the probe result is kept until forgotten");
	flash_fast_forget();
	check(flash_fast_available() == 0, "FLASH_FAST: after forgetting, the new cartridge is probed");
	check_clean("fast gating");
#else
	hw_reset(&HW_S29GL064N_03); flash_fast_forget();
	check(flash_fast_available() == 0, "FLASH_FAST=0: never offered");
#endif
}

int main(void)
{
	test_detects();
	test_layout_probe();
	test_program();
	test_erase();
	test_read_back();
	test_write_buffer();
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
