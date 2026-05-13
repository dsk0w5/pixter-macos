#ifndef _BB_BUS_H_
#define _BB_BUS_H_

#include <stdint.h>

void bbBusInitMulti(void);
void bbBusWriteCycleMulti(uint16_t addr, uint8_t data);
uint8_t bbBusReadCycleMulti(uint16_t addr);



void bbBusInitColor(void);
void bbBusWriteCycleColor(uint16_t addr, uint8_t data);
uint8_t bbBusReadCycleColor(uint16_t addr);




#endif

