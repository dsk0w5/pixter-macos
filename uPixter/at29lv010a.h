#ifndef _AT29LV010A_H_
#define _AT29LV010A_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>


//emulated an AT29LV010A on the pixter bus (mapping handled in here)

struct AT29LV010A;

struct AT29LV010A* at29lv010aInit(FILE *backingFile);

void at29lv010aBankSel(struct AT29LV010A *flash, uint8_t bankSel);	//we live at banksel 0xA0..0xA3
int16_t at29lv010aRead(struct AT29LV010A *flash, uint16_t busAddr);	//return negative on failure, else the byte read
bool at29lv010aWrite(struct AT29LV010A *flash, uint16_t busAddr, uint8_t byte);
void at29lv010aPeriodic(struct AT29LV010A *flash);



#endif
