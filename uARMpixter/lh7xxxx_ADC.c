#include "lh7xxxx_ADC.h"
#include "lh7xxxx_IC.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "device.h"
#include <math.h>
#include "util.h"

#define ADC_BASE			0xFFFC3000
#define ADC_SIZE			0x000000C0

#define CFG_SETTIME_MASK	0xff800000
#define CFG_SETTIME_SHIFT	23
#define CFG_INP_MASK		0x00780000
#define CFG_INP_SHIFT		19
#define CFG_INM_MASK		0x00040000
#define CFG_INM_SHIFT		18
#define CFG_REFP_MASK		0x00030000
#define CFG_REFP_SHIFT		16
#define CFG_BIASCON_MASK	0x00003ffc
#define CFG_BIASCON_SHIFT	2
#define CFG_REFM_MASK		0x00000003
#define CFG_REFM_SHIFT		0


#define NUM_FIFO_ENTRIES	16
#define NUM_CONTROL_BANKS	16

#define VERBOSE				0


struct Lh7xxxxAdc {
	struct SocIc *ic;

	uint32_t curCfg, ctrlBank[NUM_CONTROL_BANKS], idleCfg;

	uint16_t pc, fifo[NUM_FIFO_ENTRIES];
	uint8_t fifoR, fifoW, fifoCnt;

	uint8_t gc, intSta, intEn;

	uint16_t auxAdcMv[6];

	//internal state
	uint8_t cbState;
	int8_t curConversionIdx;		//-1 if we're in idle config
	int32_t x, y;
	bool lastFifoInsertFailed;	//was there an overrun
};

static bool adcPrvIsPenDown(struct Lh7xxxxAdc *adc)
{
	return adc->x >= 0 && adc->y >= 0;
}

static bool adcPrvIsIdle(struct Lh7xxxxAdc *adc)
{
	return adc->curConversionIdx < 0;
}

static bool adcPrvIsBiasedForPenDetect(struct Lh7xxxxAdc *adc)
{
	//for 4-wire displays, penirq is from X+ (AN0), which is normally pulled up, going low and switch B12 being closed
	// this necessitates Y+ and Y- being low, which require switches B6 and B7 being closed
	// see p459

	if ((adc->curCfg & 0x10a0) != 0x10a0)	//bias switches need to be set right
		return false;

	if (((adc->curCfg >> 19) & 0x0f) != 0)	//we must be sampling X+
		return false;

	return true;
}

static void adcPrvIrqsUpdate(struct Lh7xxxxAdc *adc)
{
	if (adcPrvIsIdle(adc) && adcPrvIsBiasedForPenDetect(adc)) {

		if (adcPrvIsPenDown(adc))
			adc->intSta |= 0x08;
		else
			adc->intSta &=~ 0x08;
	}

	adc->intSta &=~ 0x02;

	if (adc->fifoCnt >= 1 + ((adc->gc >> 3) & 0x0f))
		adc->intSta |= 0x02;

	socIcInt(adc->ic, LH7xxxx_I_ADC_TSCIRQ, (adc->intSta & adc->intEn) && (adc->intEn & 0x40));
	socIcInt(adc->ic, LH7xxxx_I_ADC_BOR, !!(adc->intSta & adc->intEn & 0x10));
	socIcInt(adc->ic, LH7xxxx_I_ADC_PENIRQ, !!(adc->intSta & adc->intEn & 0x08));
}

static void adcPrvCaptureMeasurement(struct Lh7xxxxAdc *adc)
{
	uint_fast16_t settime = (adc->curCfg >> 23) & 0x1ff;
	uint_fast8_t inP = (adc->curCfg >> 19) & 0x0f;
	uint_fast16_t biascon = adc->curCfg & 0x3ffc;
	uint_fast8_t refP = (adc->curCfg >> 16) & 3;
	uint_fast8_t inM = (adc->curCfg >> 18) & 1;
	uint_fast8_t refM = adc->curCfg & 3;
	uint_fast16_t result = 0;		//0x000..0x3ff


	if (VERBOSE) {

		static const char *namesINP[] = {"X+", "X-", "Y+", "Y-", "AN4/WIPER", "AN5", "AN6", "AN7", "AN8", "AN9", "VREF-", "VREF-", "VREF-", "VREF-", "VREF-", "VREF-"};
		static const char *namesINM[] = {"Ref-", "GND"};
		static const char *namesREFP[] = {"VREF+", "X+", "Y+", "AN8"};
		static const char *namesREFM[] = {"VREF-", "X-", "Y-", "AN9"};


		fprintf(stderr, "\n ## ADC[%d] 0x%08x measuring %u (%s) - %u (%s), with ref %u (%s) - %u (%s), settling time %u, bias 0x%04x\n",
			adc->curConversionIdx, adc->curCfg,
			(unsigned)inP, namesINP[inP], (unsigned)inM, namesINM[inM],
			(unsigned)refP, namesREFP[refP], (unsigned)refM, namesREFM[refM],
			(unsigned)settime, (unsigned)biascon);
	}

	//try to be hyper realistic about what's measured...because why not?

	const float vRefP = 2.1;	//not quite zero-ground 2.0Vref
	const float vRefM = 0.1;

	const float vADC = 3.3;
	const float vGND = 0.0;
	float an0 = 0.0/0.0, an1 = 0.0/0.0, an2 = 0.0/0.0, an3 = 0.0/0.0, an4 = 0.0/0.0, an6 = 0.0/0.0, an8 = 0.0/0.0, an9 = 0.0/0.0, sample = 0.0/0.0, inm, refm, refp;

	if (biascon & (1 << 2))
		an0 = vADC;

	if ((biascon & (1 << 3)) && (biascon & (1 << 4))) {
		fprintf(stderr, "ADC: short through an1\n");
	}
	else if (biascon & (1 << 3))
		an1 = vADC;
	else if (biascon & (1 << 4))
		an1 = vGND;
	
	if ((biascon & (1 << 5)) && (biascon & (1 << 6))) {
		fprintf(stderr, "ADC: short through an2\n");
	}
	else if (biascon & (1 << 5))
		an2 = vADC;
	else if (biascon & (1 << 6))
		an2 = vGND;
	
	if (biascon & (1 << 7))
		an3 = vGND;

	if (biascon & (1 << 8))
		an4 = vGND;
	else if (biascon & (1 << 13))
		an4 = vADC;
	else
		an4 = adc->auxAdcMv[4 - 4] * .001;

	an6 = adc->auxAdcMv[6 - 4] * .001;
	an8 = adc->auxAdcMv[8 - 4] * .001;
	an9 = adc->auxAdcMv[9 - 4] * .001;

	//spread over panes
	if (isnan(an0) && isnan(an1)) {
		if (isnan(an2) && !isnan(an3))
			an2 = an3;
		else if (isnan(an3) && !isnan(an2))
			an3 = an2;
	}
	else if (isnan(an2) && isnan(an3)) {
		if (isnan(an0) && !isnan(an1))
			an0 = an1;
		else if (isnan(an1) && !isnan(an0))
			an1 = an0;
	}

	if (adcPrvIsPenDown(adc)) {
		
		float alongX, alongY;

		alongX = (adc->x * an0 + (0x3ff - adc->x) * an1) / 0x3ff;
		alongY = (adc->y * an2 + (0x3ff - adc->y) * an3) / 0x3ff;

		if (isnan(an0))
			an0 = isnan(an1) ? alongY : an1;

		if (isnan(an0) && (1 << 12))
			an0 = isnan(alongY) ? vADC : alongY;

		if (isnan(an1))
			an1 = isnan(an0) ? alongY : an0;

		if (isnan(an2))
			an2 = isnan(an3) ? alongX : an3;

		if (isnan(an3))
			an3 = isnan(an2) ? alongX : an2;
		
		//special case for Z detect
		if (biascon == 0x30 && inP == 0 && inM == 0 && refP == 2 && refM == 1) {

 			an0 = 1.0;
		}
	}
	else {
		
		//if one end of a sheet is energized and another is not, the other takes on the value of the first
		if (isnan(an0) && !isnan(an1))
			an0 = an1;
		if (isnan(an1) && !isnan(an0))
			an1 = an0;
		if (isnan(an2) && !isnan(an3))
			an2 = an3;
		if (isnan(an3) && !isnan(an2))
			an3 = an2;

		if (isnan(an0) && (biascon & (1 << 12)))
			an0 = vADC;
	}

	if (isnan(an4) && (biascon & (1 << 13)))
		an4 = vADC;
	
	switch (inP) {
		case 0:	sample = an0;	break;
		case 1:	sample = an1;	break;
		case 2:	sample = an2;	break;
		case 3:	sample = an3;	break;
		case 4:	sample = an4;	break;
		case 6:	sample = an6;	break;
		case 8:	sample = an8;	break;
		case 9:	sample = an9;	break;
	}
	switch (refP) {
		case 0:	refp = vRefP;	break;
		case 1: refp = an0;		break;
		case 2: refp = an2;		break;
		case 3: refp = an8;		break;
	}
	switch (refM) {
		case 0:	refm = vRefM;	break;
		case 1:	refm = an1;		break;
		case 2:	refm = an3;		break;
		case 3:	refm = an9;		break;
	}
	inm = inM ? vGND : refm;

	if (isnan(sample))
		result = 0x200;
	else if (sample - inm < 0)
		result = 0;
	else if (sample - inm > refp - refm)
		result = 0x3ff;
	else
		result = (sample - inm) * 0x3ff / (refp - refm);

	if (VERBOSE)
		fprintf(stderr, "\n ## ADC[%d] measuring %0.3f - %0.3f against ref of %0.3f -  %0.3f -> 0x%04x\n", adc->curConversionIdx, sample, inm, refp, refm, (unsigned)result);

	//align result and record config number
	result = (result << 6) + (adc->curConversionIdx);
	if (adc->fifoCnt == NUM_FIFO_ENTRIES) {
		adc->intSta |= 0x01;
		adc->lastFifoInsertFailed = true;
	}
	else {

		adc->fifo[adc->fifoW] = result;
		adc->fifoCnt++;
		if (++adc->fifoW == NUM_FIFO_ENTRIES)
			adc->fifoW = 0;
	}
	adcPrvIrqsUpdate(adc);
}

static void adcPrvStartConversionWithIndex(struct Lh7xxxxAdc *adc, uint_fast8_t idx)
{
	adc->curConversionIdx = idx;
	adc->curCfg = adc->ctrlBank[idx];
	adc->cbState = 0b0010;
}

static void adcPrvStartNext(struct Lh7xxxxAdc *adc)
{
	if (adc->curConversionIdx >= 0 && adc->curConversionIdx < (adc->pc & 0x0f)) {	//we're already converting and can go convert the next thng - life is easy

		adcPrvStartConversionWithIndex(adc, adc->curConversionIdx + 1);
	}
	else {											//we finished a sequence or havent started one
	
		if (adc->curConversionIdx >= 0) {	//we finished a sequence

			adc->intSta |= 0x04;
		}

		if ((adc->gc & 7) == 6) {											//SSB-base conversion mode?  go do

			adc->gc &=~ 4;	//clear SSB
			adcPrvStartConversionWithIndex(adc, 0);
		}
		else if ((adc->gc & 3) == 1 && adcPrvIsPenDown(adc)) {					//convert on pen and pen is down? go do

			adcPrvStartConversionWithIndex(adc, 0);
			return;
		}
		else {
			
			//go to idle
			adc->curCfg = adc->idleCfg;
			adc->curConversionIdx = -1;
		}
	}
}

void lh7xxxxAdcPeriodic(struct Lh7xxxxAdc *adc)
{
	//fprintf(stderr, "PER: cbState is %u, curConversionIdx is %d\n", adc->cbState, adc->curConversionIdx);

	switch (adc->cbState) {
		case 0b0001:	//idle
			adcPrvStartNext(adc);
			break;

		case 0b0010:	//measuring
		case 0b0100:
			adc->cbState <<= 1;
			break;

		case 0b1000:	//measurement done
			adcPrvCaptureMeasurement(adc);
			adc->cbState = 0b0001;
			break;
	}
	adcPrvIrqsUpdate(adc);
}

static bool adcPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxAdc *adc = (struct Lh7xxxxAdc*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= ADC_BASE;
	pa >>= 2;

	if (write)
		val =  *(uint32_t*)buf;

//	if (write)
//		fprintf(stderr, "ADC WRITE to 0x%08lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);

	switch (pa) {
		
		case 0x00 / 4:		//HW
			if (write)
				return false;
			else
				val = adc->curCfg >> 16;
			break;
		
		case 0x04 / 4:		//LW
			if (write)
				return false;
			else
				val = adc->curCfg & 0xffff;
			break;

		case 0x08 / 4:		//RR
			if (write)
				return false;
			else if (!adc->fifoCnt) {

				fprintf(stderr, "XXX: reading empty ADC fifo\n");
				val = 0;
			}
			else {
				val = adc->fifo[adc->fifoR];
				if (++adc->fifoR == NUM_FIFO_ENTRIES)
					adc->fifoR = 0;
				adc->fifoCnt--;
				adcPrvIrqsUpdate(adc);
			}
			break;

		case 0x0c / 4:		//IM
			if (write) {

				adc->intEn = val & 0x5f;
				adcPrvIrqsUpdate(adc);
			}
			else
				val = adc->intEn;
			break;

		case 0x10 / 4:		//PC
			if (write)
				adc->pc = val & (deviceGetSocRev() ? 0xffff : 0x07ef);
			else
				val = adc->pc;
			break;

		case 0x14 / 4:		//GC
			if (write) {

				adc->gc = val & 0x007f;
				adcPrvIrqsUpdate(adc);
			}
			else {

				val = adc->gc;
			}
			break;

		case 0x18 / 4:		//GS
			if (write)
				return false;
			else {
				
				if (adcPrvIsPenDown(adc) && adcPrvIsIdle(adc) && adcPrvIsBiasedForPenDetect(adc))
					val += 0x100;
				val += adc->cbState << 4;
				val += 0xf & adc->curConversionIdx;
			}
			break;

		case 0x1c / 4:		//IS
			if (write)
				return false;
			else
				val = adc->intSta;
			break;

		case 0x20 / 4:		//FS
			if (write)
				return false;
			else {

				val = 0x100 * adc->fifoW + 0x10 * adc->fifoR + (adc->fifoCnt == NUM_FIFO_ENTRIES ? 0x08 : 0x00) + (adc->fifoCnt ? 0x00 : 0x04) + 
					(adc->lastFifoInsertFailed ? 0x02 : 0x00) + ((adc->fifoCnt >= 1 + ((adc->gc >> 3) & 0x0f)) ? 0x01 : 0x00);
			}
			break;

		case 0x24 / 4 ... 0x60 / 4:	//HWCBx
			if (write)
				adc->ctrlBank[pa - 0x24 / 4] = (adc->ctrlBank[pa - 0x24 / 4] & 0x0000ffff) | (val << 16);
			else
				val = adc->ctrlBank[pa - 0x24 / 4] >> 16;
			break;

		case 0x64 / 4 ... 0xa0 / 4:	//LWCBx
			if (write)
				adc->ctrlBank[pa - 0x64 / 4] = (adc->ctrlBank[pa - 0x64 / 4] & 0xffff0000) | (val & 0x3fff);
			else
				val = adc->ctrlBank[pa - 0x64 / 4] & 0xffff;
			break;

		case 0xa4 / 4:				//IHWCTRL
			if (write)
				adc->idleCfg = (adc->idleCfg & 0x0000ffff) | (val << 16);
			else
				val = adc->idleCfg >> 16;
			break;

		case 0xa8 / 4:				//ILWCTRL
			if (write)
				adc->idleCfg = (adc->idleCfg & 0xffff0000) | (val & 0x3fff);
			else
				val = adc->idleCfg & 0xffff;
			break;

		case 0xac / 4:				//MIS
			if (write)
				return false;
			else
				val = adc->intEn & adc->intSta;
			break;

		case 0xb0 / 4:				//IC
			if (write) {

				adc->intSta &=~ ((val & 7) << 2);
				adcPrvIrqsUpdate(adc);
			}
			else
				return false;
			break;

		default:
			return false;
	}
	
//	if (!write)
//		fprintf(stderr, "ADC READ [0x%08lx] -> 0x%08lx\n", (unsigned long)paorig, (unsigned long)val);

	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

void lh7xxxxAdcSetPenPos(struct Lh7xxxxAdc *adc, int32_t x, int32_t y)
{
	adc->x = x;
	adc->y = y;

	adcPrvIrqsUpdate(adc);
}

void lh7xxxxAdcSetAuxAdc(struct Lh7xxxxAdc *adc, uint_fast8_t idx, uint16_t mV)
{
	if (idx < 4 || idx > 9)
		return ;
	adc->auxAdcMv[idx - 4] = mV;
}

struct Lh7xxxxAdc* lh7xxxxAdcInit(struct ArmMem *mem, struct SocIc *ic)
{
	struct Lh7xxxxAdc *adc = (struct Lh7xxxxAdc*)calloc(1, sizeof(*adc));
	
	if (!adc)
		ERR("cannot alloc ADC");
	
	adc->ic = ic;
	adc->cbState = 1;
	adc->intSta = 0x10;
	adc->curConversionIdx = -1;
	adc->x = -1;
	adc->y = -1;

	if (!memRegionAdd(mem, ADC_BASE, ADC_SIZE, adcPrvMemAccessF, adc))
		ERR("cannot add ADC to MEM\n");
	
	return adc;
}
