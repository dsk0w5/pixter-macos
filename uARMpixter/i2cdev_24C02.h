#ifndef _I2C_24C02_H_
#define _I2C_24C02_H_


#include "soc_I2C.h"
#include <stdint.h>
#include <stdio.h>


struct AT25C02;


struct AT25C02* at24c02init(struct SocI2c* i2c, uint_fast8_t addr7bit, FILE *backingFile);	//backing file may be NULL



#endif

