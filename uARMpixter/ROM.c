//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include <string.h>
#include <stdlib.h>
#include <endian.h>
#include <stdio.h>
#include "util.h"
#include "mem.h"
#include "ROM.h"

#define STRATAFLASH_BLOCK_SIZE	0x20000ul

enum RomChipCurMode {
	RomChipNormalMode,

	StrataFlashReadStatus,
	StrataFlashSeen0x60,
	StrataFlashSetSTS,
	StrataFlashReadID,
	StrataFlashReadCFI,
	StrataFlashErzCy1,
	StrataFlashWriCy1,

	JedecUnlockStage1,
	JedecUnlockStage2,
	JedecEraseUnlockStage3,
	JedecEraseUnlockStage4,
	JedecEraseUnlockStage5,
	JedecErasing,
	JedecWriteReady,
	JedecWriteOngoing,
	JedecReadID,
};

struct ArmRom {

	uint32_t start, opAddr, vaSize, dataSize;
	uint32_t *buf;
	FILE *file;		//nonzero if file-backed
	enum RomChipType chipType;
	enum RomChipCurMode mode;
	uint16_t configReg, busyCy, stsReg, possibleConfigReg;

	bool jedecWriteOpSuccess;
	bool jedecWriteTopBit;

	//JEDEC flash only
	uint32_t blockSize;	//zero if there is no block erase command;
	uint32_t *sectorSizes;
	uint32_t numSectors, commandAddrMask;
	uint16_t jedecManuf, jedecPart;
};


static bool romPrvWrite(struct ArmRom *rom, uint32_t ofst, uint_fast16_t val)
{
//	fprintf(stderr, "FLASH write of 0x%04x at 0x%08lx\n", (unsigned)val, (unsigned long)ofst);
	
	ofst %= rom->dataSize;

	switch (rom->chipType) {
		case RomJedecFlashX8:
		case RomPixterFlashX8:
			*(((char*)rom->buf) + ofst) &= val;
			break;
		case RomJedecFlashX16:
		case RomStrataFlash16x:
			if (rom->dataSize - ofst < 2)		//no wraparound programming. it is possible, but likely a big
				return false;
			*(uint16_t*)(((char*)rom->buf) + ofst) &= le16toh(val);
			break;
		
		case RomStrataflash16x2x:
			if (rom->dataSize - ofst < 4)		//no wraparound programming. it is possible, but likely a big
				return false;
			*(uint16_t*)(((char*)rom->buf) + ofst) &= le16toh(val);
			*(uint16_t*)(((char*)rom->buf) + ofst + 0) &= le16toh(val);
			*(uint16_t*)(((char*)rom->buf) + ofst + 2) &= le16toh(val >> 16);
			break;
		
		default:
			return false;
	}

	if (rom->file) {
		rewind(rom->file);
		(void)fwrite(rom->buf, 1, rom->dataSize, rom->file);	//let libc buffer it :)
	}
	
	return true;
}

static uint32_t romPrvGetSectorSz(struct ArmRom *rom, uint32_t byteOfst)	//returns size if known, else zero
{
	uint32_t i, curOfst = 0;
	
	switch (rom->chipType) {
		case RomStrataFlash16x:
			return STRATAFLASH_BLOCK_SIZE;
		
		case RomStrataflash16x2x:
			return STRATAFLASH_BLOCK_SIZE * 2;
		
		case RomJedecFlashX8:
		case RomPixterFlashX8:
		case RomJedecFlashX16:
			break;

		default:
			return 0;
	}

	for (i = 0; i < rom->numSectors; i++) {
		uint32_t thisSecSize = rom->sectorSizes[i];
		
		if (curOfst > byteOfst)	//addr is in the middle of last sector
			return 0;
		if (curOfst == byteOfst)
			return thisSecSize;

		curOfst += thisSecSize;
	}
	return 0;
}

static int32_t romPrvGetOffsetIntoSector(struct ArmRom *rom, uint32_t byteOfst)	//returns offsert of given offset into the sector it is in, negative on error
{
	uint32_t i, curOfst = 0;
	
	switch (rom->chipType) {
		case RomStrataFlash16x:
			return byteOfst % STRATAFLASH_BLOCK_SIZE;
		
		case RomStrataflash16x2x:
			return byteOfst % (STRATAFLASH_BLOCK_SIZE * 2);
		
		case RomJedecFlashX8:
		case RomPixterFlashX8:
		case RomJedecFlashX16:
			break;

		default:
			return -1;
	}

	for (i = 0; i < rom->numSectors; i++) {
		uint32_t thisSecSize = rom->sectorSizes[i];

		if (curOfst > byteOfst)	//addr is in the middle of last sector
			return 0;

		if (byteOfst - curOfst < thisSecSize)
			return byteOfst - curOfst;

		curOfst += thisSecSize;
	}
	return -1;
}

static uint32_t romPrvGetBlockSz(struct ArmRom *rom, uint32_t byteOfst)	//returns size if known, else zero
{
	uint32_t i, curOfst = 0;
	
	switch (rom->chipType) {
		case RomJedecFlashX8:
		case RomPixterFlashX8:
		case RomJedecFlashX16:
			if (!rom->blockSize)
				return 0;
			if (byteOfst % rom->blockSize)
				return 0;
			return rom->blockSize;

		default:
			return 0;
	}
}

static bool romPrvDoErase(struct ArmRom *rom, uint32_t ofst, uint32_t len)	//assumes verified boundaries
{
	ofst %= rom->dataSize;
	if (rom->dataSize - ofst < len)		//no wraparound erasing. it is possible, but likely a big
		return false;

	memset(((char*)rom->buf) + ofst, 0xff, len);

	if (rom->file) {
		rewind(rom->file);
		(void)fwrite(rom->buf, 1, rom->dataSize, rom->file);	//let libc buffer it :)
	}

	return true;
}

static bool romPrvEraseSector(struct ArmRom *rom, uint32_t ofst)
{
	uint32_t sz = romPrvGetSectorSz(rom, ofst);

	if (!sz)
		ERR("cannot erase sector at rom offset 0x%08x, this is %d byte sinto a sector\n", ofst, romPrvGetOffsetIntoSector(rom, ofst));

	fprintf(stderr, "FLASH erase sector at 0x%08x\n", ofst);
	
	return romPrvDoErase(rom, ofst, sz);
}

static bool romPrvEraseBlock(struct ArmRom *rom, uint32_t ofst)
{
	uint32_t sz = romPrvGetBlockSz(rom, ofst);

	if (!sz)
		ERR("cannot erase block at rom offset 0x%08x\n", ofst);

	fprintf(stderr, "FLASH erase block at 0x%08x\n", ofst);

	return romPrvDoErase(rom, ofst, sz);
}

static bool romPrvEraseChip(struct ArmRom *rom)
{
	fprintf(stderr, "FLASH erase chip\n");

	return romPrvDoErase(rom, 0, rom->dataSize);
}

static bool romAccessGuts(struct ArmRom *rom, uint32_t pa, uint_fast8_t size, bool write, void* bufP)
{
	uint8_t *addr = (uint8_t*)rom->buf;
	uint32_t fromStart;
	
	fromStart = pa - rom->start;		//flashes care how far we are from start of flash, not of this arbitrary piece of it
	pa -= rom->start;
	pa %= rom->dataSize;

	addr += pa;
	
	if (write) {
		
		uint32_t addrBits, dataBits;
		bool diffData = false;
		
		addrBits = fromStart;
		
		switch (rom->chipType) {
			case RomWriteIgnore:
				return true;
			case RomWriteError:
				return false;
			case RomJedecFlashX8:
			case RomPixterFlashX8:
				if (size != 1) {
					fprintf(stderr, "JEDEC flash command of improper size!\n");
					return false;
				}
				dataBits = *(uint8_t*)bufP;
				break;
			case RomJedecFlashX16:
				if (size != 2) {
					fprintf(stderr, "JEDEC flash command of improper size!\n");
					return false;
				}
				dataBits = *(uint16_t*)bufP;
				addrBits /= 2;
				break;
			case RomStrataflash16x2x:
				if (size != 4) {
					fprintf(stderr, "StrataflashX2 command of improper size!\n");
					return false;
				}
				dataBits = *(uint32_t*)bufP;
				
				diffData = (dataBits & 0xffff) != (dataBits >> 16);
				dataBits &= 0xffff;
				addrBits /= 4;
				break;
			case RomStrataFlash16x:
				if (size != 2) {
					fprintf(stderr, "Strataflash command of improper size!\n");
					return false;
				}
				dataBits = *(uint16_t*)bufP;
				addrBits /= 2;
				break;
			default:
				return false;
		}
		
		if (rom->chipType == RomJedecFlashX16 || rom->chipType == RomJedecFlashX8 || rom->chipType == RomPixterFlashX8) {
			
			
			if (rom->mode == JedecWriteReady) {
				
				rom->mode = JedecWriteOngoing;

				rom->jedecWriteTopBit = (dataBits >> 7) & 1;
				rom->jedecWriteOpSuccess = romPrvWrite(rom, fromStart, dataBits);
				rom->busyCy = 10;
				return true;
			}
			if (dataBits == 0x00f0) {	//read
				rom->mode = RomChipNormalMode;
				return true;
			}
			if (dataBits == 0x0088) {	//enter secure mode (ignored)
				rom->mode = RomChipNormalMode;
				return true;
			}
			if (dataBits == 0x0000) {	//another reset (ignored)
				rom->mode = RomChipNormalMode;
				return true;
			}
			if (dataBits == 0x00AA && (addrBits & rom->commandAddrMask) == (0x55555 & rom->commandAddrMask) && rom->mode == RomChipNormalMode) {
				rom->mode = JedecUnlockStage1;
				return true;
			}
			if (dataBits == 0x0055 && (addrBits & rom->commandAddrMask) == (0x2AAAA & rom->commandAddrMask) && rom->mode == JedecUnlockStage1) {
				rom->mode = JedecUnlockStage2;
				return true;
			}
			if (dataBits == 0x0080 && (addrBits & rom->commandAddrMask) ==  (0x55555 & rom->commandAddrMask) && rom->mode == JedecUnlockStage2) {
				rom->mode = JedecEraseUnlockStage3;
				return true;
			}
			if (dataBits == 0x00a0 && (addrBits & rom->commandAddrMask) ==  (0x55555 & rom->commandAddrMask) && rom->mode == JedecUnlockStage2) {
				rom->mode = JedecWriteReady;
				return true;
			}
			if (dataBits == 0x00AA && (addrBits & rom->commandAddrMask) == (0x55555 & rom->commandAddrMask) && rom->mode == JedecEraseUnlockStage3) {
				rom->mode = JedecEraseUnlockStage4;
				return true;
			}
			if (dataBits == 0x0055 && (addrBits & rom->commandAddrMask) == (0x2AAAA & rom->commandAddrMask) && rom->mode == JedecEraseUnlockStage4) {
				rom->mode = JedecEraseUnlockStage5;
				return true;
			}
			if (dataBits == 0x0030 && rom->mode == JedecEraseUnlockStage5) {							//sector erase
				rom->mode = JedecErasing;

				rom->jedecWriteOpSuccess = romPrvEraseSector(rom, fromStart);
				rom->busyCy = 100;
				return true;
			}
			if (dataBits == 0x0050 && rom->mode == JedecEraseUnlockStage5) {							//block erase
				rom->mode = JedecErasing;

				rom->jedecWriteOpSuccess = romPrvEraseBlock(rom, fromStart);
				rom->busyCy = 200;
				return true;
			}
			if (dataBits == 0x0010 && (addrBits & rom->commandAddrMask) == (0x55555 & rom->commandAddrMask) && rom->mode == JedecEraseUnlockStage5) {		//chip erase
				rom->mode = JedecErasing;

				rom->jedecWriteOpSuccess = romPrvEraseChip(rom);
				rom->busyCy = 1000;
				return true;
			}
			
			if (dataBits == 0x90 && rom->mode == JedecUnlockStage2) {
				rom->mode = JedecReadID;
				return true;
			}
			fprintf(stderr, "Unknown JEDEC mode %u during write of 0x%04x\n", rom->mode, dataBits);
			return false;
		}
		else if (rom->chipType == RomStrataFlash16x || rom->chipType == RomStrataflash16x2x) {
			if (rom->mode == StrataFlashSetSTS) {
				if (diffData)
					return false;
				rom->stsReg = dataBits;
				rom->mode = RomChipNormalMode;
				return true;
			}
			else if (rom->mode == StrataFlashSeen0x60) {
				
				if (diffData)
					return false;
				
				if (dataBits == 0x03) {	//set read config reg
					
					if (rom->possibleConfigReg != addrBits) {
						
						fprintf(stderr, "Strataflash READ CONFIG REG SECOND CYCLE SAID 0x%04x, first was 0x%04x!\n", addrBits, rom->possibleConfigReg);
						return false;
					}
					rom->configReg = addrBits;
					rom->mode = RomChipNormalMode;
					return true;
				}
				else if (dataBits == 0x01 || dataBits == 0xd0 || dataBits == 0x2f) {
					
					fprintf(stderr, "strataflash block locking not supported\n");
					rom->mode = RomChipNormalMode;
					return true;
				}
				else {
					
					//unknown thing
					return false;
				}
			}
			else if (rom->mode == StrataFlashWriCy1) {	//due to the checks above for dup data, this is unlikely to work for writes to 32-bit-wide dual strata flash
				
				if (fromStart != rom->opAddr)
					return false;
				if (!romPrvWrite(rom, fromStart, dataBits))
					return false;
				rom->busyCy = 0x0010;
				rom->mode = StrataFlashReadStatus;
				return true;
			}
			else if (diffData) {
				
				fprintf(stderr, "strataflash: ignoring write of 0x%08x -> [0x%08x]\n", dataBits, fromStart);
				return true;
			}
			else switch (dataBits & 0xff) {
				
				case 0xb8:	//STS
					rom->mode = StrataFlashSetSTS;
					return true;
				
				case 0x50:
					if (rom->mode == StrataFlashErzCy1 || rom->mode == StrataFlashWriCy1)
						return false;
					
					//clear status register
					rom->mode = RomChipNormalMode;
					return true;
				
				case 0x60:
					if (rom->mode == StrataFlashErzCy1 || rom->mode == StrataFlashWriCy1)
						return false;
					
					//set read config reg
					rom->possibleConfigReg = addrBits;
					rom->mode = StrataFlashSeen0x60;
					return true;
				
				case 0x70:
					if (rom->mode == StrataFlashErzCy1 || rom->mode == StrataFlashWriCy1)
						return false;
					
					//read status register
					rom->mode = StrataFlashReadStatus;
					return true;
				
				case 0x20:
					if (rom->mode != RomChipNormalMode)
						return false;
					rom->mode = StrataFlashErzCy1;
					rom->opAddr = fromStart;
					return true;
				
				case 0x40:
					if (rom->mode != RomChipNormalMode)
						return false;
					rom->mode = StrataFlashWriCy1;
					rom->opAddr = fromStart;
					return true;
				
				case 0xd0:
					if (rom->mode != StrataFlashErzCy1)
						return false;
					if (fromStart != rom->opAddr)
						return false;
					if (!romPrvEraseSector(rom, fromStart))
						return false;
					rom->busyCy = 0x1000;
					rom->mode = StrataFlashReadStatus;
					return true;
				
				case 0x90:
					if (rom->mode == StrataFlashErzCy1 || rom->mode == StrataFlashWriCy1)
						return false;
					
					//read identifier
					rom->mode = StrataFlashReadID;
					return true;
				
				case 0x98:
					if (rom->mode == StrataFlashErzCy1 || rom->mode == StrataFlashWriCy1)
						return false;
					
					//read query CFI
					rom->mode = StrataFlashReadCFI;
					return true;
				
				case 0xff:
					if (rom->mode == StrataFlashErzCy1 || rom->mode == StrataFlashWriCy1)
						return false;
					
					//read
					rom->mode = RomChipNormalMode;
					return true;
				
				default:
					fprintf(stderr, "Unknown strataflash command 0x%04x -> [0x%08x]\n", dataBits, addrBits);
					return false;
			}
			switch (size) {
				
				case 1:
			
					*((uint8_t*)addr) = *(uint8_t*)bufP;	//our memory system is little-endian
					break;
				
				case 2:
				
					*((uint16_t*)addr) = htole16(*(uint16_t*)bufP);	//our memory system is little-endian
					break;
				
				case 4:
				
					*((uint32_t*)addr) = htole32(*(uint32_t*)bufP);
					break;
				
				default:
				
					return false;
			}
		}
	}
	else {		//read
		
		//128mbit reply
		static const uint16_t qryReplies_from_0x10[] = {
			'Q', 'R', 'Y', 1, 0, 0x31, 0, 0, 0, 0, 0, 0x27, 0x36, 0, 0,
			8, 9, 10, 0, 1, 1, 2, 0, 0x18, 1, 0, 6, 0, 1, 0x7f, 0, 0,
			3,
			
			'P', 'R', 'I', '1', '1', 0xe6, 1, 0, 0, 1, 7, 0, 0x33, 0, 2, 0x80,
			0, 3, 3, 0x89, 0, 0, 0, 0, 0, 0, 0x10, 0, 4, 4, 2, 2,
			3
			
			};
		bool command = false;
		uint32_t addrBits = fromStart;

		switch (rom->mode) {	//what modes expect a read of command size? which arent allowed at all
			
			case JedecReadID:
			case JedecErasing:
			case JedecWriteOngoing:
				switch (rom->chipType) {
					case RomJedecFlashX16:
						if (size != 2) {
							fprintf(stderr, "JedecFlashX16 read of improper size!\n");
							return false;
						}
						addrBits /= 2;
						break;
					
					case RomJedecFlashX8:
					case RomPixterFlashX8:
						if (size != 1) {
							fprintf(stderr, "JedecFlashX8 read of improper size!\n");
							return false;
						}
						break;
					
					default:
						return false;
				}
				command = true;
				break;

			case StrataFlashReadStatus:
			case StrataFlashReadID:
			case StrataFlashReadCFI:
				command = true;
				switch (rom->chipType) {
					case RomStrataFlash16x:
						if (size != 2) {
							fprintf(stderr, "Strataflash read of improper size!\n");
							return false;
						}
						addrBits /= 2;
						break;
					
					case RomStrataflash16x2x:
						if (size != 4) {
							fprintf(stderr, "StrataflashX2 read of improper size!\n");
							return false;
						}
						addrBits /= 4;
						break;
					
					default:
						return false;
				}
				break;
			
			case RomChipNormalMode:
			case StrataFlashSeen0x60:	//in this mode we can still fetch
				break;
			
			default:
				return false;
		}
		
		if (command) {
			
			bool skipdup = false;
			uint32_t reply;
			
			switch (rom->mode) {
				
				case JedecReadID:
					fprintf(stderr, "rom RDID 0x%08x, into sec: 0x%08x\n", fromStart, romPrvGetOffsetIntoSector(rom, fromStart));

					if (addrBits == 0)			//manuf ???
						reply = rom->jedecManuf;
					else if (addrBits == 1)	//dev id
						reply = rom->jedecPart;
					else {
						if (romPrvGetOffsetIntoSector(rom, fromStart) == 2 * size)	//2 bytes past the start of a sector? this is a request fo rlock bits - reply
							reply = 0x0000;
						else
							return false;
					}
					break;

				case JedecErasing:
					if (rom->busyCy) {
						rom->busyCy--;
						reply = 0;		//busy
					}
					else {
						rom->mode = RomChipNormalMode;
						reply = 0x0080 + (rom->jedecWriteOpSuccess ? 0x0000 : 0x0024);	//ready + error
					}
					break;

				case JedecWriteOngoing:
					if (rom->busyCy) {
						rom->busyCy--;

						if (rom->chipType == RomPixterFlashX8)	//during busy flash returns zeroes
							reply = 0;
						else
							reply = (rom->jedecWriteTopBit ? 0x0000 : 0x0080) | ((rom->busyCy & 1) ? 0x40 : 0x00);		//busy, toggle bit
					}
					else {
						if (rom->chipType == RomPixterFlashX8)	//during busy flash returns zeroes
							reply = 0x80;	//done
						else
							reply = (rom->jedecWriteTopBit ? 0x0080 : 0x0000) + (rom->jedecWriteOpSuccess ? 0x0000 : 0x0024);	//ready + error
						rom->mode = RomChipNormalMode;
					}
					break;

				case StrataFlashReadStatus:
					if (rom->busyCy) {
						rom->busyCy--;
						reply = 0;		//busy
					}
					else {
						rom->mode = RomChipNormalMode;	//only if not busy
						reply = 0x0080;	//ready;
					}
					break;
				
				case StrataFlashReadID:
					switch (romPrvGetOffsetIntoSector(rom, fromStart) / size % 1024 /* lolz */) {
						case 0:
							reply = 0x0089;
							break;
						case 1:
							reply = 0x8802;
							break;

						case 2:		//block lock/lockdown
							reply = 0;
							break;

						case 5:

							reply = rom->configReg;
							break;
						case 0x80:	//protection register lock
							reply = 2;
							break;
						
						case 0x81:	//protection registers (uniq ID by intel and by manuf) copied from same chip as this rom
							reply = 0x001d0017ul;
							skipdup = true;
							break;
						
						case 0x82:
							reply = 0x000a0003ul;
							skipdup = true;
							break;
						
						case 0x83:
							reply = 0x3fb03fa6ul;
							skipdup = true;
							break;
						
						case 0x84:
							reply = 0x48d9c99aul;
							skipdup = true;
							break;
						
						case 0x85 ... 0x88:
							reply = 0xffff;
							break;

						case 0x89:	//otp lock - all locked for us
							reply = 0;
							break;
						case 0x8a ... 0x109:	//otp data
							reply = 0;
							break;

						default:
							reply = 0xff;
							fprintf(stderr, "JEDEC weird read of 0x%08x (from sector is %u, size is %u) in ID mode returns 0x%04x\n",
								fromStart, romPrvGetOffsetIntoSector(rom, fromStart), size, reply);
							break;
					}
					break;
				
				case StrataFlashReadCFI:
					fprintf(stderr, "CFI Read 0x%08x\n", fromStart);
					switch (addrBits) {
						case 0x00:
							reply = 0x0089;
							break;
						case 0x01:
							reply = 0x8802;
							break;
						case 0x10 ... sizeof(qryReplies_from_0x10) + 0x10:
							reply = qryReplies_from_0x10[addrBits - 0x10];
							break;
						default:
							switch (addrBits & 0xffff) {
								case 2:		//block status register
									reply = 0;
									break;
								default:
									return false;
							}
							break;
					}
					fprintf(stderr, "CFI Read 0x%08x -> 0x%04x\n", fromStart, reply);
					break;
				
				default:
					return false;
			}
			
			if (!skipdup)
				reply |= reply << 16;
			
			switch (size) {
				case 1:
					*(uint8_t*)bufP = reply;
					break;
				case 2:
					*(uint16_t*)bufP = reply;
					break;
				case 4:
					*(uint32_t*)bufP = reply;
					break;
			}
			
			return true;
		}
		switch (size) {
			
			case 1:
				
				*(uint8_t*)bufP = *((uint8_t*)addr);
				break;
			
			case 2:
			
				*(uint16_t*)bufP = le16toh(*((uint16_t*)addr));
				break;
			
			case 4:
			
				*(uint32_t*)bufP = le32toh(*((uint32_t*)addr));
				break;
			
			case 64:
				((uint32_t*)bufP)[ 8] = le32toh(*((uint32_t*)(addr + 32)));
				((uint32_t*)bufP)[ 9] = le32toh(*((uint32_t*)(addr + 36)));
				((uint32_t*)bufP)[10] = le32toh(*((uint32_t*)(addr + 40)));
				((uint32_t*)bufP)[11] = le32toh(*((uint32_t*)(addr + 44)));
				((uint32_t*)bufP)[12] = le32toh(*((uint32_t*)(addr + 48)));
				((uint32_t*)bufP)[13] = le32toh(*((uint32_t*)(addr + 52)));
				((uint32_t*)bufP)[14] = le32toh(*((uint32_t*)(addr + 56)));
				((uint32_t*)bufP)[15] = le32toh(*((uint32_t*)(addr + 60)));
				//fallthrough
			case 32:
			
				((uint32_t*)bufP)[4] = le32toh(*((uint32_t*)(addr + 16)));
				((uint32_t*)bufP)[5] = le32toh(*((uint32_t*)(addr + 20)));
				((uint32_t*)bufP)[6] = le32toh(*((uint32_t*)(addr + 24)));
				((uint32_t*)bufP)[7] = le32toh(*((uint32_t*)(addr + 28)));
				//fallthrough
			case 16:
				
				((uint32_t*)bufP)[2] = le32toh(*((uint32_t*)(addr +  8)));
				((uint32_t*)bufP)[3] = le32toh(*((uint32_t*)(addr + 12)));
				//fallthrough
			case 8:
				((uint32_t*)bufP)[0] = le32toh(*((uint32_t*)(addr +  0)));
				((uint32_t*)bufP)[1] = le32toh(*((uint32_t*)(addr +  4)));
				break;
			
			default:
			
				return false;
		}
	}
	return true;
}

static bool romAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* bufP)
{
	struct ArmRom *rom = (struct ArmRom*)userData;
	bool ret = romAccessGuts(rom, pa, size, write, bufP);
	
	//this is a good place to lod read/writes

	return ret;
}


void romPeriodic(struct ArmRom* rom)
{
	uint_fast8_t readSz;
	uint8_t buf[8];

	switch (rom->chipType) {
		case RomStrataflash16x2x:
			readSz = 4;
			break;

		case RomStrataFlash16x:
		case RomJedecFlashX16:
			readSz = 2;
			break;
		case RomJedecFlashX8:
		case RomPixterFlashX8:
			readSz = 1;
			break;

		default:
			return;
	}

	if (rom->busyCy)
		romAccessF(rom, rom->start, readSz, false, buf);
}


struct ArmRom* romInitWithOneChunk(struct ArmMem *mem, uint32_t adr, uint32_t vaSize, void *data, uint32_t size, enum RomChipType chipType)
{
	struct ArmRom *rom = (struct ArmRom*)calloc(1, sizeof(*rom));
	uint32_t i;
	
	if (!rom)
		ERR("cannot alloc ROM at 0x%08x", adr);
	
	rom->start = adr;
	rom->dataSize = size;
	rom->vaSize = vaSize;
	rom->buf = data;
	
	rom->chipType = chipType;
	rom->mode = RomChipNormalMode;
	rom->configReg = 0xc0c2;

	rom->jedecManuf = 0x1234;
	rom->jedecPart = 0x005b;

	if (!memRegionAdd(mem, adr, vaSize, romAccessF, rom))
		ERR("cannot add ROM piece at 0x%08x to MEM\n", adr);
	
	return rom;
}

struct ArmRom* romInitWithFILE(struct ArmMem *mem, uint32_t adr, uint32_t vaSize, FILE *file, uint32_t size, enum RomChipType chipType)
{
	uint8_t *ramCopy = malloc(size);
	struct ArmRom *rom;

	if (!ramCopy)
		return NULL;

	memset(ramCopy, 0xff, size);
	rewind(file);
	(void)fread(ramCopy, 1, size, file);

	rom = romInitWithOneChunk(mem, adr, vaSize, ramCopy, size, chipType);
	if (rom)
		rom->file = file;
	else
		free(ramCopy);
	
	return rom;
}

struct ArmRom* romInitWithPixterCartRom(struct ArmMem *mem, uint32_t adr, uint32_t vaSize, struct PixterRomFile *cartRom, uint_fast8_t codeIdx, enum RomChipType chipType)
{
	static uint32_t zero = 0;
	void *buffer;
	uint32_t len;

	if (cartRom && cartRom->code[codeIdx] && cartRom->code[codeIdx]->length) {
		buffer = cartRom->code[codeIdx]->data;
		len = cartRom->code[codeIdx]->length;
	}
	else {
		buffer = &zero;
		len = sizeof(zero);
	}

	return romInitWithOneChunk(mem, adr, vaSize, buffer, len, chipType);
}

void romTuneA(struct ArmRom* rom, uint32_t commandAddrMask, uint32_t sectorSz, uint32_t blockSz)
{
	uint32_t i, nSec = (rom->dataSize + sectorSz - 1) / sectorSz;

	rom->sectorSizes = calloc(sizeof(*rom->sectorSizes), nSec);
	for (i = 0; i < nSec; i++)
		rom->sectorSizes[i] = sectorSz;

	rom->numSectors = nSec;
	rom->blockSize = blockSz;
	rom->commandAddrMask = commandAddrMask;
}

void romTuneB(struct ArmRom* rom, uint32_t commandAddrMask, const uint32_t *sectorSzs, unsigned numSecs)	//all in bytes, this func is only for when sector sizes differ and no block erase command exists
{
	uint32_t i;

	rom->sectorSizes = calloc(sizeof(*rom->sectorSizes), numSecs);
	rom->numSectors = numSecs;
	rom->blockSize = 0;

	for (i = 0; i < numSecs; i++)
		rom->sectorSizes [i] = *sectorSzs++;
	rom->commandAddrMask = commandAddrMask;
}

void romSetIDs(struct ArmRom* rom, uint16_t manufID, uint16_t partID)
{
	rom->jedecManuf = manufID;
	rom->jedecPart = partID;
}


