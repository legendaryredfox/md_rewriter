#ifndef HOST_HW_H
#define HOST_HW_H

#include <stdint.h>

/* Sector layout of the emulated 64 Mbit (4 M words) chip. */
typedef enum { HW_BOTTOM_BOOT, HW_TOP_BOOT, HW_UNIFORM } hw_layout_t;
/* Command family of the emulated chip: AMD with 11-bit unlock decode (555h/2AAh), AMD decoding 15
 * bits (5555h/2AAAh, MX29L3211), Intel/Sharp (28F400). */
typedef enum { HW_FAMILY_AMD = 0, HW_FAMILY_AMD_5555, HW_FAMILY_INTEL } hw_family_t;

typedef struct {
	hw_layout_t layout;
	uint16_t id[4];             /* words read at 0x00, 0x01, 0x0E, 0x0F in autoselect mode */
	uint64_t program_ns;        /* time one word takes to program */
	uint64_t sector_erase_ns;
	uint64_t chip_erase_ns;
	int write_buffer;           /* takes 25h/29h Write Buffer Programming (16 words) */
	hw_family_t family;
	uint32_t words;             /* chip size in words, 0 = 4 M words */
} hw_chip_config_t;

/* Chips used by the tests. */
extern const hw_chip_config_t HW_MX29LV640EB, HW_S29GL064N_03, HW_S29GL064N_04, HW_S29GL064N_01, HW_M29W640DT, HW_SST39VF6401B, HW_M29W320EB, HW_M29W320ET, HW_MX29L3211, HW_PA28F400B, HW_PA28F400T;

void hw_reset(const hw_chip_config_t *chip);     /* new chip, fully erased (0xFFFF), clock at 0 */
void hw_set_rom_only(int on);                    /* a mask ROM: reads return rom data, writes are ignored */
uint16_t hw_word(uint32_t word_addr);            /* array content */
void hw_set_word(uint32_t word_addr, uint16_t v);

/* Counters, so a test can assert on what the firmware did. */
extern int hw_errors;                            /* bus contention, writes with the bus as input, ... */
extern char hw_error_text[256];                  /* the first error */
extern int hw_sector_erases;                     /* sector erase commands accepted */
extern int hw_chip_erases;
extern int hw_program_ops;                       /* word programs accepted */
extern int hw_write_cycles;                      /* every write cycle the chip saw */
extern int hw_buffer_programs;                   /* write buffer operations accepted */
extern int hw_inject_buffer_abort;               /* 1: the next write buffer operation ends in an abort (DQ1) */
extern int hw_program_while_busy;                /* program/erase commands that arrived while busy */
extern int hw_intel_block_erases, hw_intel_programs;
extern uint64_t hw_now_ns;

/* Text sent with CDC_Transmit since the last clear. */
extern char hw_cdc_log[4096];
void hw_cdc_clear(void);

#endif
