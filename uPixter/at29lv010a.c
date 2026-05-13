#include "at29lv010a.h"
#include <stdlib.h>
#include <string.h>


#define VERBOSE					0



#define PAGE_SIZE				128
#define PAGE_COUNT				1024
#define MANUF_CODE				0x1f
#define PRODUCT_CODE			0x35
#define WRITE_TIMER				10			//units of 32KHz clock ticks
#define LOAD_TIMEOUT			75

#define PIXTER_BANKS			((PAGE_SIZE * PAGE_COUNT) / 32768)
#define PIXTER_ADR_SPACE_BANKS	32
#define PIXTER_ADR_SPACE_BASE	0xA0
#define PIXTER_BANK_SIZE		0x8000
#define PIXTER_MAP_BASE			0x4000
#define PIXTER_MAP_SIZE			PIXTER_BANK_SIZE

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
	enum FlashState state;

	uint32_t writeAddr;			//adr of last write
	uint8_t writeByte;			//val of last write
	uint8_t numBytesWritten;
	uint8_t curBank;
	uint16_t writeCounter;		//write timer and write load timeout

	uint8_t writePtr;
	uint8_t buf[PAGE_SIZE];
};

struct AT29LV010A* at29lv010aInit(FILE *backingFile)
{
	struct AT29LV010A *flash = calloc(1, sizeof(*flash));

	if (flash) {

		flash->file = backingFile;
		flash->state = FlashStateIdle;
	}

	return flash;
}

void at29lv010aBankSel(struct AT29LV010A *flash, uint8_t bankSel)
{
	flash->curBank = bankSel;
}

static bool at29lv010aPrvMapAddr(struct AT29LV010A *flash, uint16_t busAddr, uint32_t *flashAddrP)	//return true if any mnore logic is expected of us
{
	if (busAddr < PIXTER_MAP_BASE || busAddr - PIXTER_MAP_BASE >= PIXTER_MAP_SIZE)
		return false;

	if (flash->curBank < PIXTER_ADR_SPACE_BASE || flash->curBank - PIXTER_ADR_SPACE_BASE >= PIXTER_ADR_SPACE_BANKS)
		return false;

	*flashAddrP = ((flash->curBank - PIXTER_ADR_SPACE_BASE) % PIXTER_BANKS) * PIXTER_BANK_SIZE + busAddr % PIXTER_BANK_SIZE;

	return true;
}

int16_t at29lv010aRead(struct AT29LV010A *flash, uint16_t busAddr)
{
	uint32_t flashAddr;
	int16_t ret = -1;
	uint8_t val;

	if (!at29lv010aPrvMapAddr(flash, busAddr, &flashAddr))
		return -1;

	switch (flash->state) {
		case FlashStateIdle:
			if (fseek(flash->file, flashAddr, SEEK_SET) || 1 != fread(&val, 1, 1, flash->file))
				val = 0xff;
			ret = (uint16_t)val;
			break;

		case FlashStateRxWriteData:
		case FlashStateUnlockStage1:
		case FlashStateUnlockStage2:
			fprintf(stderr, "flash sequence violation, reading in state %u\n", flash->state);
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
					fprintf(stderr, "flash sequence violation, reading addr 0x%08x in ID state\n", flashAddr);
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
		fprintf(stderr, "FLASH RD [0x%02x.0x%04x -> 0x%08x] -> 0x%04x, state is %u\n", flash->curBank, busAddr, flashAddr, (0xffff & ret), flash->state);

	return ret;
}

static void at29lv010aPrvStartWrite(struct AT29LV010A *flash)
{
	flash->writeCounter = WRITE_TIMER;
	flash->state = FlashStateWriting;

	if (fseek(flash->file, flash->writeAddr / PAGE_SIZE * PAGE_SIZE, SEEK_SET) || PAGE_SIZE != fwrite(flash->buf, 1, PAGE_SIZE, flash->file)) {

		fprintf(stderr, "flash write fail to offset 0x%08x\n", flash->writeAddr / PAGE_SIZE * PAGE_SIZE);
	}
}

bool at29lv010aWrite(struct AT29LV010A *flash, uint16_t busAddr, uint8_t byte)
{
	uint32_t flashAddr;

	if (!at29lv010aPrvMapAddr(flash, busAddr, &flashAddr))
		return false;
	
	if (VERBOSE)
		fprintf(stderr, "FLASH WR [0x%02x.0x%04x -> 0x%08x] <- 0x%02x, state is %u\n", flash->curBank, busAddr, flashAddr, byte, flash->state);

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

				fprintf(stderr, "flash sequence violation, loading 0x%02x to offset 0x%08x  after previous load to 0x%08x\n", byte, flashAddr, flash->writeAddr);
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

	fprintf(stderr, "flash sequence violation, writing 0x%02x to offset 0x%08x in state %u\n", byte, flashAddr, flash->state);
	flash->state = FlashStateIdle;
	return false;
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





