#include "gpioMasterI2C.h"
#include <stdlib.h>
#include <stdio.h>


struct Dev {
	struct Dev *next;
	I2cDeviceActionF cbkF;
	void *cbkD;
};

enum BusState {
	BusIdle,
	BusRxingAddr,
	BusRxingByte,
	BusTxingAckAndThenWillRx,
	BusTxingAckAndThenWillTx,
	BusTxingByte,
	BusRxingAck,
};

struct GpioI2C {
	GpioPinReadF readSdaF, readSclF;
	GpioPinWriteF writeSdaF, writeSclF;
	void *sdaCbkD, *sclCbkD;

	struct Dev *devs;
	bool prevSDA, prevSCL;

	//bus state
	enum BusState state;
	uint8_t bitCounterIn, bitCounterOut;
	uint8_t shifterIn;
	uint8_t shifterOut;
};

#define VERBOSE		0



static uint_fast8_t prvCallDevices(struct GpioI2C *i2c, enum ActionI2C action, uint8_t param)
{
	static const char *const acts[] = {
		[i2cStart] = "start",
		[i2cRestart] = "restart",
		[i2cTx] = "tx",
		[i2cRx] = "rx",
		[i2cStop] = "stop"
	};
	uint_fast8_t ret = 0;
	struct Dev *dev;
	
	for (dev = i2c->devs; dev; dev = dev->next) {
		
		if (!dev->cbkF)
			continue;
		
		ret |= dev->cbkF(dev->cbkD, action, param);
	}
	
	if (VERBOSE)
		fprintf(stderr, "BBI2C: %s 0x%02x -> 0x%02x\n", acts[action], param, ret);
	
	return ret;
}

static void socI2cGpioDevicePrvOutputRelease(struct GpioI2C *i2c)
{
	i2c->writeSdaF(i2c->sdaCbkD, true);
}

static void socI2cGpioDevicePrvOutputLow(struct GpioI2C *i2c)
{
	i2c->writeSdaF(i2c->sdaCbkD, false);
}

static void socI2cGpioDevicePrvSomethingChanged(struct GpioI2C *i2c)
{
	bool curSDA = i2c->readSdaF(i2c->sdaCbkD);
	bool curSCL = i2c->readSclF(i2c->sclCbkD);

	if (i2c->prevSDA && !curSDA && i2c->prevSCL && curSCL) {

		if (i2c->state != BusIdle) {
			if (VERBOSE)
				fprintf(stderr, "BBI2C: restart\n");

			prvCallDevices(i2c, i2cRestart, 0);
		}
		else {

			if (VERBOSE)
				fprintf(stderr, "BBI2C: start\n");

			prvCallDevices(i2c, i2cStart, 0);
		}
		i2c->state = BusRxingAddr;
		i2c->bitCounterIn = 0;
		i2c->bitCounterOut = 0;
	}
	else if (!i2c->prevSDA && curSDA && i2c->prevSCL && curSCL) {

		if (VERBOSE)
			fprintf(stderr, "BBI2C: stop\n");

		prvCallDevices(i2c, i2cStop, 0);
		i2c->state = BusIdle;
	}
	else if (!i2c->prevSCL && curSCL) {	//we sample on rising edge

		uint_fast8_t reply;
	
		i2c->shifterIn = (i2c->shifterIn << 1) + (curSDA ? 0x01 : 0x00);
		i2c->bitCounterIn++;

		switch (i2c->state) {
			case BusRxingAddr:
				if (i2c->bitCounterIn != 8)
					break;
				i2c->bitCounterIn = 0;
				reply = prvCallDevices(i2c, i2cTx, i2c->shifterIn);
				if (reply)	//someone ACKed
					i2c->state = (i2c->shifterIn & 1) ? BusTxingAckAndThenWillTx : BusTxingAckAndThenWillRx;
				else
					i2c->state = BusIdle;
				break;

			case BusRxingByte:
				if (i2c->bitCounterIn != 8)
					break;
				i2c->bitCounterIn = 0;
				reply = prvCallDevices(i2c, i2cTx, i2c->shifterIn);
				if (reply)
					i2c->state = BusTxingAckAndThenWillRx;
				else
					i2c->state = BusIdle;
				break;

			case BusRxingAck:
				socI2cGpioDevicePrvOutputRelease(i2c);	//we must release te busk for the ack through
				//we do nothing with ACKs since we already promised the device the ack beforehand...
				//fallthrough to TX path

				//fallthrough

			case BusTxingAckAndThenWillTx:
				if (i2c->bitCounterIn != 1)
					break;
				i2c->bitCounterIn = 0;
				i2c->state = BusTxingByte;
				i2c->shifterOut = prvCallDevices(i2c, i2cRx, true);	//we always promise an ACK
				i2c->bitCounterOut = 8;
				break;

			case BusTxingAckAndThenWillRx:
				if (i2c->bitCounterIn != 1)
					break;
				i2c->bitCounterIn = 0;
				i2c->state = BusRxingByte;
				socI2cGpioDevicePrvOutputRelease(i2c);
				break;

			case BusIdle:
			case BusTxingByte:
				break;
		}
	}
	else if (i2c->prevSCL && !curSCL) {	//we output on falling edge
			
		switch (i2c->state) {
			case BusIdle:
			case BusRxingAddr:
				socI2cGpioDevicePrvOutputRelease(i2c);
				break;
			
			case BusTxingAckAndThenWillTx:
			case BusTxingAckAndThenWillRx:
				socI2cGpioDevicePrvOutputLow(i2c);
				break;

			case BusTxingByte:
				if (i2c->shifterOut & 0x80)
					socI2cGpioDevicePrvOutputRelease(i2c);
				else
					socI2cGpioDevicePrvOutputLow(i2c);
				i2c->shifterOut <<= 1;
				if (!--i2c->bitCounterOut)
					i2c->state = BusRxingAck;
				break;

			default:
				break;
		}
	}

	i2c->prevSDA = curSDA;
	i2c->prevSCL = curSCL;
}

void gpioI2cPeriodic(struct GpioI2C* i2c)
{
	socI2cGpioDevicePrvSomethingChanged(i2c);
}

struct GpioI2C* gpioI2cInit(GpioPinReadF readSdaF, GpioPinWriteF writeSdaF, void *sdaCbkD, GpioPinReadF readSclF, GpioPinWriteF writeSclF, void *sclCbkD)
{
	struct GpioI2C *i2c;
	
	i2c = (struct GpioI2C*)calloc(1, sizeof(*i2c));
	if (i2c) {
		
		i2c->state = BusIdle;
		i2c->readSdaF = readSdaF;
		i2c->writeSdaF = writeSdaF;
		i2c->readSclF = readSclF;
		i2c->writeSclF = writeSclF;
		i2c->sdaCbkD = sdaCbkD;
		i2c->sclCbkD = sclCbkD;

		i2c->prevSDA = readSdaF(sdaCbkD);
		i2c->prevSCL = readSclF(sclCbkD);
	}
	return i2c;
}

bool socI2cDeviceAdd(struct GpioI2C *i2c, I2cDeviceActionF actF, void *userData)
{
	struct Dev *dev = malloc(sizeof(*dev));

	if (!dev)
		return false;

	dev->next = i2c->devs;
	dev->cbkF = actF;
	dev->cbkD = userData;
	i2c->devs = dev;

	return true;
}




