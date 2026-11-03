/* Emulation of the cartridge bus and of an AMD/JEDEC x16 flash chip, so the real MD.c can run on
 * the PC. It models only what MD.c does: control lines on PB4/PB6/PB14, address on PA0-7 and
 * PD0-15, a 16-bit data bus on PE0-15, command cycles, autoselect, word program with DQ7/DQ6/DQ5
 * status, sector and chip erase with the boot-sector layouts. Time advances in Delay_nop() and
 * HAL_Delay(). It is a model written from the datasheets, not a measurement of a real chip. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "host_hw.h"

#define WORDS 0x400000U
#define NS_PER_NOP 55ULL         /* one Delay_nop iteration, about 4 cycles at 72 MHz (assumed) */

const hw_chip_config_t HW_MX29LV640EB    = { HW_BOTTOM_BOOT, { 0x00C2, 0x22CB, 0x0000, 0x0000 }, 9000, 500000000ULL, 40000000000ULL, 0 };
const hw_chip_config_t HW_S29GL064N_03   = { HW_TOP_BOOT,    { 0x0001, 0x227E, 0x220C, 0x2201 }, 15000, 500000000ULL, 40000000000ULL, 1 };
const hw_chip_config_t HW_S29GL064N_04   = { HW_BOTTOM_BOOT, { 0x0001, 0x227E, 0x220C, 0x2200 }, 15000, 500000000ULL, 40000000000ULL, 1 };
const hw_chip_config_t HW_S29GL064N_01   = { HW_UNIFORM,     { 0x0001, 0x227E, 0x2210, 0x2201 }, 15000, 500000000ULL, 40000000000ULL, 1 };
const hw_chip_config_t HW_SST39VF6401B  = { HW_UNIFORM,     { 0x00BF, 0x236D, 0x0000, 0x0000 }, 10000, 18000000ULL, 40000000ULL, 0 };
const hw_chip_config_t HW_M29W640DT      = { HW_TOP_BOOT,    { 0x0020, 0x22DE, 0x0000, 0x0000 }, 10000, 800000000ULL, 40000000000ULL, 0 };

typedef enum { S_READ, S_C1, S_C2, S_AUTO, S_PROG_DATA, S_E1, S_E2, S_E3, S_PROG_BUSY, S_ERASE_BUSY, S_FAIL,
               S_WB_COUNT, S_WB_DATA, S_WB_CONFIRM, S_WB_ABORT } state_t;

static hw_chip_config_t cfg;
static uint16_t *mem;
static int rom_only;
static state_t state;
static uint64_t busy_until;
static uint32_t prog_addr;
static uint16_t prog_data;
static uint16_t toggle;

static GPIO_TypeDef ports[5];
volatile uint8_t host_pin_a[16], host_pin_b[16], host_pin_e[16];
static int wr_pending;

int hw_errors, hw_sector_erases, hw_chip_erases, hw_program_ops, hw_program_while_busy;
int hw_write_cycles, hw_buffer_programs, hw_inject_buffer_abort;
static uint32_t wb_sector_start, wb_sector_size, wb_page, wb_remaining, wb_last_addr;
static uint16_t wb_data[16];
static uint16_t wb_mask;               /* which of the 16 words were loaded */
static uint16_t wb_last_data;
static int wb_abort_seq;
char hw_error_text[256];
uint64_t hw_now_ns;
char hw_cdc_log[4096];

static void fail(const char *what)
{
	if(hw_errors == 0)
		snprintf(hw_error_text, sizeof(hw_error_text), "%s", what);
	hw_errors++;
}

/* ---- sectors ---- */
static void sector_of(uint32_t a, uint32_t *start, uint32_t *size)
{
	int small = 0;
	if(cfg.layout == HW_BOTTOM_BOOT)
		small = (a < 0x8000U);
	else if(cfg.layout == HW_TOP_BOOT)
		small = (a >= WORDS - 0x8000U);
	*size = small ? 0x1000U : 0x8000U;
	*start = a & ~(*size - 1U);
}

/* ---- chip ---- */
static void erase_words(uint32_t start, uint32_t n)
{
	uint32_t i;
	for(i = 0; i < n; i++)
		mem[start + i] = 0xFFFF;
}

static void chip_write(uint32_t addr, uint16_t data)
{
	const uint8_t cmd = (uint8_t)(data & 0xFFU);
	const uint32_t a11 = addr & 0x7FFU;

	hw_write_cycles++;
	if(state == S_PROG_BUSY || state == S_ERASE_BUSY)
		{
			hw_program_while_busy++;
			return;
		}
	if(state == S_FAIL)
		{
			if(cmd == 0xF0U)
				state = S_READ;
			return;
		}
	if(rom_only)
		return;
	if(state == S_WB_ABORT)
		{
			/* Write-to-Buffer-Abort Reset: AAh@555, 55h@2AA, F0h@555 */
			if(wb_abort_seq == 0 && a11 == 0x555U && cmd == 0xAAU) wb_abort_seq = 1;
			else if(wb_abort_seq == 1 && a11 == 0x2AAU && cmd == 0x55U) wb_abort_seq = 2;
			else if(wb_abort_seq == 2 && a11 == 0x555U && cmd == 0xF0U) { state = S_READ; wb_abort_seq = 0; }
			else wb_abort_seq = 0;
			return;
		}
	if(state == S_WB_COUNT || state == S_WB_DATA || state == S_WB_CONFIRM)
		{
			/* inside a write buffer operation every cycle is a load or the confirm; anything else aborts */
			if(state == S_WB_COUNT)
				{
					const uint32_t n = (uint32_t)(data & 0x1FU) + 1U;
					uint32_t s0, sz;
					sector_of(addr, &s0, &sz);
					if((data & 0xFFE0U) != 0 || n > 16U || s0 != wb_sector_start) goto wb_abort;
					wb_remaining = n;
					wb_page = addr >> 4;
					wb_mask = 0;
					state = S_WB_DATA;
					return;
				}
			if(state == S_WB_DATA)
				{
					if((addr >> 4) != wb_page) goto wb_abort;
					wb_data[addr & 15U] = data;
					wb_mask |= (uint16_t)(1U << (addr & 15U));
					wb_last_addr = addr;
					wb_last_data = data;
					if(--wb_remaining == 0) state = S_WB_CONFIRM;
					return;
				}
			{
				uint32_t s0, sz;
				uint32_t k;
				int bad = 0;
				sector_of(addr, &s0, &sz);
				if(cmd != 0x29U || s0 != wb_sector_start || hw_inject_buffer_abort) goto wb_abort;
				for(k = 0; k < 16; k++)
					if((wb_mask >> k) & 1U)
						{
							const uint32_t a = (wb_page << 4) | k;
							if(wb_data[k] & (uint16_t)~mem[a]) bad = 1;
						}
				hw_buffer_programs++;
				prog_addr = wb_last_addr;
				prog_data = wb_last_data;
				if(bad)
					{
						state = S_FAIL;
						return;
					}
				for(k = 0; k < 16; k++)
					if((wb_mask >> k) & 1U)
						{
							const uint32_t a = (wb_page << 4) | k;
							mem[a] = (uint16_t)(mem[a] & wb_data[k]);
							hw_program_ops++;
						}
				busy_until = hw_now_ns + cfg.program_ns * (uint64_t)__builtin_popcount(wb_mask) / 2U + cfg.program_ns;
				state = S_PROG_BUSY;
				return;
			}
		wb_abort:
			state = S_WB_ABORT;
			wb_abort_seq = 0;
			hw_inject_buffer_abort = 0;
			return;
		}
	if(cmd == 0xF0U && state != S_PROG_DATA)
		{
			state = S_READ;
			return;
		}
	switch(state)
		{
		case S_READ:
		case S_AUTO:
			state = (a11 == 0x555U && cmd == 0xAAU) ? S_C1 : state;
			break;
		case S_C1:
			state = (a11 == 0x2AAU && cmd == 0x55U) ? S_C2 : S_READ;
			break;
		case S_C2:
			if(a11 == 0x555U && cmd == 0x90U) state = S_AUTO;
			else if(cmd == 0x25U && cfg.write_buffer) { sector_of(addr, &wb_sector_start, &wb_sector_size); state = S_WB_COUNT; }
			else if(a11 == 0x555U && cmd == 0xA0U) state = S_PROG_DATA;
			else if(a11 == 0x555U && cmd == 0x80U) state = S_E1;
			else state = S_READ;
			break;
		case S_PROG_DATA:
			{
				const uint16_t old = mem[addr];
				hw_program_ops++;
				prog_addr = addr;
				prog_data = data;
				if(data & (uint16_t)~old)
					{
						/* a 0 to 1 change cannot be programmed: the chip gives up (DQ5) */
						state = S_FAIL;
					}
				else
					{
						mem[addr] = (uint16_t)(old & data);
						busy_until = hw_now_ns + cfg.program_ns;
						state = S_PROG_BUSY;
					}
			}
			break;
		case S_E1:
			state = (a11 == 0x555U && cmd == 0xAAU) ? S_E2 : S_READ;
			break;
		case S_E2:
			state = (a11 == 0x2AAU && cmd == 0x55U) ? S_E3 : S_READ;
			break;
		case S_E3:
			if(cmd == 0x30U)
				{
					uint32_t start, size;
					sector_of(addr, &start, &size);
					erase_words(start, size);
					hw_sector_erases++;
					busy_until = hw_now_ns + cfg.sector_erase_ns;
					state = S_ERASE_BUSY;
				}
			else if(cmd == 0x10U && a11 == 0x555U)
				{
					erase_words(0, WORDS);
					hw_chip_erases++;
					busy_until = hw_now_ns + cfg.chip_erase_ns;
					state = S_ERASE_BUSY;
				}
			else
				state = S_READ;
			break;
		default:
			state = S_READ;
			break;
		}
}

static uint16_t status_word(uint16_t dq7, uint16_t dq5)
{
	toggle ^= 1U;
	return (uint16_t)((dq7 ? 0x80U : 0U) | (toggle ? 0x40U : 0U) | (dq5 ? 0x20U : 0U));
}

/* a write buffer abort: DQ7 = data# of the last address loaded, DQ6 toggling, DQ5 = 0, DQ1 = 1 */
static uint16_t abort_status_word(void)
{
	return (uint16_t)(status_word((uint16_t)((~wb_last_data) & 0x80U), 0) | 0x02U);
}

static uint16_t chip_read(uint32_t addr)
{
	switch(state)
		{
		case S_AUTO:
			if(rom_only) return mem[addr];
			switch(addr & 0xFFU)
				{
				case 0x00: return cfg.id[0];
				case 0x01: return cfg.id[1];
				case 0x0E: return cfg.id[2];
				case 0x0F: return cfg.id[3];
				default:   return 0;
				}
		case S_PROG_BUSY:
			if(hw_now_ns >= busy_until)
				{
					state = S_READ;
					return mem[addr];
				}
			return status_word((uint16_t)((~prog_data) & 0x80U), 0);
		case S_ERASE_BUSY:
			if(hw_now_ns >= busy_until)
				{
					state = S_READ;
					return mem[addr];
				}
			return status_word(0, 0);
		case S_FAIL:
			return status_word((uint16_t)((~prog_data) & 0x80U), 1);
		case S_WB_ABORT:
			return abort_status_word();
		default:
			return mem[addr];
		}
}

/* ---- bus ---- */
static void apply_bsrr(void)
{
	int i;
	for(i = 0; i < 5; i++)
		{
			const uint32_t v = ports[i].BSRR;
			if(v)
				{
					const uint32_t set = v & 0xFFFFU, reset = (v >> 16) & 0xFFFFU;
					ports[i].ODR = (ports[i].ODR & ~reset) | set;
					ports[i].BSRR = 0;
				}
		}
}

/* The direction is only checked while a cycle is active; between cycles CRL and CRH are written
 * one after the other, so a half-changed state is normal there. */
static int bus_is_input(void)
{
	const uint32_t lo = ports[4].CRL, hi = ports[4].CRH;
	if(lo == 0x88888888U && hi == 0x88888888U) return 1;
	if(lo == 0x33333333U && hi == 0x33333333U) return 0;
	fail("data bus CRL/CRH is neither all-input nor all-output during a cycle");
	return 0;
}

static int bus_looks_input(void)
{
	return (ports[4].CRL & 0xFU) == 0x8U;
}

static uint32_t bus_address(void)
{
	return ((ports[0].ODR & 0xFFU) << 16) | (ports[3].ODR & 0xFFFFU);
}

static void sync(void)
{
	const int cs = !host_pin_b[6], wr = !host_pin_b[14], rd = !host_pin_b[4];
	apply_bsrr();
	if(cs && wr)
		{
			if(rd) fail("RD and WR low at the same time");
			if(bus_is_input()) fail("write cycle with the data bus as input");
			wr_pending = 1;
		}
	else if(wr_pending)
		{
			wr_pending = 0;
			chip_write(bus_address(), (uint16_t)(ports[4].ODR & 0xFFFFU));
		}
}

GPIO_TypeDef *host_port(int index)
{
	const int cs = !host_pin_b[6], wr = !host_pin_b[14], rd = !host_pin_b[4];
	sync();
	if(index == 4)
		{
			if(cs && rd && !wr)
				{
					if(!bus_is_input()) fail("read cycle with the data bus as output");
					ports[4].IDR = chip_read(bus_address());
				}
			else
				ports[4].IDR = bus_looks_input() ? 0xFFFFU : (ports[4].ODR & 0xFFFFU);
		}
	return &ports[index];
}

void host_delay_nop(uint32_t nop)
{
	hw_now_ns += (uint64_t)nop * NS_PER_NOP;
	sync();
}

void HAL_Delay(uint32_t ms)
{
	hw_now_ns += (uint64_t)ms * 1000000ULL;
	sync();
}

uint32_t HAL_GetTick(void) { return (uint32_t)(hw_now_ns / 1000000ULL); }
void HAL_NVIC_SystemReset(void) {}
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin) { (void)port; (void)pin; }

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState state_)
{
	sync();
	if(state_ == GPIO_PIN_SET) port->ODR |= pins; else port->ODR &= ~(uint32_t)pins;
}

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
	int pin;
	sync();
	for(pin = 0; pin < 16; pin++)
		{
			volatile uint32_t *reg = (pin < 8) ? &port->CRL : &port->CRH;
			const int shift = (pin & 7) * 4;
			if(!(init->Pin & (1U << pin)))
				continue;
			*reg &= ~(0xFU << shift);
			if(init->Mode == GPIO_MODE_INPUT)
				{
					*reg |= 0x8U << shift;
					if(init->Pull == GPIO_PULLUP) port->ODR |= (1U << pin);
				}
			else
				*reg |= 0x3U << shift;
		}
}

void CDC_Transmit(const char *str)
{
	strncat(hw_cdc_log, str, sizeof(hw_cdc_log) - strlen(hw_cdc_log) - 1);
}

void hw_cdc_clear(void) { hw_cdc_log[0] = 0; }

/* ---- test control ---- */
void hw_reset(const hw_chip_config_t *chip)
{
	int i;
	cfg = *chip;
	if(!mem) mem = malloc(WORDS * sizeof(uint16_t));
	erase_words(0, WORDS);
	memset(ports, 0, sizeof(ports));
	for(i = 0; i < 16; i++) { host_pin_a[i] = 1; host_pin_b[i] = 1; host_pin_e[i] = 0; }
	ports[4].CRL = ports[4].CRH = 0x88888888U;   /* power-up: floating inputs */
	ports[4].ODR = 0xFFFFU;
	state = S_READ;
	wr_pending = 0;
	rom_only = 0;
	toggle = 0;
	hw_errors = hw_sector_erases = hw_chip_erases = hw_program_ops = hw_program_while_busy = 0;
	hw_write_cycles = hw_buffer_programs = hw_inject_buffer_abort = 0;
	hw_error_text[0] = 0;
	hw_now_ns = 0;
	hw_cdc_clear();
}

void hw_set_rom_only(int on) { rom_only = on; }
uint16_t hw_word(uint32_t a) { return mem[a]; }
void hw_set_word(uint32_t a, uint16_t v) { mem[a] = v; }
