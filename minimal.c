#include <stdint.h>
#include "CH573SFR.h"

#define SLEEPTIME_MS 300

#define SYS_SAFE_ACCESS(a)  do { R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1; \
								 R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2; \
								 asm volatile ("nop\nnop"); \
								 {a} \
								 R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG0; \
								 asm volatile ("nop\nnop"); } while(0)

// For debug writing to the debug interface.
#define DMDATA0 			   (*((PUINT32V)0xe0000380))

#define GPIO_Pin_8             (0x00000100)
#define GPIOA_ResetBits(pin)   (R32_PA_CLR |= (pin))
#define GPIOA_SetBits(pin)     (R32_PA_OUT |= (pin))
#define GPIOA_ModeCfg_Out(pin) R32_PA_PD_DRV &= ~(pin); R32_PA_DIR |= (pin)

typedef struct __attribute__((packed)) {
	volatile uint32_t CTLR;
	volatile uint64_t CNT;
	volatile uint64_t CMP;
	volatile uint32_t CNTFG;
} SysTick_Type;

#define CORE_PERIPH_BASE              (0xE0000000) /* System peripherals base address in the alias region */

#define SysTick_BASE                  (CORE_PERIPH_BASE + 0xF000)
#define SysTick                       ((SysTick_Type *) SysTick_BASE)
#define SysTick_LOAD_RELOAD_Msk       (0xFFFFFFFFFFFFFFFF)
#define SysTick_CTRL_RELOAD_Msk       (1 << 8)
#define SysTick_CTRL_CLKSOURCE_Msk    (1 << 2)
#define SysTick_CTRL_TICKINT_Msk      (1 << 1)
#define SysTick_CTRL_ENABLE_Msk       (1 << 0)

#define R32_CLK_SYS_CFG     (*((PUINT32V)0x40001008))  // RWA, system clock configuration, SAM
#define  RB_TX_32M_PWR_EN   0x40000                    // RWA, extern 32MHz HSE power contorl
#define  RB_PLL_PWR_EN      0x100000                   // RWA, PLL power control
void Clock60MHz() {
	SYS_SAFE_ACCESS(
		R8_PLL_CONFIG &= ~(1 << 5);
		R32_CLK_SYS_CFG = (1 << 6) | (0x48 & 0x1f) | RB_TX_32M_PWR_EN | RB_PLL_PWR_EN; // 60MHz = 0x48
	);

	asm volatile ("nop\nnop\nnop\nnop");	
	R8_FLASH_CFG = 0x53;
}

void DelayMs(int ms) {
	//uint64_t targend = SysTick->CNT + (ms * 60 * 1000); // 60MHz clock
	//while( ((int64_t)( SysTick->CNT - targend )) < 0 );
	for(int i = 0; i < ms*600; i++) asm volatile ("nop\nnop");
}

void blink(int n) {
	for(int i = n-1; i >= 0; i--) {
		GPIOA_ResetBits(GPIO_Pin_8);
		DelayMs(33);
		GPIOA_SetBits(GPIO_Pin_8);
		if(i) DelayMs(33);
	}
}

void char_debug(char c) {
	// this while is wasting clock ticks, but the easiest way to demo the debug interface
	while(DMDATA0 & 0xc0);
	DMDATA0 = 0x85 | (c << 8);
}

void print(char msg[], int size, int endl) {
	for(int i = 0; i < size; i++) {
		char_debug(msg[i]);
	}
	if(endl) {
		char_debug('\r');
		char_debug('\n');
	}
}

void print_bytes(uint8_t data[], int size) {
	char hex_digits[] = "0123456789abcdef";
	char hx[] = "0x00 ";
	for(int i = 0; i < size; i++) {
		hx[2] = hex_digits[(data[i] >> 4) & 0x0F];
		hx[3] = hex_digits[data[i] & 0x0F];
		print(hx, 5, /*endl*/FALSE);
	}
	print(0, 0, /*endl*/TRUE);
}

int main(void) {
	Clock60MHz();
	GPIOA_ModeCfg_Out(GPIO_Pin_8);
	GPIOA_SetBits(GPIO_Pin_8);
	SysTick->CTLR = SysTick_CTRL_RELOAD_Msk |
					SysTick_CTRL_CLKSOURCE_Msk |
					SysTick_CTRL_TICKINT_Msk |
					SysTick_CTRL_ENABLE_Msk; /* Enable SysTick IRQ and SysTick Timer */

	blink(5);
	print("~ ch573 ~", 9, TRUE);

	while(1) {
		DelayMs(SLEEPTIME_MS -33);
		blink(1); // 33 ms
	}
}
