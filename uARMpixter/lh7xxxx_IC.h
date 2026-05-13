//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _LH7XXXX_IC_H_
#define _LH7XXXX_IC_H_

#include "soc_IC.h"


#define LH7xxxx_I_WDT					0
#define LH7xxxx_I_UNUSED1				1
#define LH7xxxx_I_ARM_DBGCOMMRX			2
#define LH7xxxx_I_ARM_DBGCOMMTX			3
#define LH7xxxx_I_TMR0					4
#define LH7xxxx_I_TMR1					5
#define LH7xxxx_I_TMR2					6
#define LH7xxxx_I_EXT_INT_0				7
#define LH7xxxx_I_EXT_INT_1				8
#define LH7xxxx_I_EXT_INT_2				9
#define LH7xxxx_I_EXT_INT_3				10
#define LH7xxxx_I_EXT_INT_4				11
#define LH7xxxx_I_EXT_INT_5				12
#define LH7xxxx_I_EXT_INT_6				13
#define LH7xxxx_I_RTC					15
#define LH7xxxx_I_ADC_TSCIRQ			16
#define LH7xxxx_I_ADC_BOR				17
#define LH7xxxx_I_ADC_PENIRQ			18
#define LH7xxxx_I_LCD					19
#define LH7xxxx_I_UART2					29
#define LH7xxxx_I_NUM_IRQS				32

//LH754XX
	#define LH754xx_I_UNUSED_14			14
	#define LH754xx_I_SSPTXINTR			20
	#define LH754xx_I_SSPRXINTR			21
	#define LH754xx_I_SSPRORINTR		22
	#define LH754xx_I_SSPRXTOINTR		23
	#define LH754xx_I_SSPINTR			24
	#define LH754xx_I_UART1_RXINTR		25
	#define LH754xx_I_UART1_TXINTR		26
	#define LH754xx_I_UART1_INTR		27
	#define LH754xx_I_UART0_INTR		28
	#define LH754xx_I_DMA				30
	#define LH754xx_I_CAN				31

	#define LH754XX_NUM_EXT_INT_PINS	7


//LH795XX
	#define LH795xx_I_EXT_INT_7			14
	#define LH795xx_I_DMA_STRM_0		20
	#define LH795xx_I_DMA_STRM_1		21
	#define LH795xx_I_DMA_STRM_2		22
	#define LH795xx_I_DMA_STRM_3		23
	#define LH795xx_I_SSPI2SINTR		24
	#define LH795xx_I_ETHERNET			25
	#define LH795xx_I_USB				26
	#define LH795xx_I_UART0				27
	#define LH795xx_I_UART1				28
	#define LH795xx_I_USB_DMA			30
	#define LH795xx_I_I2C				31

	#define LH795XX_NUM_EXT_INT_PINS	8





#endif