#include "lh7xxxx_SMC.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

#define NUM_BCR_REGS		4

#define SMC_BASE			0xFFFF1000
#define SMC_SIZE			4 * NUM_BCR_REGS

struct Lh7xxxxSmc {
	uint32_t bcr[NUM_BCR_REGS];
};



static bool smcPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxSmc *smc = (struct Lh7xxxxSmc*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= SMC_BASE;
	pa >>= 2;

	if (write)
		val =  *(uint32_t*)buf;
	

	if (write) {
		smc->bcr[pa] = val & 0x3f00ffef;
		fprintf(stderr, "SMC: reconfigured bank %u to: x%u%s%s, WST2=%u, RBLE=%u, WST1=%u, IDCY=%u\n",
				pa,
				8 << ((val >> 28) & 3),
				(val & 0x08000000) ? ", burstROM" : "",
				(val & 0x04000000) ? ", ReadOnly" : ", ReadWrite",
				(val >> 11) & 0x1f,
				(val >> 10) & 1,
				(val >> 5) & 0x1f,
				val & 0x0f
			);
	}
	else
		val = smc->bcr[pa];

	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}

struct Lh7xxxxSmc* lh7xxxxSmcInit(struct ArmMem *mem, bool bootIn16bitMode)
{
	struct Lh7xxxxSmc *smc = (struct Lh7xxxxSmc*)calloc(1, sizeof(*smc));
	unsigned i;

	if (!smc)
		ERR("cannot alloc IOCON");

	smc->bcr[0] = bootIn16bitMode ? 0x1000FFEF : 0x0000FBEF;
	
	for (i = 1; i < NUM_BCR_REGS; i++)
		smc->bcr[i] = 0x1000FFEF;

	if (!memRegionAdd(mem, SMC_BASE, SMC_SIZE, smcPrvMemAccessF, smc))
		ERR("cannot add SMC to MEM\n");
	
	return smc;
}




