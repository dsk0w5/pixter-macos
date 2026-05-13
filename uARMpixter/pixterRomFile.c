#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pixterRomFile.h"



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

static struct PixterRomChunk* readChunk(FILE *f, const uint32_t *offsets)
{
	uint32_t startOfst = *offsets++, endOfst, size;
	struct PixterRomChunk *chunk;

	while (!(endOfst = *offsets++));
	size = endOfst - startOfst;
	
	chunk = malloc(sizeof(struct PixterRomChunk) + size);
	if (!chunk)
		return NULL;

	chunk->length = size;
	if (!fseek(f, startOfst, SEEK_SET) && readN(chunk->data, f, size))
		return chunk;

	free(chunk);
	return NULL;
}

struct PixterRomFile* romRead(FILE *f)
{
	static const uint8_t fileHeaderV1multimedia[16] = "PIXTER MULTI!!!\1";
	static const uint8_t fileHeaderV1classic[16] = "PIXTER CLASSIC!\1";
	static const uint8_t fileHeaderV1color[16] = "PIXTER COLOR!!!\1";
	uint8_t headerIn[sizeof(fileHeaderV1classic)];
	uint32_t i, j, numMelodyIndices, *offsets;
	enum PixterRomType romType;
	struct PixterRomFile *rom;
	int64_t tv;

	if (sizeof(headerIn) != fread(headerIn, 1, sizeof(headerIn), f)) {
		fprintf(stderr, "ROM file missing header bytes\n");
		return NULL;
	}

	if (!memcmp(headerIn, fileHeaderV1classic, sizeof(fileHeaderV1classic)))
		romType = PixterRomClassic;
	else if (!memcmp(headerIn, fileHeaderV1color, sizeof(fileHeaderV1color)))
		romType = PixterRomColor;
	else if (!memcmp(headerIn, fileHeaderV1multimedia, sizeof(fileHeaderV1multimedia)))
		romType = PixterRomMultimedia;
	else {
		fprintf(stderr, "ROM file missing proper header\n");
		return NULL;
	}
	
	tv = read32(f);
	if (tv < 0) {
		fprintf(stderr, "ROM file missing 'num melody indices'\n");
		return NULL;
	}
	numMelodyIndices = tv;

	rom = calloc(1, sizeof(struct PixterRomFile) + numMelodyIndices * sizeof(*rom->melodies));
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

	rom->romType = romType;
	rom->numMelodyIndices = numMelodyIndices;
	return rom;
}

void romFree(struct PixterRomFile *rom)
{
	uint32_t i;

	for (i = 0; i < rom->numMelodyIndices; i++)
		free(rom->melodies[i]);
	for (i = 0; i < sizeof(rom->code) / sizeof(*rom->code); i++)
		free(rom->code[i]);
	free(rom);
}




