//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "MMU.h"
#include "mem.h"



void mmuReset(struct ArmMmu *mmu)
{
	//not used for MPU
}

struct ArmMmu* mmuInit(struct ArmMem *mem, bool xscaleMode)
{	
	return (struct ArmMmu*)1;
}

bool mmuIsOn(struct ArmMmu *mmu)
{
	return false;
}

bool mmuTranslate(struct ArmMmu *mmu, uint32_t adr, bool priviledged, bool write, uint32_t* paP, uint_fast8_t* fsrP, uint8_t *mappingInfoP)
{
	if (paP)
		*paP = adr;
	if (mappingInfoP)
		*mappingInfoP = MMU_MAPPING_SR | MMU_MAPPING_SW | MMU_MAPPING_UR | MMU_MAPPING_UW;

	return true;
}
