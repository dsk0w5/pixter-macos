#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rom.h"



static bool readN(void *dst, FILE *f, uint32_t len)
{
	return len == fread(dst, 1, len, f);
}

static int64_t read32(FILE *f)
{
	uint8_t buf[4];

	if (!readN(buf, f, sizeof(buf)))
		return -1;

	return (((uint32_t)buf[3]) << 24) | (((uint32_t)buf[2]) << 16) | (((uint32_t)buf[1]) << 8) | (((uint32_t)buf[0]) << 0);
}

static struct RomChunk* readChunk(FILE *f, const uint32_t *offsets)
{
	uint32_t startOfst = *offsets++, endOfst, size;
	struct RomChunk *chunk;

	while (!(endOfst = *offsets++));
	size = endOfst - startOfst;
	
	chunk = malloc(sizeof(struct RomChunk) + size);
	if (!chunk)
		return NULL;

	chunk->length = size;
	if (!fseek(f, startOfst, SEEK_SET) && readN(chunk->data, f, size))
		return chunk;

	free(chunk);
	return NULL;
}

struct Rom* romRead(FILE *f)
{
	static const uint8_t fileHeaderV1[16] = "PIXTER CLASSIC!\1";
	uint8_t headerIn[sizeof(fileHeaderV1)];
	uint32_t i, j, numMelodyIndices, *offsets;
	struct Rom *rom;
	int64_t tv;

	if (sizeof(headerIn) != fread(headerIn, 1, sizeof(headerIn), f) || memcmp(headerIn, fileHeaderV1, sizeof(headerIn))) {
		fprintf(stderr, "ROM file missing proper header\n");
		return NULL;
	}
	tv = read32(f);
	if (tv < 0) {
		fprintf(stderr, "ROM file missing 'num melody indices'\n");
		return NULL;
	}
	numMelodyIndices = tv;

	rom = calloc(1, sizeof(struct Rom) + numMelodyIndices * sizeof(*rom->melodies));
	if (!rom)
		return NULL;
	offsets = calloc(sizeof(*offsets), numMelodyIndices + 3);
	if (!offsets) {
		romFree(rom);
		return NULL;
	}

	for (i = 0; i < numMelodyIndices + 3; i++) {
		tv = read32(f);
		if (tv < 0) {
			fprintf(stderr, "ROM file missing 'offset %u'\n", i);
			return NULL;
		}
		offsets[i] = tv;
	}

	for (i = 0; i < numMelodyIndices; i++) {
		if (offsets[i] && !(rom->melodies[i] = readChunk(f, offsets + i))) {
			fprintf(stderr, "Failed to read melody %u\n", i);
			romFree(rom);
			return NULL;
		}
	}

	for (j = 0; j < sizeof(rom->code) / sizeof(*rom->code); i++, j++) {
		if (offsets[i] && !(rom->code[j] = readChunk(f, offsets + i))) {
			fprintf(stderr, "Failed to read code %u\n", i);
			romFree(rom);
			return NULL;
		}
	}

	rom->numMelodyIndices = numMelodyIndices;
	return rom;
}

void romFree(struct Rom *rom)
{
	uint32_t i;

	for (i = 0; i < rom->numMelodyIndices; i++)
		free(rom->melodies[i]);
	for (i = 0; i < sizeof(rom->code) / sizeof(*rom->code); i++)
		free(rom->code[i]);
	free(rom);
}




