#include "i2cdev_24C02.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct AT25C02 {
	uint8_t addr7bit;

	uint8_t inTransaction	: 1;
	uint8_t addrSeen		: 1;
	uint8_t ourAddr			: 1;
	uint8_t isRead			: 1;
	uint8_t regAddrSeen		: 1;
	uint8_t reg;

	FILE *storage;
	unsigned offset;
};

#define VERBOSE		0

static bool at24c02prvDoRegWrite(struct AT25C02 *eeprom, uint_fast8_t reg, uint8_t val)
{
	if (VERBOSE)
		fprintf(stderr, "24C02[%02x]: writing 0x%02x -> [0x%02x]\n", eeprom->addr7bit, val, reg);
	
	return !fseek(eeprom->storage, eeprom->offset + reg, SEEK_SET) && 1 == fwrite(&val, 1, 1, eeprom->storage);
}

static uint_fast8_t at24c02prvDoRegRead(struct AT25C02 *eeprom, uint_fast8_t reg)
{
	uint8_t val;

	if (fseek(eeprom->storage, eeprom->offset + reg, SEEK_SET) || 1 != fread(&val, 1, 1, eeprom->storage))
		val = 0xf0;

	if (VERBOSE)
		fprintf(stderr, "24C02[%02x]: reading [0x%02x] -> 0x%02x\n", eeprom->addr7bit, reg, val);
	
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

struct AT25C02* at24c02init(struct GpioI2C* i2c, uint_fast8_t addr7bit, FILE *storage, unsigned offset)
{
	struct AT25C02 *eeprom = (struct AT25C02*)calloc(1, sizeof(*eeprom));
	
	if (!eeprom) {

		fprintf(stderr, "cannot alloc 24C02\n");
		return false;
	}
	
	eeprom->storage = storage;
	eeprom->offset = offset;
	eeprom->addr7bit = addr7bit;

	if (!socI2cDeviceAdd(i2c, at24c02prvI2cHandler, eeprom)) {

		fprintf(stderr, "cannot add 24C02 to I2C\n");
		return false;
	}
	
	return eeprom;
}
