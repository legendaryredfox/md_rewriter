/* Stand-in for Core/Inc/main.h when MD.c is compiled on the PC (MD_HOST_TEST).
 * Provides just what MD.c and MD.h touch: GPIO ports, the HAL calls, the pin accessors. */
#ifndef __MAIN_H
#define __MAIN_H

#include <stdint.h>

#define __ASM __asm__

typedef struct {
	volatile uint32_t CRL, CRH, IDR, ODR, BSRR;
} GPIO_TypeDef;

typedef struct {
	uint32_t Pin, Mode, Pull, Speed;
} GPIO_InitTypeDef;

typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;

#define GPIO_PIN_0  0x0001U
#define GPIO_PIN_1  0x0002U
#define GPIO_PIN_2  0x0004U
#define GPIO_PIN_3  0x0008U
#define GPIO_PIN_4  0x0010U
#define GPIO_PIN_5  0x0020U
#define GPIO_PIN_6  0x0040U
#define GPIO_PIN_7  0x0080U
#define GPIO_PIN_8  0x0100U
#define GPIO_PIN_9  0x0200U
#define GPIO_PIN_10 0x0400U
#define GPIO_PIN_11 0x0800U
#define GPIO_PIN_12 0x1000U
#define GPIO_PIN_13 0x2000U
#define GPIO_PIN_14 0x4000U
#define GPIO_PIN_15 0x8000U

#define GPIO_MODE_INPUT      0U
#define GPIO_MODE_OUTPUT_PP  1U
#define GPIO_PULLUP          1U
#define GPIO_SPEED_FREQ_HIGH 3U

/* Ports are reached through host_port(), which first lets the chip model catch up with the
 * pin changes the firmware made since the last access. */
GPIO_TypeDef *host_port(int index);
#define GPIOA (host_port(0))
#define GPIOB (host_port(1))
#define GPIOC (host_port(2))
#define GPIOD (host_port(3))
#define GPIOE (host_port(4))

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState state);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);
void HAL_Delay(uint32_t ms);
uint32_t HAL_GetTick(void);
void HAL_NVIC_SystemReset(void);
void CDC_Transmit(const char *str);
void host_delay_nop(uint32_t nop);

/* Control and data pins used through the PxOut(n) accessors of MD.h. */
extern volatile uint8_t host_pin_b[16];
extern volatile uint8_t host_pin_e[16];
extern volatile uint8_t host_pin_a[16];
#define PBout(n) host_pin_b[n]
#define PEout(n) host_pin_e[n]
#define PAout(n) host_pin_a[n]

#endif
