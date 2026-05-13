//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "cp15.h"


struct ArmCP15 {
	unsigned unused;

};

void cp15Cycle(struct ArmCP15* cp15)
{
	//nothing
}

struct ArmCP15* cp15Init(struct ArmCpu* cpu, struct ArmMmu* mmu, uint32_t cpuid, uint32_t cacheId, bool xscale, bool omap)
{
	return (struct ArmCP15*)1;	//nonzero, but we do not add the coprocessor so instructions will be invalid
}

void cp15SetFaultStatus(struct ArmCP15* cp15, uint32_t addr, uint_fast8_t faultStatus)
{
	//nothing
}
