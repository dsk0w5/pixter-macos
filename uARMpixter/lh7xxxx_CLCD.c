#include "lh7xxxx_CLCD.h"
#include "lh7xxxx_IC.h"
#include "SDL2/SDL.h"
#include "device.h"
#include <stdlib.h>
#include "util.h"


#define LCDC_BASE			0xFFFF4000
#define LCDC_SIZE			0x00000800

#define ALI_BASE			0xFFFE4000
#define ALI_SIZE			0x00000010


enum LcdSituation {
	LcdBackPorch,
	LcdActiveArea,
	LcdFrontPorch,
	LcdSync,
};

struct Lh7xxxxLcd {
	struct ArmMem *mem;
	struct SocIc *ic;

	//state
	bool on;
	uint16_t dispW, dispH;		//active area
	uint16_t curRow;			//only in active area
	enum LcdSituation horizSituation;
	enum LcdSituation vertSituation;
	uint32_t curUpBase, curLpBase, nextDmaReadL, nextDmaReadU;
	SDL_Surface *screen;
	SDL_Window *window;
	uint32_t *curOutputBufer;

	//ali regs
	uint16_t aliSetup, aliCtl, aliTiming1, aliTiming2;

	//lcdc regs
	uint32_t timing0, timing1, timing2, upbase, lpbase, ctrl;
	uint16_t clut[256];
	uint8_t intrEn, intrSta;
};


static void lh7xxxxlLcdPrvFinishDrawing(struct Lh7xxxxLcd *lcd)	//does nothing if we were not drawing
{
	if (lcd->curOutputBufer) {
		
		SDL_UnlockSurface(lcd->screen);
		SDL_BlitSurface(lcd->screen, NULL, SDL_GetWindowSurface(lcd->window), NULL);
		SDL_UpdateWindowSurface(lcd->window);
		lcd->curOutputBufer = NULL;
	}
}

static void lh7xxxxlLcdPrvStartDrawing(struct Lh7xxxxLcd *lcd)	//does nothing if we are already drawing
{
	if (!lcd->curOutputBufer) {
		
		SDL_LockSurface(lcd->screen);
		lcd->curOutputBufer = (uint32_t*)lcd->screen->pixels;
	}
}

static void lcdcPrvIrqUpdate(struct Lh7xxxxLcd *lcd)
{
	socIcInt(lcd->ic, LH7xxxx_I_LCD, !!(lcd->intrSta & lcd->intrEn));
}

static bool lh7xxxxlLcdPrvDrawLine(struct Lh7xxxxLcd *lcd, uint32_t lcdLineNo, uint32_t dataLineNo, uint32_t curBaseReg, uint32_t *nextAdrP)	//return rtue if IRQs were updated
{
	uint32_t i, j, readAddr, bppVal = (lcd->ctrl >> 1) & 7, bppBits = 1 << bppVal, bppMask = (1 << bppBits) - 1, stride = (lcd->dispW * bppBits + 15) / 16 * 2;
	uint32_t *dst = lcd->curOutputBufer + lcd->dispW * lcdLineNo;

	readAddr = curBaseReg + dataLineNo * stride;
	*nextAdrP = readAddr + stride;

	for (i = 0; i < lcd->dispW;) {
		
		uint16_t v;

		if (!memAccess(lcd->mem, readAddr, 2, false, &v)) {
			
			lcd->intrSta |= 0x10;		//MBERROR
			
			fprintf(stderr, "read error\n");
			return true;
		}
		readAddr += 2;

		for (j = 0; j < (16u >> bppVal); i++, j++, v >>= bppBits) {

			uint32_t r, g, b, mask, shift, mul, div;
			uint32_t pix = v & bppMask;

			switch (bppVal) {
				case 0:	//1bpp
				case 1:	//2bpp
				case 2:	//4bpp
				case 3:	//8bpp
					pix = lcd->clut[pix];
					break;
				case 4:
					//nothing
					break;
				default:
					pix = 0;
					break;
			}
			
			if (!(lcd->ctrl & 0x20)) {		//STN
				
				mask = 0x0f;
				shift = 1;
				mul = 17;
				div = 1;
			}
			else if (!deviceGetSocRev()) {	//TFT on rev 0 SoC
				
				mask = 0x0f;
				shift = 0;
				mul = 17;
				div = 1;
			}
			else {							//TFT on rev 1 soc
				
				mask = 0x1f;
				shift = 0;
				mul = 255;
				div = 31;
			}
			
			pix >>= shift;
			r = (pix >> 0) & mask;
			g = (pix >> 5) & mask;
			b = (pix >> 10) & mask;
			
			if (lcd->ctrl & 0x100) {		//BGR swap
				uint32_t tmp = r;
				r = b;
				b = tmp;
			}

			if (lcd->ctrl & 0x10) {			//mono
				
				g = b = r;
			}

			r = r * mul / div;
			g = g * mul / div;
			b = b * mul / div;

			pix = r * 0x10000 + g * 0x100 + b;

			*dst++ = pix;
		}
	}
	return false;
}

static bool lh7xxxxlLcdPrvProcessDrawingSingleRow(struct Lh7xxxxLcd *lcd, uint32_t row)	//called for each row in the active area, deals with dual-scan panels, 	return rtue if IRQs were updated
{
	bool irqsUpdated = false;

	//draw upper panel always
	irqsUpdated = lh7xxxxlLcdPrvDrawLine(lcd, row, row, lcd->curUpBase, &lcd->nextDmaReadU) || irqsUpdated;

	//draw lower panel if it exists
	if (lcd->ctrl & 0x80)
		irqsUpdated = lh7xxxxlLcdPrvDrawLine(lcd, row + lcd->dispH, row, lcd->curLpBase, &lcd->nextDmaReadL) || irqsUpdated;

	return irqsUpdated;
}

void lh7xxxxClcdPeriodic(struct Lh7xxxxLcd *lcd)
{
	bool needIrqUpdate = false;

	if (lcd->on && !(lcd->ctrl & 1)) {			//turning off
		
		lh7xxxxlLcdPrvFinishDrawing(lcd);			//we were possibly drawing - end that
		lcd->nextDmaReadL = lcd->lpbase;
		lcd->nextDmaReadU = lcd->upbase;
		fprintf(stderr, "LCDC disabled\n");
		lcd->on = false;

		if (lcd->screen)
			SDL_FreeSurface(lcd->screen);
		if (lcd->window)
			SDL_DestroyWindow(lcd->window);
		lcd->window = NULL;
		lcd->screen = NULL;
	}
	else if (!(lcd->ctrl & 1)) {					//was off and staying off
		
		//nothing
	}
	else if (!lcd->on) {							//freshly-enabled, we only enable!
		
		uint32_t actualLcdHeight;

		lcd->dispW = 16 * (1 + ((lcd->timing0 >> 2) & 0x3f));
		lcd->dispH = 1 + (lcd->timing1 & 0x3ff);
		lcd->horizSituation = LcdSync;
		lcd->vertSituation = LcdSync;
		lcd->curRow = 0;

		actualLcdHeight = ((lcd->ctrl & 0x80) ? 2 : 1) * lcd->dispH;

		//verify ALI, if used
		if (lcd->aliSetup & 1) {

			uint32_t aliPpl = 1 + ((lcd->aliSetup >> 4) & 0x1ff);

			if (aliPpl != lcd->dispW) {

				if (aliPpl < lcd->dispW || aliPpl - lcd->dispW > 16) {	//these cases are valid (ish)

					ERR("ALI PPL is %u, LCDC PPL is %u\n", aliPpl, lcd->dispW);
				}
				lcd->dispW = aliPpl;
			}
		}

		fprintf(stderr, "LCDC enabling at %ux%u ...\n", lcd->dispW, actualLcdHeight);

		lcd->window = SDL_CreateWindow("uARM", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, lcd->dispW, actualLcdHeight + (deviceHasGrafArea() ? 13: 0), 0);
		if (!lcd->window)
			ERR("Couldn't create window: %s\n", SDL_GetError());
		
		lcd->screen = SDL_CreateRGBSurface(0, lcd->dispW, actualLcdHeight, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x0000);
		if (!lcd->screen)
			ERR("Couldn't create screen surface: %s\n", SDL_GetError());

		lcd->on = true;
	}
	else {											//normal operation
		
		switch (lcd->horizSituation) {
			case LcdBackPorch:			//back porch -> enter active area
				lcd->horizSituation = LcdActiveArea;
				break;

			case LcdActiveArea:			//active -> front porch (draw happens in hsync)
				lcd->horizSituation = LcdFrontPorch;
				break;

			case LcdFrontPorch:			//front porch -> enter hsync
				lcd->horizSituation = LcdSync;
				break;

			case LcdSync:				//hsync -> go to next line if needed
				lcd->horizSituation = LcdBackPorch;
				switch (lcd->vertSituation) {
					case LcdBackPorch:
						lcd->vertSituation = LcdActiveArea;
						lcd->curRow = 0;
						lh7xxxxlLcdPrvStartDrawing(lcd);
						if (((lcd->ctrl >> 12) & 3) == 2) {	//irq requesed on start of v active area
							
							lcd->intrSta |= 0x08;	//VCOMP
							needIrqUpdate = true;
						}
						break;

					case LcdFrontPorch:
						lcd->vertSituation = LcdSync;
						lh7xxxxlLcdPrvFinishDrawing(lcd);
						if (((lcd->ctrl >> 12) & 3) == 0) {	//irq requesed on start of vsync
							
							lcd->intrSta |= 0x08;	//VCOMP
							needIrqUpdate = true;
						}
						break;

					case LcdSync:
						lcd->vertSituation = LcdBackPorch;
						lcd->nextDmaReadU = lcd->curUpBase = lcd->upbase;
						lcd->nextDmaReadL = lcd->curLpBase = lcd->lpbase;
						lcd->intrSta |= 0x04;	//LNBU
						needIrqUpdate = true;
						if (((lcd->ctrl >> 12) & 3) == 1) {	//irq requesed on start of v back porch
							
							lcd->intrSta |= 0x08;	//VCOMP
							needIrqUpdate = true;
						}
						break;

					case LcdActiveArea:
						needIrqUpdate = lh7xxxxlLcdPrvProcessDrawingSingleRow(lcd, lcd->curRow) || needIrqUpdate;
						if (++lcd->curRow == lcd->dispH) {

							lcd->vertSituation = LcdFrontPorch;
							if (((lcd->ctrl >> 12) & 3) == 3) {	//irq requesed on start of v front porch
								
								lcd->intrSta |= 0x08;	//VCOMP
								needIrqUpdate = true;
							}
						}
						break;
				}
				break;
		}
	}
	
	if (needIrqUpdate)
		lcdcPrvIrqUpdate(lcd);
}

static bool lcdcPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxLcd *lcd = (struct Lh7xxxxLcd*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= LCDC_BASE;
	pa >>= 2;

	if (write)
		val = *(uint32_t*)buf;
	
//	if (write)
//		fprintf(stderr, "LCDC write to 0x%08lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);
	
	switch (pa) {
		case 0x00 / 4:		//Timing0
			if (write)
				lcd->timing0 = val;
			else
				val = lcd->timing0;
			break;

		case 0x04 / 4:		//Timing1
			if (write)
				lcd->timing1 = val;
			else
				val = lcd->timing1;
			break;

		case 0x08 / 4:		//Timing2
			if (write)
				lcd->timing2 = val;
			else
				val = lcd->timing2;
			break;
		
		case 0x10 / 4:		//UPBASE
			if (write)
				lcd->upbase = val & 0xfffffffc;
			else
				val = lcd->upbase;
			break;

		case 0x14 / 4:		//LPBASE
			if (write)
				lcd->lpbase = val & 0xfffffffc;
			else
				val = lcd->lpbase;
			break;

		case 0x18 / 4:		//INTRENABLE
			if (write){

				lcd->intrEn = val & 0x1e;
				lcdcPrvIrqUpdate(lcd);
			}
			else
				val = lcd->intrEn;
			break;
		
		case 0x1c / 4:		//Ctrl
			if (write) {

				lcd->ctrl = val & 0x0001b9bf;
				lcdcPrvIrqUpdate(lcd);
			}
			else
				val = lcd->ctrl;
			break;
		
		case 0x20 / 4:		//Status
			if (write) {

				lcd->intrSta &=~ val;
				lcdcPrvIrqUpdate(lcd);
			}
			else
				val = lcd->intrSta;
			break;
		
		case 0x24 / 4:		//Interrupt
			if (write)
				return false;
			else
				val = lcd->intrSta & lcd->intrEn;
			break;
		
		case 0x28 / 4:		//UPCURR
			if (write)
				return false;
			else
				val = lcd->nextDmaReadU;
			break;
		
		case 0x2c / 4:		//LPCURR
			if (write)
				return false;
			else
				val = lcd->nextDmaReadL;
			break;

		case 0x200 / 4 ... 0x3fc / 4:	//CLUT
			if (write) {

				lcd->clut[(pa - 0x200 / 4) * 2 + 0] = val & 0x7fff;
				lcd->clut[(pa - 0x200 / 4) * 2 + 1] = (val >> 16) & 0x7fff;
			}
			else {
				val = 0x10000 * lcd->clut[(pa - 0x200 / 4) * 2 + 1] + lcd->clut[(pa - 0x200 / 4) * 2 + 0];
			}
			break;

		default:
			return false;
	}
	
//	if (!write)
//		fprintf(stderr, "LCDC READ [0x%08lx] -> 0x%08lx\n", (unsigned long)paorig, (unsigned long)val);
	
	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

static bool aliPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxLcd *lcd = (struct Lh7xxxxLcd*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= ALI_BASE;
	pa >>= 2;

	if (write)
		val =  *(uint32_t*)buf;
	
//	if (write)
//		fprintf(stderr, "ALI write to 0x%08lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);
	
	switch (pa) {
		case 0x00 / 4:		//ALISetup
			if (write)
				lcd->aliSetup = val;
			else
				val = lcd->aliSetup;
			break;
		
		case 0x04 / 4:		//ALICTRL
			if (write)
				lcd->aliCtl = val;
			else
				val = lcd->aliCtl;
			break;

		case 0x08 / 4:		//ALITiming
			if (write)
				lcd->aliTiming1 = val;
			else
				val = lcd->aliTiming1;
			break;

		case 0x0c / 4:		//ALITiming2
			if (write)
				lcd->aliTiming2 = val;
			else
				val = lcd->aliTiming2;
			break;

		default:
			return false;
	}
	
//	if (!write)
//		fprintf(stderr, "ALI READ [0x%08lx] -> 0x%08lx\n", (unsigned long)paorig, (unsigned long)val);
	
	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

struct Lh7xxxxLcd* lh7xxxxClcdInit(struct ArmMem *physMem, struct SocIc *ic, bool hardGrafArea)
{
	struct Lh7xxxxLcd *lcd = (struct Lh7xxxxLcd*)calloc(1, sizeof(*lcd));

	if (lcd) {
		
		lcd->ic = ic;
		lcd->mem = physMem;
		lcd->aliSetup = 0x000c;

		if (!memRegionAdd(physMem, LCDC_BASE, LCDC_SIZE, lcdcPrvMemAccessF, lcd))
			ERR("cannot add LCDC to MEM\n");

		if (!memRegionAdd(physMem, ALI_BASE, ALI_SIZE, aliPrvMemAccessF, lcd))
			ERR("cannot add LCDC to MEM\n");

		lcdcPrvIrqUpdate(lcd);
	}

	return lcd;
}


