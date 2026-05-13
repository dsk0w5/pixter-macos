#ifndef _V_BITBANGED_SPI_H_
#define _V_BITBANGED_SPI_H_

#include <stdbool.h>
#include <stdint.h>


struct VSPI;

enum PinOutState {
	PinLow,
	PinHigh,
	PinHiZ,
};

enum SpiMode {
	SpiMode0,
	SpiMode1,
	SpiMode2,
	SpiMode3,
};

//for below
struct VSPI* vspiInit(const char *name, enum SpiMode mode, bool ignoreClockStateOnDeselect);
void vspiPinsWritten(struct VSPI *vspi, bool mosi, bool clk, bool ncs);
enum PinOutState vspiPinRead(struct VSPI *vspi);	//return MISO
void vspiDestroy(struct VSPI *vspi);


//for above
typedef uint8_t (*VspiDeviceProvideByteF)(void *userData);
typedef void (*VspiDeviceAcceptByteF)(void *userData, uint8_t byte);
typedef void (*VspiDeviceSelectionChanged)(void *userData, bool selected);
void vspiDeviceRegister(struct VSPI *vspi, VspiDeviceProvideByteF provideF, VspiDeviceAcceptByteF acceptF, VspiDeviceSelectionChanged selectionF, void *userData);



#endif
