#ifndef _PIXTER_ROM_FILE_H_
#define _PIXTER_ROM_FILE_H_

#include <stdint.h>
#include <stdio.h>

enum PixterRomType {
	PixterRomClassic,
	PixterRomColor,
	PixterRomMultimedia,
};

struct PixterRomChunk {
	uint32_t length;
	uint8_t data[];
};

struct PixterRomFile {
	enum PixterRomType romType;
	uint32_t numMelodyIndices;
	struct PixterRomChunk *code[2];
	struct PixterRomChunk *melodies[];
};



struct PixterRomFile* romRead(FILE *f);
void romFree(struct PixterRomFile *rom);


#endif
