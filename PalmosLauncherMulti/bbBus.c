#define PERIPHS_BASE	0xfff00000
#include "LH795xx.h"
#include "LH75411.h"
#include <stdbool.h>



//we use bbBus0
static void bbBusSetDirMulti(bool out)
{
	if (out) {

		LH795xxPortC->ddr |= 0xf0;
		LH795xxPortB->ddr |= 0xc3;
		LH795xxPortA->ddr |= 0x08;
	}
	else {
		
		LH795xxPortC->ddr &= (uint8_t)~0xf0;
		LH795xxPortB->ddr &= (uint8_t)~0xc3;
		LH795xxPortA->ddr &= (uint8_t)~0x08;
	}
}

static void bbBusWritePinsMulti(uint8_t val)
{
	LH795xxPortC->dr = (val >> 1) & 0x70;
	LH795xxPortA->dr = (LH795xxPortA->dr & (uint8_t)~8) | ((val << 3) & 0x08);
	LH795xxPortB->dr = (LH795xxPortB->dr & (uint8_t)~0xc3) | ((val >> 3) & 0x03) | ((val << 4) & 0x40) | ((val << 6) & 0x80);
}

static uint8_t bbBusReadPinsMulti(void)
{
	uint32_t ret = 0, t;

	ret += (LH795xxPortC->dr << 1) & 0xe0;
	ret += (LH795xxPortA->dr >> 3) & 0x01;
	t = LH795xxPortB->dr;
	ret += (t & 0x03) << 3;
	ret += (t >> 6) & 0x02;
	ret += (t >> 4) & 0x04;

	return ret;
}

static void bbBusDelay(void)
{
	uint32_t t = 2;

	asm volatile(
		".syntax unified	\n\t"
		"1:					\n\t"
		"	subs %0, #1		\n\t"
		"	bne  1b			\n\t"
		:"+l"(t)
		:
		:"memory", "cc"
	);
}

void bbBusInitMulti(void)
{
	LH795xxPortA->ddr |= 0x30;
	LH795xxPortA->dr |= 0x30;

	bbBusSetDirMulti(true);
}

void bbBusWriteCycleMulti(uint16_t addr, uint8_t data)
{
	bbBusWritePinsMulti(addr >> 8);
	bbBusDelay();
	LH795xxPortA->dr &= (uint8_t)~0x10;
	bbBusDelay();
	bbBusWritePinsMulti(addr);
	bbBusDelay();
	LH795xxPortA->dr &= (uint8_t)~0x20;
	bbBusDelay();
	bbBusWritePinsMulti(data);
	bbBusDelay();
	LH795xxPortA->dr |= 0x30;
	bbBusDelay();
	bbBusWritePinsMulti(0);
}

uint8_t bbBusReadCycleMulti(uint16_t addr)	//must be donw with irqs off
{
	uint8_t ret;

	bbBusWritePinsMulti(addr >> 8);
	bbBusDelay();
	LH795xxPortA->dr &= (uint8_t)~0x10;
	bbBusDelay();
	bbBusWritePinsMulti(addr);
	bbBusDelay();
	LH795xxPortA->dr = (LH795xxPortA->dr & (uint8_t)~0x20) | 0x10;
	bbBusSetDirMulti(false);
	bbBusDelay();
	ret = bbBusReadPinsMulti();
	LH795xxPortA->dr |= 0x30;
	bbBusDelay();
	bbBusSetDirMulti(true);
	bbBusWritePinsMulti(0);

	return ret;
}






static void bbBusSetDirColor(bool out)
{
	if (out) {

		LH75411PORTC->ddr |= 0xe0;
		LH75411PORTD->ddr |= 0x7c;
	}
	else {
		
		LH75411PORTC->ddr &= (uint8_t)~0xe0;
		LH75411PORTD->ddr &= (uint8_t)~0x7c;
	}
}

static void bbBusWritePinsColor(uint8_t val)
{
	LH75411PORTC->dr = val & 0xe0;
	LH75411PORTD->dr = (LH75411PORTD->dr &3 ) | ((val << 2) & 0x7C);
}

static uint8_t bbBusReadPinsColor(void)
{
	uint32_t ret = 0;

	ret += LH75411PORTC->dr & 0xe0;
	ret += (LH75411PORTD->dr & 0x7c) >> 2;

	return ret;
}

void bbBusInitColor(void)
{
	LH75411PORTE->ddr |= 0x03;
	LH75411PORTE->dr |= 0x03;

	bbBusSetDirColor(true);
}

void bbBusWriteCycleColor(uint16_t addr, uint8_t data)
{
	bbBusWritePinsColor(addr >> 8);
	bbBusDelay();
	LH75411PORTE->dr &= (uint8_t)~0x11;
	bbBusDelay();
	bbBusWritePinsColor(addr);
	bbBusDelay();
	LH75411PORTE->dr &= (uint8_t)~0x02;
	bbBusDelay();
	bbBusWritePinsColor(data);
	bbBusDelay();
	LH75411PORTE->dr |= 0x03;
	bbBusDelay();
	bbBusWritePinsColor(0);
}

uint8_t bbBusReadCycleColor(uint16_t addr)	//must be donw with irqs off
{
	uint8_t ret;

	bbBusWritePinsColor(addr >> 8);
	bbBusDelay();
	LH75411PORTE->dr &= (uint8_t)~0x01;
	bbBusDelay();
	bbBusWritePinsColor(addr);
	bbBusDelay();
	LH75411PORTE->dr = (LH75411PORTE->dr & (uint8_t)~0x02) | 0x01;
	bbBusSetDirColor(false);
	bbBusDelay();
	ret = bbBusReadPinsColor();
	LH75411PORTE->dr |= 0x03;
	bbBusDelay();
	bbBusSetDirColor(true);
	bbBusWritePinsColor(0);

	return ret;
}
