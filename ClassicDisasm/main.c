#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "disasm.h"
#include "classic.h"


static int16_t code[0xc000];

static int16_t bufRead(uint32_t pos)
{
	if (pos >= sizeof(code) / sizeof(*code))
		return -1;
	return code[pos];
}

int main(int argc, char **argv)
{
	uint32_t filePos = -1, usedBytes = 0;
	int c;

	memset(code, 0xff, sizeof(code));		//-1 init

	while ((c = getchar()) != EOF) {

		int32_t bufPos;

		filePos++;
		if (filePos >= CLASSIC_INPUT_REGION_1_OFST && filePos - CLASSIC_INPUT_REGION_1_OFST < CLASSIC_INPUT_REGION_1_SIZE)
			bufPos = filePos - CLASSIC_INPUT_REGION_1_OFST + CLASSIC_INPUT_REGION_1_ADDR;
		else if (filePos >= CLASSIC_INPUT_REGION_2_OFST && filePos - CLASSIC_INPUT_REGION_2_OFST < CLASSIC_INPUT_REGION_2_SIZE)
			bufPos = filePos - CLASSIC_INPUT_REGION_2_OFST + CLASSIC_INPUT_REGION_2_ADDR;
		else
			continue;

		if (code[bufPos] >= 0) {
			fprintf(stderr, "buffer offset 0x%04x already written!\n", bufPos);
			return -1;
		}
		code[bufPos] = (uint16_t)(uint8_t)c;
		usedBytes++;
	}
	fprintf(stderr, "Read %u bytes, used %u of them\n", filePos, usedBytes);

	disasm(bufRead);

	return 0;
}