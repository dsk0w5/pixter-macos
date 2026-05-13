#include "lh7xxxx_DMA.h"
#include "lh7xxxx_IC.h"
#include <stdlib.h>
#include <string.h>
#include "device.h"
#include "util.h"
#include "mem.h"
#include "CPU.h"

#define DMA_BASE				0xfffe1000
#define DMA_SIZE				0xfc
#define NUM_CHANNELS			4

struct Channel {
	uint32_t srcStart, srcCur, dstStart, dstCur;
	uint16_t countStart, countCurr, ctrl;

	bool req;
};

struct SocDma {

	struct ArmMem *mem;
	struct SocIc *ic;
	struct Channel ch[NUM_CHANNELS];

	uint8_t intrEna, intrSta;
};

static void dmaPrvIrqRecalc(struct SocDma *dma)
{
	uint_fast16_t masked = dma->intrEna & dma->intrSta;

	if (deviceGetSocRev()) {

		uint_fast8_t i, mask = 0x11;

		for (i = 0; i < NUM_CHANNELS; i++, mask <<= 1)
			socIcInt(dma->ic, LH795xx_I_DMA_STRM_0 + i, !!(masked & mask));
	}
	else {

		socIcInt(dma->ic, LH754xx_I_DMA, !!masked);
	}
}

static void socDmaPrvDoBurst(struct SocDma *dma, struct Channel *ch, uint_fast8_t dmaChNo)
{
	static const uint8_t burstSzTab[] = {1, 4, 8, 16}, accessSzTab[] = {1, 2, 4, 0};

	uint_fast8_t i, burstSz = burstSzTab[(ch->ctrl >> 5) & 3], nowXfers = ch->countCurr > burstSz ? burstSz : ch->countCurr;
	uint_fast8_t readSz = accessSzTab[(ch->ctrl >> 3) & 3], writeSz = accessSzTab[(ch->ctrl >> 7) & 3];
	uint8_t xferBuf[64];



	//fprintf(stderr, "DMA burst ch %u. XFER %ux %u B 0x%08x -> 0x%08x, %u items left\n", dmaChNo, burstSz, readSz, ch->srcCur, ch->dstCur, ch->countCurr);

	//read
	for (i = 0; i < nowXfers; i++) {
		
		if (!memAccess(dma->mem, ch->srcCur, readSz, false, xferBuf + i * readSz)) {

			dma->intrSta |= 0x10 << dmaChNo;
			ch->ctrl &=~ 0x0001;
			ERR("DMA ch %u bus error on read %u at 0x%08x\n", dmaChNo, readSz, ch->srcCur);
		}

		if (ch->ctrl & 0x0002)
			ch->srcCur += readSz;
	}
	
	//write
	for (i = 0; i < nowXfers * readSz; i += writeSz) {

		if (!memAccess(dma->mem, ch->dstCur, writeSz, true, xferBuf + i)) {

			dma->intrSta |= 0x10 << dmaChNo;
			ch->ctrl &=~ 0x0001;
			ERR("DMA ch %u bus error on write %u at 0x%08x\n", dmaChNo, writeSz, ch->dstCur);
		}
		
		if (ch->ctrl & 0x0004)
			ch->dstCur += writeSz;
	}
	
	ch->countCurr -= nowXfers;
	if (!ch->countCurr) {

		ch->ctrl &=~ 0x0001;
		dma->intrSta |= 0x01 << dmaChNo;
	}
	
	dmaPrvIrqRecalc(dma);
}

void socDmaPeriodic(struct SocDma* dma)
{
	struct Channel *ch = dma->ch;
	uint_fast8_t i;

	for (i = 0; i < NUM_CHANNELS; i++, ch++) {

		if (!ch->req)
			continue;

		if (!(ch->ctrl & 0x0001))
			continue;
		
		socDmaPrvDoBurst(dma, ch, i);
	}
}

void socDmaExternalReq(struct SocDma* dma, uint_fast8_t chNum, bool requested)
{
	if (chNum >= NUM_CHANNELS)
		return;
	dma->ch[chNum].req = requested;
}

static bool dmaPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	uint32_t i, val = 0, paorig = pa, unitNo, unitOfst;
	struct SocDma *dma = (struct SocDma*)userData;
	

	if (write)
		switch (size){
			case 1:
		//		fprintf(stderr, "DMA 0x%02x -> [0x%08x]\n", *(uint8_t*)buf, pa);
				break;
			case 2:
		//		fprintf(stderr, "DMA 0x%04x -> [0x%08x]\n", *(uint16_t*)buf, pa);
				break;
			case 4:
		//		fprintf(stderr, "DMA 0x%08x -> [0x%08x]\n", *(uint32_t*)buf, pa);
				break;
		}

	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= DMA_BASE;
	pa >>= 2;

	if (write)
		val = *(uint32_t*)buf;
	
	switch (pa) {
		
		case 0xF0 / 4:	//mask
			if (write) {

				dma->intrEna = val;
				dmaPrvIrqRecalc(dma);
			}
			else
				val = dma->intrEna;
			break;

		case 0xF4 / 4:	//intrClear
			if (write)
				dma->intrSta &=~ val;
			else
				val = 0;
			break;

		case 0xF8 / 4:		//status
			if (write)
				return false;
			else {

				val = dma->intrSta;
				for (i = 0; i < NUM_CHANNELS; i++)
					val |= (dma->ch[i].ctrl & 0x0001) << (8 + i);
			}
			break;

		default:
			unitNo = pa / 0x10;
			unitOfst = pa % 0x10;

			if (unitNo < NUM_CHANNELS) {
				struct Channel *ch = &dma->ch[unitNo];

		//		fprintf(stderr, "DMA 0x%08x -> ch %u ofst 0x%02x\n", val, unitNo, 4 * unitOfst);

				switch (unitOfst) {
					
					case 0:		//sourceLow
						if (write)
							ch->srcStart = (ch->srcStart & 0xffff0000) | (uint16_t)val;
						else
							val = (uint16_t)ch->srcStart;
						break;

					case 1:		//source hi
						if (write)
							ch->srcStart = (ch->srcStart & 0x0000ffff) | (val << 16);
						else
							val = ch->srcStart >> 16;
						break;

					case 2:		//dest lo
						if (write)
							ch->dstStart = (ch->dstStart & 0xffff0000) | (uint16_t)val;
						else
							val = (uint16_t)ch->dstStart;
						break;

					case 3:		//dest hi
						if (write)
							ch->dstStart = (ch->dstStart & 0x0000ffff) | (val << 16);
						else
							val = ch->dstStart >> 16;
						break;
					
					case 4:		//count
						if (write)
							ch->countStart = val;
						else
							val = ch->countStart;
						break;

					case 5:		//ctrl
						if (write) {
							uint_fast16_t prevVal = ch->ctrl, newVal = val & 0x2bff, changedVals = prevVal ^ newVal, setVals = changedVals & newVal, clrVals = changedVals & prevVal;

							if (!(val & 0x0001)) {
								
								dma->intrSta &=~ (0x01 << unitNo);	//INT cleared by disable even if disabled already
							}

							ch->ctrl = newVal;
							if (clrVals & 0x0001) {	//channel disabled
								
								ch->countCurr = 0;
							}
							if (setVals) {			//channel enabled
								
								dma->intrSta &=~ (0x10 << unitNo);	//ERR cleared by enable

								if (setVals & 0x0001) {
									//When the DMA Controller is enabled, the content of the Maximum Count
									//Register loads into the Terminal Count Register.
									ch->countCurr = ch->countStart;

									if (!(ch->ctrl & 0x0200)) {

										ch->srcCur = ch->srcStart;
										ch->dstCur = ch->dstStart;
									}

			//						fprintf(stderr, "enabled 0x%08x -> 0x%08x, %u count\n", ch->srcCur, ch->dstCur, ch->countCurr);
								}

								if (!ch->countCurr || !(ch->ctrl & 0x60) || !(ch->ctrl & 0x18))	//no xfer if no data or no size
								{

									ch->ctrl &=~ 0x0001;
									ch->countCurr = 0;
								}
							}
			///				fprintf(stderr, "CTRL is now 0x%04x\n", ch->ctrl);
							dmaPrvIrqRecalc(dma);
						}
						else
							val = ch->ctrl;
						break;

					case 6:		//cur src hi
						if (write)
							ch->srcCur = (ch->srcCur & 0x0000ffff) | (val << 16);
						else
							val = ch->srcCur >> 16;
						break;

					case 7:		//cur src lo
						if (write)
							ch->srcCur = (ch->srcCur & 0xffff0000) | (uint16_t)val;
						else
							val = (uint16_t)ch->srcCur;
						break;

					case 8:		//cur dst hi
						if (write)
							ch->dstCur = (ch->dstCur & 0x0000ffff) | (val << 16);
						else
							val = ch->dstCur >> 16;
						break;

					case 9:		//cur dst lo
						if (write)
							ch->dstCur = (ch->dstCur & 0xffff0000) | (uint16_t)val;
						else
							val = (uint16_t)ch->dstCur;
						break;

					case 10:	//cur count
						if (write)
							return false;
						else
							val = (uint16_t)ch->countCurr;
						break;

					default:
						return false;
				}
				break;
			}
			return false;
	}

	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

struct SocDma* socDmaInit(struct ArmMem *mem, struct SocIc *ic)
{
	struct SocDma *dma = (struct SocDma*)calloc(1, sizeof(*dma));
	
	if (!dma)
		ERR("cannot alloc DMA");
	
	dma->mem = mem;
	dma->ic = ic;

	if (!memRegionAdd(mem, DMA_BASE, DMA_SIZE, dmaPrvMemAccessF, dma))
		ERR("cannot add DMA to MEM\n");
	
	return dma;
}


