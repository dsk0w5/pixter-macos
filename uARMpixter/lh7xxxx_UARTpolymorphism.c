//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "lh7xxxx_UARTtypeA.h"
#include "lh7xxxx_UARTtypeB.h"
#include "soc_UART.h"
#include "device.h"
#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "mem.h"


struct SocUart {

	bool isType2;
	union {
		struct Lh7xxxxUartTypeA* uA;
		struct Lh7xxxxUartTypeB* uB;
	};
};

struct SocUart* socUartInit(struct ArmMem *physMem, struct SocIc *ic, struct SocDma *dma, uint32_t baseAddr, uint8_t irq, int8_t dmaReqTx, int8_t dmaReqRx)
{
	struct SocUart *u = calloc(1, sizeof(*u));

	if (!u)
		return NULL;

	if (deviceGetSocRev()) {

		u->isType2 = false;
		u->uA = lh7xxxxUartTypeAinit(physMem, ic, dma, baseAddr, irq, -1, -1, dmaReqTx, dmaReqRx);

		if (!u->uA) {
			free(u);
			u = NULL;
		}
	}
	else if (baseAddr == 0xfffc2000) {

		u->isType2 = true;
		u->uB = lh7xxxxUartTypeBinit(physMem, ic, baseAddr, irq);

		if (!u->uB) {
			free(u);
			u = NULL;
		}
	}
	else if (baseAddr == 0xfffc0000) {

		u->isType2 = false;
		u->uA = lh7xxxxUartTypeAinit(physMem, ic, dma, baseAddr, irq, -1, -1, -1, -1);

		if (!u->uA) {
			free(u);
			u = NULL;
		}
	}
	else if (baseAddr == 0xfffc1000) {

		u->isType2 = false;
		u->uA = lh7xxxxUartTypeAinit(physMem, ic, dma, baseAddr, irq, irq - 2, irq - 1, -1, -1);

		if (!u->uA) {
			free(u);
			u = NULL;
		}
	}
	else {

		free(u);
		u = NULL;
	}
	
	return u;
}


void socUartProcess(struct SocUart *uart)
{
	if (uart->isType2)
		lh7xxxxUartTypeBprocess(uart->uB);
	else
		lh7xxxxUartTypeAprocess(uart->uA);
}

void socUartSetFuncs(struct SocUart *uart, SocUartReadF readF, SocUartWriteF writeF, void *userData)
{
	if (uart->isType2)
		lh7xxxxUartTypeBsetFuncs(uart->uB, readF, writeF, userData);
	else
		lh7xxxxUartTypeAsetFuncs(uart->uA, readF, writeF, userData);
}

