//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr
#include "mmiodev_PixterGpioExpander.h"
#include "i2s_spidev_TLV320DAC26.h"
#include "gpiodev_PixterBbBex.h"
#include "gpiodev_MasterI2C.h"
#include "mmiodev_GPIONAND.h"
#include "vBitbangedSPI.h"
#include "i2cdev_24C02.h"
#include "lh7xxxx_ADC.h"
#include "SDL2/SDL.h"
#include "soc_GPIO.h"
#include "device.h"
#include "util.h"
#include "ROM.h"
#include "RAM.h"

/*

	PIXTER MULTIMEDIA
	160x160	16bpp LCD
	LH79524
	16KB SRAM in SoC
	4MB RAM on board

	alegedly:

	*1 		A0 						*2 		D0
	*3 		A1 						*4 		D1
	*5 		A2 						*6 		D2
	*7 		A3 						*8 		D3
	*9 		A4 						*10 	D4
	*11 	A5 						*12 	D5
	*13 	A6 						*14 	D6
	*15 	A7 						*16 	D7
	*17 	A8 						*18 	D8
	*19 	A9 						*20 	D9
	*21 	A10 					*22 	D10
	*23 	A11 					*24 	D11
	*25 	A12 					*26 	D12
	*27 	A13 					*28 	D13
	*29 	A14 					*30 	D14
	*31 	A15 					*32 	D15
	*33 	A16 (PC0)				*34 	PA5
	*35 	A17 (PC1)				*36 	PA4
	*37 	A18 (PC2)				+38 	nCS2
	*39 	A19 (PC3)				+40 	nCS3
	*41 	A20 (PC4)				*42 	nOE
	*43 	A21 (PC5)				*44 	nWE
	*45 	A22 (PC6)				*46 	??? pulled up or tied high
	*47 	A23 (PC7)				*48 	PA0/INT3/UARTRX2
	+49 	PA3 					*50 	PA1/INT2/UARTTX2
	*51 	PB6/UARTRX0/INT0 		+52 	PA2
	*53 	PB7/UARTTX0/INT1 		54 		PH3 (external bbBus reset)
	*55 	PB0/nDACK/nUARTCTSO 	*56 	audio out
	*57 	PB1/DREQ/nUARTRTS0 		*58 	Vdd
	*59 	Vss 					60 		???? 

	cart eeprom is i2c, bit banged on PA1/PA3 as SCL/SDA


	cp15 regs: 
		c0 = 41807203
		c1 = 0000027f
		c2 = 60007fff

	normal memory map maps (V -> P)
		0x00000000 + 0x02000000	-> 0x20000000
		0x20000000 + 0x02000000 -> 0x20000000
		0x40000000 + 0x01000000 -> 0x40000000	//nothing here
		0x44000000 + 0x18000000	-> 0x44000000	//nCS1 is boot rom, nCS2 is cart nCS3 is cart second chip select
		0x60000000 + 0x00100000 -> 0x60000000
		0xfff00000 + 0x00100000 -> 0xfff00000
	
	framebuffer is at 0x000b60b0, LCD, 8bpp mode

	Buttons are all active-high.
		up - J7
		down - J6
		left - J5
		right - J4
		A - G1
		B - G0
		calibrate - H7

	boot controller regs: 0 0 f


	NAND is wired as:
		nCS3 8-bit iface
		R/nB	= PB1
		CLE		= PB6
		ALE		= PB7

	code assumes 32 pages per block, seemingly 1024+16B wrie, 512B read ... two planes?
	B1 needs to idle high else nand code will wait forever, otherwise all of data bus need pull ups so nand ID code reads 0xFFFF

	NAND cart seems to: wire Vdd(58) and PA2(52), data pins go to DQ0..DQ7 nCS3, nOE, and nWE are wired as expected, CLE and ALE too
	
	ADC AN9 = batteries * ??? (TBD)


	there are TWO bit banged busses. same funcs access both, whic is used is determined by word_74AC8. If nonzero,
		bb_E0 is PH0, bb_E1 is PH1 bb_DQ0..bb_DQ7 are PI0..PI7
	if zero:
		bit 0 -> PA3
		bit 1 -> PB7
		bit 2 -> PB6
		bit 3 -> PB0
		bit 4 -> PB1
		bit 5 -> PC4
		bit 6 -> PC5
		bit 7 -> PC6
		bb_E0 -> PA4
		bb_E1 -> PA5

		use of lower values of PC than in pixter color is supicious but code looks so


*/



#define PIN_NO_A0				(8 * 0 + 0)
#define PIN_NO_A1				(8 * 0 + 1)
#define PIN_NO_A2				(8 * 0 + 2)		//in-cart melody chip status
#define PIN_NO_A3				(8 * 0 + 3)
#define PIN_NO_A4				(8 * 0 + 4)
#define PIN_NO_A5				(8 * 0 + 5)
#define PIN_NO_A6				(8 * 0 + 6)
#define PIN_NO_A7				(8 * 0 + 7)
#define PIN_NO_B0				(8 * 1 + 0)
#define PIN_NO_B1				(8 * 1 + 1)
#define PIN_NO_B6				(8 * 1 + 6)
#define PIN_NO_B7				(8 * 1 + 7)
#define PIN_NO_C4				(8 * 2 + 4)
#define PIN_NO_C5				(8 * 2 + 5)
#define PIN_NO_C6				(8 * 2 + 6)
#define PIN_NO_C7				(8 * 2 + 7)
#define PIN_NO_H0				(8 * 7 + 0)
#define PIN_NO_H1				(8 * 7 + 1)
#define PIN_NO_H2				(8 * 7 + 2)
#define PIN_NO_H3				(8 * 7 + 3)
#define PIN_NO_H5				(8 * 7 + 3)		//in-device melody chip status
#define PIN_NO_I0				(8 * 8 + 0)
#define PIN_NO_I1				(8 * 8 + 1)
#define PIN_NO_I2				(8 * 8 + 2)
#define PIN_NO_I3				(8 * 8 + 3)
#define PIN_NO_I4				(8 * 8 + 4)
#define PIN_NO_I5				(8 * 8 + 5)
#define PIN_NO_I6				(8 * 8 + 6)
#define PIN_NO_I7				(8 * 8 + 7)
#define PIN_NO_M4				(8 * 12 + 4)
#define PIN_NO_M7				(8 * 12 + 7)

#define PIN_NO_DAC_NCS			PIN_NO_M4
#define PIN_NO_DAC_MISO			PIN_NO_H2
#define PIN_NO_DAC_MOSI			PIN_NO_A6
#define PIN_NO_DAC_CLK			PIN_NO_A7






//EXT bus wiring is as per logi but it doe snot match what we exopect of the slot...
//as per color, bus to pin numbering for data pins 0..7 is: 49 53 51 55 57 43 45 47, e0=36 e1=34
//for us this would be: a3 b7 b6 b0 b1 c5 c6 c7, e0=a4 e1 = a5
//yet what we see in code is a3 b7 b6 b0 b1 c4 c5 c6
//what gives? is the slot miswired? are some address pins missing due ot them knowing no rom is over 4Mx16 (needing only A0..A21)?
#define BIT_BANGED_BUS_DATA_EXT	{PIN_NO_A3, PIN_NO_B7, PIN_NO_B6, PIN_NO_B0, PIN_NO_B1, PIN_NO_C4, PIN_NO_C5, PIN_NO_C6}
#define BIT_BANGED_BUS_DATA_INT	{PIN_NO_I0, PIN_NO_I1, PIN_NO_I2, PIN_NO_I3, PIN_NO_I4, PIN_NO_I5, PIN_NO_I6, PIN_NO_I7}

#define SAVEGAME_ROM_SIZE		(128 * 1024)





#define BOARD_SDRAM_BASE		0x20000000
#define BOARD_SDRAM_SIZE		0x00800000	//we fake 8 for convenience




struct Device {
	
	struct Lh7xxxxAdc *adc;
	struct ArmRom *onboardRom;
	struct ArmRam *boardSdram;	//4M onboard inpixter color
	struct SocGpio *gpio;

	struct PixterGpioExpander *pixterMultiGpioExpander;
	struct GPIONAND *cartNAND;

	//bit banged flash state
	struct PixerBbBex *bbBexExternalBus, *bbBexInternalBus;
	struct ArmRom *savegameEeprom;

	struct SocI2c *cartBbI2C;

	struct AT25C02 *cartEEPROM;
	
	struct VSPI *dacSPI;
	struct Tlv320dac26 *dac;

	struct MelodyChip *cartMelodyChip, *consoleMelodyChip;
	struct MelodyChipXL *cartMelodyChipXL;
};

bool deviceHasGrafArea(void)
{
	return true;
}

uint_fast8_t deviceGetSocRev(void)
{
	return 1;		//LH795xx
}

static int_fast16_t pixterPrvReadGpio(struct Device *dev, uint_fast8_t gpioNo)	//negative if pin is not outut
{
	switch (socGpioGetState(dev->gpio, gpioNo)) {
		case SocGpioStateLow:		return 0;
		case SocGpioStateHigh:		return 1;
		default:					return -1;
	}
}

static void devPrvMelodyControlChange(void* userData, uint32_t gpio, bool oldPinState, bool newPinState)
{
	struct Device *dev = (struct Device*)userData;

	if (socGpioGetState(dev->gpio, PIN_NO_M7) == SocGpioStateHigh)
		melodyChipControlGpioStateChange(dev->consoleMelodyChip, pixterPrvReadGpio(dev, PIN_NO_A1) == 1, pixterPrvReadGpio(dev, PIN_NO_A0) == 1);
	else if (dev->cartMelodyChipXL)
		melodyChipXLcontrolGpioStateChange(dev->cartMelodyChipXL, pixterPrvReadGpio(dev, PIN_NO_A1) == 1, pixterPrvReadGpio(dev, PIN_NO_A0) == 1);
	else if (dev->cartMelodyChip)
		melodyChipControlGpioStateChange(dev->cartMelodyChip, pixterPrvReadGpio(dev, PIN_NO_A1) == 1, pixterPrvReadGpio(dev, PIN_NO_A0) == 1);
}

static void pixterPrvDacBitsChanged(void* userData, uint32_t gpio, bool oldPinState, bool newPinState)
{
	struct Device *dev = (struct Device*)userData;
	int_fast16_t mosi, sck, ncs;

	mosi = pixterPrvReadGpio(dev, PIN_NO_DAC_MOSI);
	sck = pixterPrvReadGpio(dev, PIN_NO_DAC_CLK);
	ncs = pixterPrvReadGpio(dev, PIN_NO_DAC_NCS);

	if (mosi >= 0 && sck >= 0 && ncs >= 0) {

		bool miso;

		vspiPinsWritten(dev->dacSPI, mosi, sck, ncs);
		miso = vspiPinRead(dev->dacSPI) != PinLow;		//hiZ = high

		socGpioSetState(dev->gpio, PIN_NO_DAC_MISO, miso);
	}
}

struct Device* deviceSetup(struct SocPeriphs *sp)
{
	static const struct NandSpecs mMultiNandSpecs = {
		.bytesPerPage = 512 + 16,
		.blocksPerDevice = 4096,
		.pagesPerBlockLg2 = 5,
		.flags = NAND_FLAG_SAMSUNG_ADDRESSED_VIA_AREAS,
		.devIdLen = 2,
		.devId = {0x98, 0x76},		//romchecks for {EC,73}, weird.. noc arts do that
	};

	static const uint8_t busPinsInt[] = BIT_BANGED_BUS_DATA_INT;
	static const uint8_t busPinsExt[] = BIT_BANGED_BUS_DATA_EXT;
	struct Device *dev;
	void *ramBuffer;
	
	dev = (struct Device*)calloc(1, sizeof(*dev));
	if (!dev)
		ERR("cannot alloc device");
	
	dev->adc = (struct Lh7xxxxAdc*)sp->adc;
	dev->cartMelodyChip = sp->cartMelodyChip;
	dev->cartMelodyChipXL = sp->cartMelodyChipXL;
	dev->consoleMelodyChip = sp->consoleMelodyChip;

	if (!sp->consoleRom || sp->consoleRom->romType != PixterRomMultimedia)
		ERR("Console rom missing or wrong type");

	dev->onboardRom = romInitWithPixterCartRom(sp->mem, 0x44000000, 16 << 20, sp->consoleRom, 0, RomWriteError);
	if (!dev->onboardRom)
		ERR("Cannot init onboard rom");

	ramBuffer = (uint32_t*)malloc(BOARD_SDRAM_SIZE);
	if (!ramBuffer)
		ERR("cannot alloc board SRAM space\n");

	dev->boardSdram = ramInitEx(sp->mem, BOARD_SDRAM_BASE, BOARD_SDRAM_SIZE, 0x20000000, ramBuffer);
	if (!dev->boardSdram)
		ERR("Cannot init board SDRAM");
	
	dev->gpio = sp->gpio;

	lh7xxxxAdcSetAuxAdc(dev->adc, 9, 6000 * 0.27);
	
	//PH7 is claibration request button (Active high)
	socGpioSetState(sp->gpio, 7 * 8 + 7, false);

	//other buttons not pressed too
	socGpioSetState(sp->gpio, 9 * 8 + 7, false);
	socGpioSetState(sp->gpio, 9 * 8 + 6, false);
	socGpioSetState(sp->gpio, 9 * 8 + 5, false);
	socGpioSetState(sp->gpio, 9 * 8 + 4, false);
	socGpioSetState(sp->gpio, 6 * 8 + 1, false);
	socGpioSetState(sp->gpio, 6 * 8 + 0, false);


	//B1 needs to idle high else nand code will wait forever, otherwise all of data bus need pull ups so nand ID code reads 0xFFFF
	socGpioSetState(sp->gpio, 1 * 8 + 1, true);

	//TWO busse sin this one, based on bank being used somehow...
	//TWO busses in this one, based on bank being used somehow...
	dev->bbBexExternalBus = pixterBbBexInit(sp->gpio, PIN_NO_A4, PIN_NO_A5, busPinsExt);
	dev->bbBexInternalBus = pixterBbBexInit(sp->gpio, PIN_NO_H0, PIN_NO_H1, busPinsInt);

	dev->savegameEeprom = romInitWithFILE(pixterBbBexGetBbBus(dev->bbBexInternalBus), BB_BUS_INTERNAL_NOR_ADDR, SAVEGAME_ROM_SIZE, sp->eepromFile, SAVEGAME_ROM_SIZE, RomPixterFlashX8);
	romTuneA(dev->savegameEeprom, 0x7fff, 1024, 0);

	sp->bbBexExternalBus = dev->bbBexExternalBus;

	//color game i2c mem
	dev->cartBbI2C = socI2cInit(NULL, NULL, NULL, 0, 0);
	if (!dev->cartBbI2C) {

		fprintf(stderr, "failed to add BB I2C master\n");
		abort();
	}
	socI2cGpioDeviceConfigPins(dev->cartBbI2C, sp->gpio, PIN_NO_A3, PIN_NO_A1);

	dev->pixterMultiGpioExpander = gpioExpanderInit(sp->mem);

	//multimedia game nand
	dev->cartNAND = gpioNandInit(sp->mem, 0x4c000000, sp->gpio, PIN_NO_B1, PIN_NO_B6, PIN_NO_B7, &mMultiNandSpecs, sp->cartRom ? sp->cartRom->code[0] : NULL);

	//color game bit-banged i2c eeprom
	dev->cartEEPROM = at24c02init(dev->cartBbI2C, 0x50, sp->savegameFile);
	if (!dev->cartEEPROM) {

		fprintf(stderr, "failed to add BB I2C EEPROM\n");
		abort();
	}

	//in-cart melody chip for color carts (sp->melodyChip is only non-NULL if we need to set it up)
	socGpioSetNotif(dev->gpio, PIN_NO_A0, devPrvMelodyControlChange, dev);		//A0 = melody chip clock
	socGpioSetNotif(dev->gpio, PIN_NO_A1, devPrvMelodyControlChange, dev);		//A1 = melody chip data


	dev->dacSPI = vspiInit("DACSPI", SpiMode1, true);
	if (!dev->dacSPI) {
		fprintf(stderr, "cannot init pixter DAC's SPI\n");
		abort();
	}

	dev->dac = tlv320dac26initWithVSPI(dev->dacSPI, sp->i2s);
	if (!dev->dac) {
		fprintf(stderr, "cannot init pixter DAC\n");
		abort();
	}
	socGpioSetNotif(sp->gpio, PIN_NO_DAC_NCS, pixterPrvDacBitsChanged, dev);
	socGpioSetNotif(sp->gpio, PIN_NO_DAC_MISO, pixterPrvDacBitsChanged, dev);
	socGpioSetNotif(sp->gpio, PIN_NO_DAC_MOSI, pixterPrvDacBitsChanged, dev);
	socGpioSetNotif(sp->gpio, PIN_NO_DAC_CLK, pixterPrvDacBitsChanged, dev);
	tlv320dac26setMClk(dev->dac, 11289600);


	return dev;
}

void devicePeriodic(struct Device *dev)
{
	if (dev->cartNAND)
		gpioNandPeriodic(dev->cartNAND);
	
	romPeriodic(dev->savegameEeprom);
}

void deviceReportMelodyChipActive(struct Device *dev, bool inConsoleChip, bool active)
{
	if (inConsoleChip)
		socGpioSetState(dev->gpio, PIN_NO_H5, active);
	else
		socGpioSetState(dev->gpio, PIN_NO_A2, active);
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

		//PH7 is claibration request button (Active high)
		socGpioSetState(dev->gpio, 7 * 8 + 7, down);
	}
	else if (key == SDLK_UP)	//up = J7, active high
		socGpioSetState(dev->gpio, 9 * 8 + 7, down);
	else if (key == SDLK_DOWN)	//down = J6, active high
		socGpioSetState(dev->gpio, 9 * 8 + 6, down);
	else if (key == SDLK_LEFT)	//left = J5, active high
		socGpioSetState(dev->gpio, 9 * 8 + 5, down);
	else if (key == SDLK_RIGHT)	//left = J4, active high
		socGpioSetState(dev->gpio, 9 * 8 + 4, down);
	else if (key == SDLK_a)	//a = G1, active high
		socGpioSetState(dev->gpio, 6 * 8 + 1, down);
	else if (key == SDLK_b)	//b = G0, active high
		socGpioSetState(dev->gpio, 6 * 8 + 0, down);
}



