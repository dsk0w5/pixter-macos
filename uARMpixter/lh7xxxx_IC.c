//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr



#include "lh7xxxx_IC.h"
#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "mem.h"

#define NUM_VECTORED_IRQS			16
#define IC_BASE						0xFFFFF000
#define IC_SIZE						0x314

#define VERBOSE						0


struct SocIc {

	struct ArmCpu *cpu;
	
	//internal state
	bool wasFiq, wasIrq, areSignallingIrq;

	//user-visible state
	uint32_t vectAddr, irqsInput, intSelect, intEnable, softInt, defVectAddr, vectAddrArray[NUM_VECTORED_IRQS];
	uint8_t vectCtrl[NUM_VECTORED_IRQS];
};

static void socIcPrvRecalc(struct SocIc *ic)
{
	uint32_t intrEnabled = (ic->irqsInput | ic->softInt) & ic->intEnable;
	uint32_t fiqsEnabled = intrEnabled & ic->intSelect;
	uint32_t irqsEnabled = intrEnabled - fiqsEnabled;	//yes
	bool fiqNow, irqNow = ic->areSignallingIrq;	//we signal irq until cleared as per spec

	fiqNow = !!fiqsEnabled;

	if (irqsEnabled && !irqNow) {	//if we are currently already signalling an irq, we do nothing
		
		uint_fast8_t i;

		for (i = 0; i < NUM_VECTORED_IRQS; i++) {

			if ((ic->vectCtrl[i] & 0x20) && (irqsEnabled & (1 << (ic->vectCtrl[i] & 0x1f)))) {		//top prio irq found

				ic->vectAddr = ic->vectAddrArray[i];
				break;
			}
		}
		if (i == NUM_VECTORED_IRQS) {	//use default
			
			ic->vectAddr = ic->defVectAddr;
		}
		irqNow = true;
	}
	
	if (fiqNow != ic->wasFiq) {

		cpuIrq(ic->cpu, true, fiqNow);
		ic->wasFiq = fiqNow;
	}
	if (irqNow != ic->wasIrq) {

		cpuIrq(ic->cpu, false, irqNow);
		ic->wasIrq = irqNow;
	}
}

static bool socIcPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct SocIc *ic = (struct SocIc*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= IC_BASE;
	pa >>= 2;

	if (write) {
		switch(size) {
			case 1:
				val = *(uint8_t*)buf;
				if (VERBOSE)
					fprintf(stderr, "IC write to 0x%02lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);
				break;
			case 2:
				val = *(uint16_t*)buf;
				if (VERBOSE)
					fprintf(stderr, "IC write to 0x%04lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);
				break;
			case 4:
				val = *(uint32_t*)buf;
				if (VERBOSE)
					fprintf(stderr, "IC write to 0x%08lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);
				break;
			default:
				return false;
		}
	}
	
	switch (pa) {
		case 0x00 / 4:		//IRQStatus
			if (write)
				return false;
			else
				val = (ic->irqsInput | ic->softInt) & ic->intEnable &~ ic->intSelect;
			break;
		
		case 0x04 / 4:		//FIQStatus
			if (write)
				return false;
			else
				val = (ic->irqsInput | ic->softInt) & ic->intEnable & ic->intSelect;
			break;
		
		case 0x08 / 4:		//RawIntr
			if (write)
				return false;
			else
				val = (ic->irqsInput | ic->softInt);
			break;

		case 0x0c / 4:		//IntSelect
			if (write) {
				ic->intSelect = val;
				socIcPrvRecalc(ic);
			}
			else
				val = ic->intSelect;
			break;

		case 0x10 / 4:		//IntEnable
			if (write) {
				ic->intEnable |= val;
				socIcPrvRecalc(ic);
			}
			else
				val = ic->intEnable;
			break;

		case 0x14 / 4:		//IntEnClear
			if (write) {
				ic->intEnable &=~ val;
				socIcPrvRecalc(ic);
			}
			else
				val = 0;
			break;

		case 0x18 / 4:		//SoftInt
			if (write) {
				ic->softInt |= val;
				socIcPrvRecalc(ic);
			}
			else
				val = ic->softInt;
			break;

		case 0x1c / 4:		//SoftIntClear
			if (write) {
				ic->softInt &=~ val;
				socIcPrvRecalc(ic);
			}
			else
				val = 0;
			break;

		case 0x30 / 4:		//VectAddr
			if (write) {
				if (ic->areSignallingIrq) {
					ic->areSignallingIrq = false;
					socIcPrvRecalc(ic);
				}
			}
			else
				val = ic->vectAddr;
			break;

		case 0x34 / 4:		//DefVectAddr
			if (write)
				ic->defVectAddr = val;
			else
				val = ic->defVectAddr;
			break;

		case 0x100 / 4 ... 0x100 / 4 + NUM_VECTORED_IRQS - 1:
			if (write)
				ic->vectAddrArray[pa - 0x100 / 4] = val;
			else
				val = ic->vectAddrArray[pa - 0x100 / 4];
			break;

		case 0x200 / 4 ... 0x200 / 4 + NUM_VECTORED_IRQS - 1:
			if (write) 
				ic->vectCtrl[pa - 0x200 / 4] = val;
			else
				val = ic->vectCtrl[pa - 0x200 / 4];
			break;

		default:
			return false;
	}
	
	if (!write) {
		switch(size) {
			case 1:
				*(uint8_t*)buf = val;
				if (VERBOSE)
					fprintf(stderr, "IC READ [0x%08lx] -> 0x%02lx\n", (unsigned long)paorig, (unsigned long)val);
				break;
			case 2:
				*(uint16_t*)buf = val;
				if (VERBOSE)
					fprintf(stderr, "IC READ [0x%08lx] -> 0x%04lx\n", (unsigned long)paorig, (unsigned long)val);
				break;
			case 4:
				*(uint32_t*)buf = val;
				if (VERBOSE)
					fprintf(stderr, "IC READ [0x%08lx] -> 0x%08lx\n", (unsigned long)paorig, (unsigned long)val);
				break;
			default:
				return false;
		}
	}
	
	return true;
}

struct SocIc* socIcInit(struct ArmCpu *cpu, struct ArmMem *physMem, uint_fast8_t socRev)
{
	struct SocIc *ic = (struct SocIc*)malloc(sizeof(*ic));
	uint_fast8_t i;
	
	if (!ic)
		ERR("cannot alloc IC");
	
	memset(ic, 0, sizeof (*ic));
	
	ic->cpu = cpu;

	if (!memRegionAdd(physMem, IC_BASE, IC_SIZE, socIcPrvMemAccessF, ic))
		ERR("cannot add IC to MEM\n");
	
	return ic;
}

void socIcInt(struct SocIc *ic, uint_fast8_t intNum, bool raise)		//interrupt caused by emulated hardware
{
	if (intNum >= LH7xxxx_I_NUM_IRQS) {
		
		ERR("irq %u doesn't exist\n", intNum);
	}
	else {

		if (raise)
			ic->irqsInput |= (1 << intNum);
		else
			ic->irqsInput &=~ (1 << intNum);
		
		socIcPrvRecalc(ic);
	}
}
