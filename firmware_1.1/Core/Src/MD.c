#include "main.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "usbd_cdc_if.h"
#include "MD.h"
#include "flashid.h"


	char disbuff[40];

	/* Active flash command set. flash_probe() chooses it from the chip ID; until a chip proves
	 * otherwise it stays AMD_555, which is what 1.0 always did (docs/KY_SUPPORT.md). */
	static flash_cmdset_t g_cmdset = FLASH_CMDSET_AMD_555;
	static uint32_t g_u1 = 0x555U, g_u2 = 0x2AAU;          /* unlock addresses of the AMD sets */
	static uint8_t g_probed = 0;                           /* 1: the variables above and g_info describe the cartridge */
	static flash_info_t g_info;
	static flash_cfi_t g_cfi;
	static uint8_t g_cfi_valid = 0;                        /* 1: an unknown chip answered the CFI query */
	static uint8_t g_info_valid = 0;                       /* 1: the chip is in the table */
	static void ensure_probed(void);
	static uint8_t intel_program_word(uint32_t addr, uint8_t hi, uint8_t lo);
	static uint8_t intel_erase_block(uint32_t addr);
	static void intel_block(uint32_t a, uint32_t *start, uint32_t *size);

	void Delay_nop(uint32_t nop)
		{
#ifdef MD_HOST_TEST
			host_delay_nop(nop);   /* the PC test advances its clock and its chip model here */
#else
			while(nop--)
			{
				__ASM volatile("nop");
			}
#endif
		}
				
	void softsystemreset(void)
			{
#ifndef MD_HOST_TEST
			__ASM volatile ("cpsid i");
#endif
			HAL_NVIC_SystemReset();	
			}


	void read_mode(void)
			{
				GPIO_InitTypeDef GPIO_InitStruct;
				HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
														|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9
														|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13
														|GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);			
				GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
														|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9
														|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13
														|GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_0|GPIO_PIN_1;		
				GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
				GPIO_InitStruct.Pull = GPIO_PULLUP;
				HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
			}
			
	void write_mode(void)
			{
				GPIO_InitTypeDef GPIO_InitStruct;
				HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9
                          |GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13
                          |GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);			
				GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9
                          |GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13
                          |GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_0|GPIO_PIN_1;		
				GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
				GPIO_InitStruct.Pull = GPIO_PULLUP;
				GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
				HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
			}
			
	void enableSram_MD(uint8_t enableSram) {
				MD_RESET = 0;//RESET 0	
				Delay_nop(100);
				MD_RESET = 1;	//RESET = 1
				MD_TIME = 1;//TIME = 1	
				Delay_nop(100);
				write_mode();
			  // Set D0 to either 1(enable SRAM) or 0(enable ROM)
				if(enableSram == 1)
					{
						PEout(0) = 1;//D0 = 1	
					}
				else
					{
						PEout(0) = 0; //D0 = 0	
					}
				  // Strobe TIME(PJ0) LOW to latch the data
				MD_TIME = 0;//TIME = 0	
				Delay_nop(100);
				// Set TIME(PJ0) HIGH
				MD_TIME = 1;//TIME = 1	
				Delay_nop(100);
				read_mode();
			}		
	
	/* One write cycle to the mapper register area ($A130xx): the cartridge sees /TIME and /LWR low
	 * with /CE0 high. The data goes on D0-D7 (the register keeps its low bits). `byte_addr` is the
	 * 68000 byte address, the bus carries it divided by two. */
	void mapper_write(uint32_t byte_addr, uint8_t value)
			{
				write_mode();
				write_data_bus(value);
				setAddress(byte_addr >> 1);
				MD_CS = 1;
				MD_RD = 1;
				MD_WR = 1;
				MD_TIME = 1;
				Delay_nop(60);
				MD_TIME = 0;
				MD_WR = 0;
				Delay_nop(30);
				MD_WR = 1;
				MD_TIME = 1;
				Delay_nop(60);
			}

	void ssf2_select_page(uint8_t page)
			{
				mapper_write(0xA130F3U, (uint8_t)(page & 0x3FU));
			}

	void ssf2_read_block(uint16_t block, uint8_t out[1024])
			{
				uint16_t i;
				read_mode();
				MD_WR = 1;
				MD_RD = 1;
				MD_CS = 1;
				for(i = 0; i < 512U; i++)
					{
						setAddress(0x40000UL + (uint32_t)block * 512UL + i);
						MD_CS = 0;
						MD_RD = 0;
						Delay_nop(30);
						out[i * 2U] = (uint8_t)(((GPIOE->IDR) >> 8) & 0xffU);
						out[i * 2U + 1U] = (uint8_t)((GPIOE->IDR) & 0xffU);
						MD_RD = 1;
						MD_CS = 1;
						Delay_nop(50);
					}
			}

	/* Same pin state as read_mode()/write_mode() (inputs with pull-up / push-pull outputs at 50 MHz),
	 * set with two register writes instead of HAL_GPIO_Init, because the polling loop switches
	 * the direction for every word. CNF/MODE nibble: 0x8 = input pull-up, 0x3 = output 50 MHz. */
	void bus_input_fast(void)
			{
				GPIOE->BSRR = 0xFFFFU;            /* ODR = 1: pull-up, as HAL does for GPIO_PULLUP */
				GPIOE->CRL = 0x88888888U;
				GPIOE->CRH = 0x88888888U;
			}

	void bus_output_fast(void)
			{
				GPIOE->BSRR = 0xFFFF0000U;        /* ODR = 0, as write_mode() does before the init */
				GPIOE->CRL = 0x33333333U;
				GPIOE->CRH = 0x33333333U;
			}

		void setAddress(uint32_t addr) 
			{
				GPIO_WriteLow(GPIOA,(addr>>16)&0xff);
				GPIO_WriteHigh(GPIOD,(addr>>8)&0xff);
				GPIO_WriteLow(GPIOD,addr&0xff);
			}
						
	void setByte(uint32_t address, uint16_t dat) 
			{
				write_data_bus(dat);
				setAddress(address);		
				MD_CS= 0;
				MD_WR= 0;
				Delay_nop(30);
				MD_WR= 1;
				MD_CS= 1;
				Delay_nop(60);
			}
						
	uint8_t read_data_busNGP(void) 
			{
				return (GPIOE->IDR) & 0xff;
			}
			
	void write_data_bus(uint16_t data)
			{
				GPIO_WriteLow(GPIOE, data & 0xff);
			}
			
	uint8_t getByte(uint32_t addr) 
			{
				setAddress(addr);
				MD_WR= 1;
				MD_RD= 0;
				MD_CS= 0;
				Delay_nop(30);
				uint8_t data = (GPIOE->IDR) & 0xff;
				MD_WR= 1;
				MD_RD= 1;
				MD_CS= 1;
				Delay_nop(50);
				return data;
			}
			
	/* DQ7 data polling after a word program (AMD algorithm). `lastLowByte` is the low byte of the
	 * word just programmed (data bus D0-D7, the only part the bus read returns). While the chip is
	 * busy, DQ7 reads as the complement of that bit; when done, it reads the real data. DQ5 set
	 * means the chip gave up: read once more, then it is a failure (typically an attempt to program
	 * a 0 back to 1 on a cell that was not erased). `buffered`: after a Write Buffer Programming
	 * operation, where DQ1 set also means failure (abort) and needs the three-cycle abort reset.
	 * Leaves the bus in write mode. Returns 1 when the word was programmed, 0 on failure or
	 * timeout (the chip is then put back in read mode). */
	static uint8_t wait_programmed(uint32_t addr, uint8_t lastLowByte, uint8_t buffered)
		{
			uint8_t ok = 0;
			uint32_t polls;
			bus_input_fast();
			for(polls = 0; polls < FLASH_PROGRAM_POLL_MAX; polls++)
				{
					uint8_t v = getByte(addr);
					if((v & 0x80U) == (lastLowByte & 0x80U))
						{
							ok = 1;
							break;
						}
					if((v & 0x20U) || (buffered && (v & 0x02U)))
						{
							v = getByte(addr);
							ok = ((v & 0x80U) == (lastLowByte & 0x80U)) ? 1 : 0;
							break;
						}
				}
			bus_output_fast();
			if(!ok)
				{
					if(buffered)
						{
							/* Write-to-Buffer-Abort Reset: AAh, 55h, F0h (also resets after DQ5) */
							setByte(g_u1, 0xaa);
							setByte(g_u2, 0x55);
						}
					setByte(g_u1, 0xf0);   /* reset: back to read array mode */
				}
			return ok;
		}

	uint8_t flash_wait_program(uint32_t addr, uint8_t lastLowByte)
		{
			return wait_programmed(addr, lastLowByte, 0);
		}

	/* One word program cycle: unlock, program command, data, then (FLASH_POLL) wait for the chip.
	 * Same bus sequence and delays as the loop in 1.0's main.c. Returns 1 when done, 0 on failure. */
	uint8_t flash_program_word(uint32_t addr, uint8_t hi, uint8_t lo)
		{
			ensure_probed();
			if(g_cmdset == FLASH_CMDSET_INTEL)
				return intel_program_word(addr, hi, lo);
			setByte(g_u1,0xaa);
			setByte(g_u2,0x55);
			setByte(g_u1,0xa0);	
			GPIO_WriteHigh(GPIOE, hi);								
			GPIO_WriteLow(GPIOE, lo);
			setAddress(addr);		
			MD_CS = 0;
			MD_WR = 0;
			Delay_nop(35);
			MD_WR = 1;
			MD_CS = 1;
			Delay_nop(80);
#if FLASH_POLL
			/* NFW-2: wait until the chip has really programmed the word. */
			return flash_wait_program(addr, lo);
#else
			return 1;
#endif
		}

	/* ---- Write Buffer Programming (FLASH_FAST) ---- */

	static uint8_t flash_fast_state = 0;   /* 0 not probed, 1 chip takes the write buffer, 2 it does not */

	/* Forget the probe result: the cartridge may have been changed. Called at the start of an image. */
	void flash_fast_forget(void)
		{
			flash_fast_state = 0;
		}

	/* 1 when FLASH_FAST is built in and the cartridge's chip is known to take Write Buffer
	 * Programming. Probes the chip ID once, then remembers it. */
	uint8_t flash_fast_available(void)
		{
#if FLASH_FAST
			if(flash_fast_state == 0)
				{
					uint8_t id[4];
					flash_probe(id);
					flash_fast_state = (g_info_valid && g_info.write_buffer) ? 1 : 2;
				}
			return (flash_fast_state == 1) ? 1 : 0;
#else
			return 0;
#endif
		}

	/* Programs one 16-word page with a Write Buffer Programming operation (S29GL064N datasheet,
	 * "Write to Buffer": AAh@555, 55h@2AA, 25h@SA, count-1@SA, the words, 29h@SA, then data polling
	 * on the last word loaded). `page_addr` is 16-word aligned, `data` holds the 16 words big
	 * endian (the order they arrive from the PC). Only the span from the first to the last word that
	 * is not 0xFFFF is loaded; a page that is all 0xFFFF costs nothing. Returns 1 on success, 0 when
	 * the chip aborted or timed out (the chip is then reset). Same delays as flash_program_word. */
	uint8_t flash_program_page(uint32_t page_addr, const uint8_t data[32])
		{
			uint8_t first = 16, last = 0, k;
			for(k = 0; k < 16; k++)
				{
					if((data[2 * k] != 0xFF) || (data[2 * k + 1] != 0xFF))
						{
							if(first == 16) first = k;
							last = k;
						}
				}
			if(first == 16)
				return 1;
			{
				const uint32_t sa = page_addr + first;
				setByte(g_u1, 0xaa);
				setByte(g_u2, 0x55);
				setByte(sa, 0x25);
				setByte(sa, (uint16_t)(last - first));       /* number of words minus one */
				for(k = first; k <= last; k++)
					{
						GPIO_WriteHigh(GPIOE, data[2 * k]);
						GPIO_WriteLow(GPIOE, data[2 * k + 1]);
						setAddress(page_addr + k);
						MD_CS = 0;
						MD_WR = 0;
						Delay_nop(35);
						MD_WR = 1;
						MD_CS = 1;
						Delay_nop(80);
					}
				setByte(sa, 0x29);                           /* Program Buffer to Flash */
			}
			return wait_programmed(page_addr + last, data[2 * last + 1], 1);
		}

	/* Last cycle of the sector erase: 30h, 50h on the SST39VF320x (chip table in flashid.c). */
	static uint8_t erase_cmd(void)
		{
			return g_info_valid ? g_info.erase_cmd : 0x30U;
		}

	uint8_t erase_sector(uint32_t sector_addr) 
		{
			uint32_t start;
			ensure_probed();
			if(g_cmdset == FLASH_CMDSET_INTEL)
				return intel_erase_block(sector_addr);
			start = HAL_GetTick();
			write_mode();
			setByte(g_u1, 0xaa);
			setByte(g_u2, 0x55);
			setByte(g_u1, 0x80);
			setByte(g_u1, 0xaa);
			setByte(g_u2, 0x55);
			setByte(sector_addr, erase_cmd());
			while (1) 
				{
					if ((HAL_GetTick() - start) > ERASE_SECTOR_TIMEOUT_MS)
						{
							/* chip never finished: put it back in read-array mode and report failure */
							write_mode();
							setByte(g_u1, 0xf0);
							return 0;
						}
					read_mode();
					uint8_t status1 = getByte(sector_addr);
					uint8_t status2 = getByte(sector_addr);
					if ((status1 & 0x40) == (status2 & 0x40)) 
						{
							if (status2 & 0x80) 
								{
									return 1; 
								}
						}
					if (status2 & 0x20) 
						{
							/* Bit 5 is also part of the data when the erase has just finished (0xFF): look
							 * again before treating it as the chip giving up. */
							const uint8_t status3 = getByte(sector_addr);
							const uint8_t status4 = getByte(sector_addr);
							if (((status3 & 0x40) == (status4 & 0x40)) && (status4 & 0x80))
								{
									return 1;
								}
							/* The data bus is an input here (read_mode above): switch before writing the command. */
							write_mode();
							setByte(g_u1, 0xaa);
							setByte(g_u2, 0x55);
							setByte(g_u1, 0x80);
							setByte(g_u1, 0xaa);
							setByte(g_u2, 0x55);
							setByte(sector_addr, erase_cmd());
						}
			}
		}
				
	/* Erase-by-size loop of the 1E command: erases sectors from *address on until `words` words are
	 * covered, printing a dot per sector. *address is left on the next sector, as in 1.0 (the
	 * "SECTORADD:" text prints it). Returns 0 when a sector never finished. Below 0x8000 words the
	 * step is 8 KiB (eight small boot sectors) only on chips that may have them, see NFW-10. */
	uint8_t erase_range(uint32_t *address, uint32_t words)
		{
			ensure_probed();
			if(g_cmdset == FLASH_CMDSET_INTEL)
				{
					/* Intel blocks are irregular (16/8/8/96/128/128/128 KB): erase the block holding each
					 * address and step to the next one. Dots as for the AMD path: one per 8 KiB below
					 * 64 KiB, one per 64 KiB above. */
					for(uint32_t i = 0; i < words; )
						{
							uint32_t bs, bn, a;
							intel_block(*address, &bs, &bn);
							if(!erase_sector(*address))
								return 0;
							Delay_nop(100);
							for(a = *address; a < bs + bn; a += (a < 0x8000U) ? 0x1000U : 0x8000U)
								CDC_Transmit(".");
							i += (bs + bn) - *address;
							*address = bs + bn;
						}
					return 1;
				}
			const uint32_t smallstep = flash_has_bottom_boot_sectors() ? 0x1000U : 0x8000U;
			for(uint32_t i = 0;i < words ;)
				{
					if(g_info_valid && (g_info.layout == FLASH_LAYOUT_TOP_BOOT) && (g_info.words != 0U) && (*address >= g_info.words - 0x8000U))
						{
							/* Top boot chip: the last 64 KiB is eight 8 KiB sectors, one 30h each (a
							 * 4 MiB ROM on a 32 Mbit chip reaches them). One dot, as for a 64 KiB step. */
							for(uint8_t k = 0; k < 8; k++)
								{
									if(!erase_sector(*address + (uint32_t)k * 0x1000U))
										return 0;
									Delay_nop(100);
								}
							CDC_Transmit(".");
							*address = *address + 0x8000U;
							i = i + 0x8000U;
							continue;
						}
					if(!erase_sector(*address))
						{
							return 0;
						}
					Delay_nop(100);
					if(*address<0x8000)
						{
							/* One dot per 8 KiB, as 1.0 printed: the PC counts dots for its progress bar and
							 * assumes eight below 64 KiB, also when this chip erases them as one sector. */
							CDC_Transmit((smallstep == 0x8000U) ? "........" : ".");
							*address = *address+smallstep;	
							i = i+smallstep;	
						}
					else
						{
							CDC_Transmit(".");
							*address = *address+0x8000;
							i = i+0x8000;	
						}		
				}
			return 1;
		}

	uint8_t eraseFLASH(void)
			{
				CDC_Transmit("-- MD CART ERASE --\r\n");	
				CDC_Transmit("FLASH ERASE START\r\n");		
			ensure_probed();
				if(g_cmdset == FLASH_CMDSET_INTEL)
					{
						/* Intel parts have no chip erase command: erase block after block. */
						uint32_t a = 0;
						const uint32_t t0 = HAL_GetTick();
						while(a < 0x40000U)
							{
								uint32_t bs, bn;
								intel_block(a, &bs, &bn);
								if(!intel_erase_block(a))
									{
										HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
										return 0;
									}
								a = bs + bn;
								HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13);
								sprintf(disbuff,"USE TIME %lu s\r\n",(unsigned long)((HAL_GetTick() - t0) / 1000U));
								CDC_Transmit(disbuff);
							}
						write_mode();
						CDC_Transmit("FLASH ERASE FINISH!!!\r\n");
						HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
						return 1;
					}
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(g_u1, 0xaa);
				setByte(g_u2, 0x55);
				setByte(g_u1, 0x80);
				setByte(g_u1, 0xaa);
				setByte(g_u2, 0x55);
				setByte(g_u1, 0x10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(10);
				read_mode();
				uint8_t eraseflag = 0;
				uint32_t time = 0;
				
				while(eraseflag == 0)
				{
					eraseflag = 1;
					for(uint8_t k = 0; k < 8; k++)
					{
						if(getByte(k) != 0xff) eraseflag = 0;
					}
					HAL_Delay(1000);
					HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13);
					sprintf(disbuff,"USE TIME %lu s\r\n",(unsigned long)time);
					CDC_Transmit(disbuff);
					time = time + 1;
					if((eraseflag == 0) && (time > ERASE_CHIP_TIMEOUT_S))
					{
						/* chip never reported erased: back to read-array mode, report failure */
						write_mode();
						setByte(g_u1, 0xf0);
						HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
						return 0;
					}
				}
				write_mode();
				CDC_Transmit("FLASH ERASE FINISH!!!\r\n");
				HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
				return 1;
			}
			
			
	/* One AMD autoselect session with the given unlock addresses: reads manufacturer, device (cycle 1)
	 * and the two further ID cycles (words 0x0E and 0x0F, low byte of each), then sends the reset
	 * command. With 0x555/0x2AA it is the same bus sequence and delays as the second half of 1.0's
	 * checkid(). Needs the chip in read-array mode. */
	static void autoselect_amd(uint32_t a1, uint32_t a2, uint8_t id[4])
			{
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(a1, 0xaa);
				setByte(a2, 0x55);
				setByte(a1, 0x90);			
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				read_mode();		
				id[0] = getByte(0x0);
				id[1] = getByte(0x1);
				id[2] = getByte(0xE);
				id[3] = getByte(0xF);
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(a1, 0xaa);
				setByte(a2, 0x55);
				setByte(a1, 0xf0);				
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
			}

	/* Intel/Sharp ID: FFh (read array), 90h (read ID), manufacturer at word 0, device at word 1
	 * (FlashKit-MD-Plus flash28IdentMfr/Dev), then FFh again. Cycles 2 and 3 do not exist. */
	static void autoselect_intel(uint8_t id[4])
			{
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(0x0, 0xff);
				setByte(0x0, 0x90);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				read_mode();
				id[0] = getByte(0x0);
				id[1] = getByte(0x1);
				id[2] = 0;
				id[3] = 0;
				write_mode();
				setByte(0x0, 0xff);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
			}

	/* ID read with the command set that is active now. */
	void flash_autoselect(uint8_t id[4])
			{
				if(g_cmdset == FLASH_CMDSET_INTEL)
					autoselect_intel(id);
				else
					autoselect_amd(g_u1, g_u2, id);
			}

	static void ensure_probed(void)
			{
				if(!g_probed)
					{
						uint8_t id[4];
						flash_probe(id);
					}
			}

	void flash_probe_forget(void)
			{
				g_probed = 0;
				g_info_valid = 0;
				g_cfi_valid = 0;
			}

	uint8_t flash_cfi_info(flash_cfi_t *out)
			{
				if(!g_cfi_valid)
					return 0;
				*out = g_cfi;
				return 1;
			}

	/* CFI query: reset twice, 98h at word 55h, then "QRY" at words 10h..12h, the command set at 13h
	 * and the size exponent at 27h; reset again. Used only for a chip the table does not know, and
	 * only to report its size: the chip stays on the AMD 555h set (what 1.0 did). */
	static uint8_t cfi_query(flash_cfi_t *out)
			{
				uint8_t q[3], cs, ex;
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(0x555, 0xf0);
				HAL_Delay(2);
				setByte(0x555, 0xf0);
				HAL_Delay(2);
				setByte(0x55, 0x98);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				read_mode();
				q[0] = getByte(0x10);
				q[1] = getByte(0x11);
				q[2] = getByte(0x12);
				cs = getByte(0x13);
				ex = getByte(0x27);
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(0x555, 0xf0);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				return (uint8_t)flash_cfi_decode(q, cs, ex, out);
			}

	static void flash_adopt(const flash_info_t *info)
			{
				g_info = *info;
				g_info_valid = 1;
				g_cmdset = info->cmdset;
				g_u1 = (g_cmdset == FLASH_CMDSET_AMD_5555) ? 0x5555U : 0x555U;
				g_u2 = (g_cmdset == FLASH_CMDSET_AMD_5555) ? 0x2AAAU : 0x2AAU;
				g_probed = 1;
				if(g_cmdset == FLASH_CMDSET_INTEL)
					{
						write_mode();
						setByte(0x0, 0xff);        /* an Intel chip may still be in ID mode from an earlier probe */
					}
			}

	/* Finds out which command set the cartridge speaks and keeps the answer until
	 * flash_probe_forget(). Order: AMD 555h (what 1.0 did: nothing else touches the bus when the
	 * chip is in the table), AMD 5555h, Intel. A chip found in none of them stays AMD 555h and
	 * `id` holds the AMD 555h answer, which is what 1.0 printed. */
	void flash_probe(uint8_t id[4])
			{
				uint8_t first[4], alt[4];
				flash_info_t info;
				g_probed = 0;
				g_info_valid = 0;
				g_cfi_valid = 0;
				g_cmdset = FLASH_CMDSET_AMD_555;
				g_u1 = 0x555U;
				g_u2 = 0x2AAU;
				autoselect_amd(0x555U, 0x2AAU, first);
				if(flash_identify(first, &info))
					{
						memcpy(id, first, 4);
						flash_adopt(&info);
						return;
					}
				autoselect_amd(0x5555U, 0x2AAAU, alt);
				if(flash_identify(alt, &info) && (info.cmdset == FLASH_CMDSET_AMD_5555))
					{
						memcpy(id, alt, 4);
						flash_adopt(&info);
						return;
					}
				autoselect_intel(alt);
				if(flash_identify(alt, &info) && (info.cmdset == FLASH_CMDSET_INTEL))
					{
						memcpy(id, alt, 4);
						flash_adopt(&info);
						return;
					}
				memcpy(id, first, 4);
				g_cfi_valid = cfi_query(&g_cfi);
				g_probed = 1;
			}

	/* 1 when erase-by-size must step through eight 8 KiB sectors at the start of the chip (what
	 * 1.0 always did), 0 only when the chip is positively known not to have them (top boot or
	 * uniform): there the first 64 KiB is one sector and stepping by 8 KiB would erase it eight
	 * times. Unknown chips and unreadable IDs give 1, the 1.0 behaviour. */
	uint8_t flash_has_bottom_boot_sectors(void)
			{
			ensure_probed();
				if(g_info_valid && ((g_info.layout == FLASH_LAYOUT_NO_BOTTOM_BOOT) || (g_info.layout == FLASH_LAYOUT_TOP_BOOT)))
					return 0;
				return 1;
			}

	/* ---- Intel/Sharp command set (28F400, FlashKit-MD-Plus flash28*) ---- */

	/* Block map of the 28F400 in words: 16 KB boot, 8 KB, 8 KB, 96 KB, then three 128 KB blocks.
	 * Bottom boot (-B) as listed, top boot (-T) mirrored. Addresses past the chip: 128 KB blocks. */
	static void intel_block(uint32_t a, uint32_t *start, uint32_t *size)
			{
				static const uint32_t bs[7] = { 0x0000U, 0x2000U, 0x3000U, 0x4000U, 0x10000U, 0x20000U, 0x30000U };
				static const uint32_t bn[7] = { 0x2000U, 0x1000U, 0x1000U, 0xC000U, 0x10000U, 0x10000U, 0x10000U };
				const int top = g_info_valid && (g_info.layout != FLASH_LAYOUT_BOTTOM_BOOT);
				const uint32_t m = top ? (0x3FFFFU - a) : a;
				uint8_t i;
				if(a >= 0x40000U)
					{
						*start = a & ~0xFFFFU;
						*size = 0x10000U;
						return;
					}
				for(i = 6; i > 0 && bs[i] > m; i--) {}
				if(top)
					{
						*start = 0x40000U - (bs[i] + bn[i]);
						*size = bn[i];
					}
				else
					{
						*start = bs[i];
						*size = bn[i];
					}
			}

	/* 40h, then the data word, then poll the status register (bit 7 = ready). Errors: bit 4
	 * (program), bit 3 (Vpp low), bit 1 (block locked). Always ends with FFh so the chip reads as
	 * an array again. Returns 1 when the word was programmed. */
	static uint8_t intel_program_word(uint32_t addr, uint8_t hi, uint8_t lo)
			{
				uint8_t sr = 0, ok = 0;
				uint32_t polls;
				setByte(addr, 0x40);
				GPIO_WriteHigh(GPIOE, hi);
				GPIO_WriteLow(GPIOE, lo);
				setAddress(addr);
				MD_CS = 0;
				MD_WR = 0;
				Delay_nop(35);
				MD_WR = 1;
				MD_CS = 1;
				Delay_nop(80);
				bus_input_fast();
				for(polls = 0; polls < FLASH_PROGRAM_POLL_MAX; polls++)
					{
						sr = getByte(addr);
						if(sr & 0x80U)
							{
								ok = 1;
								break;
							}
					}
				bus_output_fast();
				if(!ok || (sr & 0x1AU))
					{
						setByte(addr, 0x50);       /* clear status register */
						setByte(addr, 0xff);
						return 0;
					}
				setByte(addr, 0xff);
				return 1;
			}

	/* 20h, D0h (confirm) on the block, then poll bit 7. Errors: bit 5 (erase), 4 (command sequence),
	 * 3 (Vpp low), 1 (locked). Returns 1 when the block is erased. */
	static uint8_t intel_erase_block(uint32_t addr)
			{
				const uint32_t start = HAL_GetTick();
				uint8_t sr = 0, ok = 0;
				write_mode();
				setByte(addr, 0x20);
				setByte(addr, 0xd0);
				bus_input_fast();
				while((HAL_GetTick() - start) <= ERASE_SECTOR_TIMEOUT_MS)
					{
						sr = getByte(addr);
						if(sr & 0x80U)
							{
								ok = 1;
								break;
							}
					}
				bus_output_fast();
				if(!ok || (sr & 0x3AU))
					{
						setByte(addr, 0x50);
						setByte(addr, 0xff);
						return 0;
					}
				setByte(addr, 0xff);
				return 1;
			}

	void checkid(void)
			{
				CDC_Transmit("-- MD CART ID --\r\n");		
				
				char disbuff[50];
				uint8_t s1,s2;
				uint8_t id[4];
				/* flash_probe() does the autoselect session (AMD 555h first: the same bus sequence as 1.0's
				 * second session) and picks the command set. 1.0's extra first session is gone. */
				flash_probe(id);
				s1 = id[0];
				s2 = id[1];
				
				/* %02X: both bytes always have two digits, so the PC can split the ID without guessing. */
				sprintf(disbuff,"FLASHID:%02X%02X\r\n",s1,s2);
				CDC_Transmit(disbuff);
				{
					flash_info_t chip;
					if(flash_identify(id, &chip))
						{
							/* Same line layout as 1.0 ("<name> MD FLASH CART"), so old apps still see a detected cart. */
							sprintf(disbuff,"%s MD FLASH CART\r\n",chip.name);
							CDC_Transmit(disbuff);
						}
					else
						{
							flash_cfi_t cfi;
							if(flash_cfi_info(&cfi))
								{
									/* Before the closing line: apps stop reading at "NO FIND NGP CARD". */
									sprintf(disbuff,"CFI:SIZE=%luKB CMDSET=%u\r\n",(unsigned long)(cfi.bytes >> 10),(unsigned)cfi.cmdset);
									CDC_Transmit(disbuff);
								}
							CDC_Transmit("NO FIND NGP CARD\r\n");
						}
				}
			}
