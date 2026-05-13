#ifndef _IRQ_CONNECTOR_H_
#define _IRQ_CONNECTOR_H_

struct IrqConnector;
#include <stdbool.h>


typedef void (*IrqConnectorCallback)(void *userData, bool state);

struct IrqConnector* irqConnectorAllocate(IrqConnectorCallback cbkF, void *cbkD);
void irqConnectorFree(struct IrqConnector* irqC);
void irqConnectorSignal(struct IrqConnector* irqC, bool state);





#endif

