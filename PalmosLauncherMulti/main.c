#define PERIPHS_BASE	0xfff00000
#include "LH795xx.h"
#include "LH75411.h"
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "header.h"
#include "printf.h"
#include "bbBus.h"
#include "lz.h"

#define RAM_ADDRESS					0x20000000
#define RAM_LENGTH					0x00400000

#define SCREEN_STRIDE				162
#define SCREEN_WIDTH				160
#define SCREEN_HEIGHT				160
#define SCREEN_BLACK				0x00
#define SCREEN_WHITE				0xff
#define PROG_BAR_HEIGHT				10
#define PROG_BAR_WIDTH				130
#define PROG_BAR_TOP				((SCREEN_HEIGHT - PROG_BAR_HEIGHT) / 2)
#define PROG_BAR_LEFT				((SCREEN_WIDTH - PROG_BAR_WIDTH) / 2)


#define DATA_START					0x4000
#define COLOR_IMG_START				0x9000



//#define USE_BBUART
//#define USE_EMU_UART

struct LzReadData {
	uint8_t curPage;
	uint_fast8_t (*readByteF)(uint32_t ofst, void *userData);
};

static inline void prPrvPutcharRaw(uint32_t ch)
{
	#ifdef USE_BBUART
		
		uint32_t send = (ch + 0xff00) << 6, ct = 16;
		while (ct--) {
			uint32_t t = 1000;

			bbBusWriteCycleMulti(0x20, send & 0x20);
			send >>= 1;
			
			asm volatile(
				".syntax unified	\n\t"
				"1:					\n\t"
				"	subs %0, #1		\n\t"
				"	bne  1b			\n\t"
				:"+l"(t)
				:
				:"memory", "cc"
			);
		}

	#endif

	#ifdef USE_EMU_UART

		while (LH795xxUART2->fr & 0x20);			//comment out for real hardware
		LH795xxUART2->dr = (uint8_t)ch;
	
	#endif
}

void prPutchar(char ch)
{
	if (ch == '\n')
		prPrvPutcharRaw('\r');
	
	prPrvPutcharRaw((unsigned char)ch);
}

static void progBarInit(void)
{
	volatile uint8_t *fb = 2 + (volatile uint8_t*)(LH795xxCLCDC->upbase);
	uint32_t r, c;

	fb += PROG_BAR_TOP * SCREEN_STRIDE + PROG_BAR_LEFT;

	for (r = 0; r < PROG_BAR_HEIGHT; r++, fb += SCREEN_STRIDE) {
		for (c = 0; c < PROG_BAR_WIDTH; c++)
			fb[c] = SCREEN_WHITE;
	}
}

static void progBarDraw(uint32_t num, uint32_t outOf)
{
	volatile uint8_t *fb = 2 + (volatile uint8_t*)(LH795xxCLCDC->upbase);
	uint32_t progBarBodyHeight = PROG_BAR_HEIGHT - 2;
	uint32_t progBarBodyWidth = PROG_BAR_WIDTH - 2;
	uint32_t progBarBodyLeft = PROG_BAR_LEFT + 1;
	uint32_t progBarBodyTop = PROG_BAR_TOP + 1;
	uint32_t numPix = (num * progBarBodyWidth + outOf / 2) / outOf;
	uint32_t r, c;

	fb += progBarBodyTop * SCREEN_STRIDE + progBarBodyLeft;
	
	for (r = 0; r < progBarBodyHeight; r++, fb += SCREEN_STRIDE) {
		for (c = 0; c < numPix; c++)
			fb[c] = SCREEN_BLACK;
	}
}

static uint_fast8_t __attribute__((optimize("Ofast"), target("arm"))) lzReadByteMulti(uint32_t ofst, void *userData)
{
	struct LzReadData *lzrd = (struct LzReadData*)userData;
	uint8_t page = 0xc0 + ofst / 32768;

	ofst %= 32768;
	if (ofst < 0x4000)
		ofst += 0x8000;

	if (lzrd->curPage != page) {

		bbBusWriteCycleMulti(0x00, page);
		lzrd->curPage = page;
	}

	return bbBusReadCycleMulti(ofst);
}

static uint_fast8_t lzReadByteColor(uint32_t ofst, void *userData)
{
	return bbBusReadCycleColor(ofst);
}

static uint32_t prvReadWordFromCompressedData(uint32_t *ofstP, struct LzReadData *lzrd)
{
	uint32_t ret = 0, ofst = *ofstP;
	uint_fast8_t i;

	for (i = 0; i < 4; i++)
		ret = (ret << 8) + lzrd->readByteF(ofst++, lzrd);

	*ofstP = ofst;
	return ret;
}

static void infiniteLoop(void)
{
	while(1);
}

static uint32_t decompressHdrRd(struct LzState *lzs, uint32_t *curPosP, struct LzReadData *lzrd)		//return expected decompressed len or 0 on error
{
	uint32_t t;

	t = prvReadWordFromCompressedData(curPosP, lzrd);
	if (t != MAGIC) {

		pr("Magic is broken: 0x%08x\n", t);
		return 0;
	}
	else {
		uint32_t dstStart = prvReadWordFromCompressedData(curPosP, lzrd);
		uint32_t decompressedLen = prvReadWordFromCompressedData(curPosP, lzrd);
		uint32_t compressedLen = prvReadWordFromCompressedData(curPosP, lzrd);
		
		lzs->readD = lzrd;
		lzs->lzReadByteF = lzrd->readByteF;
		lzs->inPosStart = *curPosP;
		lzs->inPosPastEnd = lzs->inPosStart + compressedLen;
		lzs->dst = (uint8_t*)dstStart;

		return decompressedLen;
	}
}

static void colorRestride(uint8_t *fb, bool tft)
{
	uint8_t *src = fb + 160 * 160 / 2;	//just past data end
	uint8_t *dst = fb + 160 * (tft ? 162 : 160);
	uint32_t t, p = 0;
	int32_t r, c;

	if (tft)
		dst += 2;

	for (r = 159; r >= 0; r--) {
		
		for (c = 159; c >= 0; c -= 2) {

			uint_fast8_t data = *--src;
			*--dst = data >> 4;
			*--dst = data & 15;
		}
		if (tft)
			dst -= 2;
	}

	//now apply clut
	for (t = 0; t < 16; t++) {
		uint32_t thisClutEntry = t + (t << 5) + (t << 10);

		if (!tft)
			thisClutEntry <<= 1;

		if (t & 1)
			LH75411CLCDC->clut[t / 2] = (p << 16) + thisClutEntry;
		else
			p = thisClutEntry;
	}
}

bool draw_on_color(void)
{
	uint8_t *framebuf = (uint8_t*)LH75411CLCDC->upbase;
	uint32_t curPos = COLOR_IMG_START;
	struct LzReadData lzrd = {};
	struct LzState lzs;

	bbBusInitColor();
	bbBusWriteCycleColor(0x00, 0xc0);		//always this page

	lzrd.readByteF = lzReadByteColor;
	if (!decompressHdrRd(&lzs, &curPos, &lzrd))
		return 0;	//draw default image

	lzs.dst = framebuf;
	if (!lzUncompressStart(&lzs))
		return 0;	//draw default image

	while (lzDecompressSome(&lzs, 8192));

	//now we need to expand the image and possibly re-stride it
	colorRestride(framebuf, LH75411CLCDC->ctrl & 0x20);

	return 1; //do not draw default image
}

void* entry(void)
{
	struct LzReadData lzrd = {};
	uint32_t curPos = DATA_START;
	struct LzState lzs;
	uint32_t fullSize;

#ifdef USE_BBUART

	for (i = 0; i < 20; i++)			//bbuart init
		bbBusWriteCycleMulti(0x20, 0x20);
#endif

#ifdef USE_EMU_UART

	LH795xxUART2->ctrl = 0x101;	//enable enough for emu to work

#endif

	pr("Code is up\n");

	/* speed up cpu. stock code comes up like so:

		CONFIGURED SPEEDS:
			XTALIN: 11.29MHz
			PLL: XTALIN * 13.00 -> 146.76MHz
			HCLK: PLL / 6 -> 24.46MHz
			CPU: PLL / 4 -> 36.69MHz

		we speed itup for a lot of gain
	*/
	LH795xxRCPC->sysPllCntl = 0x1050;	//PLL = OSC * 16 = 180.63MHz
	LH795xxRCPC->sysClkPrescaler = 2;	//HCLK = PLL / 4 = 45.16MHz
	LH795xxRCPC->cpuClkPrescaler = 1;	//CPU = PLL / 2 = 90.32MHz

	bbBusInitMulti();
	bbBusWriteCycleMulti(0x00, 0xc0);
	
	lzrd.readByteF = lzReadByteMulti;
	fullSize = decompressHdrRd(&lzs, &curPos, &lzrd);

	if (fullSize){
		
		uint8_t *dstStart = lzs.dst, *dstEnd = dstStart + fullSize;

		if (!lzUncompressStart(&lzs)) {

			pr("Decompress start error\n");
		}
		else {
			
			//framebuffer might intersect with our decompressed range, find a part that does not
			uint32_t framebufferSize = SCREEN_STRIDE * SCREEN_HEIGHT;
			uint32_t posAfterEnd =  (3 +(uintptr_t)dstEnd) / 4 * 4;
			bool fbMoved = true;


			//RAM_ADDRESS
			if (RAM_ADDRESS + framebufferSize < (uintptr_t)dstStart) {	//use start of RAM

				LH795xxCLCDC->upbase = RAM_ADDRESS;
			}
			else if (posAfterEnd + framebufferSize <= RAM_ADDRESS + RAM_LENGTH) {	//use end of ram
				
				LH795xxCLCDC->upbase = posAfterEnd;
			}
			else {

				pr("Cannot find a place for framebuffer\n");
				fbMoved = false;
			}


			if (fbMoved) {
				
				memset((void*)LH795xxCLCDC->upbase, SCREEN_BLACK, framebufferSize);
				progBarInit();

				while (lzDecompressSome(&lzs, 8192)) {

					progBarDraw(lzs.dst - dstStart, dstEnd - dstStart);
				}
				
				progBarDraw(1, 1);		//fill bar

				pr("decompress ends\n");
				return dstStart;
			}
		}
	}
	return &infiniteLoop;
}







