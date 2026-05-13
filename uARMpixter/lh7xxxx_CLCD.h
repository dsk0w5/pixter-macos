#ifndef _LH7XXXX_CLCD_H_
#define _LH7XXXX_CLCD_H_

#include "mem.h"
#include "CPU.h"
#include "soc_IC.h"

struct Lh7xxxxLcd;



struct Lh7xxxxLcd* lh7xxxxClcdInit(struct ArmMem *physMem, struct SocIc *ic, bool hardGrafArea);
void lh7xxxxClcdPeriodic(struct Lh7xxxxLcd *lcd);


#endif

