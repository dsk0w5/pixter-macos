#include "lh7xxxx_RCPC.h"
#include "lh7xxxx_IC.h"
#include "device.h"
#include <stdlib.h>
#include "util.h"

#define RCPC_BASE			0xFFFE2000
#define RCPC_SIZE			0xc8

#define MAPPED_REGION_BASE	0x00000000
#define MAPPED_REGION_SIZE	0x20000000


struct Lh7xxxxRcpc {
	
	struct ArmCpu *cpu;
	struct ArmMem *mem;
	struct SocIc *ic;
	void *speedReportCbkData;

	uint16_t ctrl, apbClk0, ahbClk, intConfig;
	uint8_t remap, resetStatus, hclkPresc, apbClk1, lcdPresc, sspPresc;

	uint8_t cpuClkPre, pclksel[2], adcPre, usbPre, coreCfg;	//LH795xx only
	uint16_t sysPllCtrl, usbPllCtrl;	//LH795xx only

	uint8_t activeEdgeDetects, activeLevelDetects;
	uint32_t extPinState;

	uint32_t remapBase;
};

bool lh7xxxxRcpcSetExtIntPin(struct Lh7xxxxRcpc *rcpc, uint_fast8_t which, bool state)
{
	uint32_t newState, oldState = rcpc->extPinState, changedState;
	bool fire = false;

	if (which >= (deviceGetSocRev() ? LH795XX_NUM_EXT_INT_PINS : LH754XX_NUM_EXT_INT_PINS))
		return false;

	if (state)
		newState = oldState | (1 << which);
	else
		newState = oldState &~ (1 << which);
	changedState = newState ^ oldState;
	rcpc->extPinState = newState;

	switch ((rcpc->intConfig >> (2 * which)) & 3) {
		case 0:	//low level
			fire = !state;
			if (fire)
				rcpc->activeLevelDetects |= 1 << which;
			else
				rcpc->activeLevelDetects &=~ (1 << which);
			break;

		case 1:	//high level
			fire = !!state;
			if (fire)
				rcpc->activeLevelDetects |= 1 << which;
			else
				rcpc->activeLevelDetects &=~ (1 << which);
			break;

		case 2:	//falling edge
			fire = !!(changedState & oldState);
			if (fire)
				rcpc->activeEdgeDetects |= 1 << which;
			break;

		case 3:	//rising edge
			fire = !!(changedState & newState);
			if (fire)
				rcpc->activeEdgeDetects |= 1 << which;
			break;
	}
	socIcInt(rcpc->ic, LH7xxxx_I_EXT_INT_0 + which, !!((rcpc->activeLevelDetects | rcpc->activeEdgeDetects) & (1 << which)));

	return true;
}

static void lh7xxxxRcpcRecalcIrqs(struct Lh7xxxxRcpc *rcpc)
{
	uint_fast8_t which = 0;

	for (which = 0; which < (deviceGetSocRev() ? LH795XX_NUM_EXT_INT_PINS : LH754XX_NUM_EXT_INT_PINS); which++) {

		lh7xxxxRcpcSetExtIntPin(rcpc, which, !!(rcpc->extPinState & (1 << which)));
	}
}

static float rcpcPrvGetXtalInFreq(struct Lh7xxxxRcpc *rcpc)
{
	return deviceGetSocRev() ? 11.2896f : 18.0f;
}

static void rcpcPrvReportAndShowSpeeds(struct Lh7xxxxRcpc *rcpc)
{
	float xtalInFreq = rcpcPrvGetXtalInFreq(rcpc), pllMult, pllSpeed;

	if (deviceGetSocRev()) {

		pllMult = (float)(rcpc->sysPllCtrl & 0x3f) / (float)((rcpc->sysPllCtrl >> 6) & 0x3f);
	}
	else {

		pllMult = 7;	//constant multiplier
	}

	pllSpeed = xtalInFreq * pllMult;
	fprintf(stderr, "CONFIGURED SPEEDS:\n");
	fprintf(stderr, "  XTALIN: %02.2fMHz\n", xtalInFreq);
	fprintf(stderr, "  PLL: XTALIN * %02.2f -> %02.2fMHz\n", pllMult, pllSpeed);
	fprintf(stderr, "  HCLK: PLL / %u -> %02.2fMHz\n", 2 * rcpc->hclkPresc, pllSpeed / (2 * rcpc->hclkPresc));
	if (deviceGetSocRev())
		fprintf(stderr, "  CPU: PLL / %u -> %02.2fMHz\n", 2 * rcpc->cpuClkPre, pllSpeed / (2 * rcpc->cpuClkPre));

	socReportSpeeds(rcpc->speedReportCbkData, 
		rcpcPrvGetXtalInFreq(rcpc) * 1000000,
		pllSpeed * 1000000,
		pllSpeed / (2 * rcpc->hclkPresc) * 1000000,
		(deviceGetSocRev() ? (pllSpeed / (2 * rcpc->cpuClkPre)) : (pllSpeed / (2 * rcpc->hclkPresc))) * 1000000
	);
}


static bool rcpcPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxRcpc *rcpc = (struct Lh7xxxxRcpc*)userData;
	uint32_t val, paOrig = pa;

	if (size != 4 || pa % 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}

	pa -= RCPC_BASE;
	pa /= 4;

	if (write)
		val = *(uint32_t*)buf;
	
	switch (pa) {
		case 0x00 / 4:		//Ctrl
			if (write) {

				if (!(rcpc->ctrl & 0x200) && (val & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				rcpc->ctrl = val;
				if (val & 0x1c) {

					//fprintf(stderr, "%s: sleeping...\n", __func__);
				}
			}
			else
				val = rcpc->ctrl &~ 0x1c;
			break;

		case 0x04 / 4:		//Identification
			if (write)
				return false;
			else
				val = deviceGetSocRev() ? 0x00005240 : 0x00005411;
			break;

		case 0x08 / 4:		//Remap
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				rcpc->remap = val & 3;
				switch (rcpc->remap){
					case 0b00:	rcpc->remapBase = deviceGetSocRev() ? 0x44000000 : 0x40000000;	break;
					case 0b01:	rcpc->remapBase = deviceGetSocRev() ? 0x20000000 : 0x80000000;	break;
					case 0b10:	rcpc->remapBase = 0x60000000;									break;
					case 0b11:	rcpc->remapBase = deviceGetSocRev() ? 0x40000000 : 0x00000000;	break;
				}
			}
			else
				val = rcpc->remap;
			break;

		case 0x0c / 4:		//SoftReset
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				if ((uint16_t)val == 0xdead) {
					fprintf(stderr, "RESET\n");
					cpuReset(rcpc->cpu, 0);
				}
				else {

					return false;
				}
			}
			else
				val = 0;
			break;
		
		case 0x10 / 4:		//ResetStatus
			if (write)
				return false;
			else
				val = rcpc->resetStatus;
			break;

		case 0x14 / 4:		//ResetStatusClr
			if (write) {
				
				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->resetStatus &=~ val;
			}
			else
				val = 0;
			break;

		case 0x18 / 4:		//SysClkPrescaler
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				val &= 0x0f;
				if (!val)
					val = 1;

				rcpc->hclkPresc = val;
				rcpcPrvReportAndShowSpeeds(rcpc);
			}
			else
				val = rcpc->hclkPresc;
			break;

		case 0x1c / 4:		//CPUCLKPRE
			if (!deviceGetSocRev())
				return false;
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				val &= 0x0f;
				if (!val)
					val = 1;

				rcpc->cpuClkPre = val;
				rcpcPrvReportAndShowSpeeds(rcpc);
			}
			else
				val = rcpc->cpuClkPre;
			break;

		case 0x24 / 4:		//APBPeriphClkCtrl0
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->apbClk0 = val & 0x207;
			}
			else
				val = rcpc->apbClk0;
			break;

		case 0x28 / 4:		//APBPeriphClkCtrl1
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->apbClk1 = val & (deviceGetSocRev() ? 0x0f : 0x03);
			}
			else
				val = rcpc->apbClk1;
			break;
		
		case 0x2c / 4:		//AhbClkCtrl
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->ahbClk = val & (deviceGetSocRev() ? 0x1f : 0x03);
			}
			else
				val = rcpc->ahbClk;
			break;
		
		case 0x30 / 4:		//PCLKSEL0
			if (!deviceGetSocRev())
				return false;
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->pclksel[0] = val & 0x07;
			}
			else
				val = rcpc->pclksel[0];
			break;
		
		case 0x34 / 4:		//PCLKSEL1
			if (!deviceGetSocRev())
				return false;
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				rcpc->pclksel[1] = val & 0x0e;
			}
			else
				val = rcpc->pclksel[1];
			break;

		case 0x3C / 4:		//SILICONREV
			if (!deviceGetSocRev())
				return false;
			if (write)
				return false;
			else
				val = 0x01;
			break;

		case 0x40 / 4:		//LCDPrescaler
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->lcdPresc = val & 0xff;
			} 
			else
				val = rcpc->lcdPresc;
			break;
		
		case 0x44 / 4:		//SSPPrescaler
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->sspPresc = val & 0xff;
			} 
			else
				val = rcpc->sspPresc;
			break;
		
		case 0x48 / 4:		//ADCPRE
			if (!deviceGetSocRev())
				return false;
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->adcPre = val & 0xff;
			} 
			else
				val = rcpc->adcPre;
			break;
		
		case 0x4c / 4:		//USBPRE
			if (!deviceGetSocRev())
				return false;
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->usbPre = val & 0xff;
			} 
			else
				val = rcpc->usbPre;
			break;
		
		case 0x80 / 4:		//IntConfig
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->intConfig = val & (deviceGetSocRev() ? 0xffff : 0x3fff);
				lh7xxxxRcpcRecalcIrqs(rcpc);
			} 
			else
				val = rcpc->intConfig;
			break;
		
		case 0x84 / 4:		//IntClear
			if (write) {

				rcpc->activeEdgeDetects &=~ val;
				lh7xxxxRcpcRecalcIrqs(rcpc);
			}
			else
				val = 0;
			break;

		case 0x88 / 4:		//CORECONFIG
			if (!deviceGetSocRev())
				return false;
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->coreCfg = val & 0x03;
			} 
			else
				val = rcpc->coreCfg;
			break;
		
		case 0xc0 / 4:		//SYSPLLCTL
			if (!deviceGetSocRev())
				return false;
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->sysPllCtrl = 0x2000 | (val & 0x1fff);
				rcpcPrvReportAndShowSpeeds(rcpc);
			} 
			else
				val = rcpc->sysPllCtrl;
			break;
		
		case 0xc4 / 4:		//USBPLLCTL
			if (!deviceGetSocRev())
				return false;
			if (write) {

				if (!(rcpc->ctrl & 0x200)) {
					fprintf(stderr, "%s: RCPC Write of 0x%08x to 0x%08x while RCPC is locked\n", __func__, val, paOrig);
					return false;
				}
				
				rcpc->usbPllCtrl = 0x2000 | (val & 0x1fff);
			} 
			else
				val = rcpc->usbPllCtrl;
			break;
		
		default:
			return false;
	}
	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

static bool mappedMemPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxRcpc *rcpc = (struct Lh7xxxxRcpc*)userData;

	return memAccess(rcpc->mem, pa - MAPPED_REGION_BASE + rcpc->remapBase, size, MEM_ACCCESS_FLAG_NOERROR | (write ? MEM_ACCESS_TYPE_WRITE : MEM_ACCESS_TYPE_READ), buf);	//noerror for gdb's sake
}

struct Lh7xxxxRcpc *lh7xxxxRcpcInit(struct ArmCpu *cpu, struct ArmMem *mem, struct SocIc *ic, void *speedReportCbkData)
{
	struct Lh7xxxxRcpc *rcpc = (struct Lh7xxxxRcpc*)calloc(1, sizeof(*rcpc));
	uint_fast8_t i;
	
	if (!rcpc)
		ERR("cannot alloc RCPC");
	
	rcpc->cpu = cpu;
	rcpc->mem = mem;
	rcpc->ic = ic;
	rcpc->speedReportCbkData = speedReportCbkData;
	rcpc->ctrl = 0x263;
	rcpc->resetStatus = 1;
	rcpc->apbClk1 = deviceGetSocRev() ? 0x0f : 0x03;
	rcpc->ahbClk = deviceGetSocRev() ? 0x00 : 0x03;
	rcpc->hclkPresc = 0x0f;
	rcpc->cpuClkPre = 0x0f;
	rcpc->remapBase = deviceGetSocRev() ? 0x44000000 : 0x40000000;
	rcpc->sysPllCtrl = 0x2045;
	rcpc->usbPllCtrl = 0x2045;

	if (!memRegionAdd(mem, RCPC_BASE, RCPC_SIZE, rcpcPrvMemAccessF, rcpc))
		ERR("cannot add RCPC to MEM\n");
	
	if (!memRegionAdd(mem, MAPPED_REGION_BASE, MAPPED_REGION_SIZE, mappedMemPrvMemAccessF, rcpc))
		ERR("cannot add MAPPED MEM to MEM\n");
	
	rcpcPrvReportAndShowSpeeds(rcpc);

	return rcpc;
}

