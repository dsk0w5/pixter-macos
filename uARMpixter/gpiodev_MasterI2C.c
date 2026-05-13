#include "gpiodev_MasterI2C.h"
#include "util.h"


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

struct SocI2c {
	struct SocGpio *gpio;
	struct Dev *devs;
	uint8_t gpioSDA, gpioSCL;
	enum SocGpioState prevSDA, prevSCL;

	//bus state
	enum BusState state;
	uint8_t bitCounterIn, bitCounterOut;
	uint8_t shifterIn;
	uint8_t shifterOut;
};

#define VERBOSE		0

/*
	enum ActionI2C {	//designed so returns can be ORRed together with good results
		i2cStart,		//no params, no returns
		i2cRestart,		//no params, no returns
		i2cStop,		//no params, no returns
		i2cTx,			//param is byte master sent, return is bool "Ack"
		i2cRx,			//param is "bool willBeAcked", return is byte slave sent
	};
	typedef uint_fast8_t (*I2cDeviceActionF)(void *userData, enum ActionI2C stimulus, uint_fast8_t value);
*/


static uint_fast8_t prvCallDevices(struct SocI2c *i2c, enum ActionI2C action, uint8_t param)
{
	static const char *const acts[] = {
		[i2cStart] = "start",
		[i2cRestart] = "restart",
		[i2cTx] = "tx",
		[i2cRx] = "rx",
		[i2cStop] = "stop"
	};
	uint_fast8_t ret = 0, i;
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

static bool prvGpioIsSeenHi(struct SocI2c *i2c, enum SocGpioState sta)
{
	return sta != SocGpioStateLow;	//assume pullup and *we* are not driving it low :D
}

static void socI2cGpioDevicePrvOutputRelease(struct SocI2c *i2c)
{
	socGpioSetState(i2c->gpio, i2c->gpioSDA, true);
}

static void socI2cGpioDevicePrvOutputLow(struct SocI2c *i2c)
{
	socGpioSetState(i2c->gpio, i2c->gpioSDA, false);
}

static void socI2cGpioDevicePrvSomethingChanged(struct SocI2c *i2c)
{
	enum SocGpioState curSDA = socGpioGetState(i2c->gpio, i2c->gpioSDA);
	enum SocGpioState curSCL = socGpioGetState(i2c->gpio, i2c->gpioSCL);

	static const char *states[] = {
		[SocGpioStateLow] = "LO",
		[SocGpioStateHigh] = "HI", 
		[SocGpioStateHiZ] = "ZZ",
	};

	if (VERBOSE > 1)
		fprintf(stderr, "BBI2C: {SDA: %s->%s  SCL: %s->%s}\n", states[i2c->prevSDA], states[curSDA], states[i2c->prevSCL], states[curSCL]);

	if (prvGpioIsSeenHi(i2c, i2c->prevSDA) && !prvGpioIsSeenHi(i2c, curSDA) && prvGpioIsSeenHi(i2c, i2c->prevSCL) && prvGpioIsSeenHi(i2c, curSCL)) {	//SDA falls while SCL is high

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
	else if (!prvGpioIsSeenHi(i2c, i2c->prevSDA) && prvGpioIsSeenHi(i2c, curSDA) && prvGpioIsSeenHi(i2c, i2c->prevSCL) && prvGpioIsSeenHi(i2c, curSCL)) {	//SDA rises while SCL is high

		if (VERBOSE)
			fprintf(stderr, "BBI2C: stop\n");

		prvCallDevices(i2c, i2cStop, 0);
		i2c->state = BusIdle;
	}
	else if (!prvGpioIsSeenHi(i2c, i2c->prevSCL) && prvGpioIsSeenHi(i2c, curSCL)) {	//we sample on rising edge

		uint_fast8_t reply;
	
		i2c->shifterIn = (i2c->shifterIn << 1) + (prvGpioIsSeenHi(i2c, curSDA) ? 0x01 : 0x00);
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
				//we do nothing with ACKs since we already promised the device the ack beforehand...
				//fallthrough to TX path

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
				break;

			case BusIdle:
			case BusTxingByte:
				break;
		}
	}
	else if (prvGpioIsSeenHi(i2c, i2c->prevSCL) && !prvGpioIsSeenHi(i2c, curSCL)) {	//we output on falling edge
		
		uint_fast8_t reply;
	
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

static void socI2cGpioDevicePrvLineChanged(void* userData, uint32_t gpio, bool oldPinState, bool newPinState)
{
	socI2cGpioDevicePrvSomethingChanged((struct SocI2c*)userData);
}

static void socI2cGpioDevicePrvLineDirMaybeChanged(void* userData)
{
	socI2cGpioDevicePrvSomethingChanged((struct SocI2c*)userData);
}

struct SocI2c* socI2cInit(struct ArmMem *physMem, struct SocIc *ic, struct SocDma *dma, uint32_t base, uint32_t irqNo)
{
	struct SocI2c *i2c;
	
	if (physMem || ic || dma || base || irqNo) {
		ERR("GPIO I2C master has no DMA, interrupts, or MMIO address");
		return NULL;
	}

	i2c = (struct SocI2c*)calloc(1, sizeof(*i2c));
	if (i2c) {
		
		i2c->state = BusIdle;
	}
	return i2c;
}

bool socI2cDeviceAdd(struct SocI2c *i2c, I2cDeviceActionF actF, void *userData)
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

void socI2cGpioDeviceConfigPins(struct SocI2c *i2c, struct SocGpio *gpio, uint8_t gpioSDA, uint8_t gpioSCL)
{
	i2c->gpio = gpio;
	i2c->gpioSDA = gpioSDA;
	i2c->gpioSCL = gpioSCL;
	
	socGpioSetNotif(gpio, gpioSDA, socI2cGpioDevicePrvLineChanged, i2c);
	socGpioSetNotif(gpio, gpioSCL, socI2cGpioDevicePrvLineChanged, i2c);
	socGpioSetDirsChangedNotif(gpio, socI2cGpioDevicePrvLineDirMaybeChanged, i2c);

	i2c->prevSDA = socGpioGetState(gpio, gpioSDA);
	i2c->prevSCL = socGpioGetState(gpio, gpioSCL);
}





