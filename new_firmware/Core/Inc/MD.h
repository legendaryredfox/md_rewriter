#ifndef __MD_H
#define __MD_H

#include "main.h"
#include <stdint.h>

#define GPIO_WriteLow(GPIOx,a)    GPIOx->BSRR=(((uint32_t)(uint8_t)~(a))<<16)|((uint32_t)(uint8_t)(a))
#define GPIO_WriteHigh(GPIOx,a)    GPIOx->BSRR=(((uint8_t)(uint8_t)~(a))<<24)|(((uint32_t)(uint8_t)(a))<<8)
#ifndef MD_HOST_TEST   /* the PC test (tests/host) supplies its own pin accessors */
#define BITBAND(addr, bitnum) ((addr & 0xF0000000)+0x2000000+((addr &0xFFFFF)<<5)+(bitnum<<2)) 
#define MEM_ADDR(addr)  *((volatile unsigned long  *)(addr)) 
#define BIT_ADDR(addr, bitnum)   MEM_ADDR(BITBAND(addr, bitnum)) 

#define GPIOA_ODR_Addr    (GPIOA_BASE+12) //0x4001080C 
#define GPIOB_ODR_Addr    (GPIOB_BASE+12) //0x40010C0C 
#define GPIOC_ODR_Addr    (GPIOC_BASE+12) //0x4001100C 
#define GPIOD_ODR_Addr    (GPIOD_BASE+12) //0x4001140C 
#define GPIOE_ODR_Addr    (GPIOE_BASE+12) //0x4001180C 
#define GPIOF_ODR_Addr    (GPIOF_BASE+12) //0x40011A0C    
#define GPIOG_ODR_Addr    (GPIOG_BASE+12) //0x40011E0C    

#define GPIOA_IDR_Addr    (GPIOA_BASE+8) //0x40010808 
#define GPIOB_IDR_Addr    (GPIOB_BASE+8) //0x40010C08 
#define GPIOC_IDR_Addr    (GPIOC_BASE+8) //0x40011008 
#define GPIOD_IDR_Addr    (GPIOD_BASE+8) //0x40011408 
#define GPIOE_IDR_Addr    (GPIOE_BASE+8) //0x40011808 
#define GPIOF_IDR_Addr    (GPIOF_BASE+8) //0x40011A08 
#define GPIOG_IDR_Addr    (GPIOG_BASE+8) //0x40011E08 
 
 
#define PAout(n)   BIT_ADDR(GPIOA_ODR_Addr,n)   
#define PAin(n)    BIT_ADDR(GPIOA_IDR_Addr,n)   

#define PBout(n)   BIT_ADDR(GPIOB_ODR_Addr,n)   
#define PBin(n)    BIT_ADDR(GPIOB_IDR_Addr,n)   

#define PCout(n)   BIT_ADDR(GPIOC_ODR_Addr,n)  														
#define PCin(n)    BIT_ADDR(GPIOC_IDR_Addr,n)  	 

#define PDout(n)   BIT_ADDR(GPIOD_ODR_Addr,n)   
#define PDin(n)    BIT_ADDR(GPIOD_IDR_Addr,n)  

#define PEout(n)   BIT_ADDR(GPIOE_ODR_Addr,n)  
#define PEin(n)    BIT_ADDR(GPIOE_IDR_Addr,n)   

#define PFout(n)   BIT_ADDR(GPIOF_ODR_Addr,n)   
#define PFin(n)    BIT_ADDR(GPIOF_IDR_Addr,n)   

#define PGout(n)   BIT_ADDR(GPIOG_ODR_Addr,n)   
#define PGin(n)    BIT_ADDR(GPIOG_IDR_Addr,n)  
#endif

#define MD_WR 			PBout(14)
#define MD_RD  			PBout(4)
#define MD_CS 			PBout(6)
#define MD_RESET 		PBout(15)
#define MD_TIME 		PBout(12)
#define MD_M3 			PBout(13)

/* USB <-> main loop shared state. Written from the USB interrupt, read by the
 * main loop, hence volatile. */
#define RX_BLOCKS      16U   /* 16 x 64 bytes = one 1024-byte data block */
#define RX_PKT_SIZE    64U   /* CDC full-speed bulk packet size */
#define CMD_MAX_LEN    16U   /* longest command packet the PC sends is 9 bytes */
#define CDC_TX_TIMEOUT_MS   2000U

/* Version text sent right after the connect banner ("FWVER:x"). Set by the Makefile. */
#ifndef FW_VERSION
#define FW_VERSION "1.1.0-dev"
#endif

/* AMD/JEDEC unlock addresses (word addresses on the cartridge bus). */
#define FLASH_UNLOCK_1      0x555U
#define FLASH_UNLOCK_2      0x2AAU

/* Wait for the end of each word program with DQ7 data polling (NFW-2).
 * Build with -DFLASH_POLL=0 to get exactly the 1.0 write sequence (fixed delay only), for
 * comparing against the old firmware on real hardware. */
#ifndef FLASH_POLL
#define FLASH_POLL          1
#endif
/* Upper bound of polls per word. One poll costs a few microseconds, a word program
 * takes 10 us typical and 200 us at most on the supported chips. */
/* Write Buffer Programming (16 words per operation) for chips whose datasheet documents it, today
 * the S29GL064N. Off by default: nothing here has run on hardware. -DFLASH_FAST=1 turns it on;
 * the chip is still checked by ID before each image, every other chip keeps the word program. */
#ifndef FLASH_FAST
#define FLASH_FAST          0
#endif
#define FLASH_PROGRAM_POLL_MAX   2000U
#define ERASE_SECTOR_TIMEOUT_MS  15000U
#define ERASE_CHIP_TIMEOUT_S     300U

extern volatile uint8_t receiveBuffer[RX_BLOCKS][RX_PKT_SIZE];
extern volatile uint8_t cmdbuff[RX_PKT_SIZE];
extern volatile uint8_t buffcnt;


void Delay_nop(uint32_t nop);
void read_mode(void);
void write_mode(void);
void enableSram_MD(uint8_t enableSram);
void softsystemreset(void);
void read_mode(void);
void write_mode(void);
uint8_t getByte(uint32_t addr);
void setAddress(uint32_t addr);
void write_data_bus(uint16_t data);
void setByte(uint32_t address, uint16_t dat);
uint8_t eraseFLASH(void);
void checkid(void);
void flash_autoselect(uint8_t id[4]);
uint8_t flash_has_bottom_boot_sectors(void);
void checkheadermenu(void);
uint8_t erase_sector(uint32_t sector_addr);
uint8_t flash_wait_program(uint32_t addr, uint8_t lastLowByte);
uint8_t flash_program_word(uint32_t addr, uint8_t hi, uint8_t lo);
uint8_t flash_program_page(uint32_t page_addr, const uint8_t data[32]);
uint8_t flash_fast_available(void);
void flash_fast_forget(void);
uint8_t erase_range(uint32_t *address, uint32_t words);
void bus_input_fast(void);
void bus_output_fast(void);

		
#endif




