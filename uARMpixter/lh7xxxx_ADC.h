#ifndef _LH7XXXX_ADC_H_
#define _LH7XXXX_ADC_H_

#include "mem.h"
#include "soc_IC.h"

struct Lh7xxxxAdc;



struct Lh7xxxxAdc* lh7xxxxAdcInit(struct ArmMem *physMem, struct SocIc *ic);
void lh7xxxxAdcPeriodic(struct Lh7xxxxAdc *adc);


void lh7xxxxAdcSetPenPos(struct Lh7xxxxAdc *adc, int32_t x, int32_t y);				//negative for pen up
void lh7xxxxAdcSetAuxAdc(struct Lh7xxxxAdc *adc, uint_fast8_t idx, uint16_t mV);	//4..9 are valid, 




#endif
