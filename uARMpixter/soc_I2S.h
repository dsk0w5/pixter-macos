//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _SOC_I2S_H_
#define _SOC_I2S_H_

#include "mem.h"
#include "CPU.h"
#include "soc_IC.h"
#include "soc_DMA.h"
#include <stdbool.h> 
#include <stdint.h> 
#include <stdio.h> 




struct SocI2s;

typedef void (*I2sClientProcF)(void* userData, uint_fast8_t nBits, uint_fast16_t sent);	//no mic support


struct SocI2s* socI2sInit(struct ArmMem *physMem, struct SocIc *ic, struct SocDma *dma);
void socI2sPeriodic(struct SocI2s *i2s);

bool socI2sSetClient(struct SocI2s *i2s, I2sClientProcF procF, void* userData);			//one client max



#endif

