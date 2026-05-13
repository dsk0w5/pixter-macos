#include <stdint.h>

static const __attribute__((used, section(".image"))) uint8_t mImage[] = {
	#include "image.inc"
};

static const __attribute__((used, section(".bigimage"))) uint8_t mBigImage[] = {
	#include "bigimage.inc"
};
