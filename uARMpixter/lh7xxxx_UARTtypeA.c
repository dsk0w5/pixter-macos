//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "lh7xxxx_UARTtypeA.h"
#include <string.h>
#include <stdlib.h>
#include "device.h"
#include "util.h"
#include "mem.h"


struct Fifo {
	uint8_t nItems, fifoSize;
	uint16_t val[32];		//only fifoSize units used
};


struct Lh7xxxxUartTypeA {

	struct SocIc *ic;
	struct SocDma *dma;
	uint32_t base;
	uint8_t irq;
	int8_t rxIntrIrq, txIntrIrq, dmaReqTx, dmaReqRx;
	
	SocUartReadF readF;
	SocUartWriteF writeF;
	void* accessFuncsData;
	
	struct Fifo rxFifo, txFifo;

	uint16_t baudInt, lcrh, cr, imsc, ris;
	uint8_t irDivisor, realFifoSize, stickyErrors, baudFrac, ifls, dmactl;
};

static bool uartPrvFifoWrite(struct Lh7xxxxUartTypeA *uart, struct Fifo *fifo, uint_fast16_t val)
{
	if (fifo->nItems >= fifo->fifoSize)
		return false;
	fifo->val[fifo->nItems++] = val;

	return true;
}

static int32_t uartPrvFifoRead(struct Lh7xxxxUartTypeA *uart, struct Fifo *fifo)
{
	uint32_t ret;

	if (!fifo->nItems)
		return -1;

	ret = fifo->val[0];
	memmove(fifo->val + 0, fifo->val + 1, sizeof(fifo->val) - sizeof(*fifo->val));
	fifo->nItems--;

	return ret;
}

static int32_t uartPrvFifoPeek(struct Lh7xxxxUartTypeA *uart, struct Fifo *fifo)
{
	if (!fifo->nItems)
		return -1;
	else
		return (uint32_t)fifo->val[0];
}

static uint_fast8_t uartPrvFifoCount(struct Lh7xxxxUartTypeA *uart, struct Fifo *fifo)
{
	return fifo->nItems;
}

static bool uartPrvFifoIsFull(struct Lh7xxxxUartTypeA *uart, struct Fifo *fifo)
{
	return fifo->nItems >= fifo->fifoSize;
}

static void uartPrvRecalc(struct Lh7xxxxUartTypeA *uart)
{
	static const uint8_t eigthsScale[] = {1, 2, 4, 6, 7, 8, 8, 8};	
	uint_fast8_t numRxIrq = eigthsScale[(uart->ifls >> 3) & 7];
	uint_fast8_t numTxIrq = eigthsScale[uart->ifls & 7];

	if (uart->lcrh & 0x10) {	//scale by fifo size
		
		numRxIrq = numRxIrq * uart->realFifoSize  /8;
		numTxIrq = numTxIrq * uart->realFifoSize  /8;
	}
	else {						//no fifo -> different thresholds

		numRxIrq = 1;	//fifo on 1 or more
		numTxIrq = 1;	//fifo on 1 free space or more
	}

	if (uartPrvFifoCount(uart, &uart->rxFifo) >= numRxIrq)
		uart->ris |= 0x10;
	if (uart->txFifo.fifoSize - uartPrvFifoCount(uart, &uart->txFifo) >= numTxIrq)
		uart->ris |= 0x20;

	if (uart->dmaReqRx >=0)
		socDmaExternalReq(uart->dma, uart->dmaReqRx, (uart->dmactl & 0x04) && (uart->dmactl & 0x01) && (uart->ris & 0x10));

	if (uart->dmaReqTx >=0)
		socDmaExternalReq(uart->dma, uart->dmaReqRx, (uart->dmactl & 0x04) && (uart->dmactl & 0x02) && (uart->ris & 0x20));

	if (uart->rxIntrIrq >= 0)
		socIcInt(uart->ic, uart->rxIntrIrq, !!(uart->ris & uart->imsc & 0x50));

	if (uart->txIntrIrq >= 0)
		socIcInt(uart->ic, uart->txIntrIrq, !!(uart->ris & uart->imsc & 0x20));

	socIcInt(uart->ic, uart->irq, !!(uart->ris & uart->imsc));
}

static bool uartPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxUartTypeA *uart = (struct Lh7xxxxUartTypeA*)userData;
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
//		fprintf(stderr, "UARTtypeA write to 0x%08lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);
	

	switch (pa) {
		
		case 0x00 / 4:	//fifo access
			if (write) {

				val &= 0xff;
				if (uart->lcrh & 0x200) {	//9-bit mode ?
					val |= uart->lcrh & 0x100;
					uart->lcrh &=~ 0x100;
				}
				if (!uartPrvFifoWrite(uart, &uart->txFifo, val)) {
					fprintf(stderr, "TX fifo overflow\n");
					return false;
				}
				uartPrvRecalc(uart);
			}
			else {

				int32_t t = uartPrvFifoRead(uart, &uart->rxFifo);
				if (t < 0) {
					fprintf(stderr, "RX fifo underflow\n");
					return false;
				}
				val = t;
			}
			break;

		case 0x04 / 4:	//UARTRSR/UARTECR
			if (write) {
				uart->stickyErrors = 0;
				uartPrvRecalc(uart);
			}
			else {
				val = uart->stickyErrors;
			}
			break;

		case 0x18 / 4:	//UARTFR
			if (write)
				return false;
			else {

				val = 0;

				if (!uartPrvFifoCount(uart, &uart->txFifo))
					val |= 0x80;
				if (uartPrvFifoIsFull(uart, &uart->rxFifo))
					val |= 0x40;
				if (uartPrvFifoIsFull(uart, &uart->txFifo))
					val |= 0x20;
				if (!uartPrvFifoCount(uart, &uart->rxFifo))
					val |= 0x10;
				if (uartPrvFifoCount(uart, &uart->txFifo))
					val |= 0x08;
			}
			break;

		case 0x20 / 4:
			if (!deviceGetSocRev())
				return false;
			if (write)
				uart->irDivisor = val;
			else
				val = uart->irDivisor;
			break;

		case 0x24 / 4:	//UARTIBRD
			if (write)
				uart->baudInt = val;
			else
				val = uart->baudInt;
			break;
		
		case 0x28 / 4:	//UARTFBRD
			if (write)
				uart->baudFrac = val & 0x3f;
			else
				val = uart->baudFrac;
			break;

		case 0x2c / 4:	//UARTLCR_H
			if (write) {

				if ((uart->lcrh ^ val) & 0x10) {	//fifo enablement changes - flush all
					
					uart->txFifo.nItems = 0;
					uart->rxFifo.nItems = 0;
				}

				uart->lcrh = val & (deviceGetSocRev() ? 0x3ff : 0x0ff);
				
				if (val & 0x10) {

					uart->txFifo.fifoSize = uart->realFifoSize;
					uart->rxFifo.fifoSize = uart->realFifoSize;
				}
				else {
					
					uart->txFifo.fifoSize = 1;
					uart->rxFifo.fifoSize = 1;
				}
				uartPrvRecalc(uart);
			}
			else
				val = uart->lcrh;
			break;

		case 0x30 / 4:	//UARTCR
			if (write) {
				uart->cr = val & (deviceGetSocRev() ? 0xcb87 : 0x0381);
				uartPrvRecalc(uart);
			}
			else
				val = uart->cr;
			break;

		case 0x34 / 4:	//UARTIFLS
			if (write) {
				uart->ifls = val & 0x3f;
				uartPrvRecalc(uart);
			}
			else
				val = uart->ifls;
			break;

		case 0x38 / 4:	//UARIMSC
			if (write) {
				uart->imsc = val & (deviceGetSocRev() ? 0x07f2 : 0x07f0);
				uartPrvRecalc(uart);
			}
			else
				val = uart->imsc;
			break;

		case 0x3c / 4:	//UARTRIS
			if (write)
				return false;
			else
				val = uart->ris;
			break;

		case 0x40 / 4:	//UARTMIS
			if (write)
				return false;
			else
				val = uart->ris & uart->imsc;
			break;

		case 0x44 / 4:	//UARTICR
			if (write) {
				uart->ris &=~ val;
				uartPrvRecalc(uart);
			}
			else
				return false;
			break;
		
		case 0x48 / 4:	//DMACTL
			if (write) {
				uart->dmactl = val & 7;
				uartPrvRecalc(uart);
			}
			else
				val = uart->dmactl;
			break;

		default:
			return false;
	}
	
//	if (!write)
//		fprintf(stderr, "UARTtypeA READ [0x%08lx] -> 0x%08lx\n", (unsigned long)paorig, (unsigned long)val);

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

void lh7xxxxUartTypeAsetFuncs(struct Lh7xxxxUartTypeA *uart, SocUartReadF readF, SocUartWriteF writeF, void* userData)
{
	if (!readF)
		readF = socUartPrvDefaultRead;		//these are special funcs since they get their own private data - the uart :)
	if (!writeF)
		writeF = socUartPrvDefaultWrite;
	
	uart->readF = readF;
	uart->writeF = writeF;
	uart->accessFuncsData = userData;
}

struct Lh7xxxxUartTypeA* lh7xxxxUartTypeAinit(struct ArmMem *physMem, struct SocIc *ic, struct SocDma *dma, uint32_t baseAddr, uint8_t irq, int8_t rxIntrIrq, int8_t txIntrIrq, int8_t dmaReqTx, int8_t dmaReqRx)
{
	struct Lh7xxxxUartTypeA *uart = (struct Lh7xxxxUartTypeA*)calloc(1, sizeof(*uart));
	
	if (!uart)
		ERR("cannot alloc UART at 0x%08x", baseAddr);
	
	uart->ic = ic;
	uart->dma = dma;
	uart->irq = irq;
	uart->base = baseAddr;
	uart->rxIntrIrq = rxIntrIrq;
	uart->txIntrIrq = txIntrIrq;
	uart->dmaReqTx = dmaReqTx;
	uart->dmaReqRx = dmaReqRx;

	uart->realFifoSize = deviceGetSocRev() ? 32 : 16;
	uart->txFifo.fifoSize = 1;	//fifos init to being off
	uart->rxFifo.fifoSize = 1;

	uart->ifls = 0x12;
	uart->cr = 0x300;
	
	lh7xxxxUartTypeAsetFuncs(uart, NULL, NULL, NULL);
	
	if (!memRegionAdd(physMem, baseAddr, 0x4c, uartPrvMemAccessF, uart))
		ERR("cannot add UART at 0x%08x to MEM\n", baseAddr);
	
	return uart;
}

void lh7xxxxUartTypeAprocess(struct Lh7xxxxUartTypeA *uart)		//send and rceive up to one character
{
	int_fast16_t readCh = -1;
	bool anythingDone = false;

	if (uart->cr & 0x0001) {

		if ((uart->cr & 0x100) && uartPrvFifoCount(uart, &uart->txFifo)) {
			uint_fast16_t txChar = uartPrvFifoRead(uart, &uart->txFifo);

			if (uart->writeF == socUartPrvDefaultWrite)
				uart->writeF(txChar, uart);
			else
				uart->writeF(txChar, uart->accessFuncsData);

			if ((uart->cr & 0x200) && (uart->cr & 0x80)) {	//RX is on and in loopback mode?
				
				readCh = txChar;
			}
			anythingDone = true;
		}

		if (uart->cr & 0x200) {

			if (!(uart->cr & 0x80)) {		//only read outside world if not in loopback mode

				readCh = (uart->readF == socUartPrvDefaultRead) ? uart->readF(uart) : uart->readF(uart->accessFuncsData);
				if (readCh == UART_CHAR_NONE)
					readCh =- 1;
			}

			if (readCh >= 0) {

				if (!uartPrvFifoWrite(uart, &uart->rxFifo, readCh))
					uart->ris |= 0x0400;
				anythingDone = true;
			}
		}

		//RX timeout
		if (uartPrvFifoCount(uart, &uart->rxFifo) && !(uart->ris & 0x10)) {

			uart->ris |= 0x40;
			anythingDone = true;
		}
	}

	if (anythingDone)
		uartPrvRecalc(uart);
}

