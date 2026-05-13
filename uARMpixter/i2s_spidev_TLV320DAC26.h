#ifndef _TLV320DAC26_H_
#define _TLV320DAC26_H_

#include "vBitbangedSPI.h"
#include "irqConnector.h"
#include "soc_I2S.h"
#include "soc_I2S.h"
#include "soc_SSP.h"

struct Tlv320dac26;


struct Tlv320dac26* tlv320dac26initWithSSP(struct SocSsp* spi, struct SocI2s *i2s);
struct Tlv320dac26* tlv320dac26initWithVSPI(struct VSPI *vspi, struct SocI2s *i2s);

void tlv320dac26setMClk(struct Tlv320dac26 *dac, uint32_t hz);
struct IrqConnector *tlv320dac26getNcsPinCtrl(struct Tlv320dac26 *dac); 		//only SSP mode uses this


#endif
