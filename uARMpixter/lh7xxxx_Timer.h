#ifndef _LH7XXXX_TIMER_H_
#define _LH7XXXX_TIMER_H_

#include "mem.h"
#include "soc_IC.h"

struct Lh7xxxxTimer;



struct Lh7xxxxTimer* lh7xxxxTimerInit(struct ArmMem *physMem, struct SocIc *ic, uint32_t base, uint_fast8_t irqNo, bool isType0);
void lh7xxxxTimerPeriodic(struct Lh7xxxxTimer *timer);


#endif
