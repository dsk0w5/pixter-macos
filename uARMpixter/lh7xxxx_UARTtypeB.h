//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _LH7XXXX_UART_TYPE_B_H_
#define _LH7XXXX_UART_TYPE_B_H_

#include "mem.h"
#include "CPU.h"
#include "soc_IC.h"
#include "soc_UART.h"



struct Lh7xxxxUartTypeB;



struct Lh7xxxxUartTypeB* lh7xxxxUartTypeBinit(struct ArmMem *physMem, struct SocIc *ic, uint32_t baseAddr, uint8_t irq);
void lh7xxxxUartTypeBprocess(struct Lh7xxxxUartTypeB *uart);		//write out data in TX fifo and read data into RX fifo

void lh7xxxxUartTypeBsetFuncs(struct Lh7xxxxUartTypeB *uart, SocUartReadF readF, SocUartWriteF writeF, void *userData);

#endif
