#ifndef _6502_m23_H_
#define _6502_m23_H_



typedef enum IRQn
{
  Reset_IRQn                    = -15,
  NonMaskableInt_IRQn           = -14,
  HardFault_IRQn                = -13,
  MemManageFault_IRQn           = -12,
  BusFault_IRQn                 = -11,
  UsageFault_IRQn               = -10,
  SecureFault_IRQn              = -9,
  SVCall_IRQn                   = -5,
  Debug_IRQn                    = -4,
  PendSV_IRQn                   = -2,
  SysTick_IRQn                  = -1,

/******  CPU Specific Interrupt Numbers *************************/

} IRQn_Type;



#define __NVIC_PRIO_BITS          2 
#include "core_cm23.h"


#define VTOR					(*(volatile uint32_t*)0x40000080)


#endif
