#include "lh7xxxx_IOCON.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "device.h"
#include "util.h"

#define IOCON_BASE			0xFFFE5000
#define IOCON_SIZE			0x000000c4


struct Lh7xxxxIocon {
	union {
		struct {
			uint16_t ebiMux, pdMux, peMux, timerMux, paResMux, pbResMux, pcResMux, pdResMux, peResMux;
			uint8_t adcMux, lcdMux;
		} g1;

		struct {
			uint16_t vals[49];
		} g2;
	};
};

//in G2, even numbered regs are muxtl, odd are resctl
static const uint16_t mRegMasksG2[] = {
		/* 0x00 */	0x03ff, 0x03ff, 0x0000, 0x0000, 0x0003, 0x0003, 0x0fff, 0x0fff,
		/* 0x20 */	0xffff, 0xffff, 0x00ff, 0x00ff, 0xffff, 0xffff, 0x0000, 0x0000,
		/* 0x40 */	0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xf000, 0xffff,
		/* 0x60 */	0x0000, 0x000f, 0xff3f, 0x0000, 0x0003, 0x0003, 0x0000, 0x0000,
		/* 0x80 */	0x0000, 0x0003, 0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff,
		/* 0xA0 */	0x0fff, 0x0fff, 0xffff, 0xffff, 0xffff, 0xffff, 0x3fff, 0x3fff,
		/* 0xC0 */	0xffff,
	};

static const uint16_t mRegValsG2[] = {
		[0x04 / 4] = 0x0195,
		[0x14 / 4] = 0x0002,
		[0x1c / 4] = 0x0a55,
		[0x24 / 4] = 0x5556,
		[0x2c / 4] = 0x000a,
		[0x34 / 3] = 0x5555,
		[0x48 / 4] = 0x5555,
		[0x4c / 4] = 0x5555,
		[0x50 / 4] = 0x4441,
		[0x54 / 4] = 0x5555,
		[0x58 / 4] = 0x1000,
		[0x5c / 4] = 0x5555,
		[0x64 / 4] = 0x0005,
		[0x74 / 4] = 0x0002,
		[0x94 / 4] = 0x9555,
		[0x98 / 4] = 0x1110,
		[0x9c / 4] = 0x5555,
		[0xa4 / 4] = 0x0555,
		[0xac / 4] = 0x5555,
		[0xb4 / 4] = 0x5555,
		[0xbc / 4] = 0x1555,
	};





static bool ioconGen2prvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxIocon *ioc = (struct Lh7xxxxIocon*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= IOCON_BASE;
	pa >>= 2;

	if (write)
		ioc->g2.vals[pa] = mRegMasksG2[pa] & *(uint32_t*)buf;
	else
		*(uint32_t*)buf = ioc->g2.vals[pa];

	return true;
}

static bool ioconGen1prvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxIocon *ioc = (struct Lh7xxxxIocon*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= IOCON_BASE;
	pa >>= 2;

	if (write)
		val =  *(uint32_t*)buf;
	

	switch (pa) {
		
		case 0x00 / 4:		//EBI_MUX
			if (write)
				ioc->g1.ebiMux = val & 0x7fff;
			else
				val = ioc->g1.ebiMux;
			break;

		case 0x04 / 4:		//PD_MUX
			if (write)
				ioc->g1.pdMux = val & 0x03ff;
			else
				val = ioc->g1.pdMux;
			break;

		case 0x08 / 4:		//PE_MUX
			if (write)
				ioc->g1.peMux = val & 0x03c3;
			else
				val = ioc->g1.peMux;
			break;

		case 0x0c / 4:		//TIMER_MUX
			if (write)
				ioc->g1.timerMux = val;
			else
				val = ioc->g1.timerMux;
			break;

		case 0x10 / 4:		//LCD_MUX
			if (write) {
				static const char *lcdModes[] = {"none (000)", "4bit single mono stn", "4bit dual mono stn", "8 bit stn", "TFT", "none (101)", "none (110)", "none (111)", };

				fprintf(stderr, "IOCON: LCD mode now %s\n", lcdModes[val & 7]);
				ioc->g1.lcdMux = val & 0x07;
			}
			else
				val = ioc->g1.lcdMux;
			break;

		case 0x14 / 4:		//PA_RES_MUX
			if (write)
				ioc->g1.paResMux = val;
			else
				val = ioc->g1.paResMux;
			break;

		case 0x18 / 4:		//PB_RES_MUX
			if (write)
				ioc->g1.pbResMux = val & 0x0fff;
			else
				val = ioc->g1.pbResMux;
			break;

		case 0x1c / 4:		//PC_RES_MUX
			if (write)
				ioc->g1.pcResMux = val;
			else
				val = ioc->g1.pcResMux;
			break;

		case 0x20 / 4:		//PD_RES_MUX
			if (write)
				ioc->g1.pdResMux = val & 0x3fff;
			else
				val = ioc->g1.pdResMux;
			break;

		case 0x024/ 4:		//PE_RES_MUX
			if (write)
				ioc->g1.peResMux = val;
			else
				val = ioc->g1.peResMux;
			break;

		case 0x28 / 4:		//ADC_MUX
			if (write)
				ioc->g1.adcMux = val & 0xff;
			else
				val = ioc->g1.adcMux;
			break;

		default:
			return false;
	}

	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

struct Lh7xxxxIocon* lh7xxxxIoconInit(struct ArmMem *mem, bool bootIn16bitMode)
{
	struct Lh7xxxxIocon *ioc = (struct Lh7xxxxIocon*)calloc(1, sizeof(*ioc));
	
	if (!ioc)
		ERR("cannot alloc IOCON");
	
	if (deviceGetSocRev()) {

		memcpy(ioc->g2.vals, mRegValsG2, sizeof(mRegValsG2));
	}
	else {
	
		ioc->g1.ebiMux = bootIn16bitMode ? 0x4000 : 0x0000;
		ioc->g1.paResMux = 0xaaaa;
		ioc->g1.pbResMux = 0x0555;
		ioc->g1.pdResMux = 0x095a;
		ioc->g1.peResMux = 0x4455;
	}

	if (!memRegionAdd(mem, IOCON_BASE, IOCON_SIZE, deviceGetSocRev() ? ioconGen2prvMemAccessF : ioconGen1prvMemAccessF, ioc))
		ERR("cannot add IOCON to MEM\n");
	
	return ioc;
}




