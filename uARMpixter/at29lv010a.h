#ifndef _AT29LV010A_H_
#define _AT29LV010A_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "mem.h"


//emulated an AT29LV010A
//it differs enough from a normal ROM that it is worth being a separte thing
//we only use it for savegames, so it is always backed by a FILE*

struct AT29LV010A;

struct AT29LV010A* at29lv010aInit(struct ArmMem *mem, uint32_t adr, uint32_t vaSize, FILE *file);
void at29lv010aPeriodic(struct AT29LV010A *flash);		//this chip has timeouts and we need to implement them. call at ~32vKHz



#endif
