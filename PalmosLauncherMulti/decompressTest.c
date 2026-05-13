#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "header.h"
#include "lz.h"

struct ReadData {
	uint32_t lastOffsetRead;
	const uint8_t *src;
};

static uint32_t readBE(uint8_t **ptrP)
{
	uint8_t *ptr = *ptrP;
	uint32_t val = 0;
	uint_fast8_t i;

	for (i = 0; i < 4; i++)
		val = (val << 8) + *ptr++;

	*ptrP = ptr;
	return val;
}

uint_fast8_t lzExtReadByte(uint32_t ofst, void *userData)
{
	struct ReadData *rd = (struct ReadData*)userData;

	rd->lastOffsetRead = ofst;

	return rd->src[ofst];
}

int main(int argc, char **argv)
{
	uint32_t len = 0, pos = 0, outLen, t, myInPosStart = 0;
	uint8_t *buf = NULL, *inPtr, *out;
	struct ReadData rd;
	struct LzState lzs;
	int c, ret = -1;

	while ((c = getchar()) != EOF) {

		if (len == pos)
			buf = realloc(buf, len = len * 2 + 1);
		
		buf[pos++] = c;
	}
	fprintf(stderr, "read %u bytes of compressed data\n", (unsigned)pos);
	inPtr = buf;
	if (pos <= 17) {

		fprintf(stderr, "size too small to contain a header and minimal compressed data\n");
	}
	else if ((t = readBE(&inPtr)) != MAGIC) {

		fprintf(stderr, "magic 0x%08x seen, 0x%08x expected\n", t, MAGIC);
	}
	else {

		uint32_t destAddr = readBE(&inPtr);
		uint32_t decompressedSize = readBE(&inPtr);
		uint32_t compressedSize = readBE(&inPtr);

		if (compressedSize + 16 != pos) {
			
			fprintf(stderr, "compressed size recorded as %u + %u bytes of header, but we read %u\n", compressedSize, 16, pos);
		}
		else {

			out = malloc(decompressedSize);

			rd.lastOffsetRead = 0xffffffff;
			rd.src = inPtr;

			lzs.readD = &rd;
			lzs.inPosStart = myInPosStart;
			lzs.inPosPastEnd = lzs.inPosStart + compressedSize;
			lzs.dst = out;

			if (!lzUncompressStart(&lzs)) {

				fprintf(stderr, "lz init failed\n");
			}
			else {

				while (lzDecompressSome(&lzs, 8192)) {
					fprintf(stderr, " %u -> %u\n", rd.lastOffsetRead, (unsigned)(lzs.dst - out));
				}

				if (lzs.dst - out != decompressedSize) {

					fprintf(stderr, "produced %u bytes, %u expected\n", (unsigned)(lzs.dst - out), decompressedSize);
				}
				else {
					
					fprintf(stderr, "produced %u bytes as expected\n", decompressedSize);
					ret = 0;

					for (t = 0; t < decompressedSize; t++)
						putchar(out[t]);
				}
			}
		}
	}

	free(out);
	free(buf);

	return ret;
}
