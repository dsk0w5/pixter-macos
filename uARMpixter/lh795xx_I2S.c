#include "lh7xxxx_I2S.h"
#include "lh7xxxx_IC.h"
#include "util.h"

#define I2S_BASE		0xFFFC8000
#define I2S_SIZE		0x00000018


struct SocI2s {
	struct SocIc *ic;
	struct SocDma *dma;
	struct SocSsp *ssp;

	uint16_t state;
	uint8_t ctrl, intrEn, intrSta;

	I2sClientProcF clientProcF;
	void* clientProcD;
};


static void i2sPrvRecalc(struct SocI2s *i2s)
{
	if (i2s->ctrl & 0x01)
		socIcInt(i2s->ic, LH795xx_I_SSPI2SINTR, !!(i2s->intrEn & i2s->intrSta));
}

void socI2sPeriodic(struct SocI2s *i2s)
{
	//nothing
}

static bool i2sPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct SocI2s *i2s = (struct SocI2s*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= I2S_BASE;
	pa >>= 2;

	if (write)
		val = *(uint32_t*)buf;
	
	switch (pa) {
		
		case 0x00 / 4:
			if (write)
				i2s->ctrl = val & 0x7f;
			else
				val = i2s->ctrl;
			break;

		case 0x04 / 4:
			if (write)
				return false;
			else
				val = i2s->state;
			break;
		
		case 0x08 / 4:
			if (write) {

				i2s->intrEn = true;
				i2sPrvRecalc(i2s);
			}
			else
				val = i2s->intrEn;
			break;

		case 0x0c / 4:
			if (write)
				return false;
			else
				val = i2s->intrSta;
			break;
		
		case 0x10 / 4:
			if (write)
				return false;
			else
				val = i2s->intrSta & i2s->intrEn;
			break;
		
		case 0x14 / 4:	//ICR
			if (write) {

				i2s->intrSta &=~ (val & 0x70);
				i2sPrvRecalc(i2s);
			}
			else
				val = 0;
			break;

		default:
			return false;
	}

	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

bool socI2sSetClient(struct SocI2s *i2s, I2sClientProcF procF, void* userData)
{
	if (i2s->clientProcF && procF)	//allow clearing it...
		return false;

	i2s->clientProcF = NULL;
	i2s->clientProcD = userData;
	i2s->clientProcF = procF;

	return true;
}

struct SocI2s* socI2sInit(struct ArmMem *mem, struct SocIc *ic, struct SocDma *dma)
{
	struct SocI2s *i2s = (struct SocI2s*)calloc(1, sizeof(*i2s));
	
	if (!i2s)
		ERR("cannot alloc I2S");
	
	i2s->ic = ic;
	i2s->dma = dma;
	i2s->state = 0x52;
	i2s->intrSta = 0x08;
	if (!memRegionAdd(mem, I2S_BASE, I2S_SIZE, i2sPrvMemAccessF, i2s))
		ERR("cannot add I2S to MEM\n");
	
	return i2s;
}

static uint_fast16_t lh795xxI2sSspClientProc(void* userData, uint_fast8_t nBits, uint_fast16_t sent)
{
	struct SocI2s *i2s = (struct SocI2s*)userData;

	if (i2s->ctrl % 4 == 3) {
	
		if (i2s->clientProcF)
			i2s->clientProcF(i2s->clientProcD, nBits, sent);
	}

	return 0;
}

void lh795xxI2sSetSSP(struct SocI2s *i2s, struct SocSsp *ssp)
{
	if (i2s->ssp && i2s->ssp != ssp)
		abort();

	i2s->ssp = ssp;

	if (!socSspAddClient(i2s->ssp, lh795xxI2sSspClientProc, i2s))
		abort();
}
