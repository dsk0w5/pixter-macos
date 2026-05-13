#ifndef _LH7XXXX_RCPS_H_
#define _LH7XXXX_RCPS_H_


#include "mem.h"
#include "soc_GPIO.h"

struct Lh7xxxxRcpc;

#define LH754XX_NUM_EXT_INT_PINS			7

struct Lh7xxxxRcpc* lh7xxxxRcpcInit(struct ArmCpu *cpu, struct ArmMem *physMem, struct SocIc *ic, void *speedReportCbkData);
bool lh7xxxxRcpcSetExtIntPin(struct Lh7xxxxRcpc *rcpc, uint_fast8_t which, bool state);


//provided externally
void socReportSpeeds(void *speedReportCbkData, uint32_t crystalHz, uint32_t pllHz, uint32_t hclkHz, uint32_t cpuHz);



#endif
