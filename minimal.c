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
#define GPIOA_InverseBits(pin) (R32_PA_OUT ^= pin)
#define GPIOA_ModeCfg_Out(pin) R32_PA_PD_DRV &= ~(pin); R32_PA_DIR |= (pin)

#define RTC_FREQ               32000
#define RTC_MAX_COUNT          0xA8BFFFFF
#define CLK_PER_US             (1.0 / ((1.0 / RTC_FREQ) * 1000 * 1000))
#define CLK_PER_MS             (CLK_PER_US * 1000)
#define US_TO_RTC(us)          ((uint32_t)((us) * CLK_PER_US + 0.5))
#define MS_TO_RTC(ms)          ((uint32_t)((ms) * CLK_PER_MS + 0.5))

typedef struct __attribute__((packed)) {
	volatile uint32_t CTLR;
	volatile uint64_t CNT;
	volatile uint64_t CMP;
	volatile uint32_t CNTFG;
} SysTick_Type;

/* memory mapped structure for Program Fast Interrupt Controller (PFIC) */
typedef struct
{
	volatile uint32_t  ISR[8];           // 0
	volatile uint32_t  IPR[8];           // 20H
	volatile uint32_t ITHRESDR;         // 40H
	volatile uint32_t FIBADDRR;         // 44H
	volatile uint32_t  CFGR;             // 48H
	volatile uint32_t  GISR;             // 4CH
	volatile uint8_t  VTFIDR[4];        // 50H
	uint8_t       RESERVED0[0x0C];  // 54H
	volatile uint32_t VTFADDR[4];       // 60H
	uint8_t       RESERVED1[0x90];  // 70H
	volatile uint32_t  IENR[8];          // 100H
	uint8_t       RESERVED2[0x60];  // 120H
	volatile uint32_t  IRER[8];          // 180H
	uint8_t       RESERVED3[0x60];  // 1A0H
	volatile uint32_t  IPSR[8];          // 200H
	uint8_t       RESERVED4[0x60];  // 220H
	volatile uint32_t  IPRR[8];          // 280H
	uint8_t       RESERVED5[0x60];  // 2A0H
	volatile uint32_t IACTR[8];         // 300H
	uint8_t       RESERVED6[0xE0];  // 320H
	volatile uint8_t  IPRIOR[256];      // 400H
	uint8_t       RESERVED7[0x810]; // 500H
	volatile uint32_t SCTLR;            // D10H
} PFIC_Type;

#define CORE_PERIPH_BASE              (0xE0000000) /* System peripherals base address in the alias region */

#define PFIC_BASE                     (CORE_PERIPH_BASE + 0xE000)
#define SysTick_BASE                  (CORE_PERIPH_BASE + 0xF000)

#define PFIC                          ((PFIC_Type *) PFIC_BASE)
#define NVIC                          PFIC

#define SysTick_BASE                  (CORE_PERIPH_BASE + 0xF000)
#define SysTick                       ((SysTick_Type *) SysTick_BASE)
#define SysTick_LOAD_RELOAD_Msk       (0xFFFFFFFFFFFFFFFF)
#define SysTick_CTRL_RELOAD_Msk       (1 << 8)
#define SysTick_CTRL_CLKSOURCE_Msk    (1 << 2)
#define SysTick_CTRL_TICKINT_Msk      (1 << 1)
#define SysTick_CTRL_ENABLE_Msk       (1 << 0)

// #define CH57x
// #define MCU_PACKAGE 3
// #include "isler.h"

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
	uint64_t targend = SysTick->CNT - (ms * 60 * 1000); // 60MHz clock
	while( ((int64_t)( SysTick->CNT - targend )) > 0 );
}

static inline void RTCTrigger(uint32_t cyc)
{
	//get the rtc current time
	uint32_t alarm = (uint32_t) R16_RTC_CNT_32K | ( (uint32_t) R16_RTC_CNT_2S << 16 );

	alarm += cyc;

	if( alarm > RTC_MAX_COUNT )
	{
		alarm-=	RTC_MAX_COUNT;
	}

	SYS_SAFE_ACCESS
	(
		R32_RTC_TRIG = alarm;
	);
}

void RTC_IRQHandler(void) __attribute__((section(".highcode"))) __attribute__((interrupt));
void RTC_IRQHandler(void)
{
	// clear timer and trigger flags
	R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);

	// Flip the gpio (GPIO_InverseBits is a ch5xx specific macro)
	GPIOA_InverseBits(GPIO_Pin_8);

	// Set a trigger again.
	// The TMR function of the RTC can also be used for this,
	// but like this the demo basically shows both
	RTCTrigger( MS_TO_RTC(333) );
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

#define MSG "~ ch573 ~"
int main(void) {
	Clock60MHz();
	GPIOA_ModeCfg_Out(GPIO_Pin_8);
	GPIOA_SetBits(GPIO_Pin_8);
	SysTick->CMP = SysTick_LOAD_RELOAD_Msk -1; // SysTick IS COUNTING DOWN!!
	SysTick->CTLR = SysTick_CTRL_RELOAD_Msk |
					SysTick_CTRL_CLKSOURCE_Msk |
					SysTick_CTRL_TICKINT_Msk |
					SysTick_CTRL_ENABLE_Msk; /* Enable SysTick IRQ and SysTick Timer */

	// RFCoreInit(0x14);
	SYS_SAFE_ACCESS
	(
		R32_RTC_TRIG = 0;
		R32_RTC_CTRL |= RB_RTC_LOAD_HI;
		R32_RTC_CTRL |= RB_RTC_LOAD_LO;
		R8_RTC_MODE_CTRL |= RB_RTC_TRIG_EN;  //enable RTC trigger
	);
	NVIC->IENR[((uint32_t)(RTC_IRQn) >> 5)] = (1 << ((uint32_t)(RTC_IRQn) & 0x1F));

	blink(5);
	print(MSG, sizeof(MSG), TRUE);
	RTCTrigger( MS_TO_RTC(333) );

	while(1) {
//		DelayMs(SLEEPTIME_MS -33);
//		blink(1); // 33 ms
	}
}
