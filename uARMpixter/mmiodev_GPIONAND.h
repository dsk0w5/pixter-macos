//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _GPIO_NAND_H_
#define _GPIO_NAND_H_

#include "soc_GPIO.h"
#include <stdio.h> 
#include "nand.h"
#include "mem.h"
#include "CPU.h"
#include "pixterRomFile.h"


struct GPIONAND;


struct GPIONAND* gpioNandInit(struct ArmMem *physMem, uint32_t addr, struct SocGpio* gpio, int_fast8_t rdyPin, uint_fast8_t clePin, uint_fast8_t alePin, const struct NandSpecs *specs, const struct PixterRomChunk *chunk);

void gpioNandPeriodic(struct GPIONAND *nand);

#endif