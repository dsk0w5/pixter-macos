//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "gpiodev_PixterBbBex.h"
#include "pixterMelodyChipXL.h"
#include "pixterMelodyChip.h"
#include "soc_uWire.h"
#include "soc_GPIO.h"
#include "soc_UART.h"
#include "soc_SSP.h"
#include "soc_I2C.h"
#include "soc_I2S.h"
#include <stdbool.h>
#include <stdio.h>
#include "keys.h"
#include "nand.h"
#include "ROM.h"
#include "mem.h"


struct SocPeriphs {
	//in to deviceSetup
	struct SocGpio *gpio;
	struct SocI2c *i2c;
	struct SocI2s *i2s;
	struct SocSsp *ssp;
	struct SocSsp *ssp2;	//assp for xscale
	struct SocSsp *ssp3;	//nssp for scale
	struct ArmMem *mem;
	struct SoC *soc;
	
	struct SocUart *uarts[3];
	
	void *adc;		//some cases need this
	void *kpc;		//some cases need this

	struct PixterRomFile *consoleRom;
	struct PixterRomFile *cartRom;
	struct MelodyChip *cartMelodyChip, *consoleMelodyChip;
	struct MelodyChipXL *cartMelodyChipXL;

	FILE *eepromFile;
	FILE *savegameFile;		//can be NULL

	//out of device init, each only if needed:
	struct PixerBbBex *bbBexExternalBus;
};

enum RamTermination {		//what's after ram in phys map? (some devices probe)
	RamTerminationMirror,
	RamTerminationWriteIgnore,
	RamTerminationNone,
};

struct Device;

//simple queries
bool deviceHasGrafArea(void);
uint32_t deviceGetRamSize(void);
enum RamTermination deviceGetRamTerminationStyle(void);
enum RomChipType deviceGetRomMemType(void);
uint_fast8_t deviceGetSocRev(void);

//device handling
struct Device* deviceSetup(struct SocPeriphs *sp);
void deviceKey(struct Device *dev, uint32_t key, bool down);
void deviceTouch(struct Device *dev, int x, int y);

void deviceReportMelodyChipActive(struct Device *dev, bool inConsoleChip, bool active);


void devicePeriodic(struct Device *dev);


#endif
