#ifndef _MELODY_CHIP_H_
#define _MELODY_CHIP_H_


#include <stdbool.h>
#include <stdint.h>
#include "SDL2/SDL.h"
#include "SDL2/SDL_audio.h"
#include "rom.h"


struct MelodyChip;


struct MelodyChip* melodyChipInit(const struct Rom *rom, SDL_AudioDeviceID audioDev);
bool melodyChipIsPlaying(struct MelodyChip *mc);
void melodyChipControlGpioStateChange(struct MelodyChip *mc, bool dataHi, bool clockHi);
void melodyChipPeriodic(struct MelodyChip *mc);

#endif
