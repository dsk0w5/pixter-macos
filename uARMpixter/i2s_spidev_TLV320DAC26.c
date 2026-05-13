#include "i2s_spidev_TLV320DAC26.h"
#include "SDL2/SDL_audio.h"
#include <stdlib.h>
#include <stdio.h>

#define VERBOSE 			0

#define REGS_PER_PAGE		32

enum DacCommsState {
	DacCommsIdle,
	DacCommsWriteWait,
	DacCommsReadWait,
	DacCommsDone,
};

struct Tlv320dac26 {
	//audio
	SDL_AudioDeviceID audioDev;

	//spi comms
	struct IrqConnector *ncsCon;
	uint32_t dataShiftOut;
	uint16_t dataShiftIn;
	uint8_t dataShiftInBits;
	bool selected;

	//clocking
	uint32_t MClk, refClk, sampleClk;

	//internal state
	uint16_t regs[3 * 32];
	enum DacCommsState commsState;
	uint16_t *regP;
};

static const uint32_t mRegsExist[] = {0x00000000, 0x00000010, 0x7fffffff};	//mask for extant regs



static void tlv320dac26prvCreateAudioChannelIfNeeded(struct Tlv320dac26 *dac)
{
	SDL_AudioSpec desiredSpecMusic = {
		.freq = dac->sampleClk / 25 * 25,	//rounds then weird 8k sampling rate used
		.format = 0x8010,	//s16
		.channels = 2,
	};

	if (!dac->audioDev) {

		SDL_AudioSpec gotten;

		dac->audioDev = SDL_OpenAudioDevice(NULL, false, &desiredSpecMusic, &gotten, 0);
		if (!dac->audioDev) {

			fprintf(stderr, "SDL audio error: %s\n", SDL_GetError());
			abort();
		}
		SDL_PauseAudioDevice(dac->audioDev, false);
	}
}

static void tlv320dac26prvI2sClientProc(void* userData, uint_fast8_t nBits, uint_fast16_t sent)
{
	struct Tlv320dac26 *dac = (struct Tlv320dac26*)userData;

	if (nBits == 16) {
	
		//audio halfword
		uint16_t sentU16 = sent;

		tlv320dac26prvCreateAudioChannelIfNeeded(dac);
		SDL_QueueAudio(dac->audioDev, &sentU16, sizeof(sentU16));
	}
}

static bool tlv320dac26prvClockCalc(struct Tlv320dac26 *dac)	//return if anything changes
{
	static const uint8_t divs[] = {2, 3, 4, 6, 8, 10, 11, 12, };
	uint_fast16_t pllProg1B = dac->regs[2 * REGS_PER_PAGE + 0x1b];
	uint_fast16_t pllProg1C = dac->regs[2 * REGS_PER_PAGE + 0x1c];
	uint32_t q = (pllProg1B >> 11) & 0x0f, p = (pllProg1B >> 8) & 0x07, j = (pllProg1B >> 2) & 0x3f, d = (pllProg1C >> 2) & 0x3fff, refClkTimes2, refClk, sampleClk;

	if (pllProg1B & 0x8000)		//PLL on
		refClkTimes2 = ((uint64_t)dac->MClk * (j * 1000 + d)) / (1024 * 1000 * p);
	else						//PLL off
		refClkTimes2 = dac->MClk / (q * 64);
	refClk = refClkTimes2 / 2;
	sampleClk = refClkTimes2 / divs[(dac->regs[2 * REGS_PER_PAGE + 0x00] >> 3) & 0x07];

	if (dac->refClk == refClk && dac->sampleClk == sampleClk)
		return false;

	if (VERBOSE)
		fprintf(stderr, "TLV320DAC26: MCLK %uHz, RefClk %uHz -> %uHz, SampClk %uHz -> %uHz\n", dac->MClk, dac->refClk, refClk, dac->sampleClk, sampleClk);

	dac->refClk = refClk;
	dac->sampleClk = sampleClk;

	if (dac->audioDev) {	//let it be reopened if needed

		SDL_CloseAudioDevice(dac->audioDev);
		dac->audioDev = 0;
	}

	return true;
}

static uint16_t tlv320dac26prvMessage(struct Tlv320dac26 *dac, uint16_t msg)
{
	if (dac->commsState == DacCommsIdle) {

		uint_fast8_t page = (msg >> 11) & 0x0f;
		uint_fast8_t reg = (msg >> 5) & 0x1f;
		bool write = !(msg & 0x8000);

		if ((msg & 0x1f) || page >= sizeof(mRegsExist) / sizeof(*mRegsExist) || !(mRegsExist[page] & (1 << reg))) {
			fprintf(stderr, "TLV320DAC26: not sure what to do with command 0x%04x (seemingly a %s to %u,0x%02x)\n", msg, write ? "WR" : "RD", page, reg);
			dac->commsState = DacCommsDone;
			msg = 0xffff;
		}
		else if (write) {

			dac->regP = &dac->regs[page * REGS_PER_PAGE + reg];
			dac->commsState = DacCommsWriteWait;
			msg = 0xffff;
		}
		else {
			dac->commsState = DacCommsReadWait;
			msg = dac->regs[page * REGS_PER_PAGE + reg];

			if (VERBOSE)
				fprintf(stderr, "TLV320DAC26: read [%u,0x%02x] -> 0x%04x\n", (unsigned)page, (unsigned)reg, msg);
		}
	}
	else if (dac->commsState == DacCommsWriteWait) {

		if (VERBOSE)
			fprintf(stderr, "TLV320DAC26: write 0x%04x -> [%u,0x%02x]\n", msg, (unsigned)(dac->regP - dac->regs) / REGS_PER_PAGE, (unsigned)(dac->regP - dac->regs) % REGS_PER_PAGE);

		*dac->regP = msg;
		dac->regP = NULL;
		msg = 0xffff;

		tlv320dac26prvClockCalc(dac);
		dac->commsState = DacCommsDone;
	}
	else if (dac->commsState == DacCommsReadWait) {

		msg = 0xffff;
		dac->commsState = DacCommsDone;
	}
	else {

		msg = 0xffff;
	}

	return msg;
}

static uint_fast16_t tlv320dac26prvSpiXfer(void* userData, uint_fast8_t nBits, uint_fast16_t sent)
{
	struct Tlv320dac26 *dac = (struct Tlv320dac26*)userData;
	uint_fast8_t bitsConsumed = 0;
	uint_fast16_t ret;

	if (!dac->selected)
		return 0;

	sent &= (1 << nBits) - 1;	//nobody promised top bits are clear, but we need it so

	//we process 16 bits at a time, but data may be provided as more or less by spi. we need to realign it here
	//handling replies is equally hard
	while (nBits) {
		
		dac->dataShiftIn = dac->dataShiftIn * 2 + ((sent >> (nBits - 1)) & 1);
		if (++dac->dataShiftInBits == 16) {
			uint32_t reply = tlv320dac26prvMessage(dac, dac->dataShiftIn);
			
			dac->dataShiftOut |= reply << (16 - bitsConsumed);
			dac->dataShiftInBits = 0;
		}
		nBits--;
		bitsConsumed++;
	}
	ret = dac->dataShiftOut >> (32 - nBits);
	dac->dataShiftOut <<= nBits;

	return ret;
}

static uint8_t tlv320dac26prvVspiProvideByte(void *userData)
{
	struct Tlv320dac26 *dac = (struct Tlv320dac26*)userData;
	uint8_t ret;

	if (!dac->selected)
		return 0;

	ret = dac->dataShiftOut >> 24;
	dac->dataShiftOut <<= 8;

	return ret;
}

static void tlv320dac26prvVspiAcceptByte(void *userData, uint8_t byte)
{
	struct Tlv320dac26 *dac = (struct Tlv320dac26*)userData;
	
	if (!dac->selected)
		return;

	dac->dataShiftIn = dac->dataShiftIn * 256 + byte;
	if ((dac->dataShiftInBits += 8) == 16) {

		dac->dataShiftOut |= ((uint32_t)tlv320dac26prvMessage(dac, dac->dataShiftIn)) << 16;
		dac->dataShiftInBits = 0;
	}
}

static void tlv320dac26prvVspiSelectionChanged(void *userData, bool selected)
{
	struct Tlv320dac26 *dac = (struct Tlv320dac26*)userData;
	
	if (!dac->selected && selected)
		dac->commsState = DacCommsIdle;
	dac->selected = selected;
}

static void tlv320dac26prvSelectionChanged(void *userData, bool state)
{
	struct Tlv320dac26 *dac = (struct Tlv320dac26*)userData;

	if ((dac->selected && !state) || (!dac->selected && state))
		return;

	dac->selected = !state;
	if (state) {		//deselected

		if (dac->dataShiftInBits)
			fprintf(stderr, "DAC deselected with %u bits still not procesed\n", dac->dataShiftInBits);
	}
	else {				//selected

		dac->dataShiftInBits = 0;
		dac->commsState = DacCommsIdle;
	}
}

static void tlv320dac26prvCommonInit(struct Tlv320dac26 *dac, struct SocI2s *i2s)
{
	if (!socI2sSetClient(i2s, tlv320dac26prvI2sClientProc, dac)) {
		
		fprintf(stderr, "cannot subscribe DAC to I2S traffic");
		abort();
	}
	dac->commsState = DacCommsIdle;
}

struct Tlv320dac26 *tlv320dac26initWithSSP(struct SocSsp* spi, struct SocI2s *i2s)
{
	struct Tlv320dac26 *dac = calloc(1, sizeof(*dac));

	if (dac) {
		dac->ncsCon = irqConnectorAllocate(tlv320dac26prvSelectionChanged, dac);
		if (!dac->ncsCon) {
			fprintf(stderr, "cannot alloc ncs pin for DAC");
			abort();
		}

		if (!socSspAddClient(spi, tlv320dac26prvSpiXfer, dac)) {
			fprintf(stderr, "cannot append DAC to spi client list");
			abort();
		}
		tlv320dac26prvCommonInit(dac, i2s);
	}

	return dac;
}

struct Tlv320dac26* tlv320dac26initWithVSPI(struct VSPI *vspi, struct SocI2s *i2s)
{
	struct Tlv320dac26 *dac = calloc(1, sizeof(*dac));

	if (dac) {

		vspiDeviceRegister(vspi, tlv320dac26prvVspiProvideByte, tlv320dac26prvVspiAcceptByte, tlv320dac26prvVspiSelectionChanged, dac);
		tlv320dac26prvCommonInit(dac, i2s);
	}

	return dac;
}

struct IrqConnector *tlv320dac26getNcsPinCtrl(struct Tlv320dac26 *dac)
{
	return dac->ncsCon;
}

void tlv320dac26setMClk(struct Tlv320dac26 *dac, uint32_t hz)
{
	dac->MClk = hz;
	tlv320dac26prvClockCalc(dac);
}






