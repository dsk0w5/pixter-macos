#ifndef _LH75411_H_
#define _LH75411_H_

#include <stdint.h>

#define IO		volatile uint32_t
#define IOW		volatile uint32_t
#define IOR		const volatile uint32_t
#define PAD		uint32_t

struct Lh75411smc {
	IO bcr[4];
};

struct Lh75411rcpc {
	IO ctrl;
	IOR identification;
	IO remap;
	IO softReset;
	IOR resetStatus;
	IOW resetStatusClr;
	IO sysClkPrescaler;
	PAD pad0[2];
	IO apbPeriphClkCtrl[2];
	IO ahbClkCtrl;
	PAD pad1[4];
	IO lcdPrescaler;
	IO sspPRescaler;
	PAD pad2[14];
	IO intConfig;
	IOW intClear;
};

struct Lh75411vic {
	IOR irqStatus;
	IOR fiqStatus;
	IOR rawIntr;
	IO intSelect;
	IO intEnable;
	IOW intEnClear;
	IO softInt;
	IOW softIntClear;
	PAD pad0[4];
	IO vectAddr;
	IO defVectAddr;
	PAD pad1[50];
	IO vectAddrX[16];
	PAD pad2[48];
	IO vectCtrlX[16];
};

struct Lh75411iocon {
	IO ebiMux;
	IO pdMux;
	IO peMux;
	IO timerMux;
	IO lcdMux;
	IO paResMux;
	IO pbResMux;
	IO pcResMux;
	IO pdResMux;
	IO peResMux;
	IO adcMux;
};

struct Lh75411dma {
	IO srcLo;
	IO srcHi;
	IO destLo;
	IO destHi;
	IO max;
	IO ctrl;
	IOR soCurHi;
	IOR soCurrLo;
	IOR deCurrHi;
	IOR deCurrLo;
	IO tCnt;
};

struct Lh75411clcdc {
	IO timing[3];
	PAD pad0[1];
	IO upbase;
	IO lpbase;
	IO intrEnable;
	IO ctrl;
	IO status;
	IO interrupt;
	IOR upcurr;
	IOR lpcurr;
	PAD pad1[116];
	IO clut[128];
};

struct Lh75411ali {
	IO aliSetup;
	IO aliCtrl;
	IO aliTiming1;
	IO aliTiming2;
};

struct Lh75411timers {
	IO t0ctrl;
	IO t0cmpCapCtr;
	IO t0intCtrl;
	IO t0status;
	IO t0cnt;
	IO t0cmp[2];
	IO t0cap[5];

	IO t1ctrl;
	IO t1intCtrl;
	IO t1status;
	IO t1cnt;
	IO t1cmp[2];
	IO t1cap[2];

	IO t2ctrl;
	IO t2intCtrl;
	IO t2status;
	IO t2cnt;
	IO t2cmp[2];
	IO t2cap[2];
};

struct Lh75411wdt {
	IO ctrl;
	IOW cntr;
	IO tstr;
	IOR cnt[4];
};

struct Lh75411rtc {
	IOR dr[2];
	IO mr[2];
	union {
		IOR state;
		IOW eoi;
	};
	IO lr[2];
	IO ctrl;
};

struct Lh75411ssp {
	IO ctrl[2];
	IO dr;
	IOR sr;
	IO cpsr;
	union {
		IOR iir;
		IOW icr;
	};
	IO rxto;
};

struct Lh75411uartA {		//similar to 16C550
	IO dr;
	union {
		IOR rsr;
		IOW ecr;
	};
	PAD pad0[4];
	IOR fr;
	PAD pad1[2];
	IO ibrd;
	IO fbrd;
	IO lctrlH;
	IO ctrl;
	IO ifls;
	IO imsc;
	IOR ris;
	IOR mis;
	IO icr;
	IO dmactrl;
};

struct Lh75411uartB {		//similar to 82510
	union {
		struct {
			union {
				IOW txd;	//DLAB = 0
				IOR rxd;	//DLAB = 0
				IO bal;		//DLAB = 1
			};
			union {
				IO ger;		//DLAB = 0
				IO bah;		//DLAB = 1
			};
			IO gir;
			IO lcr;
			IO mctrl;
			IO lsr;
			PAD pad0[1];
			IO actrl0;
		} bank0;

		struct {
			union {
				IOW txd;
				IOR rxd;
			};
			union {
				IOW txf;
				IOR rxf;
			};
			IO gir;
			union {
				IOW tmctrl;
				IOR tmst;
			};
			union {
				IOW mctrl;
				IOR flr;
			};
			union {
				IOW rcm;
				IOR rst;
			};
			IOW tcm;
			union {
				IOW icm;
				IOR gsr;
			};
		} bank1;

		struct {
			PAD pad0;
			IO fmd;
			IO gir;
			IO tmd;
			IO imd;
			IO actrl1;
			IO rie;
			IO rmd;
		} bank2;

		struct {
			union {
				IO clcf;	//DLAB = 0
				IO bbl;		//DLAB = 1
			};
			union {
				IO bacf;	//DLAB = 0
				IO bbh;		//DLAB = 1
			};
			IO gir;
			IO bbcf;
			PAD pad0[2];
			IO tmie;
		} bank3;
	};
};

struct Lh75411port {
	IO dr;
	PAD pad0[1];
	IO ddr;
};

struct Lh75411inPort {
	IOR dr;
};

struct Lh75411adc {
	IOR hw;
	IOR lw;
	IOR rr;
	IO im;
	IO pc;
	IO gc;
	IOR gs;
	IOR is;
	IOR fs;
	IO hwcb[16];
	IO lwcb[16];
	IO ihwctrl;
	IO ilwctrl;
	IOR mis;
	IOW ic;
};

#define LH75411UART0		((struct Lh75411uartA*)		0xfffc0000)
#define LH75411UART1		((struct Lh75411uartA*)		0xfffc1000)
#define LH75411UART2		((struct Lh75411uartB*)		0xfffc2000)
#define LH75411ADC			((struct Lh75411adc*)		0xfffc3000)
#define LH75411TIMERS		((struct Lh75411timers*)	0xfffc4000)
#define LH75411SSP			((struct Lh75411ssp*)		0xfffc6000)
#define LH75411PORTI		((struct Lh75411port*)		0xfffdb000)
#define LH75411PORTJ		((struct Lh75411inPort*)	0xfffdb004)
#define LH75411PORTG		((struct Lh75411port*)		0xfffdc000)
#define LH75411PORTH		((struct Lh75411port*)		0xfffdc004)
#define LH75411PORTE		((struct Lh75411port*)		0xfffdd000)
#define LH75411PORTF		((struct Lh75411port*)		0xfffdd004)
#define LH75411PORTC		((struct Lh75411port*)		0xfffde000)
#define LH75411PORTD		((struct Lh75411port*)		0xfffde004)
#define LH75411PORTA		((struct Lh75411port*)		0xfffdf000)
#define LH75411PORTB		((struct Lh75411port*)		0xfffdf004)
#define LH75411RTC			((struct Lh75411rtc*)		0xfffe0000)
#define LH75411DMA			((struct Lh75411dma*)		0xfffe1000)
#define LH75411RCPC			((struct Lh75411rcpc*)		0xfffe2000)
#define LH75411WDT			((struct Lh75411wdt*)		0xfffe3000)
#define LH75411ALI			((struct Lh75411ali*)		0xfffe4000)
#define LH75411IOCON		((struct Lh75411iocon*)		0xfffe5000)
#define LH75411SMC			((struct Lh75411smc*)		0xffff1000)
#define LH75411CLCDC		((struct Lh75411clcdc*)		0xffff4000)
#define LH75411VIC			((struct Lh75411vic*)		0xfffff000)


//IRQ #s
#define IRQn_WDT					0
#define IRQn_UNUSED_1				1
#define IRQn_ARM_DBGCOMMRX			2
#define IRQn_ARM_DBGCOMMTX			3
#define IRQn_TMR0					4
#define IRQn_TMR1					5
#define IRQn_TMR2					6
#define IRQn_EXT_INT_0				7
#define IRQn_EXT_INT_1				8
#define IRQn_EXT_INT_2				9
#define IRQn_EXT_INT_3				10
#define IRQn_EXT_INT_4				11
#define IRQn_EXT_INT_5				12
#define IRQn_EXT_INT_6				13
#define IRQn_UNUSED_14				14
#define IRQn_RTC_ALARM				15
#define IRQn_ADC_TSCIRQ				16
#define IRQn_ADC_BOR				17
#define IRQn_ADC_PENIRQ				18
#define IRQn_LCD					19
#define IRQn_SSPTXINTR				20
#define IRQn_SSPRXINTR				21
#define IRQn_SSPRORINTR				22
#define IRQn_SSPRXTOINTR			23
#define IRQn_SSPINTR				24
#define IRQn_UART1_RXINTR			25
#define IRQn_UART1_TXINTR			26
#define IRQn_UART1_INTR				27
#define IRQn_UART0_INTR				28
#define IRQn_UART2					29
#define IRQn_DMA					30




#undef IO
#undef PAD
#undef IOR

#endif
