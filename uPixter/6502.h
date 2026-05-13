#ifndef _6502_H_
#define _6502_H_


#include <stdint.h>


//stask is page1 (0x100 - 0x1FF)
//page0 is 0x00-0xff
//BRK/int vector is fffe-ffff
//rst vec is fffc-fffd
//nmi is fffa-fffb
//stack is empty, descending

struct CPU {

	uint16_t pc;
	uint8_t a,x,y,sp,sr, in_intr;
};

#define SR_N	0x80	//N flag
#define SR_V	0x40	//signed overflow
#define SR_R	0x20	//reserved: always set
#define SR_B	0x10	//set if BRK is executed
#define SR_D	0x08	//decimal flag
#define SR_I	0x04	//set to block interrupts
#define SR_Z	0x02	//zero flag
#define SR_C	0x01	//carry

void cpuInit(struct CPU* cpu);
void cpuIrq(struct CPU* cpu, uint8_t on);
void cpuNmi(struct CPU* cpu);
unsigned cpuRun(struct CPU* cpu, unsigned cycles);		//returns num cycles EXECUTED, will be >= requested

//extern
uint8_t memR(uint16_t addr);
void memW(uint16_t addr, uint8_t v);




#endif