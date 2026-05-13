#ifndef _ROM_H_
#define _ROM_H_

#include <stdint.h>

struct RomChunk {
	uint32_t length;
	uint8_t data[];
};

struct Rom {
	uint32_t numMelodyIndices;
	struct RomChunk *code[2];
	struct RomChunk *melodies[];
};



struct Rom* romRead(FILE *f);
void romFree(struct Rom *rom);


#endif
