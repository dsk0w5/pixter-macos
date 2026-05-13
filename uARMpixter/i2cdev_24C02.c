//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "i2cdev_24C02.h"
#include <string.h>
#include <stdlib.h>
#include "util.h"

struct AT25C02 {
	uint8_t addr7bit;

	uint8_t inTransaction	: 1;
	uint8_t addrSeen		: 1;
	uint8_t ourAddr			: 1;
	uint8_t isRead			: 1;
	uint8_t regAddrSeen		: 1;
	uint8_t reg;

	uint8_t data[256];

	FILE *backingFile;
};

#define VERBOSE		0

static bool at24c02prvDoRegWrite(struct AT25C02 *eeprom, uint_fast8_t reg, uint_fast8_t val)
{
	if (VERBOSE)
		fprintf(stderr, "24C02: writing 0x%02x -> [0x%02x]\n", val, reg);
	
	eeprom->data[reg] = val;

	//let libc buffer :)
	rewind(eeprom->backingFile);
	fwrite(eeprom->data, 1, sizeof(eeprom->data), eeprom->backingFile);
	
	return true;
}

static uint_fast8_t at24c02prvDoRegRead(struct AT25C02 *eeprom, uint_fast8_t reg)
{
	uint_fast8_t val = eeprom->data[reg];

	if (VERBOSE)
		fprintf(stderr, "24C02: reading [0x%02x] -> 0x%02x\n", reg, val);
	
	return val;
}

static uint_fast8_t at24c02prvI2cHandler(void *userData, enum ActionI2C stimulus, uint_fast8_t value)
{
	struct AT25C02 *eeprom = (struct AT25C02*)userData;
	
	switch (stimulus) {
		case i2cStart:
			eeprom->inTransaction = 1;
			eeprom->regAddrSeen = 0;
			//fallthrough
		
		case i2cRestart:
			eeprom->addrSeen = 0;
			eeprom->ourAddr = 0;
			return 0;
		
		case i2cStop:
			eeprom->inTransaction = 0;
			return 0;
		
		case i2cTx:
			if (!eeprom->inTransaction)
				return 0;
			if (!eeprom->addrSeen) {
				eeprom->addrSeen = 1;
				if ((value >> 1) != eeprom->addr7bit)
					return 0;
				eeprom->ourAddr = 1;
				eeprom->isRead = value & 1;
				if (!eeprom->isRead)			//writes always begin with an address, even after a restart
					eeprom->regAddrSeen = 0;
				return 1;
			}
			if (!eeprom->ourAddr)
				return 0;
			if (eeprom->isRead) {
				
				fprintf(stderr, "unexpected write in read mode\n");
				return 0;
			}
			//write to us
			if (!eeprom->regAddrSeen) {
				eeprom->regAddrSeen = 1;
				eeprom->reg = value;
				
				return 1;
			}
			//write to a reg
			return at24c02prvDoRegWrite(eeprom, eeprom->reg++, value) ? 1 : 0;
		
		case i2cRx:
			if (!eeprom->inTransaction || !eeprom->addrSeen || !eeprom->ourAddr)
				return 0;
			if (!eeprom->isRead) {
				
				fprintf(stderr, "unexpected read in write mode\n");
				return 0;
			}
			if (!eeprom->regAddrSeen) {
				
				fprintf(stderr, "unexpected read before register specified\n");
				return 0;
			}
			return at24c02prvDoRegRead(eeprom, eeprom->reg++);
		
		default:
			return 0;
	}
}

struct AT25C02* at24c02init(struct SocI2c* i2c, uint_fast8_t addr7bit, FILE *backingFile)
{
	struct AT25C02 *eeprom = (struct AT25C02*)calloc(1, sizeof(*eeprom));
	
	if (!eeprom)
		ERR("cannot alloc 24C02");
	
	memset(eeprom->data, 0xff, sizeof(eeprom->data));
	eeprom->addr7bit = addr7bit;
	eeprom->backingFile = backingFile;

	if (backingFile) {
		rewind(backingFile);
		fread(eeprom->data, 1, sizeof(eeprom->data), backingFile);
	}

	if (!socI2cDeviceAdd(i2c, at24c02prvI2cHandler, eeprom))
		ERR("cannot add 24C02 to I2C\n");
	
	return eeprom;
}
