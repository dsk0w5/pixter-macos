//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _GPIO_EXPANDER_H_
#define _GPIO_EXPANDER_H_

#include "soc_GPIO.h"
#include <stdio.h> 
#include "nand.h"
#include "mem.h"
#include "CPU.h"


struct PixterGpioExpander;


struct PixterGpioExpander* gpioExpanderInit(struct ArmMem *physMem);


#endif