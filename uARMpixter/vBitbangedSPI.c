#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "vBitbangedSPI.h"

struct VSPI {
	//device
	VspiDeviceProvideByteF provideF;
	VspiDeviceAcceptByteF acceptF;
	VspiDeviceSelectionChanged selectionF;
	bool ignoreClockStateOnDeselect;
	enum SpiMode mode;

	void *userData;

	//state
	bool selected, prevClk, prevData, ignoreEdge;
	uint8_t shifterIn, shifterOut, bitsDoneSoFar;

	char name[];
};

//mode 0: change on down, capture on up

static bool vspiPrvGetCpol(struct VSPI *vspi)
{
	switch (vspi->mode) {
		case SpiMode0:
		case SpiMode1:
			return false;

		case SpiMode2:
		case SpiMode3:
			return true;

		default:
			__builtin_unreachable();
			return false;
	}
}

static bool vspiPrvGetCpha(struct VSPI *vspi)
{
	switch (vspi->mode) {
		case SpiMode0:
		case SpiMode2:
			return false;

		case SpiMode1:
		case SpiMode3:
			return true;

		default:
			__builtin_unreachable();
			return false;
	}
}

static bool vspiPrvGetDataSamplingEdge(struct VSPI *vspi)	//NEW state of clock post edge of the edge on which data is sampled
{
	switch (vspi->mode) {
		case SpiMode1:
		case SpiMode2:
			return false;

		case SpiMode0:
		case SpiMode3:
			return true;

		default:
			__builtin_unreachable();
			return false;
	}
}

static const char* vspiPrvGetModeName(struct VSPI *vspi)
{
	switch (vspi->mode) {
		case SpiMode0:
			return "mode 0";

		case SpiMode1:
			return "mode 1";

		case SpiMode2:
			return "mode 2";

		case SpiMode3:
			return "mode 3";

		default:
			__builtin_unreachable();
			return NULL;
	}
}

static void vspiMessage(struct VSPI *vspi, const char *fmt, ...)
{
	char *fmtstr;
	va_list vl;

	if (asprintf(&fmtstr, "VSPI '%s' error: %s", vspi->name, fmt) >= 0) {
		
		va_start(vl, fmt);
		vfprintf(stderr, fmtstr, vl);
		va_end(vl);
		free(fmtstr);
	}
}

static void vspiPrvXchg(struct VSPI *vspi)
{
	if (vspi->acceptF)
		vspi->acceptF(vspi->userData, vspi->shifterIn);
	vspi->shifterOut = vspi->provideF ? vspi->provideF(vspi->userData) : 0x00;
	//vspiMessage(vspi, "RX: %02x -> %02x\n", vspi->shifterIn, vspi->shifterOut);
	vspi->bitsDoneSoFar = 0;
}

void vspiPinsWritten(struct VSPI *vspi, bool mosi, bool clk, bool ncs)
{
	//if (vspi->mode == SpiMode3)
	//	vspiMessage(vspi, "nCS %u CLK %u MOSI %u\n", ncs, clk, mosi);

	if (!vspi->selected && ncs)	//ignore things while not selected
		return;

	if (!vspi->selected) {		//we are being selected

		if (vspiPrvGetCpol(vspi) != clk) {

			vspiMessage(vspi, "unexpected clock state (%s) on spi select for a device in %s\n", clk ? "HI" : "LO", vspiPrvGetModeName(vspi));
			abort();
		}

		vspi->selected = true;
		vspi->bitsDoneSoFar = 0;
		vspi->prevData = mosi;
		vspi->prevClk = clk;
		vspi->ignoreEdge = vspiPrvGetCpha(vspi);

	//	vspiMessage(vspi, "SELECTED\n");

		if(vspi->selectionF)
			vspi->selectionF(vspi->userData, true);
		vspi->shifterOut = vspi->provideF ? vspi->provideF(vspi->userData) : 0x00;
		return;
	}

	if (ncs) {					//we are being deselected
		
		if (vspi->ignoreClockStateOnDeselect && vspi->bitsDoneSoFar == 7) {	//deselected on write before we shifted might be acceptable
			
			vspiPrvXchg(vspi);
		}
		else {
			if (vspiPrvGetCpol(vspi) != clk) {

				vspiMessage(vspi, "unexpected clock state (%s) on spi deselect for a device in %s\n", clk ? "HI" : "LO", vspiPrvGetModeName(vspi));
				abort();
			}

			if (vspi->bitsDoneSoFar){
				vspiMessage(vspi, "unexpected deselect at %u bits done, shifter = 0x%08x\n", vspi->bitsDoneSoFar, vspi->shifterIn);
				abort();
			}
		}

		if (vspi->selectionF)
			vspi->selectionF(vspi->userData, false);
		vspi->selected = false;


	//	vspiMessage(vspi, "DESELECTED\n");

		return;
	}

	//we are selected and are staying so

	if (clk == vspi->prevClk) {			//clock did not change - do nothing but adjust our state of "previously seen data"

		vspi->prevData = mosi;
		return;
	}
	vspi->prevClk = clk;

	if (vspi->ignoreEdge) {

		vspi->prevData = mosi;
		vspi->ignoreEdge = false;
		return;
	}

	if (clk == vspiPrvGetDataSamplingEdge(vspi)) {		//sampling edge
		
		if (mosi != vspi->prevData) {

			vspiMessage(vspi, "unexpected data state change on sampling clock\n");
		//	abort();
		}

	//	if (vspi->mode == SpiMode3)
	//		vspiMessage(vspi, "shifted in %u \n", mosi);

		vspi->shifterIn = vspi->shifterIn * 2 + (mosi ? 1 : 0);
	}
	else {												//shifting edge

		vspi->shifterOut <<= 1;
		vspi->bitsDoneSoFar++;
		vspi->prevData = mosi;							//data is allowed to change now

		if (vspi->bitsDoneSoFar == 8) {

			vspiPrvXchg(vspi);
		}
	}
}

enum PinOutState vspiPinRead(struct VSPI *vspi)
{
	if (!vspi->selected)
		return PinHiZ;
	else
		return (vspi->shifterOut & 0x80) ? PinHigh : PinLow;
}

struct VSPI* vspiInit(const char *name, enum SpiMode mode, bool ignoreClockStateOnDeselect)
{
	struct VSPI *vspi = calloc(1, sizeof(struct VSPI) + strlen(name) + 1);

	strcpy(vspi->name, name);
	vspi->mode = mode;
	vspi->ignoreClockStateOnDeselect = ignoreClockStateOnDeselect;
	return vspi;
}

void vspiDestroy(struct VSPI *vspi)
{
	free(vspi);
}

void vspiDeviceRegister(struct VSPI *vspi, VspiDeviceProvideByteF provideF, VspiDeviceAcceptByteF acceptF, VspiDeviceSelectionChanged selectionF, void *userData)
{
	vspi->provideF = provideF;
	vspi->acceptF = acceptF;
	vspi->selectionF = selectionF;
	vspi->userData = userData;
}
