#ifndef _GPIO_I2C_MASTER_H_
#define _GPIO_I2C_MASTER_H_


#include "soc_I2C.h"
#include "soc_GPIO.h"


void socI2cGpioDeviceConfigPins(struct SocI2c *i2c, struct SocGpio *gpio, uint8_t gpioSDA, uint8_t gpioSCL);





#endif
