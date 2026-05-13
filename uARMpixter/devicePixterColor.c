//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr
#include "gpiodev_PixterBbBex.h"
#include "pixterMelodyChipXL.h"
#include "gpiodev_MasterI2C.h"
#include "pixterMelodyChip.h"
#include "i2cdev_24C02.h"
#include "lh7xxxx_ADC.h"
#include "SDL2/SDL.h"
#include "soc_GPIO.h"
#include "device.h"
#include "util.h"
#include "ROM.h"
#include "RAM.h"

/*

	PIXTER
	160x160	12bpp LCD
	LH75411
	16KB TCM + 16KB SRAM in SoC

	slot pinout: https://elinux.org/Pixter_Expansion_Slot
	board info: https://elinux.org/Pixter_Chip_List
	    Cypress CY62127 64K x 16 Static RAM Media:CY62127.pdf    -> 128K
	    SL386D Low Voltage Audio Power AMP Media:SL386D.pdf
	    MC34063 Step-Up/Step-Down Inverting Switch Regulator Media:MC34063A-D.pdf
	    LM324 Quad Operational Amplifier Media:LM324.pdf
	    Chip On Board Audio IC
	    Chip On Board ROM

	LCD is a VDS VG-T161658 no info 


	//Pixter doe snot use the RTC, so RTC is not emulated in LH754 :)



	J6 is button in back (Active high)
	G6 is cart detect (active high, pulled up on cart)

	ADC ranges:

	left to right is F200 to 0e00	(these are left justified)
	top to bottom is 2900 to f800

	ADC AN8 = batteries * .24 (yes not .25)
	touchscreen is sideways, X & y swapped!!!


	//portH lower 4 bits are contrast. output zero, direction = value in bottom 4 bits



	PINOUT OF THE SLOT (Correct)

	1 	A0					2 	D0
	3 	A1					4 	D1
	5 	A2					6 	D2
	7 	A3					8 	D3
	9 	A4					10 	D4
	11 	A5					12 	D5
	13 	A6					14 	D6
	15 	A7					16 	D7
	17 	A8					18 	D8
	19 	A9					20 	D9
	21 	A10 				22 	D10
	23 	A11 				24 	D11
	25 	A12 				26 	D12
	27 	A13 				28 	D13
	29 	A14 				30 	D14
	31 	A15 				32 	D15
	33 	A16 (PC0)			34 	PE1 (UARTTX2)
	35 	A17 (PC1)			36 	PE0 (UARTRX2)
	37 	A18 (PC2)			38 	nCS2
	39 	A19 (PC3)			40 	nCS3
	41 	A20 (PC4)			42 	nOE
	43 	A21 (PC5)			44 	nWE
	45 	A22 (PC6)			46 	PG6 31K puill to ground - cart detect
	47 	A23 (PC7)			48 	PD0
	49 	PD2		 			50 	PD1
	51 	PD4 (UARTRX1)	 	52 	PF6
	53 	PD3 (UARTTX1)	 	54 	PF4
	55 	PD5			 		56 	audio out
	57 	PD6			 		58 	Vdd
	59 	Vss 				60 	pin1 and 7 on mc34

	cart eeprom is i2c, bit banged on PD1/PD2 as SCL/SDA

*/


#define PIN_NO_C5				(8 * 2 + 5)
#define PIN_NO_C6				(8 * 2 + 6)
#define PIN_NO_C7				(8 * 2 + 7)
#define PIN_NO_D0				(8 * 3 + 0)
#define PIN_NO_D1				(8 * 3 + 1)
#define PIN_NO_D2				(8 * 3 + 2)
#define PIN_NO_D3				(8 * 3 + 3)
#define PIN_NO_D4				(8 * 3 + 4)
#define PIN_NO_D5				(8 * 3 + 5)
#define PIN_NO_D6				(8 * 3 + 6)
#define PIN_NO_E0				(8 * 4 + 0)
#define PIN_NO_E1				(8 * 4 + 1)

#define PIN_NO_F0				(8 * 5 + 0)	//coosole melody chip clock
#define PIN_NO_F1				(8 * 5 + 1)	//coosole melody chip status
#define PIN_NO_F2				(8 * 5 + 2)	//coosole melody chip data
#define PIN_NO_F6				(8 * 5 + 6)	//cart melody chip status

#define BIT_BANGED_BUS_DATA		{PIN_NO_D2, PIN_NO_D3, PIN_NO_D4, PIN_NO_D5, PIN_NO_D6, PIN_NO_C5, PIN_NO_C6, PIN_NO_C7}

#define SAVEGAME_ROM_SIZE		(128 * 1024)

#define BOARD_SRAM_BASE		0x44000000
#define BOARD_SRAM_SIZE		0x00020000

struct Device {
	
	struct Lh7xxxxAdc *adc;
	struct ArmRom *onboardRom;
	struct ArmRam *boardSram;		//on pixter board 64Kx16
	struct SocGpio *gpio;

	struct PixerBbBex *bbNor;
	struct ArmRom *savegameEeprom;

	struct SocI2c *cartBbI2C;

	struct AT25C02 *cartEEPROM;

	struct MelodyChip *cartMelodyChip, *consoleMelodyChip;
	struct MelodyChipXL *cartMelodyChipXL;
};

bool deviceHasGrafArea(void)
{
	return true;
}

enum RomChipType deviceGetRomMemType(void)
{
	return RomJedecFlashX16;
}

uint_fast8_t deviceGetSocRev(void)
{
	return 0;		//LH754xx
}

static int_fast16_t pixterPrvReadGpio(struct Device *dev, uint_fast8_t gpioNo)	//negative if pin is not outut
{
	switch (socGpioGetState(dev->gpio, gpioNo)) {
		case SocGpioStateLow:		return 0;
		case SocGpioStateHigh:		return 1;
		default:					return -1;
	}
}

static void devPrvCartMelodyControlChange(void* userData, uint32_t gpio, bool oldPinState, bool newPinState)
{
	struct Device *dev = (struct Device*)userData;

	if (dev->cartMelodyChipXL)
		melodyChipXLcontrolGpioStateChange(dev->cartMelodyChipXL, pixterPrvReadGpio(dev, PIN_NO_D1) == 1, pixterPrvReadGpio(dev, PIN_NO_D0) == 1);
	else if (dev->cartMelodyChip)
		melodyChipControlGpioStateChange(dev->cartMelodyChip, pixterPrvReadGpio(dev, PIN_NO_D1) == 1, pixterPrvReadGpio(dev, PIN_NO_D0) == 1);
}

static void devPrvConsoleMelodyControlChange(void* userData, uint32_t gpio, bool oldPinState, bool newPinState)
{
	struct Device *dev = (struct Device*)userData;

	melodyChipControlGpioStateChange(dev->consoleMelodyChip, pixterPrvReadGpio(dev, PIN_NO_F2) == 1, pixterPrvReadGpio(dev, PIN_NO_F0) == 1);
}

struct Device* deviceSetup(struct SocPeriphs *sp)
{
	static const uint8_t busPins[] = BIT_BANGED_BUS_DATA;
	struct Device *dev;
	void *ramBuffer;
	
	dev = (struct Device*)calloc(1, sizeof(*dev));
	if (!dev)
		ERR("cannot alloc device");
	
	dev->adc = (struct Lh7xxxxAdc*)sp->adc;
	dev->cartMelodyChip = sp->cartMelodyChip;
	dev->cartMelodyChipXL = sp->cartMelodyChipXL;
	dev->consoleMelodyChip = sp->consoleMelodyChip;
	
	if (!sp->consoleRom || sp->consoleRom->romType != PixterRomColor)
		ERR("Console rom missing or wrong type");

	dev->onboardRom = romInitWithPixterCartRom(sp->mem, 0x40000000, 16 << 20, sp->consoleRom, 0, RomWriteError);
	if (!dev->onboardRom)
		ERR("Cannot init onboard rom");

	ramBuffer = (uint32_t*)malloc(BOARD_SRAM_SIZE);
	if (!ramBuffer)
		ERR("cannot alloc board SRAM space\n");

	dev->boardSram = ramInit(sp->mem, BOARD_SRAM_BASE, BOARD_SRAM_SIZE, ramBuffer);
	if (!dev->boardSram)
		ERR("Cannot init board SRAM");
	
	//PJ6 is claibration request button (Active high)
	socGpioSetState(sp->gpio, 9 * 8 + 6, false);

	//PG6 must be high if we have a cartridge
	socGpioSetState(sp->gpio, 6 * 8 + 6, true);

	//PC7 shoudl be high so we pass check at ROM:40041E6A
	socGpioSetState(sp->gpio, 2 * 8 + 7, true);

	//PE6 should be low since we emulate the 18MHz crystal option and not the 17.7456 MHz crystel
	socGpioSetState(sp->gpio, 4 * 8 + 6, true);

	dev->gpio = sp->gpio;

	lh7xxxxAdcSetAuxAdc(dev->adc, 8, 6000 * 0.24);

	sp->bbBexExternalBus = pixterBbBexInit(sp->gpio, PIN_NO_E0, PIN_NO_E1, busPins);
	dev->savegameEeprom = romInitWithFILE(pixterBbBexGetBbBus(sp->bbBexExternalBus), BB_BUS_INTERNAL_NOR_ADDR, SAVEGAME_ROM_SIZE, sp->eepromFile, SAVEGAME_ROM_SIZE, RomPixterFlashX8);
	romTuneA(dev->savegameEeprom, 0x7fff, 1024, 0);

	//color game i2c mem
	dev->cartBbI2C = socI2cInit(NULL, NULL, NULL, 0, 0);
	if (!dev->cartBbI2C) {

		fprintf(stderr, "failed to add BB I2C master\n");
		abort();
	}
	socI2cGpioDeviceConfigPins(dev->cartBbI2C, sp->gpio, PIN_NO_D2, PIN_NO_D1);

	//color game bit-banged i2c eeprom
	dev->cartEEPROM = at24c02init(dev->cartBbI2C, 0x50, sp->savegameFile);
	if (!dev->cartEEPROM) {

		fprintf(stderr, "failed to add BB I2C EEPROM\n");
		abort();
	}

	//in-cart melody chip for color carts (sp->melodyChip is only non-NULL if we need to set it up)
	socGpioSetNotif(dev->gpio, PIN_NO_D0, devPrvCartMelodyControlChange, dev);		//D0 = cart melody chip clock
	socGpioSetNotif(dev->gpio, PIN_NO_D1, devPrvCartMelodyControlChange, dev);		//D1 = cart melody chip data

	//in-console melody chip for color carts
	socGpioSetNotif(dev->gpio, PIN_NO_F0, devPrvConsoleMelodyControlChange, dev);		//F0 = cart melody chip clock
	socGpioSetNotif(dev->gpio, PIN_NO_F2, devPrvConsoleMelodyControlChange, dev);		//F2 = cart melody chip data



	return dev;
}

void devicePeriodic(struct Device *dev)
{
	romPeriodic(dev->savegameEeprom);
}

void deviceReportMelodyChipActive(struct Device *dev, bool inConsoleChip, bool active)
{
	if (inConsoleChip)
		socGpioSetState(dev->gpio, PIN_NO_F1, active);
	else
		socGpioSetState(dev->gpio, PIN_NO_F6, active);
}

void deviceTouch(struct Device *dev, int x, int y)
{
	lh7xxxxAdcSetPenPos(dev->adc, y >= 0 ? 122 + y * 39 / 8 : y, x >= 0 ? 0x3c2 - x * 94 / 16 : x);
}

void deviceKey(struct Device *dev, uint32_t key, bool down)
{
	if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

		//PJ6 is claibration request button (Active high)
		fprintf(stderr, "calibrate button %s\n", down ? "PRESS" : "RELEASE");
		socGpioSetState(dev->gpio, 9 * 8 + 6, down);
	}
}



