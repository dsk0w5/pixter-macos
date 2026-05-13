#ifndef _DISASM_H_
#define _DISASM_H_

#include <stdint.h>
#include <stdbool.h>


#define FIRST_VALID_ADDR			0x4000
#define LAST_VALID_ADDR				0xBFFF

typedef int16_t (*DisasmCodeReadByteF)(uint32_t pos);	//return -1 for unreadable code

void disasm(DisasmCodeReadByteF readF);

#endif
