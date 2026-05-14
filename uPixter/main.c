#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include "SDL2/SDL.h"
#include "almostSST39SF010A.h"
#include "SDL2/SDL_audio.h"
#include "gpioMasterI2C.h"
#include "i2cdev_24C02.h"
#include "at29lv010a.h"
#include "melodyChip.h"
#include "6502.h"
#include "rom.h"

#ifdef UDP_IR

	#define PORT		19180	//if we cannot lsiten on this we assume we are instance #2 and we'll SEND there, this logic will need to get fancier if two machines are ever used, that is left as an exercise for the readers

	#include <sys/socket.h>
	#include <sys/types.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>

#endif


//NOTE: playing music is not necessary, just taking the time to pretend it, even that is only approximately necessary :D
//simpler hosts can skip it, games do not depends on background music, sound effects will still play


#if defined(PIXTER_2_0)

	#if defined(PIXTER_CLASSIC) || defined(PIXTER_PLUS)
		#error " pick only one target!"
	#endif
	#define RAM_END_ADDR		0x1000
	#define FCLK				5480000
	#define PIN_SCK				(8 * 3 + 3)	//D3
	#define PIN_SDA				(8 * 3 + 7)	//D7
	#define FIRST_I2C_MEMORY	0x50

#elif defined(PIXTER_PLUS)

	#if defined(PIXTER_CLASSIC) || defined(PIXTER_2_0)
		#error " pick only one target!"
	#endif
	#define RAM_END_ADDR		0x1000
	#define FCLK				6000000

#elif defined(PIXTER_CLASSIC)

	#if defined(PIXTER_PLUS) || defined(PIXTER_2_0)
		#error " pick only one target!"
	#endif
	#define RAM_END_ADDR		0x800
	#define FCLK				6000000
	#define PIN_SCK				(8 * 2 + 5)	//C5
	#define PIN_SDA				(8 * 3 + 7)	//D7
	#define FIRST_I2C_MEMORY	0x50

#else

	#error " pick a target!"

#endif





static struct Rom *mCartRom, *mConsoleRom;
static struct MelodyChip *mCartMelodyChip, *mConsoleMelodyChip;
static uint8_t mUnusedRom[0x4000];
static uint8_t *mRomL, *mRomH, *mRomC000;
static uint8_t mIO[0x40], mRAM[RAM_END_ADDR - sizeof(mIO)], mTmr0WriTmp, mTmr1WriTmp;	//registers keep current timer vale, reload value stored in mTmrXreload
static struct CPU mCPU;
static uint8_t mIrqMask, mIrqsActive, mCartBexID, mInternalFlashBexID;
static uint16_t mTmr0reload, mTmr1reload, mUartBrgCounter, mUartBRG;
static int16_t mPenX = -1, mPenY = -1, mCurUartRxByte = -1, mNextUartRxByte = -1, mNextUartTxByte = -1;
static bool mCalibBtnPressed, mHaveCart, mSdaPulledLow, mTimer1overflowToggle;
static SDL_AudioDeviceID mAudioDevEffects, mAudioDevCartMelody, mAudioDevConsoleMelody;	//no existing game uses in-console and in-cart melody at once, but this DOES work so i emulate it
static struct GpioI2C *mBbI2C;
static struct AT25C02 *mMemories[4];
static FILE *mEepromFile, *mFlashFile;
static struct AT29LV010A *mCartFlash;
static struct AlmostSST39SF010A *mInternalFlash;





#define TOUCH_WIDTH			80
#define DISP_HEIGHT			80
#define BUTTONS_HEIGHT		6
#define TOUCH_HEIGHT		(DISP_HEIGHT + BUTTONS_HEIGHT)
#define RAM_BASE			(sizeof(mIO))

#define ADDR_ROM_MAP_PG_L			0x4000
#define ADDR_ROM_MAP_PG_H			0x8000
#define ADDR_ROM_CONST				0xc000
#define ADDR_ROM_PG_SZ				0x4000

#define CART_SPECIAL_MEM_BUS_ADDR	0x2000u		//at srat of file
#define CART_SPECIAL_MEM_SIZE		0x2000u

//pen coords are 0..65535
static bool prvTouchPlateComparator(bool myPlusDriven, bool myMinusDriven, bool otherPlusDriven, bool otherMinusDriven, bool isPenDown, uint16_t penAlongMyAxis, uint16_t alongOtherAxis)
{
	#define FAKE_ADX_MAX	0xa0
	#define FAKE_ADC_MIN	0x02

	//the "ADC" is a R-2R dac and a comparator. This function repoies with true of false to indicate compartor output. This is done by decing on an
	// analog value 0...255 the input to the comparator has, and then properly comparing to DAC pins's states. The analog value we decide on is "ret"
	// and it is bracketed by FAKE_ADC_MIN and FAKE_ADX_MAX. Input pen coords are 0..65536 for the entire range. "penAlong_X_axis" is 0 at minus end
	//of the plate and 65535 at plus end.
	uint8_t ret;

	//if neither of the other plate's ends is driven, the pen being pressed has no effect, so treat it like it is not
	if (!otherPlusDriven && !otherMinusDriven)
		isPenDown = false;

	if (myPlusDriven) {

		//if we're driving our sample point, we'll see max input. other things do not matter as the resistance is higher in all other paths
		ret = FAKE_ADX_MAX;
	}
	else if (!isPenDown && myMinusDriven) {

		//if minus is driven, plus is not, and pen is not down, we'll see minimum value
		ret = FAKE_ADC_MIN;
	}
	else if (!isPenDown) {

		//if neither my plus nor my minus are driven, and there is no pen touch, our input will be floating. both plates have very weak pull downs when totally floating

		ret = FAKE_ADC_MIN + 1;
	}
	else {	//cases when pen is down (we know at least one side of the other plate is driven)
		
		uint16_t otherPlatePartial = 0x8000;	//this value to shut up th eocmpiler, it is never used due to check above at func start
		uint16_t partial;

		if (otherPlusDriven && otherMinusDriven)
			otherPlatePartial = alongOtherAxis;
		else if (otherPlusDriven)
			otherPlatePartial = 0xffff;
		else if (otherMinusDriven)
			otherPlatePartial = 0x0000;

		//we assume that the touch creates a connection of 0 ohms
		//so if we're fighting our own driven minus, it is proportional
		if (myMinusDriven)
			partial = (penAlongMyAxis * 0 + (65536 - penAlongMyAxis) * otherPlatePartial) / 65536;
		else
			partial = otherPlatePartial;

		//we now know the value, scale it to our range
		ret = partial * (FAKE_ADX_MAX - FAKE_ADC_MIN) / 65536 + FAKE_ADC_MIN;
	}

	return ret <= mIO[0x08];
}

static uint8_t prvPortReadEx(uint_fast8_t portIdx, uint8_t dir, uint8_t attrib, uint8_t latch)
{
	uint8_t outputHi = dir & latch;
	uint8_t outputLo = dir & (~latch) & (~attrib);
	uint8_t pullHi = (~dir) & (~attrib) & latch;
	uint8_t pullLo = (~dir) & (~attrib) & (~latch);
	//in other cases it is floating, note that output vals canot conflict

	(void)pullLo;

	uint8_t outVal = 0, outSure = 0, extStimuliMask = 0, extStimuliVal = 0;

	//we assume our buffers are infinitely strong
	outVal |= outputHi;
	outSure |= outputHi | outputLo;

	//apply external stimuli
	switch (portIdx) {
		case 0:	//port a: R-2R ladder for fake adc - all output - no known external stimuli
			break;

		case 1:	//port b: external bus - no external stimuli
			break;

		case 2:	//port c: 0x80 = connector pin PD1, we make it look low
			extStimuliMask |= 0x80;

			//C4 can be overriden to output PWM waveform, VM opcode 0x7A uses this to measure CPU speed
			//this overrides all other functions of the pin including direction control
			if ((mIO[0x16] & 0xE0) == 0xE0) {

				outSure |= 0x10;
				if (mTimer1overflowToggle)
					outVal |= 0x10;
				else
					outVal &=~ 0x10;
			}
			
			break;

		case 3:	//port d: 0x01 - touch Y plate, 0x02 - touch X plate
			
			#ifdef PIXTER_2_0
					
				/*
					we expect two pins called Q W E R, where Q is acive high to energize X-, W is acive-high to energize Y-, E is active-low to energize X+ and R is active-low to energize Y+
						we thus expect:
							Z measurement samples X, no X plates driven Y+ driven, Y- not driven		Q=0 W=0 E=1 R=0,	PORTC is seen as 0xF0
							X-measurement samples Y plate, no Y paltes driven X+ driven, X- driven		Q=1 W=0 E=0 R=1,	PORTC is seen as 0xF9
							Y-measurement samples X plate, no X paltes driven Y+ driven, Y- driven		Q=0 W=1 E=1 R=0,	PORTC is seen as 0xF4

						thus
							W = 0x04		definitely
							Q|R = 0x09		definitely, we randomly assign one to each
							E = !R			guess since nothing else makes sense. must be an inverter onboard
						thereis much less data to guess based on here sinc eunlike classic, in 2.0 there is no calibration to undo the "inidividuality" of the BJTs
				*/

				//0x01 - touch Y plate comparator
				extStimuliMask |= 0x01;
				if (prvTouchPlateComparator(!(mIO[0x0B] & 0x08), !!(mIO[0x0B] & 0x04), !!(mIO[0x0B] & 0x08), !!(mIO[0x0B] & 0x01), (mPenX >=0 && mPenY >= 0), (TOUCH_HEIGHT - 1 - mPenY) * 65536 / TOUCH_HEIGHT, mPenX * 65536 / TOUCH_WIDTH))
					extStimuliVal |= 0x01;

				//0x02 - touch X plate comparator
				extStimuliMask |= 0x02;
				if (prvTouchPlateComparator(!!(mIO[0x0B] & 0x08), !!(mIO[0x0B] & 0x01), !(mIO[0x0B] & 0x08), !!(mIO[0x0B] & 0x04), (mPenX >=0 && mPenY >= 0), mPenX * 65536 / TOUCH_WIDTH, (TOUCH_HEIGHT - 1 - mPenY) * 65536 / TOUCH_HEIGHT))
					extStimuliVal |= 0x02;

			#else	//plus and classic are same here

				//0x01 - touch Y plate comparator
				extStimuliMask |= 0x01;
				if (prvTouchPlateComparator(!(mIO[0x0B] & 0x10), !!(mIO[0x0B] & 0x01), !(mIO[0x0B] & 0x08), !!(mIO[0x0B] & 0x04), (mPenX >=0 && mPenY >= 0), (TOUCH_HEIGHT - 1 - mPenY) * 65536 / TOUCH_HEIGHT, mPenX * 65536 / TOUCH_WIDTH))
					extStimuliVal |= 0x01;

				//0x02 - touch X plate comparator
				extStimuliMask |= 0x02;
				if (prvTouchPlateComparator(!(mIO[0x0B] & 0x08), !!(mIO[0x0B] & 0x04), !(mIO[0x0B] & 0x10), !!(mIO[0x0B] & 0x01), (mPenX >=0 && mPenY >= 0), mPenX * 65536 / TOUCH_WIDTH, (TOUCH_HEIGHT - 1 - mPenY) * 65536 / TOUCH_HEIGHT))
					extStimuliVal |= 0x02;

			#endif

			//0x10 - calibrate button
			if (mCalibBtnPressed)
				extStimuliVal |= 0x10;
			extStimuliMask |= 0x10;

			//0x20 - cart requested reset
			extStimuliVal |= 0x20;
			extStimuliMask |= 0x20;

			//0x40 - PF6
			extStimuliMask |= 0x40;
			if (mCartMelodyChip && melodyChipIsPlaying(mCartMelodyChip))
				extStimuliVal |= 0x40;

			//there is *NO* "is playing" signal for in-console melody chip, as far as i can tell
			//there *IS* a pin on th emelody blob that goes low when a command is issued and goes up either at end
			//of command (if no such melody) or at end of melody) when done, but it seems to not be wired to the SoC
			//this isgnal can be sampled on the side of R42 closer to the melody blob in pixter classic

			//0x80 - i2C SDA
			if (mSdaPulledLow) {

				extStimuliVal &=~ 0x80;
				extStimuliMask |= 0x80;
			}
			break;
	}

	outVal |= (extStimuliVal & extStimuliMask) &~ outSure;
	outSure |= extStimuliMask;

	//apply pulls, assume idle pins are low so pull low does nothing
	outVal |= pullHi &~ outSure;

	return outVal;
}

static uint8_t portRead(uint_fast8_t portIdx)
{
	static const uint8_t dirRegs[] = {0x07, 0x29, 0x2B, 0x2D};
	static const uint8_t attrRegs[] = {0x06, 0x28, 0x2A, 0x2C};
	static const uint8_t latchRegs[] = {0x08, 0x0A, 0x0B, 0x09};

	return prvPortReadEx(portIdx, mIO[dirRegs[portIdx]], mIO[attrRegs[portIdx]], mIO[latchRegs[portIdx]]);
}

static void prvIrqsUpdate(void)
{
	cpuIrq(&mCPU, mIrqMask & mIrqsActive);
}

static void prvMemMapUpdate(void)
{
	uint8_t *cartRomBase = mCartRom ? mCartRom->code[0]->data : mUnusedRom;
	uint32_t cartRomSize = mCartRom ? mCartRom->code[0]->length : sizeof(mUnusedRom);

	uint8_t *consoleRomBase = mConsoleRom ? mConsoleRom->code[0]->data : mUnusedRom;
	uint32_t consoleRomSize = mConsoleRom ? mConsoleRom->code[0]->length : sizeof(mUnusedRom);

	if (mInternalFlash && mIO[0x0d] == mInternalFlashBexID)
		almostSst39sf010aBankSel(mInternalFlash, mIO[0x00]);

	if (mCartFlash && mIO[0x0d] == mCartBexID)
		at29lv010aBankSel(mCartFlash, mIO[0x00]);

	//bases
	mRomH = (mIO[0x00] & 0x80) ? ((mIO[0x00] & 0x20) ? mUnusedRom : cartRomBase) : consoleRomBase;
	mRomL = (mIO[0x00] & 0x80) ? ((mIO[0x00] & 0x20) ? mUnusedRom : cartRomBase) : consoleRomBase;
	mRomC000 = (mIO[0x0b] & 0x02) ? cartRomBase : consoleRomBase;		//this is verified on real hardware. weird thing is that it goes out on the bus as the full address, top bit and all. no carts use this but TBD what GPBA01 will do about it

	//disable external iface if off
	if (!(mIO[0x03] & 0x80)) {
		if (mRomH == cartRomBase)
			mRomH = mUnusedRom;
		if (mRomL == cartRomBase)
			mRomL = mUnusedRom;
		if (mRomC000 == cartRomBase)
			mRomC000 = mUnusedRom;

		if (mCartFlash)
			at29lv010aBankSel(mCartFlash, 0);
		if (mInternalFlash)
			almostSst39sf010aBankSel(mInternalFlash, 0);
	}

	//offsets
	if (mRomH != mUnusedRom)
		mRomH += ((mIO[0x00] & 0x3f) * 2 + 0) * ADDR_ROM_PG_SZ;
	if (mRomL != mUnusedRom)
		mRomL += ((mIO[0x00] & 0x3f) * 2 + 1) * ADDR_ROM_PG_SZ;
	if (mRomC000 != mUnusedRom)
		mRomC000 += ADDR_ROM_PG_SZ;
}

static uint8_t ioR(uint8_t addr)
{
	uint8_t ret = mIO[addr];

	switch (addr) {
		case 0x00:	//BANKSEL
		case 0x04:	//CPU_Clk_Ctrl
		case 0x05:	//Port_A_Strobe_Pull
		case 0x06:	//Port_A_Attribution
		case 0x07:	//Port_A_Direction
		case 0x28:	//Port_B_Attribution
		case 0x29:	//Port_B_Direction
		case 0x2B:	//Port_C_Attribution
		case 0x2A:	//Port_C_Direction
		case 0x2D:	//Port_D_Attribution
		case 0x2C:	//Port_D_Direction
		case 0x27:	//Port_CD_Config
		case 0x12:	//Ch0_PWM_DAC_Ctrl
		case 0x13:	//Ch0_PWM_DAC_Output 
		case 0x16:	//Ch1_PWM_DAC_Ctrl
		case 0x17:	//Ch1_PWM_DAC_Output 
		case 0x0C:	//TimeBaseSelect
		case 0x0E:	//32768_EN
		case 0x0F:	//TimerCtrl
		case 0x10:	//TM0LowByte
		case 0x11:	//TM0HighByte
		case 0x14:	//TM1LowByte
		case 0x15:	//TM1HighByte
		case 0x03:	//BEXMExt
		case 0x0D:	//BMIVolumeID
		case 0x18:	//Wakeup_Ctrl
			ret = mIO[addr];
			break;

//2.0 only
#ifdef PIXTER_2_0

		case 0x1b:
			ret = mCurUartRxByte >= 0 ? mCurUartRxByte : 0x00;
			mCurUartRxByte = -1;
			mIO[0x1a] &=~ 0x80;
			mIrqsActive &=~ 0x80;
			prvIrqsUpdate();
			break;

		case 0x1a:
		case 0x1c:
		case 0x1d:
		case 0x1e:
		case 0x1f:
			ret = mIO[addr];
			break;
#endif


//things we do not understand
		case 0x20:	//BEXPort0
		case 0x21:	//BEXPort1
		case 0x22:	//BEXPort2
		case 0x23:	//BEXConfig
		case 0x32:	//???
			ret = mIO[addr];
			break;

		case 0x08:	//Port_A_Data
			ret = portRead(0);
			break;

		case 0x0A:	//Port_B_Data
			ret = portRead(1);
			break;

		case 0x0B:	//Port_C_Data
			ret = portRead(2);
			break;

		case 0x09:	//Port_D_Data
			ret = portRead(3);
			break;

		case 0x01:	//Interrupt_Ctrl
			ret = mIrqsActive & mIrqMask;		//i think it only reads irqs that are unmasked
			break;

		case 0x02:	//Interrupt_Clear
			ret = 0;
			break;

		default:
			fprintf(stderr, "uknown mIO read 0x%02x\n", addr);
			abort();
			break;
	}

	//fprintf(stderr, "IOe {0x%02X} -> 0x%02x\n", addr, ret);


	return ret;
}

static void prvRecalcUartRate(void)
{
	uint32_t desiredRate = 0x1000 * mIO[0x1f] + 0x10 * mIO[0x1e];
	uint32_t correctDiv = (FCLK + desiredRate / 2) / desiredRate;

	switch (mIO[0x19] & 3) {
		case 0b01:		//32KHz / divider
		case 0b11:		//Fosc / divider

			mUartBRG = 0x100 * mIO[0x1d] + mIO[0x1c];
			if (mUartBRG < 1)	//yes, divider is value MINUS 1, this is verified on real hardware
				mUartBRG = 0;
			else
				mUartBRG--;
			break;

		case 0b00:		//Fosc / auto-calculted divider calibrated to 32KHz
			if (correctDiv < 16 || correctDiv >= 0x10000) {	//impossible
				mIO[0x1a] &=~ 0x01;
				mUartBRG = 0;
			}
			else {
				mUartBRG = correctDiv;
				mIO[0x1a] |= 0x01;
			}
			break;

		case 0b10:		//reserved value
			mUartBRG = 0;
			break;
	}
}

static void gpioChangesForInternalMelodyChip(uint_fast8_t newVal, uint_fast8_t oldVal, uint_fast8_t dataMask, uint_fast8_t clockMask)
{
	uint_fast8_t changed = (newVal ^ oldVal) & (dataMask | clockMask);	//pins we care about

	if (changed && mConsoleMelodyChip)
		melodyChipControlGpioStateChange(mConsoleMelodyChip, !!(newVal & dataMask), !!(newVal & clockMask));
}

static uint8_t ioW(uint8_t addr, uint8_t val)
{
	//fprintf(stderr, "IOw {0x%02X} <- 0x%02x\n", addr, val);

	switch (addr) {
		case 0x17:	//Ch1_PWM_DAC_Output 
			if (mAudioDevEffects) {

				if (SDL_QueueAudio(mAudioDevEffects, &val, 1))
					fprintf(stderr, "SDL audio q error: %s\n", SDL_GetError());
			}
			mIO[addr] = val;
			break;
		
		//in-pixter melody chip is on this port, I2c is too, so fallthrough chain is complex
		case 0x09:	//Port_D_Data
	#if defined(PIXTER_CLASSIC) || defined (PIXTER_PLUS)
			gpioChangesForInternalMelodyChip(val, mIO[addr], 0x04, 0x08);
	#endif
			//fallthrough

		//these ports affect i2c, so we reprocess i2c on their write
		case 0x2B:	//Port_C_Attribution
		case 0x2A:	//Port_C_Direction
		case 0x2D:	//Port_D_Attribution
		case 0x2C:	//Port_D_Direction
		case 0x27:	//Port_CD_Config
			if (mBbI2C) {

				gpioI2cPeriodic(mBbI2C);
			}
			//fallthrough

		case 0x3b:	//another output-only port. only usewd for enabling secondary BEX chip
		case 0x04:	//CPU_Clk_Ctrl
		case 0x05:	//Port A_Strobe_Pull
		case 0x06:	//Port_A_Attribution
		case 0x07:	//Port_A_Direction
		case 0x08:	//Port_A_Data / Port_A_Wakeup
		case 0x28:	//Port_B_Attribution
		case 0x29:	//Port_B_Direction
		case 0x0A:	//Port_B_Data
		case 0x12:	//Ch0_PWM_DAC_Ctrl
		case 0x13:	//Ch0_PWM_DAC_Output 
		case 0x16:	//Ch1_PWM_DAC_Ctrl
		case 0x0C:	//TimeBaseSelect
		case 0x0E:	//32768_EN
		case 0x0F:	//TimerCtrl
		case 0x18:	//Wakeup_Ctrl
			mIO[addr] = val;
			break;

		case 0x20:	//BEXPort0
			if ((mIO[0x23] >> 5) == 2) {	//GPIO config -- control of cart music
				
				uint_fast8_t prevState = mIO[addr];
				uint_fast8_t curState = val;
				uint_fast8_t changed = (prevState ^ curState) & 0x30;	//pins we care about

				if (changed && mCartMelodyChip)
					melodyChipControlGpioStateChange(mCartMelodyChip, !!(curState & 0x20), !!(curState & 0x10));
			}
			mIO[addr] = val;
			break;

		case 0x21:	//BEXPort1
		case 0x22:	//BEXPort2
		case 0x23:	//BEXConfig
		case 0x32:	//???
			mIO[addr] = val;
			break;

		//this port affect i2c, so we reprocess i2c on its write
		//on 2.0, this also affects i2c
		case 0x0B:	//Port_C_Data
	#ifdef PIXTER_2_0
			gpioChangesForInternalMelodyChip(val, mIO[addr], 0x04, 0x01);
	#endif
			if (mBbI2C) {

				gpioI2cPeriodic(mBbI2C);
			}
			//fallthrough

		//writes that also adjust memory map
		case 0x03:	//BEXMExt
		case 0x00:	//BANKSEL
			mIO[addr] = val;
			prvMemMapUpdate();
			break;


		case 0x0D:	//BMIVolumeID
			
			//initial write is asignment. in plus the bus is weird but internal flash is AFTER cart...go figure
			if (!mCartBexID && val) {

				fprintf(stderr, "BEX ID %u assigned to cart\n", val);
				mCartBexID = val;
			}
			#if defined(PIXTER_PLUS)
				if ((mIO[0x3b] & 4) && !mInternalFlashBexID && val) {

					fprintf(stderr, "BEX ID %u assigned to internal flash\n", val);
					mInternalFlashBexID = val;
				}
			#endif
			mIO[addr] = val;
			break;
			
		case 0x10:	//TM0LowByte
			mTmr0WriTmp = val;
			break;
			
		case 0x14:	//TM1LowByte
			mTmr1WriTmp = val;
			break;
			
		case 0x11:	//TM0HighByte
			mTmr0reload = (((uint16_t)val) << 8) + mTmr0WriTmp;
			mIO[0x10] = mTmr0WriTmp;
			mIO[0x11] = val;
			break;

		case 0x15:	//TM1HighByte
			mTmr1reload = (((uint16_t)val) << 8) + mTmr1WriTmp;
			mIO[0x14] = mTmr1WriTmp;
			mIO[0x15] = val;
			break;

		case 0x01:	//Interrupt_Ctrl
			mIrqMask = val;
			prvIrqsUpdate();
			break;

		case 0x02:	//Interrupt_Clear
			mIrqsActive &=~ val;
			prvIrqsUpdate();
			break;

//2.0 only
#ifdef PIXTER_2_0

		case 0x1a:
			//nothing
			break;

		case 0x1b:
			mNextUartTxByte = (uint16_t)(uint8_t)val;
			mIO[0x1a] &=~ 0x40;
			mIrqsActive &=~ 0x40;
			prvIrqsUpdate();
			break;

		case 0x19:
			if (val & 0x20) {
				val &=~ 0x20;	//reset done :)
				mIO[0x1a] &=~ 0x80;
				mIO[0x1a] |= 0x40;
				mIrqsActive &=~ 0xc0;
				prvIrqsUpdate();
			}
			//fallthrough

		case 0x1c:
		case 0x1d:
		case 0x1e:
		case 0x1f:
			mIO[addr] = val;
			prvRecalcUartRate();
			break;
#endif

		default:
			fprintf(stderr, "uknown mIO write 0x%02x\n", addr);
			abort();
			break;
	}
	return 0;
}

static void renderScreen(void)
{
	const uint32_t framebufferBase = 16 * mIO[0x05] % RAM_END_ADDR;
	const uint8_t *src = (mRAM + framebufferBase - sizeof(mIO) /* since addr is given as absolute and not relative to ram start */) - 1 /* to make our logic easier */;
	static SDL_Surface *mScreen = NULL;
	static SDL_Window *mWindow = NULL;
	uint32_t *dst, *dst2;
	SDL_Event event;
	uint32_t r, c, dispW = 16 * mIO[0x0e];


	if (!mIO[0x05]) {		//screen turned off

		if (mScreen)
			SDL_FreeSurface(mScreen);
		if (mWindow)
			SDL_DestroyWindow(mWindow);
		mScreen = NULL;
		mWindow = NULL;
		return;
	}

	if (!mWindow) {	//screen turned on
		
		mWindow = SDL_CreateWindow("uPixter", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, dispW * 2, TOUCH_HEIGHT * 2, 0);
		if (mWindow == NULL) {
			printf("Couldn't create window: %s\n", SDL_GetError());
			exit(-1);
		}
		
		mScreen = SDL_CreateRGBSurface(0, dispW * 2, DISP_HEIGHT * 2, 16, 0x00f800, 0x07e0, 0x001f, 0x0000);
		if (mScreen == NULL) {
			printf("Couldn't create screen surface: %s\n", SDL_GetError());
			exit(-1);
		}
	}

	SDL_LockSurface(mScreen);
	dst = (uint32_t*)mScreen->pixels;	//write 2 px at a time

	
	for (r = 0; r < DISP_HEIGHT; r++) {
		uint_fast8_t mask = 0;

		dst2 = dst + dispW;
		for (c = 0; c < dispW; c++, mask >>= 1) {
			if (!mask) {
				mask = 0x80;
				src++;
			}
			if (*src & mask) {

				*dst++ = 0x00000000;
				*dst2++ = 0x00000000;
			}
			else {

				*dst++ = 0xffffffff;
				*dst2++ = 0xffffffff;
			}
		}
		dst = dst2;
	}

	SDL_UnlockSurface(mScreen);
	dst = NULL;
	SDL_BlitSurface(mScreen, NULL, SDL_GetWindowSurface(mWindow), NULL);
	SDL_UpdateWindowSurface(mWindow);


	if (SDL_PollEvent(&event)) switch (event.type) {
		
		case SDL_QUIT:
			if (mEepromFile)
				fclose(mEepromFile);
			if (mFlashFile)
				fclose(mFlashFile);
			mFlashFile = NULL;
			mEepromFile = NULL;
			exit(0);
			break;
		
		case SDL_MOUSEBUTTONDOWN:
			if (event.button.button != SDL_BUTTON_LEFT)
				break;
			mPenX = event.button.x / 2;
			mPenY = event.button.y / 2;
			break;
		
		case SDL_MOUSEBUTTONUP:
			if (event.button.button != SDL_BUTTON_LEFT)
				break;
			mPenX = -1;
			mPenY = -1;
			break;
		
		case SDL_MOUSEMOTION:
			if (mPenX >=0 && mPenY >= 0) {	//motion is only a drag if pen is already down
				
				mPenX = event.motion.x / 2;
				mPenY = event.motion.y / 2;
			}
			break;
		
		case SDL_KEYDOWN:
			if (event.key.keysym.sym == SDLK_RETURN)
				mCalibBtnPressed = true;
			break;
		
		case SDL_KEYUP:
			if (event.key.keysym.sym == SDLK_RETURN)
				mCalibBtnPressed = false;
			break;
	}
}

uint8_t memR(uint16_t addr)
{
	int16_t flashRet;
	uint8_t ret;

	if (addr < sizeof(mIO)) {

		ret = ioR(addr);
	}
	else if (addr - RAM_BASE < sizeof(mRAM)) {

		ret =  mRAM[addr - RAM_BASE];
	}
	else if (addr >= ADDR_ROM_MAP_PG_L && addr - ADDR_ROM_MAP_PG_L < ADDR_ROM_PG_SZ) {

		if (mCartFlash && mIO[0x0d] == mCartBexID && (flashRet = at29lv010aRead(mCartFlash, addr)) >= 0)
			ret = (uint8_t)flashRet;
		else if (mInternalFlash && mIO[0x0d] == mInternalFlashBexID && (flashRet = almostSst39sf010aRead(mInternalFlash, addr)) >= 0)
			ret = (uint8_t)flashRet;
		else
			ret = mRomL[addr - ADDR_ROM_MAP_PG_L];
	}
	else if (addr >= ADDR_ROM_MAP_PG_H && addr - ADDR_ROM_MAP_PG_H < ADDR_ROM_PG_SZ) {

		if (mCartFlash && mIO[0x0d] == mCartBexID && (flashRet = at29lv010aRead(mCartFlash, addr)) >= 0)
			ret = (uint8_t)flashRet;
		else if (mInternalFlash && mIO[0x0d] == mInternalFlashBexID && (flashRet = almostSst39sf010aRead(mInternalFlash, addr)) >= 0)
			ret = (uint8_t)flashRet;
		else
			ret = mRomH[addr - ADDR_ROM_MAP_PG_H];
	}
	else if (addr >= ADDR_ROM_CONST && addr - ADDR_ROM_CONST < ADDR_ROM_PG_SZ) {

		ret =  mRomC000[addr - ADDR_ROM_CONST];
	}
	else if (addr >= CART_SPECIAL_MEM_BUS_ADDR && addr - CART_SPECIAL_MEM_BUS_ADDR < CART_SPECIAL_MEM_SIZE) {

		ret = (mCartRom && mCartRom->code[1] && addr - CART_SPECIAL_MEM_BUS_ADDR < mCartRom->code[1]->length) ? mCartRom->code[1]->data[addr - CART_SPECIAL_MEM_BUS_ADDR] : 0xff;
	}
	else {
		//some unknown read...
		fprintf(stderr, "Mr {0x%04X} -> ???\n", addr);
		ret = 0xff;	
	}
	//fprintf(stderr, "MR [%04X] -> %02X @0x%04x\n", addr, ret, mCPU.pc);
	return ret;
}

void memW(uint16_t addr, uint8_t v)
{
	if (addr < sizeof(mIO)) {

		ioW(addr, v);
		return;
	}
	
	if (addr - RAM_BASE < sizeof(mRAM)) {

		mRAM[addr - RAM_BASE] = v;

		return;
	}
	
	if ((addr >= ADDR_ROM_MAP_PG_L && addr - ADDR_ROM_MAP_PG_L < ADDR_ROM_PG_SZ) || (addr >= ADDR_ROM_MAP_PG_H && addr - ADDR_ROM_MAP_PG_H < ADDR_ROM_PG_SZ)) {

		//range that flash might be in (ROM writes are a bug)
		if (mCartFlash && mIO[0x0d] == mCartBexID && at29lv010aWrite(mCartFlash, addr, v))
			return;
		if (mInternalFlash && mIO[0x0d] == mInternalFlashBexID && almostSst39sf010aWrite(mInternalFlash, addr, v))
			return;
	}

	//some unknown write...
	fprintf(stderr, "Mw {0x%04X} <- 0x%02x\n", addr, v);
}


static void prvTickTM1(void)
{
	if (!++mIO[0x14] && !++mIO[0x15]) {	//tick and check for overflow

		mIrqsActive |= 0x20;
		prvIrqsUpdate();

		mIO[0x14] = mTmr1reload;
		mIO[0x15] = mTmr1reload >> 8;

		mTimer1overflowToggle = !mTimer1overflowToggle;
	}
}

static void prvTickTM0(void)		//return true if we overflowed
{
	if (!++mIO[0x10] && !++mIO[0x11]) {	//tick and check for overflow

		mIrqsActive |= 0x10;
		prvIrqsUpdate();

		mIO[0x10] = mTmr0reload;
		mIO[0x11] = mTmr0reload >> 8;

		//if TM1 is clocked by TM0 overflow, tick it
		if ((mIO[0x0f] & 3) == 3)
			prvTickTM1();
	}
}

#if defined(PIXTER_CLASSIC) || defined(PIXTER_2_0)

	static bool prvReadPinForGpio(void *userData)
	{
		uint_fast16_t pinNo = (uint_fast16_t)(uintptr_t)userData;

		return (portRead(pinNo / 8) >> (pinNo % 8)) & 1;
	}

	static void prvWritePinForGpio(void *userData, bool hi)
	{
		uint_fast16_t pinNo = (uint_fast16_t)(uintptr_t)userData;

		if (pinNo == PIN_SDA)
			mSdaPulledLow = !hi;
		//ignote attempts to manipulate SCL since pixter drives it anyways
	}
#endif

FILE* prvFopenWithCreate(const char *path)
{
	//fopen just sucks for "create but not truncate, but also allow me to seek" use cases
	int fd = open(path, O_CREAT | O_RDWR, 0755);
	FILE *ret;

	if (fd < 0)
		return NULL;
	ret = fdopen(fd, "r+b");

	if (ret)
		return ret;
	close(fd);

	return NULL;
}

static void prvUartTick(void)	//called every tick of source clock
{
	static int sock = -1, dstPort = PORT ^ 1;

	if (!mUartBRG)
		return;
	if (--mUartBrgCounter)
		return;

	#ifdef UDP_IR
		if (sock == -1) {
			struct sockaddr_in sin;

			sin.sin_addr.s_addr = inet_addr("127.0.0.1");
			sin.sin_family = AF_INET;
			sin.sin_port = htons(PORT);

			sock = socket(AF_INET, SOCK_DGRAM, 0);
			if (sock >= 0) {
				if (bind(sock, (struct sockaddr *)&sin, sizeof(sin))) {
					if (errno == EADDRINUSE) {
						fprintf(stderr, "socket addr is in use, assuming we are number 2");
						dstPort ^= 1;

						sin.sin_port = htons(PORT ^ 1);
						if (bind(sock, (struct sockaddr *)&sin, sizeof(sin))) {

							fprintf(stderr, "bind 2 failed too. giving up\n");
							sock = -2;
						}
					}
					else {
						fprintf(stderr, "socket bind fails: %d\n", errno);
						sock = -2;
					}
				}
				else {
					fprintf(stderr, "IR socket bound to port %u\n", PORT);
				}
			}
			else {
				sock = -2;
			}
		}

	#endif

	mUartBrgCounter = mUartBRG * ((mIO[0x19] & 0x04) ? 11 : 10);	//wait for while byte length to do anything

	if (mNextUartTxByte >= 0) {
		
		bool sent = true;

		#ifdef UDP_IR
			if (sock >= 0) {
				uint8_t byte = mNextUartTxByte;
				struct sockaddr_in sin;

				sin.sin_addr.s_addr = inet_addr("127.0.0.1");
				sin.sin_family = AF_INET;
				sin.sin_port = htons(dstPort);

				sent = 1 == sendto(sock, &byte, 1, 0, (struct sockaddr *)&sin, sizeof(sin));
			}
		#endif

		if (sent) {
			
			mNextUartTxByte = -1;
			mIO[0x1a] |= 0x40;
			
			mIrqsActive |= mIO[0x19] & 0x40;
			prvIrqsUpdate();
		}
	}

	#ifdef UDP_IR
		if (mNextUartRxByte < 0 && sock >= 0) {
			uint8_t byte;

			if (1 == recv(sock, &byte, 1, MSG_DONTWAIT))
				mNextUartRxByte = (uint_fast16_t)(uint8_t)byte;
		}
	#endif

	if (mNextUartRxByte >= 0) {

		if (mCurUartRxByte >= 0)	//overflow?
			mIO[0x1a] |= 0x10;
		else {
			mIO[0x1a] &=~ 0x38;
			mCurUartRxByte = mNextUartRxByte;
		}
		mIO[0x1a] |= 0x80;
		mIrqsActive |= mIO[0x19] & 0x80;
		prvIrqsUpdate();
		mNextUartRxByte = -1;
	}
}

int main(int argc, char** argv)
{
	static const SDL_AudioSpec desiredSpecEffects = {
		.freq = 8000,
		.format = 8,		//u8
		.channels = 1,
	};
	static const SDL_AudioSpec desiredSpecMusic = {
		.freq = 22050,
		.format = 0x8010,	//S16
		.channels = 1,
	};
	SDL_AudioSpec gotten;
	uint32_t slow32Kctr;
	int32_t instrsAllowed = 0;
	struct timespec next32Ktime;
	bool mayTickTmr0 = false;
	unsigned pos;
	FILE* f;
	
	if(argc < 3 || argc > 5){
		fprintf(stderr, "USAGE: %s console.rom console.eeprom [cart.rom [cart.flash]]\n", argv[0]);
		return -1;
	}
	f = fopen(argv[1], "rb");
	if(!f){
		fprintf(stderr, "Failed to open CONSOLE ROM '%s'\n", argv[1]);
		return -2;
	}

	mConsoleRom = romRead(f);
	if (!mConsoleRom) {
		fprintf(stderr, "Failed to parse CONSOLE ROM '%s'\n", argv[1]);
		return -3;
	}
	fclose(f);
	
	mEepromFile = prvFopenWithCreate(argv[2]);
	if (!mEepromFile) {

		fprintf(stderr, "Failed to open CONSOLE EEPROM '%s'\n", argv[2]);
		return -2;
	}

	if (argc >= 4) {
		

		f = fopen(argv[3], "rb");
		if (!f){
			fprintf(stderr, "Failed to open CONSOLE CART ROM '%s'\n", argv[3]);
			return -3;
		}
		mCartRom = romRead(f);
		if (!mCartRom) {
			fprintf(stderr, "Failed to parse CONSOLE CART ROM '%s'\n", argv[3]);
			return -3;
		}
		fclose(f);

		mHaveCart = true;

		if (argc == 5) {
		
			mFlashFile = prvFopenWithCreate(argv[4]);
			if (!mFlashFile) {

				fprintf(stderr, "Failed to open CART FLASH '%s'\n", argv[4]);
				return -2;
			}
			mCartFlash = at29lv010aInit(mFlashFile);
			if (!mCartFlash) {

				fprintf(stderr, "Failed to init CART FLASH\n");
				return -2;
			}
		}
	}
	
	prvMemMapUpdate();
	cpuInit(&mCPU);

	#if defined(PIXTER_CLASSIC) || defined(PIXTER_2_0)		//create the i2c eeprom(s)

		mBbI2C = gpioI2cInit(prvReadPinForGpio, prvWritePinForGpio, (void*)(uintptr_t)PIN_SDA, prvReadPinForGpio, prvWritePinForGpio, (void*)(uintptr_t)PIN_SCK);
		if (!mBbI2C) {
			fprintf(stderr, "Cannot init i2c\n");
			abort();
		}
		for (pos = 0; pos < sizeof(mMemories) / sizeof(*mMemories); pos++) {
			mMemories[pos] = at24c02init(mBbI2C, FIRST_I2C_MEMORY + pos, mEepromFile, pos * 256);
			if (!mMemories[pos]) {
				fprintf(stderr, "Cannot init i2c memory %u\n", pos);
				abort();
			}
		}

	#endif
	
	#if defined(PIXTER_PLUS)								//create the flash that stores things in the plus
		
		mInternalFlash = almostSst39sf010aInit(mEepromFile);
		if (!mInternalFlash) {
			fprintf(stderr, "Cannot init internal flash memory\n");
			abort();
		}

	#endif

	SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS);
	mAudioDevEffects = SDL_OpenAudioDevice(NULL, false, &desiredSpecEffects, &gotten, 0);
	if (!mAudioDevEffects) {

		fprintf(stderr, "SDL audio (effects) error: %s\n", SDL_GetError());
		abort();
	}
	SDL_PauseAudioDevice(mAudioDevEffects, false);

	mAudioDevCartMelody = SDL_OpenAudioDevice(NULL, false, &desiredSpecMusic, &gotten, 0);
	if (!mAudioDevCartMelody) {

		fprintf(stderr, "SDL audio (cart melody) error: %s\n", SDL_GetError());
		abort();
	}
	SDL_PauseAudioDevice(mAudioDevCartMelody, false);

	mAudioDevConsoleMelody = SDL_OpenAudioDevice(NULL, false, &desiredSpecMusic, &gotten, 0);
	if (!mAudioDevConsoleMelody) {

		fprintf(stderr, "SDL audio (console melody) error: %s\n", SDL_GetError());
		abort();
	}
	SDL_PauseAudioDevice(mAudioDevConsoleMelody, false);

	mCartMelodyChip = melodyChipInit(mCartRom, mAudioDevCartMelody);
	mConsoleMelodyChip = melodyChipInit(mConsoleRom, mAudioDevConsoleMelody);

	slow32Kctr = 0;

	clock_gettime(CLOCK_MONOTONIC, &next32Ktime);
	while (1) {
		
		static const uint16_t slowTimebaseMask[] = {16383, 4095, 8191, 2047};
		static const uint8_t fastTimebaseMask[] = {255, 63, 127, 31};
		uint32_t clocksHappened;
		struct timespec nowTime;
		bool did32kTick = false;


		#ifdef PREDICTABLE_TIME

			static uint32_t ctr = 0;

			did32kTick = !(++ctr & 127);

		#else	//realtime
			clock_gettime(CLOCK_MONOTONIC, &nowTime);
			if (next32Ktime.tv_sec < nowTime.tv_sec || (next32Ktime.tv_sec == nowTime.tv_sec && next32Ktime.tv_nsec <= nowTime.tv_nsec)) {

				//we had a 32K tick - calc next one
				next32Ktime.tv_nsec += 30518;
				if (next32Ktime.tv_nsec >= 1000000000) {
					next32Ktime.tv_nsec -= 1000000000;
					next32Ktime.tv_sec++;
				}
				did32kTick = true;
			}
		#endif

		//32.768KHz clock
		if (did32kTick) {

			//let CPU run
			instrsAllowed += (FCLK + 32768 - 1) / 32768;

			//melody chips may need to do things
			if (mCartMelodyChip)
				melodyChipPeriodic(mCartMelodyChip);
			if (mConsoleMelodyChip)
				melodyChipPeriodic(mConsoleMelodyChip);

			//if we have flash, it needs to know time passed in case it is writing
			if (mCartFlash)
				at29lv010aPeriodic(mCartFlash);

			if (mInternalFlash)
				almostSst39sf010aPeriodic(mInternalFlash);

			//do the stuff
			slow32Kctr++;

			//UART might be running from 32K clock, if so, do a tick
		#ifdef PIXTER_2_0
			if ((mIO[0x19] & 3) == 1)
				prvUartTick();
		#endif

			//run screen at 64Hz
			if (!(slow32Kctr & 511))
				renderScreen();
			
			if (!(slow32Kctr & slowTimebaseMask[(mIO[0x0C] >> 2) & 3])) {
				mIrqsActive |= 0x04;
				prvIrqsUpdate();
			}

			if (!(slow32Kctr & fastTimebaseMask[mIO[0x0C] & 3])) {
				mIrqsActive |= 0x08;
				prvIrqsUpdate();
			}

			//mask timer 0
			switch ((mIO[0x0f] >> 2) & 7) {
				case 0:	//always
					mayTickTmr0 = true;
					break;

				case 1:	//TBL
					mayTickTmr0 = !(slow32Kctr & (slowTimebaseMask[(mIO[0x0C] >> 2) & 3] + 1));
					break;

				case 2:	//TBH
					mayTickTmr0 = !(slow32Kctr & (fastTimebaseMask[mIO[0x0C] & 3] + 1));
					break;

				case 3:	//EXTI (never active)
					mayTickTmr0 = false;
					break;

				case 4:	//2Hz
					mayTickTmr0 = !(slow32Kctr & 16384);
					break;

				case 5:	//8Hz
					mayTickTmr0 = !(slow32Kctr & 4096);
					break;

				case 6:	//32Hz
					mayTickTmr0 = !(slow32Kctr & 1024);
					break;

				case 7:	//64Hz
					mayTickTmr0 = !(slow32Kctr & 256);
					break;
			}

			//if TMR0 ticks at 32k, tick it
			if (((mIO[0x0f] >> 5) & 3) == 2) {

				//if TM1 ticks on TM0 overflow, tick it
				prvTickTM0();
			}

			//if TMR1 ticks at 32k, tick it
			if ((mIO[0x0f] & 3) == 2)
				prvTickTM1();
		}
		
		while (instrsAllowed > 0) {

			clocksHappened = cpuRun(&mCPU, 1);	//mCPU used this many mCPU clocks, but mCPU may operate at a de-rated clock
			switch (mIO[0x04] >> 5) {
				case 0:	clocksHappened *= 4;	break;
				case 1:	clocksHappened *= 2;	break;
				case 2:	clocksHappened *= 1;	break;
				case 5:	clocksHappened *= 32;	break;
				case 6:	clocksHappened *= 16;	break;
				default:
					fprintf(stderr, "CLOCK REG 0x%02x not known to work\n", mIO[0x04]);
			}

			instrsAllowed -= clocksHappened;
			while (clocksHappened--) {

				//UART might be running from main clock, if so, do ticks
		#ifdef PIXTER_2_0
				if ((mIO[0x19] & 3) != 1)
					prvUartTick();
		#endif

				//tick TM0, if at Rosc, based on mask (which is pre-evaluaged)
				if (mayTickTmr0 && ((mIO[0x0f] >> 5) & 3) == 1) {

					prvTickTM0();
				}

				//tick tm1
				if ((mIO[0x0f] & 3) == 1)
					prvTickTM1();
			}
		}
	}
	
	return 0;
}


