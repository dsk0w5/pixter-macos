#ifndef _LH7XXXX_IOCON_H_
#define _LH7XXXX_IOCON_H_

#include "mem.h"

struct Lh7xxxxIocon;



struct Lh7xxxxIocon* lh7xxxxIoconInit(struct ArmMem *physMem, bool bootIn16bitMode);


#endif
