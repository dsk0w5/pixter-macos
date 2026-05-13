#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "disasm.h"
#include "classic.h"


static uint8_t mCode[ROM_MAX_SIZE];
static uint32_t mCodeLen = 0;

static int16_t bufRead(uint32_t pos)
{
	if (pos < ROM_BASE)
		return -1;

	pos -= ROM_BASE;

	if (pos >= mCodeLen)
		return -1;

	return mCode[pos];
}

int main(int argc, char **argv)
{
	int c;

	memset(mCode, 0xff, sizeof(mCode));		//-1 init

	while ((c = getchar()) != EOF) {

		if (mCodeLen >= sizeof(mCode)) {
			fprintf(stderr, "stopped reading input...\n");
			break;
		}
		mCode[mCodeLen++] = c;
	}

	fprintf(stderr, "Read %u bytes\n", mCodeLen);

	disasm(bufRead);

	return -1;
}