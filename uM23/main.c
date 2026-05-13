#include <stdbool.h>
#include <stdint.h>

#define FIXED_SHIFT			8
#define TO_FIXED(_float)	(int_fast16_t)((_float) * (1 << FIXED_SHIFT))

#define DISPLAY 			((volatile uint8_t*)0x400004e0)
#define ROWS				80
#define COLS				80


#define MIN_X				-2
#define MAX_X				0.7
#define MIN_Y				-1
#define MAX_Y				1





struct cpx {
	int16_t re, im;
};

int_fast16_t fixedAdd(int_fast16_t a, int_fast16_t b)
{
	return a + b;
}

int_fast16_t fixedSub(int_fast16_t a, int_fast16_t b)
{
	return a - b;
}

int_fast16_t fixedMul(int_fast16_t a, int_fast16_t b)
{
	return ((int32_t)a * (int32_t)b) / (1 << FIXED_SHIFT);
}

struct cpx cpxAdd(struct cpx a, struct cpx b)
{
	struct cpx ret = {.re = fixedAdd(a.re, b.re), .im = fixedAdd(a.im, b.im), };

	return ret;
}

struct cpx cpxMul(struct cpx a, struct cpx b)
{
	struct cpx ret = {.re = fixedSub(fixedMul(a.re, b.re), fixedMul(a.im, b.im)), .im = fixedAdd(fixedMul(a.re, b.im), fixedMul(a.im, b.re)), };

	return ret;
}

uint32_t cpxMagnitudeSquared(struct cpx a)
{
	return fixedAdd(fixedMul(a.re, a.re), fixedMul(a.im, a.im));
}

static void setPix(uint_fast8_t row, uint_fast8_t col, bool set)
{
	volatile uint8_t *dst = DISPLAY + row * (COLS / 8) + (col / 8);
	uint_fast8_t mask = 0x80 >>(col % 8);

	if (set)
		dst[0] |= mask;
	else
		dst[0] &=~ mask;
}


void main(void)
{
	uint32_t thresh = 32;
	int_fast16_t x, y;
	uint32_t r, c;

	for (r = 0, y = TO_FIXED(MIN_Y); r < ROWS; y += TO_FIXED((float)(MAX_Y - MIN_Y) / ROWS), r++) {
		for (c = 0, x = TO_FIXED(MIN_X); c < COLS; x += TO_FIXED((float)(MAX_X - MIN_X) / COLS), c++) {
			
			uint32_t nIter = 0;
			struct cpx zv = {}, cv = {.re = x, .im = y, };

			while (cpxMagnitudeSquared(zv) < TO_FIXED(8 * 8) && nIter < thresh) {
				nIter++;
				zv = cpxAdd(cpxMul(zv, zv), cv);
			}

			setPix(r, c, nIter != thresh);
		}
	}

	while(1);
}




