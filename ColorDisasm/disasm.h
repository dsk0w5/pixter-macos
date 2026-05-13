#ifndef _DISASM_H_
#define _DISASM_H_

#include <stdint.h>
#include <stdbool.h>


#define ROM_BASE				0x48000000
#define ROM_MAX_SIZE			(16 << 20)

#define ROM_OFST_CONSTANTS		0x000c



typedef int16_t (*DisasmCodeReadByteF)(uint32_t pos);	//return -1 for unreadable code

void disasm(DisasmCodeReadByteF readF);

#endif
