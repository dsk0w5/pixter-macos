//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _CP15_H_
#define _CP15_H_


#include <stdbool.h>
#include <stdint.h>
#include "CPU.h"
#include "MMU.h"

struct ArmCP15;


struct ArmCP15* cp15Init(struct ArmCpu* cpu, struct ArmMmu* mmu, uint32_t cpuid, uint32_t cacheId, bool xscale, bool omap);
void cp15SetFaultStatus(struct ArmCP15* cp15, uint32_t addr, uint_fast8_t faultStatus);
void cp15Cycle(struct ArmCP15* cp15);

//cpu only
void cp15SetTcmSizes(struct ArmCP15* cp15, uint32_t itcmSize, uint32_t dtcmSize);

#endif

