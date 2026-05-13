#include "at29lv010a.h"
#include <stdlib.h>
#include <string.h>
#include "util.h"


#define VERBOSE					1



#define PAGE_SIZE				128
#define PAGE_COUNT				1024
#define MANUF_CODE				0x1f
#define PRODUCT_CODE			0x35
#define WRITE_TIMER				10			//units of 32KHz clock ticks
#define LOAD_TIMEOUT			75


enum FlashState {
	FlashStateIdle,
	FlashStateUnlockStage1,
	FlashStateUnlockStage2,
	FlashStateProductID,
	FlashStateRxWriteData,
	FlashStateWriting,
};

struct AT29LV010A {

	FILE *file;
	uint32_t pa;
	enum FlashState state;

	uint32_t writeAddr;			//adr of last write
	uint8_t writeByte;			//val of last write
	uint8_t numBytesWritten;
	uint16_t writeCounter;		//write timer and write load timeout

	uint8_t writePtr;
	uint8_t buf[PAGE_SIZE];
};

static int16_t at29lv010aPrvRead(struct AT29LV010A *flash, uint32_t flashAddr)
{
	int16_t ret = -1;
	uint8_t val;

	switch (flash->state) {
		case FlashStateIdle:
			if (!flash->file || fseek(flash->file, flashAddr, SEEK_SET) || 1 != fread(&val, 1, 1, flash->file)) {

				if (flash->file)
					fprintf(stderr, "AT29LV010A: failed to read at offset 0x%08x\n", flashAddr);
				val = 0xff;
			}
			ret = (uint16_t)val;
			break;

		case FlashStateRxWriteData:
		case FlashStateUnlockStage1:
		case FlashStateUnlockStage2:
			fprintf(stderr, "AT29LV010A sequence violation, reading in state %u\n", flash->state);
			break;

		case FlashStateProductID:
			switch (flashAddr) {
				case 0:
					ret = MANUF_CODE;
					break;

				case 1:
					ret = PRODUCT_CODE;
					break;

				default:
					fprintf(stderr, "AT29LV010A sequence violation, reading addr 0x%08x in ID state\n", flashAddr);
					break;
			}
			break;

		case FlashStateWriting:

			flash->writeByte ^= 0x40;		//any read toggles toggle bit

			if (flashAddr == flash->writeAddr) {

				ret = flash->writeByte ^ 0x80;	//read verification is only same as written addr
			}
			else {

				ret = flash->writeByte & 0x40;
			}
			break;

		default:
			//not reached
			break;
	}

	if (VERBOSE)
		fprintf(stderr, "AT29LV010A RD [0x%08x] -> 0x%04x, state is %u\n", flashAddr, (0xffff & ret), flash->state);

	return ret;
}

static void at29lv010aPrvStartWrite(struct AT29LV010A *flash)
{
	flash->writeCounter = WRITE_TIMER;
	flash->state = FlashStateWriting;

	if (flash->file) {

		if (fseek(flash->file, flash->writeAddr / PAGE_SIZE * PAGE_SIZE, SEEK_SET) || PAGE_SIZE != fwrite(flash->buf, 1, PAGE_SIZE, flash->file)) {

			fprintf(stderr, "AT29LV010A write fail to offset 0x%08x\n", flash->writeAddr / PAGE_SIZE * PAGE_SIZE);
		}
		else if (VERBOSE) {

			fprintf(stderr, "AT29LV010A write %u bytes to 0x%08x\n", PAGE_SIZE, flash->writeAddr / PAGE_SIZE * PAGE_SIZE);
		}
	}
}

static bool at29lv010aPrvWrite(struct AT29LV010A *flash, uint32_t flashAddr, uint8_t byte)
{
	if (VERBOSE)
		fprintf(stderr, "AT29LV010A WR [0x%08x] <- 0x%02x, state is %u\n", flashAddr, byte, flash->state);

	switch (flash->state) {
		case FlashStateWriting:
			break;

		case FlashStateIdle:
			if (flash->state == FlashStateIdle && !(flashAddr & 0x7f)) {		//le sigh, this is not as per spec but it seems to work and pixter does this ...

				flash->state = FlashStateRxWriteData;
				memset(flash->buf, 0xff, sizeof(flash->buf));
				flash->numBytesWritten = 0;
				flash->writeCounter = LOAD_TIMEOUT;
				goto accept_written_byte;
			}
			//fallthrough

		case FlashStateProductID:
			if ((flashAddr & 0x7fff) == 0x5555 && byte == 0xaa) {

				flash->state = FlashStateUnlockStage1;
				return true;
			}
			break;

		case FlashStateUnlockStage1:
			if ((flashAddr & 0x7fff) == 0x2aaa && byte == 0x55) {

				flash->state = FlashStateUnlockStage2;
				return true;
			}
			break;
		
		case FlashStateUnlockStage2:
			if ((flashAddr & 0x7fff) == 0x5555) {
				switch (byte) {
					case 0xa0:
						flash->state = FlashStateRxWriteData;
						memset(flash->buf, 0xff, sizeof(flash->buf));
						flash->numBytesWritten = 0;
						flash->writeCounter = LOAD_TIMEOUT;
						return true;

					case 0x90:
						flash->state = FlashStateProductID;
						return true;

					case 0xf0:
						flash->state = FlashStateIdle;
						return true;
					
					default:
						break;
				}
			}
			break;

		case FlashStateRxWriteData:
	accept_written_byte:
			if (flash->numBytesWritten && (flashAddr ^ flash->writeAddr) / PAGE_SIZE != 0) {

				fprintf(stderr, "AT29LV010A sequence violation, loading 0x%02x to offset 0x%08x  after previous load to 0x%08x\n", byte, flashAddr, flash->writeAddr);
				flash->state = FlashStateIdle;
				return false;
			}
			flash->writeAddr = flashAddr;
			flash->writeByte = byte;
			flash->buf[flashAddr % PAGE_SIZE] = byte;
			flash->writeCounter = LOAD_TIMEOUT;
			if (++flash->numBytesWritten == PAGE_SIZE)
				at29lv010aPrvStartWrite(flash);
			return true;
	}

	fprintf(stderr, "AT29LV010A sequence violation, writing 0x%02x to offset 0x%08x in state %u\n", byte, flashAddr, flash->state);
	flash->state = FlashStateIdle;
	return false;
}

static bool at29lv010aPrvAccess(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* bufP)
{
	struct AT29LV010A *flash = (struct AT29LV010A*)userData;
	
	pa -= flash->pa;
	pa %= PAGE_SIZE * PAGE_COUNT;

	if (size != 1) {

		return false;
	}
	else if (write) {

		return at29lv010aPrvWrite(flash, pa, *(const uint8_t*)bufP);
	}
	else {

		int16_t ret = at29lv010aPrvRead(flash, pa);

		if (ret < 0)
			return false;

		*(uint8_t*)bufP = ret;

		return true;
	}
}

struct AT29LV010A* at29lv010aInit(struct ArmMem *mem, uint32_t adr, uint32_t vaSize, FILE *file)
{
	struct AT29LV010A *flash = calloc(1, sizeof(*flash));

	if (!flash)
		return NULL;

	if (!file) {
		fprintf(stderr, "AT29LV010A: no file given - changes will be discarded\n");
	}

	flash->pa = adr;
	flash->file = file;
	flash->state = FlashStateIdle;

	if (file) {
		//if file is too small, fill with 0xFFFF
		fseek(file, 0, SEEK_END);
		while (ftell(file) < PAGE_SIZE * PAGE_COUNT) {
			uint8_t ff = 0xff;

			fwrite(&ff, 1, 1, file);
		}
	}

	if (!memRegionAdd(mem, adr, vaSize, at29lv010aPrvAccess, flash))
		ERR("cannot add AT29LV010A at 0x%08x to MEM\n", adr);

	return flash;
}

void at29lv010aPeriodic(struct AT29LV010A *flash)
{
	if (flash->state == FlashStateWriting) {

		if (!--flash->writeCounter)	//write over?
			flash->state = FlashStateIdle;
	}

	if (flash->state == FlashStateRxWriteData) {

		if (!--flash->writeCounter) {
		
			//load timeout
			at29lv010aPrvStartWrite(flash);
		}
	}
}






