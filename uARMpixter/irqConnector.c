#include "irqConnector.h"
#include <stdlib.h>



struct IrqConnector {
	IrqConnectorCallback cbkF;
	void *cbkD;
};



struct IrqConnector* irqConnectorAllocate(IrqConnectorCallback cbkF, void *cbkD)
{
	struct IrqConnector* ret = calloc(1, sizeof(*ret));

	ret->cbkF = cbkF;
	ret->cbkD = cbkD;

	return ret;
}

void irqConnectorFree(struct IrqConnector* irqC)
{
	free(irqC);
}

void irqConnectorSignal(struct IrqConnector* irqC, bool state)
{
	irqC->cbkF(irqC->cbkD, state);
}



