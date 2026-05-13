//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "mmiodev_PixterGpioExpander.h"
#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "nand.h"
#include "mem.h"
#include "CPU.h"

//this is a port expander presenr in pixter multimedia. use is unknown and seems to be only at boot


#define NUM_PORTS			16

struct PixterGpioExpander {
	uint16_t ports[NUM_PORTS];

	uint16_t curState, curShift;
};

static bool gpioNandPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf){

	struct PixterGpioExpander *exp = (struct PixterGpioExpander*)userData;
	
	if (size != 2) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	if (write) {
		
		uint_fast16_t val = *(uint16_t*)buf;

		if ((val & 0x4) && !(exp->curState & 4)) {		//rising clock

			exp->curShift = exp->curShift * 2 + (val / 2) % 2;
		}

		if ((val & 0x20) && !(exp->curState & 0x20)) {	//rising latch
			uint_fast16_t portNo = exp->curShift / 512 % NUM_PORTS;
			uint_fast16_t valWritten = exp->curShift % 512;


			fprintf(stderr, "PIXTER GPIO EXP port %2u 0x%03x -> 0x%03x\n", (unsigned)portNo, (unsigned)exp->ports[portNo], (unsigned)valWritten);
			exp->ports[portNo] = valWritten;
		}
		exp->curState = val;
	}
	else {
		
		*(uint16_t*)buf = exp->curState;
	}
	
	return true;
}

struct PixterGpioExpander* gpioExpanderInit(struct ArmMem *physMem)
{
	struct PixterGpioExpander *exp = (struct PixterGpioExpander*)calloc(1, sizeof(*exp));
	
	if (!exp)
		ERR("cannot alloc GPIO EXPANDER");
	

	if (!memRegionAdd(physMem, 0x4C500000, 2, gpioNandPrvMemAccessF, exp))
		ERR("cannot add NAND to MEM\n");
	
	return exp;
}
