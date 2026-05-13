#include "gpiodev_PixterBbBex.h"
#include "soc_GPIO.h"
#include <stdlib.h>
#include <string.h>
#include "mem.h"
#include "ROM.h"

struct PixerBbBex {
	struct SocGpio *gpio;

	uint8_t stateGpioLo, stateGpioHi;
	uint8_t busGpios[8];

	uint8_t busState;				//e1..e0
	uint16_t flashAddr;
	uint32_t curPagePtr;
	uint32_t curSpecialPtr;

	struct ArmMem *savegameMem;

	struct MelodyChip *cartMelodyChip;

	//current bex chip state
	uint8_t curPageNo, bexCfg, bexPort[3];

	uint8_t data[];
};


#define VERBOSE			0
#define PAGE_SIZE		0x8000



static uint8_t pixterPrvBitBangedFlashReadCycle(struct PixerBbBex *bex, uint16_t addr)
{
	uint32_t rawAccessAddr;
	uint8_t val[64];

	if (VERBOSE)
		fprintf(stderr, "BBF: RD [0x%04x]\n", addr);

	switch (addr >> 13) {
		case 2:	//0x4000 .. 0xBFFF
		case 3:
		case 4:
		case 5:
			rawAccessAddr = (addr & 0x7fff) + bex->curPagePtr;
	do_access:
			if (!memAccess(bex->savegameMem, rawAccessAddr, 1, MEM_ACCESS_TYPE_READ | MEM_ACCCESS_FLAG_NOERROR, val)) {
				
				fprintf(stderr, "BBF: RD [0x%04x] -> ???? (raw access addr 0x%08x)\n", addr, rawAccessAddr);
			}
			return val[0];

		case 1:	//0x2000..0x3fff
			rawAccessAddr = (addr & 0x1fff) + bex->curSpecialPtr;
			goto do_access;

		case 0:	//0x0000..0x1fff
			switch (addr) {
				case 0x00:	//page select
					return bex->curPageNo;

				case 0x20 ... 0x22:
					return bex->bexPort[addr - 0x20];

				case 0x23:	//chip config
					return bex->bexCfg;

				default:
					break;
			}
			break;

		default:
			break;
	}

	fprintf(stderr, "BBF: RD [0x%04x] -> ????\n", addr);
	return 0;
}

static void pixterPrvBitBangedFlashWriteCycle(struct PixerBbBex *bex, uint16_t addr, uint8_t val)
{
	uint32_t rawAccessAddr;
	uint8_t valInitial = val;

	if (VERBOSE)
		fprintf(stderr, "BBF: WR [0x%04x] <- 0x%02x\n", addr, valInitial);

	switch (addr >> 13) {
		case 2:	//0x4000 .. 0xBFFF
		case 3:
		case 4:
		case 5:
			rawAccessAddr = (addr & 0x7fff) + bex->curPagePtr;
	do_access:
			if (!memAccess(bex->savegameMem, rawAccessAddr, 1, MEM_ACCESS_TYPE_WRITE | MEM_ACCCESS_FLAG_NOERROR, &val)) {
				
				fprintf(stderr, "BBF: WR [0x%04x] <- 0x%02x, FAILED (raw access addr 0x%08x)\n", addr, valInitial, rawAccessAddr);
			}
			return;

		case 1:	////0x2000..0x3fff
			rawAccessAddr = (addr & 0x1fff) + bex->curSpecialPtr;
			goto do_access;

		case 0:
			switch (addr) {
				case 0x00:	//page select
					if (VERBOSE)
						fprintf(stderr, "page sel 0x%02x\n", val);

					bex->curPageNo = val;
					switch (val >> 5) {
						case 0b100:	//internal NOR
							bex->curPagePtr = BB_BUS_INTERNAL_NOR_ADDR + PAGE_SIZE * (val & 0x1f);
							bex->curSpecialPtr = BB_BUS_INTERNAL_NOR_ADDR + BB_BUS_ADDEND_SPECIAL_REGION;
							break;

						case 0b101:	//cart NOR
							bex->curPagePtr = BB_BUS_EXTERNAL_CART_NOR_ADDR + PAGE_SIZE * (val & 0x1f);
							bex->curSpecialPtr = BB_BUS_EXTERNAL_CART_NOR_ADDR + BB_BUS_ADDEND_SPECIAL_REGION;
							break;

						case 0b110:	//cart ROM
							bex->curPagePtr = BB_BUS_EXTERNAL_CART_ROM_ADR + PAGE_SIZE * (val & 0x1f);
							bex->curSpecialPtr = BB_BUS_EXTERNAL_CART_ROM_ADR + BB_BUS_ADDEND_SPECIAL_REGION;
							break;

						default:
							bex->curPagePtr = 0;
							bex->curSpecialPtr = 0;
							break;
					}
					return;

				case 0x20 ... 0x22:
					if (addr == 0x20 && (bex->bexCfg >> 5) == 2) {	//control of cart music
						
						uint_fast8_t prevState = bex->bexPort[addr - 0x20];
						uint_fast8_t curState = val;
						uint_fast8_t changed = (prevState ^ curState) & 0x30;	//pins we care about

						if (changed && bex->cartMelodyChip)
							melodyChipControlGpioStateChange(bex->cartMelodyChip, !!(curState & 0x20), !!(curState & 0x10));
					}
					bex->bexPort[addr - 0x20] = val;
					return;

				case 0x23:	//chip config
					bex->bexCfg = val;
					return;
			}
			break;

		default:
			break;
	}

	fprintf(stderr, "BBF: WR [0x%04x] <- 0x%02x, UNHANDLED\n", addr, valInitial);
}

static int_fast16_t pixterPrvReadGpio(struct PixerBbBex *bex, uint_fast8_t gpioNo)	//negative if pin is not outut
{
	switch (socGpioGetState(bex->gpio, gpioNo)) {
		case SocGpioStateLow:		return 0;
		case SocGpioStateHigh:		return 1;
		default:					return -1;
	}
}

static int_fast16_t pixterBbBexPrvBusRead(struct PixerBbBex *bex)	//returns neagtive on error
{
	uint_fast16_t ret = 0, i;

	for (i = 0; i < 8; i++) {

		int_fast16_t t = pixterPrvReadGpio(bex, bex->busGpios[i]);
		if (t < 0)
			return t;

		ret |= t << i;
	}
	return ret;
}

static void pixterBbBexPrvBusWrite(struct PixerBbBex *bex, uint_fast8_t val)
{
	uint_fast16_t i;

	for (i = 0; i < 8; i++, val >>= 1)
		socGpioSetState(bex->gpio, bex->busGpios[i], val & 1);
}

static void pixterBbBexPrvBitBangedFlashBusStateChanged(void* userData, uint32_t gpio, bool oldPinState, bool newPinState)
{
	struct PixerBbBex *bex = (struct PixerBbBex*)userData;
	uint_fast8_t newBusState = 0;
	int_fast16_t t;

	t = pixterPrvReadGpio(bex, bex->stateGpioLo);
	if (t < 0)
		return;
	newBusState += t * 1;
	t = pixterPrvReadGpio(bex, bex->stateGpioHi);
	if (t < 0)
		return;
	newBusState += t * 2;

	if (newBusState == bex->busState)
		return;

	t = pixterBbBexPrvBusRead(bex);

	switch (bex->busState * 4 + newBusState) {		//"old new"
		case 0b1110:	//11 -> 10   flash samples high byte of the address word
			if (t < 0)
				fprintf(stderr, "BBF: cannot read bus on ADDR_HI edge\n");
			else
				bex->flashAddr = (bex->flashAddr & 0x00ff) + 256 * t;
			break;

		case 0b1000:	//10 -> 00   flash samples low byte of the address word, stays in INPUT mode (this is a write cycle)
			if (t < 0)
				fprintf(stderr, "BBF: cannot read bus on ADDR_LO_WR edge\n");
			else
				bex->flashAddr = (bex->flashAddr & 0xff00) + t;
			break;

		case 0b0011:	//00 -> 11   flash samples written data, we're back to normal
			if (t < 0)
				fprintf(stderr, "BBF: cannot read bus on DATA_WR edge\n");
			else
				pixterPrvBitBangedFlashWriteCycle(bex, bex->flashAddr, t);
			break;

		case 0b1001:	//10 -> 01   flash samples low byte of the address word, goes to OUTPUT mode (this is a read cycle), pushes data out on the bus immediately
			if (t < 0)
				fprintf(stderr, "BBF: cannot read bus on ADDR_LO_RD edge\n");
			else {

				bex->flashAddr = (bex->flashAddr & 0xff00) + t;
				pixterBbBexPrvBusWrite(bex, pixterPrvBitBangedFlashReadCycle(bex, bex->flashAddr));
			}
			break;

		case 0b0111:	//01 -> 11   flash returns to normal after a read cycle
			break;

		default:
			fprintf(stderr, "BBF: unknown transition %u%u -> %u%u\n", bex->busState >> 1, bex->busState & 1, newBusState >> 1, newBusState & 1);
			break;
	}
	bex->busState = newBusState;
}

struct PixerBbBex* pixterBbBexInit(struct SocGpio *gpio, uint8_t stateGpioLo, uint8_t stateGpioHi, const uint8_t busGpios[static 8])
{
	struct PixerBbBex *bex;

	bex = calloc(1, sizeof(struct PixerBbBex));
	if (bex) {

		bex->savegameMem = memInit();

		bex->stateGpioLo = stateGpioLo;
		bex->stateGpioHi = stateGpioHi;
		memcpy(bex->busGpios, busGpios, sizeof(bex->busGpios));
		bex->busState = 3;
		bex->gpio = gpio;

		bex->curPagePtr = 0;
		bex->curSpecialPtr = 0;

		socGpioSetNotif(bex->gpio, stateGpioLo, pixterBbBexPrvBitBangedFlashBusStateChanged, bex);
		socGpioSetNotif(bex->gpio, stateGpioHi, pixterBbBexPrvBitBangedFlashBusStateChanged, bex);
	}
	return bex;
}

void pixterBbBexAttachMelodyChip(struct PixerBbBex *bex, struct MelodyChip *cartMelodyChip)
{
	bex->cartMelodyChip = cartMelodyChip;
}

struct ArmMem* pixterBbBexGetBbBus(struct PixerBbBex *bex)
{
	return bex->savegameMem;
}



