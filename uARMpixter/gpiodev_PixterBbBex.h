#ifndef _GPIODEV_PIXTER_BB_NOR_H_
#define _GPIODEV_PIXTER_BB_NOR_H_



#include "pixterMelodyChip.h"
#include "soc_GPIO.h"
#include "mem.h"

struct PixerBbBex;

//these are not real, we just pick them for convenience since ArmMem needs addresses
#define BB_BUS_INTERNAL_NOR_ADDR			0x10000000
#define BB_BUS_EXTERNAL_CART_ROM_ADR		0x20000000
#define BB_BUS_EXTERNAL_CART_NOR_ADDR		0x30000000

#define BB_BUS_ADDEND_SPECIAL_REGION		0x01000000


struct PixerBbBex* pixterBbBexInit(struct SocGpio *gpio, uint8_t stateGpioLo, uint8_t stateGpioHi, const uint8_t busGpios[static 8]);

struct ArmMem* pixterBbBexGetBbBus(struct PixerBbBex *bex);	//to allow you to add another memory at BB_BUS_SECONDARY_MEM_ADDR
void pixterBbBexAttachMelodyChip(struct PixerBbBex *bex, struct MelodyChip *cartMelodyChip);


#endif
