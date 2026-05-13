//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "soc_IC.h"
#include "soc_GPIO.h"
#include "device.h"
#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "mem.h"


#define ADDRESS_SPACE_PER_PORT		0x1000

#define NUM_PORTS_LH754xx			5			//each of our prts is a pair of LH754 ports
#define NUM_PORTS_LH795xx			7
#define NUM_SUBPORTS_PER_PORT		2
#define NUM_PINS_PER_SUBPORT		8

#define NUM_PORTS_IMPLEMENTED		(NUM_PORTS_LH754xx > NUM_PORTS_LH795xx ? NUM_PORTS_LH754xx : NUM_PORTS_LH795xx)
#define NUM_GPIOS(_numPorts)		(_numPorts * NUM_SUBPORTS_PER_PORT * NUM_PINS_PER_SUBPORT)


#define GPIO_BASE					0xFFFD9000
#define GPIO_SIZE					(NUM_PORTS_IMPLEMENTED * ADDRESS_SPACE_PER_PORT)
#define NUM_GPIOS_IMPLEMENTED		NUM_GPIOS(NUM_PORTS_IMPLEMENTED)


struct Subport {
	uint8_t isOutput;	//1 = output
	uint8_t latch;		//internally driven
	uint8_t inputState;	//externally driven
};

struct Port {
	struct Subport sub[NUM_SUBPORTS_PER_PORT];
};

struct DirChangeNotifEntry {
	struct DirChangeNotifEntry *next;
	GpioDirsChangedF notifF;
	void *notifD;
};

struct ValChangeEntry {
	struct ValChangeEntry *next;
	GpioChangedNotifF notifF;
	void *notifD;
};

struct SocGpio {

	struct SocIc *ic;
	
	struct Port port[NUM_PORTS_IMPLEMENTED];

	struct ValChangeEntry *notif[NUM_GPIOS_IMPLEMENTED];
	struct DirChangeNotifEntry *notifDir;
};

static void socGpioPrvCallDirChangeHandlers(struct SocGpio *gpio)
{
	struct DirChangeNotifEntry *dce;

	for (dce = gpio->notifDir; dce; dce = dce->next)
		dce->notifF(dce->notifD);
}

static bool socGpioPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct SocGpio *gpio = (struct SocGpio*)userData;
	uint32_t val = 0, paorig = pa;
	struct Subport *sub;
	struct Port *port;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= GPIO_BASE;

	if (write)
		val =  *(uint32_t*)buf;
	
	port = &gpio->port[NUM_PORTS_IMPLEMENTED - 1 - pa / ADDRESS_SPACE_PER_PORT];		//adrs go from high to low as ports increase
	pa %= ADDRESS_SPACE_PER_PORT;
	pa /= 4;
	sub = &port->sub[pa % 2];
	pa /= 2;

	if (port - gpio->port >= (deviceGetSocRev() ? NUM_PORTS_LH795xx : NUM_PORTS_LH754xx))
		return false;

	switch (pa) {
		case 0:	//data reg
			if (write) {

				uint_fast8_t i, gpioGlobalNum = ((port - gpio->port) * NUM_SUBPORTS_PER_PORT + sub - port->sub) * NUM_PINS_PER_SUBPORT;
				uint8_t prevLatchVal = sub->latch, changedLatchVals = prevLatchVal ^ val;

				sub->latch = val;

				for (i = 0; i < NUM_PINS_PER_SUBPORT; i++, gpioGlobalNum++) {

					struct ValChangeEntry *vce;

					if (!(sub->isOutput & (1 << i)))
						continue;

					if (!(changedLatchVals & (1 << i)))
						continue;

					for (vce = gpio->notif[gpioGlobalNum]; vce; vce = vce->next)
						vce->notifF(vce->notifD, gpioGlobalNum, (prevLatchVal >> i) & 1, (val >> i) & 1);
				}
			}
			else {

				val = (sub->latch & sub->isOutput) | (sub->inputState &~ sub->isOutput);
			}
			break;

		case 1:	//direction reg
			if (write) {

				sub->isOutput = val;
				socGpioPrvCallDirChangeHandlers(gpio);
			}
			else
				val = sub->isOutput;
			break;

		default:
			return false;
	}

	if (!write)
		*(uint32_t*)buf = val;

	return true;
}

struct SocGpio* socGpioInit(struct ArmMem *physMem, struct SocIc *ic, uint_fast8_t socRev)
{
	struct SocGpio *gpio = (struct SocGpio*)calloc(1, sizeof(*gpio));
	
	if (!gpio)
		ERR("cannot alloc GPIO");
	
	(void)ic;
	(void)socRev;
	
	if (!memRegionAdd(physMem, GPIO_BASE, GPIO_SIZE, socGpioPrvMemAccessF, gpio))
		ERR("cannot add GPIO to MEM\n");
	
	return gpio;
}

void socGpioSetState(struct SocGpio *gpio, uint_fast8_t gpioNum, bool on)		//for input pins ont
{
	struct Subport *sub;
	struct Port *port;

	if (gpioNum >= (deviceGetSocRev() ? NUM_GPIOS(NUM_PORTS_LH795xx) : NUM_GPIOS(NUM_PORTS_LH754xx)))
		return;
	
	port = &gpio->port[gpioNum / (NUM_SUBPORTS_PER_PORT * NUM_PINS_PER_SUBPORT)];
	gpioNum %= NUM_SUBPORTS_PER_PORT * NUM_PINS_PER_SUBPORT;

	sub = &port->sub[gpioNum / NUM_PINS_PER_SUBPORT];
	gpioNum %= NUM_PINS_PER_SUBPORT;

	if (on)
		sub->inputState |= 1 << gpioNum;
	else
		sub->inputState &=~ (1 << gpioNum);
}

enum SocGpioState socGpioGetState(struct SocGpio *gpio, uint_fast8_t gpioNum)
{
	struct Subport *sub;
	struct Port *port;

	if (gpioNum >= (deviceGetSocRev() ? NUM_GPIOS(NUM_PORTS_LH795xx) : NUM_GPIOS(NUM_PORTS_LH754xx)))
		return SocGpioStateNoSuchGpio;
	
	port = &gpio->port[gpioNum / (NUM_SUBPORTS_PER_PORT * NUM_PINS_PER_SUBPORT)];
	gpioNum %= NUM_SUBPORTS_PER_PORT * NUM_PINS_PER_SUBPORT;

	sub = &port->sub[gpioNum / NUM_PINS_PER_SUBPORT];
	gpioNum %= NUM_PINS_PER_SUBPORT;

	if (!(sub->isOutput & (1 << gpioNum)))
		return SocGpioStateHiZ;
	if (sub->latch & (1 << gpioNum))
		return SocGpioStateHigh;
	return SocGpioStateLow;
}

void socGpioSetNotif(struct SocGpio *gpio, uint_fast8_t gpioNum, GpioChangedNotifF notifF, void* userData)
{
	struct ValChangeEntry *vce;
	if (gpioNum >= (deviceGetSocRev() ? NUM_GPIOS(NUM_PORTS_LH795xx) : NUM_GPIOS(NUM_PORTS_LH754xx)))
		return;
	
	vce = calloc(1, sizeof(*vce));
	vce->next = gpio->notif[gpioNum];
	vce->notifF = notifF;
	vce->notifD = userData;
	gpio->notif[gpioNum] = vce;
}

void socGpioSetDirsChangedNotif(struct SocGpio *gpio, GpioDirsChangedF notifF, void *userData)
{
	struct DirChangeNotifEntry *dce = malloc(sizeof(*dce));

	if (dce) {
		dce->next = gpio->notifDir;
		dce->notifF = notifF;
		dce->notifD = userData;
		gpio->notifDir = dce;
	}
}

