#define WEAK __attribute__ ((weak))
#define ALIAS(f) __attribute__ ((weak, alias (#f)))

#include "6502m23.h"



WEAK void IntDefaultHandler(void);
WEAK void NMI_Handler(void) ALIAS(IntDefaultHandler);
WEAK void HardFault_Handler(void) ALIAS(IntDefaultHandler);
WEAK void SVC_Handler(void) ALIAS(IntDefaultHandler);
WEAK void PendSV_Handler(void) ALIAS(IntDefaultHandler);
WEAK void SysTick_Handler(void) ALIAS(IntDefaultHandler);


void SPI0_IRQHandler(void) ALIAS(IntDefaultHandler);
void SPI1_IRQHandler(void) ALIAS(IntDefaultHandler);
void UART0_IRQHandler(void) ALIAS(IntDefaultHandler);
void UART1_IRQHandler(void) ALIAS(IntDefaultHandler);
void UART2_IRQHandler(void) ALIAS(IntDefaultHandler);
void I2C_IRQHandler(void) ALIAS(IntDefaultHandler);
void SCT_IRQHandler(void) ALIAS(IntDefaultHandler);
void MRT_IRQHandler(void) ALIAS(IntDefaultHandler);
void CMP_IRQHandler(void) ALIAS(IntDefaultHandler);
void WDT_IRQHandler(void) ALIAS(IntDefaultHandler);
void BOD_IRQHandler(void) ALIAS(IntDefaultHandler);
void WKT_IRQHandler(void) ALIAS(IntDefaultHandler);
void PININT0_IRQHandler(void) ALIAS(IntDefaultHandler);
void PININT1_IRQHandler(void) ALIAS(IntDefaultHandler);
void PININT2_IRQHandler(void) ALIAS(IntDefaultHandler);
void PININT3_IRQHandler(void) ALIAS(IntDefaultHandler);
void PININT4_IRQHandler(void) ALIAS(IntDefaultHandler);
void PININT5_IRQHandler(void) ALIAS(IntDefaultHandler);
void PININT6_IRQHandler(void) ALIAS(IntDefaultHandler);
void PININT7_IRQHandler(void) ALIAS(IntDefaultHandler);


//main must exist
extern int main(void);

//stack top (provided by linker)
extern void __stack_top();
extern void __data_data();
extern void __data_start();
extern void __data_end();
extern void __bss_start();
extern void __bss_end();



#define INFINITE_LOOP_LOW_POWER		while (1) {				\
							asm("wfi":::"memory");	\
						}




void __attribute__((noreturn)) IntDefaultHandler(void)
{
	INFINITE_LOOP_LOW_POWER
}


static void __attribute__((noreturn)) ResetISR(void)
{
	unsigned int *dst, *src, *end;

	//copy data
	dst = (unsigned int*)&__data_start;
	src = (unsigned int*)&__data_data;
	end = (unsigned int*)&__data_end;
	while(dst != end)
		*dst++ = *src++;

	//init bss
	dst = (unsigned int*)&__bss_start;
	end = (unsigned int*)&__bss_end;
	while(dst != end)
		*dst++ = 0;

	main();

	__builtin_unreachable();
	while(1);
}




__attribute__ ((section(".vectors"))) void (*const __VECTORS[]) (void) =
{
	&__stack_top,		// The initial stack pointer
	ResetISR,		// The reset handler
	NMI_Handler,		// The NMI handler
	HardFault_Handler,	// The hard fault handler
	
	0,			// Reserved
	0,			// Reserved
	0,			// Reserved
	0,			// Reserved
	0,			// Reserved
	0,			// Reserved
	0,			// Reserved
	
	
	SVC_Handler,		// SVCall handler
	0,			// Reserved
	0,			// Reserved
	PendSV_Handler,		// The PendSV handler
	SysTick_Handler,	// The SysTick handler
	// Chip Level irqs here
};


