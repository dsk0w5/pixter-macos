#include "lh7xxxx_I2S.h"
#include "util.h"

struct SocI2s;

struct SocI2s* socI2sInit(struct ArmMem *physMem, struct SocIc *ic, struct SocDma *dma)
{
	ERR("no I2S on this chip");
	return NULL;
}

void socI2sPeriodic(struct SocI2s *i2s)
{
	ERR("no I2S on this chip");
}

void lh795xxI2sSetSSP(struct SocI2s *i2s, struct SocSsp *ssp)
{
	ERR("no I2S on this chip");
}
