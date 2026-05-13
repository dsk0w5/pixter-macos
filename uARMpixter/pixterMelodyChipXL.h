#ifndef _PIXTER_MELODY_CHIP_XL_H_
#define _PIXTER_MELODY_CHIP_XL_H_


#include <stdbool.h>
#include <stdint.h>
#include "SDL2/SDL.h"
#include "SDL2/SDL_audio.h"
#include "pixterRomFile.h"


struct MelodyChipXL;

bool melodyChipXLisXLromFile(const struct PixterRomFile *rom);						//does this rom file call for the XL chip?

struct MelodyChipXL* melodyChipXLinit(const struct PixterRomFile *rom, SDL_AudioDeviceID audioDev);
bool melodyChipXLisPlaying(struct MelodyChipXL *mc);
void melodyChipXLcontrolGpioStateChange(struct MelodyChipXL *mc, bool dataHi, bool clockHi);
void melodyChipXLperiodic(struct MelodyChipXL *mc);	//to be called every MELODY_XL_PERIODIC_RATE_NSEC nanoseconds precisely

#define MELODY_XL_PERIODIC_RATE_NSEC			15485922


#endif
