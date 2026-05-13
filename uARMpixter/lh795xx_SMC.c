#include "lh7xxxx_SMC.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"


#define SMC_BASE			0xFFFF1000
#define SMC_SIZE			0x27c

struct Lh7xxxxSmc {
	uint32_t regs[160];	//let's be lazy
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
		smc->regs[pa] = *(uint32_t*)buf;
	else
		*(uint32_t*)buf = smc->regs[pa];
	
	return true;
}

struct Lh7xxxxSmc* lh7xxxxSmcInit(struct ArmMem *mem, bool bootIn16bitMode)
{
	struct Lh7xxxxSmc *smc = (struct Lh7xxxxSmc*)calloc(1, sizeof(*smc));
	unsigned i;

	if (!smc)
		ERR("cannot alloc IOCON");

	if (!memRegionAdd(mem, SMC_BASE, SMC_SIZE, smcPrvMemAccessF, smc))
		ERR("cannot add SMC to MEM\n");
	
	return smc;
}




