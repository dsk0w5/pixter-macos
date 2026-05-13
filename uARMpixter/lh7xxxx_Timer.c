#include "lh7xxxx_Timer.h"
#include "SDL2/SDL_audio.h"
#include "lh7xxxx_IC.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"


struct Lh7xxxxTimer {
	struct SocIc *ic;
	uint32_t base;
	uint8_t irqNo;
	bool isType0;

	uint16_t ctrl, cnt;
	uint16_t cmp[2];
	uint16_t cap[5];		//2+ for type0 only
	uint16_t cmpCapCtrl;	//type0 only
	uint8_t intrCtrl, sta;

	//internal
	uint8_t divideCtr;
};


//state shared by all timers together
static SDL_AudioDeviceID mAudioDevPwmOutput;
static volatile uint8_t mHiSample = 0x80, mLoSample = 0x80;

static void tmrPrvQueueSample(void)
{
	int_fast16_t diff;
	uint8_t s;

	diff = (int16_t)(uint16_t)(uint8_t)(0x80 - mHiSample) - (int16_t)(uint16_t)(uint8_t)(0x80 - mLoSample) + 0x80;
	if (diff < 0)
		diff = 0;
	else if (diff > 255)
		diff = 255;

	s = diff;
	SDL_QueueAudio(mAudioDevPwmOutput, &s, 1);
}

static void tmrPrvCreateAudioChannelIfNeeded(void)
{
	//there is literally NO way this will work well since the emulator keeps no time at all...

	static const SDL_AudioSpec desiredSpecMusic = {
		.freq = 8000,
		.format = 8,	//u8
		.channels = 1,
	};

	if (!mAudioDevPwmOutput) {

		SDL_AudioSpec gotten;

		mAudioDevPwmOutput = SDL_OpenAudioDevice(NULL, false, &desiredSpecMusic, &gotten, 0);
		if (!mAudioDevPwmOutput) {

			fprintf(stderr, "SDL audio (pwm) error: %s\n", SDL_GetError());
			abort();
		}
		SDL_PauseAudioDevice(mAudioDevPwmOutput, false);
	}
}

static void tmrPrvUpdateIrq(struct Lh7xxxxTimer *tmr)
{
	socIcInt(tmr->ic, tmr->irqNo, !!(tmr->sta & tmr->intrCtrl));
}

static void tmrPrvPostChange(struct Lh7xxxxTimer *tmr, uint16_t prevVal)
{
	bool irqChanged = false, overflowOnCmp1 = tmr->isType0 ? !!(tmr->cmpCapCtrl & 0x4000) : !!(tmr->ctrl & 0x2000);
	uint_fast8_t i;

	for (i = 0; i < 2; i++) {

		if (prevVal == tmr->cmp[i]) {

			irqChanged = true;
			tmr->sta |= 0x02 << i;
		}
	}
	if (overflowOnCmp1 && prevVal == tmr->cmp[1]) {

		irqChanged = true;
		tmr->cnt = 0;
		tmr->sta |= 0x01;
	}
	else if (!overflowOnCmp1 && !tmr->cnt) {

		irqChanged = true;
		tmr->sta |= 0x01;
	}

	if (irqChanged)
		tmrPrvUpdateIrq(tmr);
}

void lh7xxxxTimerPeriodic(struct Lh7xxxxTimer *tmr)		//called at HCLK / 2 (oof)
{
	if ((tmr->ctrl & 0x02) && !--tmr->divideCtr) {	//we act as if CTCCLK is SysClk / 256 since that make the logic here easier
		
		tmr->divideCtr = 1 << ((tmr->ctrl >> 2) & 7);
		tmrPrvPostChange(tmr, tmr->cnt++);
	}
}

static bool tmrPrvMemAccessF(void* userData, uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	struct Lh7xxxxTimer *tmr = (struct Lh7xxxxTimer*)userData;
	uint32_t val = 0, paorig = pa;
	
	if (pa % 4 || size != 4) {
		fprintf(stderr, "%s: Unexpected %s of %u bytes to 0x%08lx\n", __func__, write ? "write" : "read", size, (unsigned long)pa);
		return false;
	}
	
	pa -= tmr->base;
	pa >>= 2;

	if (write)
		val = *(uint32_t*)buf;
	
	//if (write)
	//	fprintf(stderr, "Timer write to 0x%08lx => [0x%08lx]\n", (unsigned long)val, (unsigned long)paorig);
	
	if (tmr->isType0) {

		if (pa == 0x04 / 4) {

			if (write)
				tmr->cmpCapCtrl = val;
			else
				val = tmr->cmpCapCtrl;
			goto done;
		}
		else if (pa > 0x04 / 4) {

			pa--;
			//skip it to make it look like the other timers...
		}
	}

	switch (pa) {
		
		case 0x00 / 4:				//CTRL
			if (write) {

				tmr->ctrl = val &~ 1;
				if (val & 1) {
					uint_fast16_t prevVal = tmr->cnt;

					tmr->cnt = 0;
					tmrPrvPostChange(tmr, prevVal);
				}
			}
			else
				val = tmr->ctrl;
			break;

		case 0x04 / 4:				//intCtrl
			if (write) {

				tmr->intrCtrl = val;
				tmrPrvUpdateIrq(tmr);
			}
			else
				val = tmr->intrCtrl;
			break;

		case 0x08 / 4:				//status
			if (write) {

				tmr->sta &=~ val;
				tmrPrvUpdateIrq(tmr);
			}
			else
				val = tmr->sta;
			break;

		case 0x0C / 4:				//CNT
			if (write) {
				uint_fast16_t prevVal = tmr->cnt;

				tmr->cnt = val;
				tmrPrvPostChange(tmr, prevVal);
			}
			else
				val = tmr->cnt;
			break;

		case 0x10 / 4:				//CMPx
		case 0x14 / 4:
			if (write) {
				if (pa == 0x10 / 4) {
					if (tmr->base == 0xfffc4030) {
						mHiSample = val & 0x7f;
						tmrPrvQueueSample();		//emulating this from first principles is hard on the CPU, so we use knowledege that T2 is written before T2
					}
					else if (tmr->base == 0xfffc4050) {

						mLoSample = val & 0x7f;
					}
				}
				tmr->cmp[pa - 0x10 / 4] = val;
			}
			else
				val = tmr->cmp[pa - 0x10 / 4];
			break;

		case 0x18 / 4:					//CAPx (limited by size for non-0-type
		case 0x1c / 4:
		case 0x20 / 4:
		case 0x24 / 4:
		case 0x28 / 4:
			if (write)
				return false;
			else
				val = tmr->cap[pa - 0x18 / 4];
			break;

		default:
			return false;
	}
	
	//if (!write)
	//	fprintf(stderr, "Timer READ [0x%08lx] -> 0x%08lx\n", (unsigned long)paorig, (unsigned long)val);

done:
	if (!write)
		*(uint32_t*)buf = val;
	
	return true;
}


struct Lh7xxxxTimer* lh7xxxxTimerInit(struct ArmMem *mem, struct SocIc *ic, uint32_t base, uint_fast8_t irqNo, bool isType0)
{
	struct Lh7xxxxTimer *tmr = (struct Lh7xxxxTimer*)calloc(1, sizeof(*tmr));
	uint_fast8_t i;
	
	if (!tmr)
		ERR("cannot alloc Timer at 0x%08x", base);
	
	tmr->ic = ic;
	tmr->base = base;
	tmr->irqNo = irqNo;
	tmr->isType0 = isType0;

	memset(tmr->cmp, 0xff, sizeof(tmr->cmp));

	if (!memRegionAdd(mem, base, isType0 ? 0x30 : 0x20, tmrPrvMemAccessF, tmr))
		ERR("cannot add TMR to MEM\n");
	
	tmrPrvCreateAudioChannelIfNeeded();
	return tmr;
}
