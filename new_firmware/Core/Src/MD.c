#include "main.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "usbd_cdc_if.h"
#include "MD.h"
#include "flashid.h"


	char disbuff[40];

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
							setByte(FLASH_UNLOCK_1, 0xaa);
							setByte(FLASH_UNLOCK_2, 0x55);
						}
					setByte(FLASH_UNLOCK_1, 0xf0);   /* reset: back to read array mode */
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
			setByte(0x555,0xaa);
			setByte(0x2aa,0x55);
			setByte(0x555,0xa0);	
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
					flash_info_t info;
					flash_autoselect(id);
					flash_fast_state = (flash_identify(id, &info) && info.write_buffer) ? 1 : 2;
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
				setByte(FLASH_UNLOCK_1, 0xaa);
				setByte(FLASH_UNLOCK_2, 0x55);
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

	uint8_t erase_sector(uint32_t sector_addr) 
		{
			uint32_t start = HAL_GetTick();
			write_mode();
			setByte(0x555, 0xaa);
			setByte(0x2aa, 0x55);
			setByte(0x555, 0x80);
			setByte(0x555, 0xaa);
			setByte(0x2aa, 0x55);
			setByte(sector_addr, 0x30);
			while (1) 
				{
					if ((HAL_GetTick() - start) > ERASE_SECTOR_TIMEOUT_MS)
						{
							/* chip never finished: put it back in read-array mode and report failure */
							write_mode();
							setByte(0x555, 0xf0);
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
							setByte(0x555, 0xaa);
							setByte(0x2aa, 0x55);
							setByte(0x555, 0x80);
							setByte(0x555, 0xaa);
							setByte(0x2aa, 0x55);
							setByte(sector_addr, 0x30);
						}
			}
		}
				
	/* Erase-by-size loop of the 1E command: erases sectors from *address on until `words` words are
	 * covered, printing a dot per sector. *address is left on the next sector, as in 1.0 (the
	 * "SECTORADD:" text prints it). Returns 0 when a sector never finished. Below 0x8000 words the
	 * step is 8 KiB (eight small boot sectors) only on chips that may have them, see NFW-10. */
	uint8_t erase_range(uint32_t *address, uint32_t words)
		{
			const uint32_t smallstep = flash_has_bottom_boot_sectors() ? 0x1000U : 0x8000U;
			for(uint32_t i = 0;i < words ;)
				{
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
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(0x555, 0xaa);
				setByte(0x2aa, 0x55);
				setByte(0x555, 0x80);
				setByte(0x555, 0xaa);
				setByte(0x2aa, 0x55);
				setByte(0x555, 0x10);
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
						setByte(0x555, 0xf0);
						HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
						return 0;
					}
				}
				write_mode();
				CDC_Transmit("FLASH ERASE FINISH!!!\r\n");
				HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
				return 1;
			}
			
			
	/* One autoselect session: reads manufacturer, device (cycles 1) and the two further ID cycles
	 * (words 0x0E and 0x0F, low byte of each), then sends the reset command. Same bus sequence and
	 * delays as the second half of 1.0's checkid(). Needs the chip in read-array mode. */
	void flash_autoselect(uint8_t id[4])
			{
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(0x555, 0xaa);
				setByte(0x2aa, 0x55);
				setByte(0x555, 0x90);			
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
				setByte(0x555, 0xaa);
				setByte(0x2aa, 0x55);
				setByte(0x555, 0xf0);				
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
			}

	/* 1 when erase-by-size must step through eight 8 KiB sectors at the start of the chip (what
	 * 1.0 always did), 0 only when the chip is positively known not to have them (top boot or
	 * uniform): there the first 64 KiB is one sector and stepping by 8 KiB would erase it eight
	 * times. Unknown chips and unreadable IDs give 1, the 1.0 behaviour. */
	uint8_t flash_has_bottom_boot_sectors(void)
			{
				uint8_t id[4];
				flash_info_t info;
				flash_autoselect(id);
				if(flash_identify(id, &info) && (info.layout == FLASH_LAYOUT_NO_BOTTOM_BOOT))
					return 0;
				return 1;
			}

	void checkid(void)
			{
				CDC_Transmit("-- MD CART ID --\r\n");		
				
				char disbuff[50];
				uint8_t s1,s2;
				uint8_t id[4];
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(0x555, 0xaa);
				setByte(0x2aa, 0x55);
				setByte(0x555, 0x90);			
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				read_mode();		
				s1 = getByte(0x0);
				s2 = getByte(0x1);
				write_mode();
				HAL_Delay(10);
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				HAL_Delay(1);
				setByte(0x555, 0xaa);
				setByte(0x2aa, 0x55);
				setByte(0x555, 0xf0);	
				MD_RD= 1;
				MD_CS= 1;
				MD_WR= 1;
				read_mode();
				HAL_Delay(100);
				flash_autoselect(id);
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
							CDC_Transmit("NO FIND NGP CARD\r\n");
						}
				}
			}
