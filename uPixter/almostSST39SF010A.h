#ifndef _ALMOST_SST39SF010A_H_
#define _ALMOST_SST39SF010A_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>


//a SST39SF010A except that erase blocks are 1KB, do not ask...

struct AlmostSST39SF010A;

struct AlmostSST39SF010A* almostSst39sf010aInit(FILE *backingFile);

void almostSst39sf010aBankSel(struct AlmostSST39SF010A *flash, uint8_t bankSel);	//we live at banksel 0xA0..0xA3
int16_t almostSst39sf010aRead(struct AlmostSST39SF010A *flash, uint16_t busAddr);	//return negative on failure, else the byte read
bool almostSst39sf010aWrite(struct AlmostSST39SF010A *flash, uint16_t busAddr, uint8_t byte);
void almostSst39sf010aPeriodic(struct AlmostSST39SF010A *flash);



#endif
