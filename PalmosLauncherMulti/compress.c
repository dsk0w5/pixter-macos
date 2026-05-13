#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "header.h"
#include "lz.h"



static void writeBE(uint32_t val)
{
	uint_fast8_t i;

	for (i = 0; i < 4; i++, val <<= 8)
		putchar(val >> 24);
}

uint_fast8_t lzExtReadByte(uint32_t ofst, void *userData)
{
	abort();
	return 0;
}

int main(int argc, char **argv)
{
	uint32_t len = 0, pos = 0, outLen;
	uint8_t *buf = NULL, *out;
	unsigned long long base;
	int c;

	if (argc != 2 || (base = strtoll(argv[1], NULL, 0)) >= 0x100000000ull) {

		fprintf(stderr, "USAGE: %s <LOAD ADDR>  < rom.bin > rom_compressed.bin\n", argv[0]);
		return -1;
	}

	while ((c = getchar()) != EOF) {

		if (len == pos)
			buf = realloc(buf, len = len * 2 + 1);
		
		buf[pos++] = c;
	}
	fprintf(stderr, "read %u bytes of rom, compressing\n", (unsigned)pos);

	out = malloc((257 * pos + 255) / 256 + 1);
	outLen = lzCompress(buf, out, pos, 1 << 21);
	
	fprintf(stderr, "compressed data size: %u bytes (%u%% of original size). Total size will be %u bytes (0x%08x). Load addr 0x%08x\n",
			(unsigned)outLen, (unsigned)(((unsigned long long)outLen * 100 + pos / 2) / pos), (unsigned)outLen + 16, (unsigned)outLen + 16, (unsigned)base);

	writeBE(MAGIC);
	writeBE(base);
	writeBE(pos);
	writeBE(outLen);

	for (len = 0; len < outLen; len++)
		putchar(out[len]);

	free(out);
	free(buf);
	
	return 0;
}
