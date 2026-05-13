//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _LH7XXXX_UART_TYPE_A_H_
#define _LH7XXXX_UART_TYPE_A_H_

#include "mem.h"
#include "CPU.h"
#include "soc_IC.h"
#include "soc_UART.h"



struct Lh7xxxxUartTypeA;



struct Lh7xxxxUartTypeA* lh7xxxxUartTypeAinit(struct ArmMem *physMem, struct SocIc *ic, struct SocDma *dma, uint32_t baseAddr, uint8_t irq, int8_t rxIntrIrq, int8_t txIntrIrq, int8_t dmaReqTx, int8_t dmaReqRx);
void lh7xxxxUartTypeAprocess(struct Lh7xxxxUartTypeA *uart);		//write out data in TX fifo and read data into RX fifo

void lh7xxxxUartTypeAsetFuncs(struct Lh7xxxxUartTypeA *uart, SocUartReadF readF, SocUartWriteF writeF, void *userData);

#endif
