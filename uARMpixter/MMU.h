//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _MMU_H_
#define _MMU_H_


#include <stdbool.h>
#include <stdint.h>
#include "mem.h"


struct ArmMmu;

#define MMU_DISABLED_TTP		0xFFFFFFFFUL

#define MMU_MAPPING_CACHEABLE	0x0001
#define MMU_MAPPING_BUFFERABLE	0x0002
#define MMU_MAPPING_UR			0x0004
#define MMU_MAPPING_UW			0x0008
#define MMU_MAPPING_SR			0x0010
#define MMU_MAPPING_SW			0x0020


struct ArmMmu* mmuInit(struct ArmMem *mem, bool xscaleMode);
void mmuReset(struct ArmMmu *mmu);

bool mmuTranslate(struct ArmMmu *mmu, uint32_t va, bool priviledged, bool write, uint32_t* paP, uint_fast8_t* fsrP, uint8_t *mappingInfoP);

bool mmuIsOn(struct ArmMmu *mmu);

void mmuTlbFlush(struct ArmMmu *mmu);


//MPU only
void mmuMpuSetEnabled(struct ArmMmu *mmu, bool on);
uint_fast8_t mmuMpuGetNumRegions(struct ArmMmu *mmu);
uint_fast16_t mmuMpuGetAP(struct ArmMmu *mmu);
void mmuMpuSetAP(struct ArmMmu *mmu, uint_fast16_t val);
uint32_t mmuMpuGetRegionCfg(struct ArmMmu *mmu, uint_fast8_t idx);
void mmuMpuSetRegionCfg(struct ArmMmu *mmu, uint_fast8_t idx, uint32_t val);
uint_fast8_t mmuMpuGetC(struct ArmMmu *mmu);
void mmuMpuSetC(struct ArmMmu *mmu, uint_fast8_t c);
uint_fast8_t mmuMpuGetB(struct ArmMmu *mmu);
void mmuMpuSetB(struct ArmMmu *mmu, uint_fast8_t b);

void mmuMpuSetDtcmLoc(struct ArmMmu *mmu, uint32_t size, uint32_t areaSize, uint32_t targetAddr);
void mmuMpuSetItcmLoc(struct ArmMmu *mmu, uint32_t size, uint32_t areaSize);
void mmuMpuSetTcmPAs(struct ArmMmu *mmu, uint32_t itcmPA, uint32_t dtcmPA);	//i am lazy so TCM also has a permanent PA at an adress hopefully nobody needs
void mmuMpuSetTcmCfg(struct ArmMmu *mmu, bool itcmOn, bool itcmLoad, bool dtcmOn, bool dtcmLoad);


//MMU only
uint32_t mmuGetTTP(struct ArmMmu *mmu);
void mmuSetTTP(struct ArmMmu *mmu, uint32_t ttp);
void mmuSetS(struct ArmMmu *mmu, bool on);
void mmuSetR(struct ArmMmu *mmu, bool on);
bool mmuGetS(struct ArmMmu *mmu);
bool mmuGetR(struct ArmMmu *mmu);
uint32_t mmuGetDomainCfg(struct ArmMmu *mmu);
void mmuSetDomainCfg(struct ArmMmu *mmu, uint32_t val);
void mmuDump(struct ArmMmu *mmu);		//for calling in GDB :)


#endif

