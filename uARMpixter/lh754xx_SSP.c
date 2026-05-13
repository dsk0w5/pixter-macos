#include "lh7xxxx_IC.h"
#include "soc_SSP.h"
#include "device.h"
#include <stdlib.h> 
#include <string.h> 
#include "util.h"

#define SSP_BASE				0xfffc6000
#define SSP_SIZE				0x1c

#define MAX_CLIENTS				8

#define FIFO_SIZE				8


struct SspClient {
	SspClientProcF f;
	void *d;
};

struct Fifo {
	uint16_t words[FIFO_SIZE];
	uint8_t nItems;
};

struct SocSsp {
	struct SocIc *ic;

	struct Fifo rxFifo;
	struct Fifo txFifo;

	struct SspClient client[MAX_CLIENTS];
	uint8_t numClients;

	uint8_t ctrl1, intr, clkDiv;
	uint16_t rto, ctrl0;

	uint32_t clkDivCounter;
};

//technically INTR does not have a bit for RXTO, but this is implied in the docs soi suspect they forgot it... we use bit 0x08 for that

static void sspPrvIrqRecalc(struct SocSsp *ssp)
{
	ssp->intr &=~ 0x03;

	if (ssp->txFifo.nItems < 4)
		ssp->intr |= 0x02;
	if (ssp->rxFifo.nItems >= 4)
		ssp->intr |= 0x01;

	socIcInt(ssp->ic, LH754xx_I_SSPRORINTR, !!(ssp->intr & ssp->ctrl1 & 0x04));
	socIcInt(ssp->ic, LH754xx_I_SSPTXINTR, !!(ssp->intr & ssp->ctrl1 & 0x02));
	socIcInt(ssp->ic, LH754xx_I_SSPRXINTR, !!(ssp->intr & ssp->ctrl1 & 0x01));
	socIcInt(ssp->ic, LH754xx_I_SSPRXTOINTR, !!(ssp->intr & 0x08));
	socIcInt(ssp->ic, LH754xx_I_SSPINTR, !!(ssp->intr & (ssp->ctrl1 | 0x08)));
}

static uint_fast16_t sspPrvDoOneXfer(struct SocSsp *ssp, uint_fast8_t nBits, uint_fast16_t in)	//u16 -> u16
{
	uint_fast16_t ret = 0;
	uint_fast8_t i;

	if (ssp->ctrl1 & 0x08)	//loopbak
		ret = in;
	else {

		for (i = 0; i < ssp->numClients; i++) {

			ret |= ssp->client[i].f(ssp->client[i].d, nBits, in);
		}
	}
	
	return ret;
}

static uint_fast16_t sspPrvFifoR(struct SocSsp *ssp, struct Fifo *fifo)
{
	uint_fast16_t ret;

	if (!fifo->nItems) {
		fprintf(stderr, "SSP FIFO UNDERRUN\n");
		return 0;
	}
	ret = fifo->words[0];
	memmove(fifo->words + 0, fifo->words + 1, sizeof(fifo->words) - sizeof(*fifo->words));
	fifo->nItems--;
	return ret;
}

static void sspPrvFifoW(struct SocSsp *ssp, struct Fifo *fifo, uint_fast16_t val)
{
	if (fifo->nItems == MAX_CLIENTS)
		fprintf(stderr, "SSP FIFO OVERRUN\n");
	else
		fifo->words[fifo->nItems++] = val;
}

void socSspPeriodic(struct SocSsp *ssp)
{
	if (!(ssp->ctrl1 & 0x10))	//is it even on?
		return;

	if (!ssp->clkDivCounter) {

		ssp->clkDivCounter = (1 + ((ssp->ctrl0 >> 8) & 0xff)) * ssp->clkDiv;
		if (!ssp->clkDivCounter)
			ssp->clkDivCounter++;
	}
	if (!--ssp->clkDivCounter) {	//do work
		
		if (ssp->txFifo.nItems) {

			uint_fast16_t val = sspPrvDoOneXfer(ssp, sspPrvFifoR(ssp, &ssp->txFifo), 1 + (ssp->ctrl0 & 0x0f));

			if (ssp->rxFifo.nItems == FIFO_SIZE)
				ssp->intr |= 0x04;
			else
				sspPrvFifoW(ssp, &ssp->rxFifo, val);

			sspPrvIrqRecalc(ssp);
		}
		else if (ssp->rto) {		//RTO happens fast for us :)

			ssp->intr |= 0x08;
			sspPrvIrqRecalc(ssp);
		}
	}
}

static bool sspPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct SocSsp *ssp = (struct SocSsp*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= SSP_BASE;
	pa >>= 2;

	if (write)
		val = *(uint32_t*)buf;
	
	switch (pa) {
		
		case 0x00 / 4:			//CTRL0
			if (write)
				ssp->ctrl0 = val;
			else
				val = ssp->ctrl0;
			break;

		case 0x04 / 4:			//CTRL1
			if (write) {

				ssp->ctrl1 = val & 0x1f;
				
				//rxFifoOverrun clears if RORIE is cleared
				if (!(val & 0x04))
					ssp->intr &=~ 0x04;

				sspPrvIrqRecalc(ssp);
			}
			else
				val = ssp->ctrl1;
			break;

		case 0x08 / 4:			//fifo
			if (write)
				sspPrvFifoW(ssp, &ssp->txFifo, val);
			else
				val = sspPrvFifoR(ssp, &ssp->rxFifo);
			break;

		case 0x0c / 4:			//SR
			if (write)
				return false;
			else {

				val =
					(ssp->txFifo.nItems ? 0x10 : 0x00) +				//we are busy as long as we have something to transmit
					(ssp->rxFifo.nItems == FIFO_SIZE ? 0x08 : 0x00) +
					(ssp->rxFifo.nItems != 0 ? 0x04 : 0x00) +
					(ssp->txFifo.nItems != FIFO_SIZE ? 0x02 : 0x00) +
					(ssp->txFifo.nItems == 0 ? 0x01 : 0x00);
			}
			break;

		case 0x10 / 4:			//CPSR
			if (write)
				ssp->clkDiv = val & 0xfe;
			else
				val = ssp->clkDiv;
			break;

		case 0x14 / 4:			//IIR
			if (write) {

				ssp->intr &=~ 0x04;
				sspPrvIrqRecalc(ssp);
			}
			else
				val = ssp->intr;
			break;

		case 0x18 / 4:			//RTO
			if (write) {

				ssp->rto = val;
				ssp->intr &=~ 0x08;	//writing this reg clearx rx timeout status
			}
			else
				val = ssp->rto;
			break;

		default:
			return false;
	}

	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}


struct SocSsp* socSspInit(struct ArmMem *mem, struct SocIc* ic, struct SocDma *dma, uint32_t base, uint_fast8_t irqNo, int_fast8_t dmaReqNoBase)
{
	struct SocSsp *ssp = (struct SocSsp*)calloc(1, sizeof(*ssp));
	
	if (!ssp)
		ERR("cannot alloc SSP");
	
	ssp->ic = ic;

	if (!memRegionAdd(mem, SSP_BASE, SSP_SIZE, sspPrvMemAccessF, ssp))
		ERR("cannot add SSP to MEM\n");
	
	return ssp;
}

bool socSspAddClient(struct SocSsp *ssp, SspClientProcF procF, void* userData)
{
	if (ssp->numClients >= MAX_CLIENTS)
		return false;

	ssp->client[ssp->numClients].f = procF;
	ssp->client[ssp->numClients].d = userData;
	ssp->numClients++;

	return true;
}

