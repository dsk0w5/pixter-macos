//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _ROM_H_
#define _ROM_H_


#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pixterRomFile.h"


enum RomChipType {
	RomWriteIgnore,
	RomWriteError,
	RomStrataFlash16x,
	RomStrataflash16x2x,
	RomJedecFlashX16,
	RomJedecFlashX8,
	RomPixterFlashX8,	//like JEDEC in commands, but no toggle bit, just top bit goes high when erase or program done
};


struct ArmRom;

//construct a ROM
struct ArmRom* romInitWithPixterCartRom(struct ArmMem *mem, uint32_t adr, uint32_t vaSize, struct PixterRomFile *cartRom, uint_fast8_t codeIdx, enum RomChipType chipType);
struct ArmRom* romInitWithOneChunk(struct ArmMem *mem, uint32_t adr, uint32_t vaSize, void *data, uint32_t size, enum RomChipType chipType);
struct ArmRom* romInitWithFILE(struct ArmMem *mem, uint32_t adr, uint32_t vaSize, FILE *file, uint32_t size, enum RomChipType chipType);


//tweak things for jedec flash
void romTuneA(struct ArmRom* rom, uint32_t commandAddrMask, uint32_t sectorSz, uint32_t blockSz);			//all in bytes, this func is only for when both sector and block erase operations work, andall things are equal in size
void romTuneB(struct ArmRom* rom, uint32_t commandAddrMask, const uint32_t *sectorSzs, unsigned numSecs);	//all in bytes, this func is only for when sector sizes differ and no block erase command exists
void romSetIDs(struct ArmRom* rom, uint16_t manufID, uint16_t partID);


//some devices (ahem...pixter...) do not poll flash and just wait, we need to allow this to work
void romPeriodic(struct ArmRom* rom);

#endif
