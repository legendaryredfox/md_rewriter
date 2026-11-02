#include "main.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "usbd_cdc_if.h"
#include "MD.h"


	char disbuff[40];

	/* Flash chips that take the AMD/JEDEC command set used by this firmware (NFW-1).
	 * Only the low byte of each ID word is read (the bus read is 8 bits wide), so the codes are
	 * the low bytes of manufacturer and device. "confirmed": checked against a datasheet. */
	typedef struct {
		uint8_t manufacturer;
		uint8_t device;
		const char *name;
	} flash_chip_t;

	static const flash_chip_t flash_chips[] = {
		{ 0xC2, 0xCB, "MX29LV640EB" },   /* 1.0 chip: confirmed by the released firmware */
		{ 0xC2, 0xC9, "MX29LV640ET" },   /* not confirmed against a datasheet */
		{ 0x20, 0xDF, "M29W640DB" },     /* confirmed (ST datasheet) */
		{ 0x20, 0xDE, "M29W640DT" },     /* confirmed (ST datasheet) */
		{ 0x20, 0x7E, "M29W640G" },      /* not confirmed against a datasheet */
	};

	static const flash_chip_t *find_flash_chip(uint8_t manufacturer, uint8_t device)
		{
			uint32_t i;
			for(i = 0; i < (sizeof(flash_chips) / sizeof(flash_chips[0])); i++)
				{
					if((flash_chips[i].manufacturer == manufacturer) && (flash_chips[i].device == device))
						return &flash_chips[i];
				}
			return NULL;
		}
	
	
	void Delay_nop(uint32_t nop)
		{
			while(nop--)
			{
				__ASM volatile("nop");
			}
		}
				
	void softsystemreset(void)
			{
			__ASM volatile ("cpsid i");
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
	 * a 0 back to 1 on a cell that was not erased). Leaves the bus in write mode. Returns 1 when
	 * the word was programmed, 0 on failure or timeout (the chip is then put back in read mode). */
	uint8_t flash_wait_program(uint32_t addr, uint8_t lastLowByte)
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
					if(v & 0x20U)
						{
							v = getByte(addr);
							ok = ((v & 0x80U) == (lastLowByte & 0x80U)) ? 1 : 0;
							break;
						}
				}
			bus_output_fast();
			if(!ok)
				{
					setByte(FLASH_UNLOCK_1, 0xf0);   /* reset: back to read array mode */
				}
			return ok;
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
			
			
	void checkid(void)
			{
				CDC_Transmit("-- MD CART ID --\r\n");		
				
				char disbuff[50];
				uint8_t s1,s2;
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
				
				/* %02X: both bytes always have two digits, so the PC can split the ID without guessing. */
				sprintf(disbuff,"FLASHID:%02X%02X\r\n",s1,s2);
				CDC_Transmit(disbuff);
				{
					const flash_chip_t *chip = find_flash_chip(s1,s2);
					if(chip != NULL)
						{
							/* Same line layout as 1.0 ("<name> MD FLASH CART"), so old apps still see a detected cart. */
							sprintf(disbuff,"%s MD FLASH CART\r\n",chip->name);
							CDC_Transmit(disbuff);
						}
					else
						{
							CDC_Transmit("NO FIND NGP CARD\r\n");
						}
				}
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
			