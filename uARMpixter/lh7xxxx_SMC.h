#ifndef _LH7XXXX_SMC_H_
#define _LH7XXXX_SMC_H_

#include "mem.h"

struct Lh7xxxxSmc;



struct Lh7xxxxSmc* lh7xxxxSmcInit(struct ArmMem *physMem, bool bootIn16bitMode);


#endif
