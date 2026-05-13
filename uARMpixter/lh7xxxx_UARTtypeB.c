//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "lh7xxxx_UARTtypeB.h"
#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "mem.h"

#define FIFO_SIZE		4

struct Fifo {
	uint16_t val[FIFO_SIZE];		//top bits are RXF/TXF regs
	uint8_t nItems;
};

struct Lh7xxxxUartTypeB {

	struct SocIc *ic;
	uint32_t base;
	uint8_t irq;
	
	SocUartReadF readF;
	SocUartWriteF writeF;
	void* accessFuncsData;

	struct Fifo rxFifo, txFifo;

	uint8_t bal, bah, ger, gir, lcr, mctrl, lsr, actrl0;
	uint8_t txf, tmctrl, tmst, rcm, tcm, rst, icm, gsr;
	uint8_t fmd, tmd, imd, actrl1, rie, rmd, clcf, bacf;
	uint8_t bbl, bbh, bbcf, tmie;
	
};

static bool uartPrvFifoWrite(struct Lh7xxxxUartTypeB *uart, struct Fifo *fifo, uint_fast16_t val)
{
	if (fifo->nItems >= FIFO_SIZE)
		return false;
	fifo->val[fifo->nItems++] = val;

	return true;
}

static int32_t uartPrvFifoRead(struct Lh7xxxxUartTypeB *uart, struct Fifo *fifo)
{
	uint32_t ret;

	if (!fifo->nItems)
		return -1;

	ret = fifo->val[0];
	memmove(fifo->val + 0, fifo->val + 1, sizeof(fifo->val) - sizeof(*fifo->val));
	fifo->nItems--;

	return ret;
}

static int32_t uartPrvFifoPeek(struct Lh7xxxxUartTypeB *uart, struct Fifo *fifo)
{
	if (!fifo->nItems)
		return -1;
	else
		return (uint32_t)fifo->val[0];
}

static uint_fast8_t uartPrvFifoCount(struct Lh7xxxxUartTypeB *uart, struct Fifo *fifo)
{
	return fifo->nItems;
}

static void uartPrvRecalc(struct Lh7xxxxUartTypeB *uart)
{
	//todo
}

static bool uartPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxUartTypeB *uart = (struct Lh7xxxxUartTypeB*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= uart->base;
	pa >>= 2;

	if (write)
		val = *(uint32_t*)buf;

//	if (write)
//		fprintf(stderr, "UART2 write to 0x%08lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);

	if (pa >= 0x20 / 4)
		return false;

	pa += (uart->gir & 0x60) >> 1;	//put bank select on top of reg addr (top nibble for readability)

	switch (pa) {
		
		case 0x00:		//bank 0 reg 0
			if (uart->lcr & 0x80) {
				if (write)
					uart->bal = val;
				else
					val = uart->bal;
			}
			else {

				if (write)
					uartPrvFifoWrite(uart, &uart->txFifo, (uint8_t)val + 256 * uart->txf);
				else
					val = uartPrvFifoRead(uart, &uart->rxFifo);
			}
			break;

		case 0x01:		//bank 0 reg 1
			if (uart->lcr & 0x80) {
				if (write)
					uart->bah = val;
				else
					val = uart->bah;
			}
			else {

				if (write)
					uart->ger = val & 0x3e;
				else
					val = uart->ger;
			}
			break;

		case 0x02:		//bank 0 reg 2
		case 0x12:		//bank 1 reg 2
		case 0x22:		//bank 1 reg 2
		case 0x32:		//bank 1 reg 2
			if (write)
				uart->gir = (uart->gir & 0x0e) | (val & 0x60);
			else
				val = uart->gir;
			break;

		case 0x03:		//bank 0 reg 3
			if (write)
				uart->lcr = val;
			else
				val = uart->lcr;
			break;

		case 0x04:		//bank 0 reg 4
			if (write)
				uart->mctrl = val & 0x10;
			else
				val = uart->mctrl;
			break;

		case 0x05:		//bank 0 reg 5
			if (write)
				uart->lsr = val;
			else
				val = uart->lsr;
			break;
		
		case 0x07:		//bank 0 reg 7
			if (write)
				uart->actrl0 = val;
			else
				val = uart->actrl0;
			break;

		case 0x10:		//bank 1 reg 0
			if (write)
				uartPrvFifoWrite(uart, &uart->txFifo, (uint8_t)val + 256 * uart->txf);
			else
				val = uartPrvFifoRead(uart, &uart->rxFifo) & 0xff;
			break;

		case 0x11:		//bank 1 reg 1
			if (write)
				uart->txf = val & 0xe0;
			else
				val = uartPrvFifoPeek(uart, &uart->rxFifo) >> 8;
			break;

		case 0x13:		//bank 1 reg 3
			if (write)
				uart->tmctrl = val & 0x33;
			else
				val = uart->tmst;
			break;

		case 0x14:		//bank 1 reg 4
			if (write)
				uart->mctrl = val & 0x10;
			else
				val = 16 * uartPrvFifoCount(uart, &uart->rxFifo) + uartPrvFifoCount(uart, &uart->txFifo);
			break;

		case 0x15:		//bank 1 reg 5
			if (write)
				uart->rcm = val & 0xfc;
			else
				val = uart->rst;
			break;

		case 0x16:		//bank 1 reg 6
			if (write)
				uart->tcm = val & 0x0f;
			else
				return false;
			break;

		case 0x17:		//bank 1 reg 7
			if (write)
				uart->icm = val & 0x0c;
			else
				val = uart->gsr;
			break;

		case 0x21:		//bank 2 reg 1
			if (write)
				uart->fmd = val & 0x33;
			else
				val = uart->fmd;
			break;
		
		case 0x23:		//bank 2 reg 3
			if (write)
				uart->tmd = val & 0xe7;
			else
				val = uart->tmd;
			break;
		
		case 0x24:		//bank 2 reg 4
			if (write)
				uart->imd = val & 0x0f;
			else
				val = uart->imd;
			break;
		
		case 0x25:		//bank 2 reg 5
			if (write)
				uart->actrl1 = val;
			else
				val = uart->actrl1;
			break;
		
		case 0x26:		//bank 2 reg 6
			if (write)
				uart->rie = val;
			else
				val = uart->rie;
			break;
		
		case 0x27:		//bank 2 reg 7
			if (write)
				uart->rmd = val & 0xf8;
			else
				val = uart->rmd;
			break;
		
		case 0x30:		//bank 3 reg 0
			if (uart->lcr & 0x80) {
				if (write)
					uart->bbl = val;
				else
					val = uart->bbl;
			}
			else {
				
				if (write)
					uart->clcf = val & 0x50;
				else
					val = uart->clcf;
			}
			break;
		
		case 0x31:		//bank 3 reg 1
			if (uart->lcr & 0x80) {
				if (write)
					uart->bbh = val;
				else
					val = uart->bbh;
			}
			else {
				
				if (write)
					uart->bacf = val & 0x04;
				else
					val = uart->bacf;
			}
			break;
		
		case 0x33:		//bank 3 reg 3
			if (write)
				uart->bbcf = val & 0xf4;
			else
				val = uart->bbcf;
			break;

		case 0x36:		//bank 3 reg 6
			if (write)
				uart->tmie = val & 0x03;
			else
				val = uart->tmie;
			break;

		default:
			return false;
	}
	
//	if (!write)
//		fprintf(stderr, "UART2 READ [0x%08lx] -> 0x%08lx\n", (unsigned long)paorig, (unsigned long)val);

	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

static uint_fast16_t socUartPrvDefaultRead(void* userData)							//these are special funcs since they always get their own userData - the uart pointer :)
{
	(void)userData;
	
	return UART_CHAR_NONE;	//we read nothing..as always
}

static void socUartPrvDefaultWrite(uint_fast16_t chr, void* userData)				//these are special funcs since they always get their own userData - the uart pointer :)
{
	(void)chr;
	(void)userData;
	
	//nothing to do here
}

void lh7xxxxUartTypeBsetFuncs(struct Lh7xxxxUartTypeB *uart, SocUartReadF readF, SocUartWriteF writeF, void* userData)
{
	if (!readF)
		readF = socUartPrvDefaultRead;		//these are special funcs since they get their own private data - the uart :)
	if (!writeF)
		writeF = socUartPrvDefaultWrite;
	
	uart->readF = readF;
	uart->writeF = writeF;
	uart->accessFuncsData = userData;
}

struct Lh7xxxxUartTypeB* lh7xxxxUartTypeBinit(struct ArmMem *physMem, struct SocIc *ic, uint32_t baseAddr, uint8_t irq)
{
	struct Lh7xxxxUartTypeB *uart = (struct Lh7xxxxUartTypeB*)calloc(1, sizeof(*uart));
	
	if (!uart)
		ERR("cannot alloc UART at 0x%08x", baseAddr);
	
	uart->ic = ic;
	uart->irq = irq;
	uart->base = baseAddr;
	
	lh7xxxxUartTypeBsetFuncs(uart, NULL, NULL, NULL);
	
	if (!memRegionAdd(physMem, baseAddr, 0x20, uartPrvMemAccessF, uart))
		ERR("cannot add UART at 0x%08x to MEM\n", baseAddr);
	
	return uart;
}

void lh7xxxxUartTypeBprocess(struct Lh7xxxxUartTypeB *uart)		//send and rceive up to one character
{
	uint_fast16_t readCh;

	if (uartPrvFifoCount(uart, &uart->txFifo)) {
		if (uart->writeF == socUartPrvDefaultWrite)
			uart->writeF(uartPrvFifoRead(uart, &uart->txFifo), uart);
		else
			uart->writeF(uartPrvFifoRead(uart, &uart->txFifo), uart->accessFuncsData);
	}

	readCh = (uart->readF == socUartPrvDefaultRead) ? uart->readF(uart) : uart->readF(uart->accessFuncsData);
	if (readCh != UART_CHAR_NONE) {

		uartPrvFifoWrite(uart, &uart->rxFifo, readCh);
	}
	
	uartPrvRecalc(uart);
}


