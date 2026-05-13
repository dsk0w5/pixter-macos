#ifndef _GPIO_I2C_MASTER_H_
#define _GPIO_I2C_MASTER_H_

#include <stdbool.h>
#include <stdint.h>

struct GpioI2C;


enum ActionI2C {	//designed so returns can be ORRed together with good results
	i2cStart,		//no params, no returns
	i2cRestart,		//no params, no returns
	i2cStop,		//no params, no returns
	i2cTx,			//param is byte master sent, return is bool "Ack"
	i2cRx,			//param is "bool willBeAcked", return is byte slave sent
};
typedef uint_fast8_t (*I2cDeviceActionF)(void *userData, enum ActionI2C stimulus, uint_fast8_t value);
typedef bool (*GpioPinReadF)(void *userData);
typedef void (*GpioPinWriteF)(void *userData, bool high);







struct GpioI2C* gpioI2cInit(GpioPinReadF readSdaF, GpioPinWriteF writeSdaF, void *sdaCbkD, GpioPinReadF readSclF, GpioPinWriteF writeSclF, void *sclCbkD);
void gpioI2cPeriodic(struct GpioI2C* i2c);



bool socI2cDeviceAdd(struct GpioI2C *i2c, I2cDeviceActionF actF, void *userData);




#endif
