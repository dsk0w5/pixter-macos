#include "almostSst39sf010a.h"
#include <stdlib.h>
#include <string.h>




#define VERBOSE					0



#define PAGE_SIZE				1024
#define PAGE_COUNT				128
#define MANUF_CODE				0xc7
#define PRODUCT_CODE			0xd5


#define PIXTER_BANKS			((PAGE_SIZE * PAGE_COUNT) / 32768)
#define PIXTER_ADR_SPACE_BANKS	32
#define PIXTER_ADR_SPACE_BASE	0x80
#define PIXTER_BANK_SIZE		0x8000
#define PIXTER_MAP_BASE			0x4000
#define PIXTER_MAP_SIZE			PIXTER_BANK_SIZE

#define PAGE_ERASE_TIME			100
#define CHIP_ERASE_TIME			1000
#define BYTE_PROGRAM_TIME		3


enum FlashState {
	FlashStateIdle,
	FlashStateUnlockStage1,
	FlashStateUnlockStage2,
	FlashStateUnlockStage3,
	FlashStateUnlockStage4,
	FlashStateUnlockStage5,
	FlashStateProductID,
	FlashStateRxWriteData,
	FlashStateErasing,
	FlashStateEraseDone,
	FlashStateWriting,
	FlashStateWriteDone,
	FlashStateInvalid,
};

struct AlmostSST39SF010A {

	FILE *file;
	enum FlashState state;

	uint8_t writeByte;			//val of last write
	uint8_t numBytesWritten;
	uint8_t curBank;

	uint16_t busyCounter;		//write timer and write load timeout
};

struct AlmostSST39SF010A* almostSst39sf010aInit(FILE *backingFile)
{
	struct AlmostSST39SF010A *flash = calloc(1, sizeof(*flash));

	if (flash) {

		flash->file = backingFile;
		flash->state = FlashStateIdle;
	}

	return flash;
}

void almostSst39sf010aBankSel(struct AlmostSST39SF010A *flash, uint8_t bankSel)
{
	flash->curBank = bankSel;
}

static bool almostSst39sf010aPrvMapAddr(struct AlmostSST39SF010A *flash, uint16_t busAddr, uint32_t *flashAddrP)	//return true if any mnore logic is expected of us
{
	if (busAddr < PIXTER_MAP_BASE || busAddr - PIXTER_MAP_BASE >= PIXTER_MAP_SIZE)
		return false;

	if (flash->curBank < PIXTER_ADR_SPACE_BASE || flash->curBank - PIXTER_ADR_SPACE_BASE >= PIXTER_ADR_SPACE_BANKS)
		return false;

	*flashAddrP = ((flash->curBank - PIXTER_ADR_SPACE_BASE) % PIXTER_BANKS) * PIXTER_BANK_SIZE + busAddr % PIXTER_BANK_SIZE;

	return true;
}

static bool almostSst39sf010aPrvProgram(struct AlmostSST39SF010A *flash, uint32_t flashAddr, uint8_t byte)
{
	uint8_t prevVal, newVal;

	if (fseek(flash->file, flashAddr, SEEK_SET))
		return false;

	if (1 != fread(&prevVal, 1, 1, flash->file))
		return false;

	if (fseek(flash->file, flashAddr, SEEK_SET))
		return false;

	newVal = prevVal & byte;

	if (1 != fwrite(&newVal, 1, 1, flash->file))
		return false;

	if (VERBOSE)
		fprintf(stderr, "WRITE BYTE 0x%02x (becoming 0x%02x due to existing value 0x%02x) at 0x%08x\n", byte, newVal, prevVal, flashAddr);

	flash->writeByte = newVal;

	return true;
}

static bool almostSst39sf010aPrvRead(struct AlmostSST39SF010A *flash, uint32_t flashAddr, uint8_t *byteP)
{
	uint8_t byte;

	if (fseek(flash->file, flashAddr, SEEK_SET))
		return false;

	if (1 != fread(&byte, 1, 1, flash->file))
		return false;

	if (VERBOSE)
		fprintf(stderr, "READ BYTE 0x%02x at 0x%08x\n", byte, flashAddr);

	*byteP = byte;

	return true;
}

static bool almostSst39sf010aPrvErasePage(struct AlmostSST39SF010A *flash, uint32_t pageIdx)
{
	uint32_t blank = 0xffffffff;
	uint32_t i;
	
	if (fseek(flash->file, pageIdx * PAGE_SIZE, SEEK_SET))
		return false;
	
	if (VERBOSE)
		fprintf(stderr, "ERASE PAGE at 0x%08x\n", pageIdx * PAGE_SIZE);

	for (i = 0; i < PAGE_SIZE; i += sizeof(blank)) {

		if (sizeof(blank) != fwrite(&blank, 1, sizeof(blank), flash->file))
			return false;
	}
	return true;
}

static bool almostSst39sf010aPrvEraseChip(struct AlmostSST39SF010A *flash)
{
	uint32_t page;

	for (page = 0; page < PAGE_COUNT; page++) {

		if (!almostSst39sf010aPrvErasePage(flash, page))
			return false;
	}
	return true;
}

int16_t almostSst39sf010aRead(struct AlmostSST39SF010A *flash, uint16_t busAddr)
{
	uint32_t flashAddr;
	int16_t ret = -1;
	uint8_t val;

	if (!almostSst39sf010aPrvMapAddr(flash, busAddr, &flashAddr))
		return -1;

	switch (flash->state) {
		case FlashStateIdle:
			if (!almostSst39sf010aPrvRead(flash, flashAddr, &val))
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

		case FlashStateEraseDone:
		case FlashStateWriteDone:
			ret = 0xff;
			break;

		case FlashStateErasing:
			flash->writeByte ^= 0x40;
			ret = flash->writeByte & 0x40;
			break;

		case FlashStateWriting:
			flash->writeByte ^= 0x40;
			ret = (flash->writeByte ^ 0x80) & 0xc0;
			break;

		default:
			//not reached
			break;
	}

	if (VERBOSE)
		fprintf(stderr, "FLASH RD [0x%02x.0x%04x -> 0x%08x] -> 0x%04x, state is %u\n", flash->curBank, busAddr, flashAddr, (0xffff & ret), flash->state);

	return ret;
}

bool almostSst39sf010aWrite(struct AlmostSST39SF010A *flash, uint16_t busAddr, uint8_t byte)
{
	uint32_t flashAddr;

	if (!almostSst39sf010aPrvMapAddr(flash, busAddr, &flashAddr))
		return false;
	
	if (VERBOSE)
		fprintf(stderr, "FLASH WR [0x%02x.0x%04x -> 0x%08x] <- 0x%02x, state is %u\n", flash->curBank, busAddr, flashAddr, byte, flash->state);

	switch (flash->state) {
		case FlashStateWriting:
		case FlashStateErasing:
			break;

		case FlashStateInvalid:
		case FlashStateIdle:
		case FlashStateProductID:
		case FlashStateEraseDone:
		case FlashStateWriteDone:
			if (byte == 0xf0) {
				flash->state = FlashStateIdle;
				return true;
			}

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
		
		case FlashStateUnlockStage3:
			if ((flashAddr & 0x7fff) == 0x5555 && byte == 0xaa) {

				flash->state = FlashStateUnlockStage4;
				return true;
			}
			break;

		case FlashStateUnlockStage4:
			if ((flashAddr & 0x7fff) == 0x2aaa && byte == 0x55) {

				flash->state = FlashStateUnlockStage5;
				return true;
			}
			break;

		case FlashStateUnlockStage5:
			if (byte == 0x30) {

				if (!almostSst39sf010aPrvErasePage(flash, flashAddr / PAGE_SIZE))
					break;
				flash->state = FlashStateErasing;
				flash->busyCounter = PAGE_ERASE_TIME;
				return true;
			}
			else if (byte == 0x10) {
				if (!almostSst39sf010aPrvEraseChip(flash))
					break;
				flash->state = FlashStateErasing;
				flash->busyCounter = CHIP_ERASE_TIME;
				return true;
			}
			break;

		case FlashStateUnlockStage2:
			if ((flashAddr & 0x7fff) == 0x5555) {
				switch (byte) {
					case 0xa0:
						flash->state = FlashStateRxWriteData;
						return true;

					case 0x80:
						flash->state = FlashStateUnlockStage3;
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
			if (!almostSst39sf010aPrvProgram(flash, flashAddr, byte))
				break;
			flash->state = FlashStateErasing;
			flash->busyCounter = BYTE_PROGRAM_TIME;
			return true;
	}

	fprintf(stderr, "flash sequence violation, writing 0x%02x to offset 0x%08x in state %u\n", byte, flashAddr, flash->state);
	flash->state = FlashStateInvalid;
	return false;
}

void almostSst39sf010aPeriodic(struct AlmostSST39SF010A *flash)
{
	if (flash->busyCounter && !--flash->busyCounter) {

		if (flash->state == FlashStateErasing)	{

			flash->state = FlashStateEraseDone;
		}
		else if (flash->state == FlashStateWriting) {

			flash->state = FlashStateWriteDone;
		}
		else {

			fprintf(stderr, "flash sequence violation, work finishes in state %u\n", flash->state);
			flash->state = FlashStateInvalid;
		}
	}
}





