//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "mmiodev_GPIONAND.h"
#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "nand.h"
#include "mem.h"
#include "CPU.h"



struct GPIONAND {
	struct SocGpio *gpio;
	struct NAND *nand;
	
	uint8_t clePin, alePin;
	int8_t rdyPinNo;
};

static void gpioNandPrvReady(void *userData, bool ready)
{
	struct GPIONAND *gpioNand = (struct GPIONAND*)userData;
	
	if (gpioNand->rdyPinNo >= 0)
		socGpioSetState(gpioNand->gpio, gpioNand->rdyPinNo, ready);
}

static bool gpioNandPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf){

	struct GPIONAND *gpioNand = (struct GPIONAND*)userData;
	bool ret, cle = false, ale = false;
	
	if (size != 1) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	switch (socGpioGetState(gpioNand->gpio, gpioNand->clePin)) {
		case SocGpioStateLow:
			cle = false;
			break;

		case SocGpioStateHigh:
			cle = true;
			break;

		default:
			return false;
	}
	
	switch (socGpioGetState(gpioNand->gpio, gpioNand->alePin)) {
		case SocGpioStateLow:
			ale = false;
			break;

		case SocGpioStateHigh:
			ale = true;
			break;

		default:
			return false;
	}

	
	if (write) {
		
		ret = nandWrite(gpioNand->nand, cle, ale, *(uint8_t*)buf);
//		if (pa & 6)
//			fprintf(stderr, "NAND w c%u a%u 0x%02x -> %s\n", cle, ale, *(uint8_t*)buf, ret ? "OK" : "FAIL");
	}
	else {
		
		ret = nandRead(gpioNand->nand, cle, ale, (uint8_t*)buf);
//		if (pa & 6)
//			fprintf(stderr, "NAND r c%u a%u 0x%02x -> %s\n", cle, ale, *(uint8_t*)buf, ret ? "OK" : "FAIL");
	}
	
	if (!ret)
		ERR("NAND ACCESS FAILS\n");
	
	return ret;
}

struct GPIONAND* gpioNandInit(struct ArmMem *physMem, uint32_t addr, struct SocGpio* gpio, int_fast8_t rdyPin, uint_fast8_t clePin, uint_fast8_t alePin, const struct NandSpecs *specs, const struct PixterRomChunk *chunk)
{
	struct GPIONAND *gpioNand = (struct GPIONAND*)calloc(1, sizeof(*gpioNand));
	
	if (!gpioNand)
		ERR("cannot alloc NAND");
	
	gpioNand->gpio = gpio;
	gpioNand->rdyPinNo = rdyPin;
	gpioNand->clePin = clePin;
	gpioNand->alePin = alePin;

	
	gpioNand->nand = nandInit(chunk, specs, gpioNandPrvReady, gpioNand);
	if (!gpioNand->nand)
		ERR("Cannot init underlying NAND\n");
	
	if (!memRegionAdd(physMem, addr, 4, gpioNandPrvMemAccessF, gpioNand))
		ERR("cannot add NAND to MEM\n");
	
	return gpioNand;
}

void gpioNandPeriodic(struct GPIONAND *gpioNand)
{
	nandPeriodic(gpioNand->nand);
}
