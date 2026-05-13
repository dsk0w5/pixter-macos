#ifndef _I2C_24C02_H_
#define _I2C_24C02_H_


#include "gpioMasterI2C.h"
#include <stdint.h>
#include <stdio.h>


struct AT25C02;


struct AT25C02* at24c02init(struct GpioI2C* i2c, uint_fast8_t addr7bit, FILE *storage, unsigned offset);



#endif

