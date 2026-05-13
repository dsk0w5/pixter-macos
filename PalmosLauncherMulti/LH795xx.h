#ifndef _LH795xx_H_
#define _LH795xx_H_

#include <stdint.h>

#define IO		volatile uint32_t
#define IOW		volatile uint32_t
#define IOR		const volatile uint32_t
#define PAD		uint32_t


struct Lh795xxRcpc {
	IO ctrl;
	IOR chipid;
	IO remap;
	IO softReset;
	IOR resetStatus;
	IOW resetStatusClr;
	IO sysClkPrescaler;
	IO cpuClkPrescaler;
	PAD pad0[1];
	IO apbPeriphClkCtrl[2];
	IO ahbClkCtrl;
	IO pckSel[2];
	PAD pad1[1];
	IOR siliconrev;
	IO lcdPrescaler;
	IO sspPrescaler;
	IO adcPrescaler;
	IO usbPrescaler;
	PAD pad2[12];
	IO intConfig;
	IOW intClear;
	IO coreConfig;
	PAD pad3[13];
	IO sysPllCntl;
	IO usbPllCntl;
};

struct Lh795xxVic {
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
	PAD pad3[51];
	IOR itop;
};

struct Lh795xxIocon {
	IO muxctl1, resctl1;
	PAD pad0[2];
	IO muxctl3, resctl3;
	IO muxctl4, resctl4;
	IO muxctl5, resctl5;
	IO muxctl6, resctl6;
	IO muxctl7, resctl7;
	PAD pad1[4];
	IO muxctl10, resctl10;
	IO muxctl11, resctl11;
	IO muxctl12, resctl12;
	PAD pad2[1];
	IO resctl14;
	IO muxctl14;
	PAD pad3[1];
	IO muxctl15, resctl15;
	PAD pad4[3];
	IO resctl17;
	PAD pad5[2];
	IO muxctl19, resctl19;
	IO muxctl20, resctl20;
	IO muxctl21, resctl21;
	IO muxctl22, resctl22;
	IO muxctl23, resctl23;
	IO muxctl24, resctl24;
	IO muxctl25;
};

struct LhDmaCh {
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
	PAD pad0[5];
};

struct Lh975xxDma {
	union {
		struct LhDmaCh ch[4];
		struct {
			PAD pad0[0x3c];
			IO intrMask;
			IOW intrClr;
			IOR intrSta;
		};
	};
};

struct Lh795xxClcdc {
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

struct Lh795xxAli {
	IO aliSetup;
	IO aliCtrl;
	IO aliTiming1;
	IO aliTiming2;
};

struct Lh795xxTimers {
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

struct Lh795xxWdt {
	IO ctl;
	IOW rst;
	IOR status;
	IOR cnt[4];
};

struct Lh795xxRtc {
	IOR dr;
	IO mr;
	IO lr;
	IO cr;
	IO imsc;
	IOR ris;
	IOR mis;
	IOW icr;
};

struct Lh795xxSsp {
	IO ctrl[2];
	IO dr;
	IOR sr;
	IO cpsr;
	IO imsc;
	IOR ris;
	IOR mis;
	IOW icr;
	IO dcr;
};

struct Lh795xxUart {		//similar to 16C550
	IO dr;
	union {
		IOR rsr;
		IOW ecr;
	};
	PAD pad0[4];
	IOR fr;
	PAD pad1[1];
	IOR ilpr;
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

struct Lh7595xxPort {
	IO dr;
	PAD pad0[1];
	IO ddr;
};

struct Lh795xxInPort {
	IOR dr;
};

struct Lh795xxAdc {
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

struct Lh795xxBc{
	IOR pbc;
	IO cs1ov;
	IO epm;
};

struct Lh795xxI2S {
	IO ctr;
	IOR stat;
	IO imsc;
	IOR ris;
	IOR mis;
	IOW icr;
};


struct Lh795xxCsCfg {
	IO sconfig;
	IO swaitwen;
	IO swaitoen;
	IO swaitrd;
	IO swaitpage;
	IO swaitwr;
	IO sturn;
	PAD pad0[1];
};

struct Lh795xxEmc {
	IO control;
	IOR status;
	PAD pad0[6];
	IO dynmctrl;
	IO dynmref;
	IO dynrcon;
	PAD pad1[1];
	IO precharge;
	IO dynm2pre;
	IO refexit;
	IO doactive;
	IO diactive;
	IO dwrt;
	IO dynactcmd;
	IO dynauto;
	IO dynrefexit;
	IO dynactiveab;
	IO dynamicmrd;
	PAD pad2[9];
	IO wait;
	PAD pad3[31];
	IO dyncfg0;
	IO dynrascas0;
	PAD pad4[6];
	IO dyncfg1;
	IO dynrascas1;
	PAD pad5[54];
	struct Lh795xxCsCfg cs[4];
};

#define LH795xxUART0		((struct Lh795xxUart*)	(PERIPHS_BASE + 0x0c0000))
#define LH795xxUART1		((struct Lh795xxUart*)	(PERIPHS_BASE + 0x0c1000))
#define LH795xxUART2		((struct Lh795xxUart*)	(PERIPHS_BASE + 0x0c2000))
#define LH795xxADC			((struct Lh795xxAdc*)	(PERIPHS_BASE + 0x0c3000))
#define LH795xxTIMERS		((struct Lh795xxTimers*)(PERIPHS_BASE + 0x0c4000))
#define LH795xxSSP			((struct Lh795xxSsp*)	(PERIPHS_BASE + 0x0c6000))
#define LH795xxI2S			((struct Lh795xxI2S*)	(PERIPHS_BASE + 0x0c8000))
#define LH795xxPortM		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0d9000))
#define LH795xxPortN		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0d9004))
#define LH795xxPortK		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0da000))
#define LH795xxPortL		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0da004))
#define LH795xxPortI		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0db000))
#define LH795xxPortJ		((struct Lh795xxInPort*)(PERIPHS_BASE + 0x0db004))
#define LH795xxPortG		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0dc000))
#define LH795xxPortH		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0dc004))
#define LH795xxPortE		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0dd000))
#define LH795xxPortF		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0dd004))
#define LH795xxPortC		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0de000))
#define LH795xxPortD		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0de004))
#define LH795xxPortA		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0df000))
#define LH795xxPortB		((struct Lh7595xxPort*)	(PERIPHS_BASE + 0x0df004))
#define LH795xxRTC			((struct Lh795xxRtc*)	(PERIPHS_BASE + 0x0e0000))
#define LH795xxDMA			((struct Lh975xxDma*)	(PERIPHS_BASE + 0x0e1000))
#define LH795xxRCPC			((struct Lh795xxRcpc*)	(PERIPHS_BASE + 0x0e2000))
#define LH795xxWDT			((struct Lh795xxWdt*)	(PERIPHS_BASE + 0x0e3000))
#define LH795xxALI			((struct Lh795xxAli*)	(PERIPHS_BASE + 0x0e4000))
#define LH795xxIOCON		((struct Lh795xxIocon*)	(PERIPHS_BASE + 0x0e5000))
#define LH795xxBC			((struct Lh795xxBc*)	(PERIPHS_BASE + 0x0e6000))
#define LH795xxEMC			((struct Lh795xxEmc*)	(PERIPHS_BASE + 0x0f1000))
#define LH795xxCLCDC		((struct Lh795xxClcdc*)	(PERIPHS_BASE + 0x0f4000))
#define LH795xxVIC			((struct Lh795xxVic*)	(PERIPHS_BASE + 0x0ff000))


//IRQ #s
#define IRQn_WDT					0
#define IRQn_UNUSED1				1
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
#define IRQn_EXT_INT_7				14
#define IRQn_RTC					15
#define IRQn_ADC_TSCIRQ				16
#define IRQn_ADC_BOR				17
#define IRQn_ADC_PENIRQ				18
#define IRQn_LCD					19
#define IRQn_DMA_STRM_0				20
#define IRQn_DMA_STRM_1				21
#define IRQn_DMA_STRM_2				22
#define IRQn_DMA_STRM_3				23
#define IRQn_SSPI2SINTR				24
#define IRQn_ETHERNET				25
#define IRQn_USB					26
#define IRQn_UART0					27
#define IRQn_UART1					28
#define IRQn_UART2					29
#define IRQn_USB_DMA				30
#define IRQn_I2C					31
#define IRQn_NUM_IRQS				32





#undef IO
#undef PAD
#undef IOR

#endif
