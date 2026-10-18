#include "pixterMelodyChip.h"
#include <stdlib.h>


#define DEBUG	 					0



#define MELODY_CHIP_TIMEOUT			6				//ticks since last bit till we consider command  completed
#define MELODY_IDX_SILENT			0x3f			//used to stop all music so this melody cannot exist
#define MELODY_IDX_GO_ADVANCED		0x3c			//used to go to advanced mode

#define ADVANCED_INDEX_BASE			0x100			//in rom, advanced melody instruments will have this index
#define ADVANCED_MAX_INSTRS			0x40			//max number of instruments (actual limit is lower in the only cart that does this)
#define ADVANCED_INSTR_GO_BASIC		0xf3cf3c		//in advanced mode this command makes us to go basic mode

#define MAX_MELODIES_AT_ONCE		4				//local limitation that also must match advanced mode

enum MelodyChipType {
	MelodyChipNormal,				//62 melodies, stop is 63
	MelodyChipAdvanced,				//4 instruments at once (for Music Studio Pixter CVlassic game)
};

enum PlaybackState {
	PlaybackIdle,
	PlaybackPlayingOnce,
	PlaybackPlayingLooped,
};

enum CommsState {
	CommsIdle,
	CommsRxing,
};

struct MelodyChip {
	const struct PixterRomFile *rom;
	SDL_AudioDeviceID audioDev;

	uint32_t commsInput;
	uint8_t commsNumBits, lastVolume;
	bool prevClock, prevData, isAdvancedChip, isInAdvancedMode, lastTransitionWasStartOfStart;
	enum MelodyChipType chipType;


	enum PlaybackState playState;
	enum CommsState commsState;

	uint32_t lastValidCommand;
	uint32_t lastBitTime;

	uint32_t timestamp;
};


struct MelodyChip* melodyChipInit(const struct PixterRomFile *rom, SDL_AudioDeviceID audioDev)
{
	bool advancedIndicesSeen = false, melodyPresentAtAdvancedCmd = false, invalidAdvancedIndexSeen = false;
	struct MelodyChip *mc;
	unsigned i;

	if (!rom)
		return NULL;

	for (i = 0; i < rom->numMelodyIndices; i++) {

		if (!rom->melodies[i] || !rom->melodies[i]->length)
			continue;

		if (i == MELODY_IDX_SILENT) {

			fprintf(stderr, "MELODYCHIP: This ROM claims to have a melody with index 0x%02x, but this index is reserved for stoppping music. Melody chip will not work!\n", MELODY_IDX_SILENT);
			return NULL;
		}

		if (i >= ADVANCED_INDEX_BASE && i - ADVANCED_INDEX_BASE < ADVANCED_MAX_INSTRS)
			advancedIndicesSeen = true;

		if (i == ADVANCED_INDEX_BASE + ADVANCED_INSTR_GO_BASIC % ADVANCED_MAX_INSTRS)
			invalidAdvancedIndexSeen = true;

		if (i == MELODY_IDX_GO_ADVANCED)
			melodyPresentAtAdvancedCmd = true;
	}


	if (!advancedIndicesSeen) {

		fprintf(stderr, "MELODYCHIP: This melody chip is NORMAL\n");
	}
	else if (melodyPresentAtAdvancedCmd) {
		fprintf(stderr, "MELODYCHIP: This ROM contains advanced melody indices but seems to have a melody at GO_ADVANCED command (0x%02x). Melody chip will not work!\n",
			MELODY_IDX_GO_ADVANCED);
		return NULL;
	}
	else if (invalidAdvancedIndexSeen) {
		fprintf(stderr, "MELODYCHIP: This ROM contains advanced melody indices but that invludex index %u which is reserved for existing advanced mode! Melody chip will not work!\n",
			ADVANCED_INDEX_BASE + ADVANCED_INSTR_GO_BASIC % ADVANCED_MAX_INSTRS);
		return NULL;
	}
	else {

		fprintf(stderr, "MELODYCHIP: This melody chip is ADVANCED\n");
	}

	mc = malloc(sizeof(struct MelodyChip));
	if (mc) {
		unsigned i;

		mc->rom = rom;
		mc->audioDev = audioDev;
		mc->playState = PlaybackIdle;
		mc->commsState = CommsIdle;
		mc->prevClock = false;
		mc->prevData = false;
		mc->timestamp = 0;
		mc->chipType = advancedIndicesSeen ? MelodyChipAdvanced : MelodyChipNormal;
		mc->isInAdvancedMode = false;
		mc->commsNumBits = 0;
		mc->lastTransitionWasStartOfStart = false;
		mc->lastVolume = 3;
	
		if (DEBUG > 1){
			unsigned i;

			for (i = 0; i < mc->rom->numMelodyIndices; i++) {
				
				if (mc->rom->melodies[i] && mc->rom->melodies[i]->length) {
					fprintf(stderr, "melody %u (0x%04x) exists. press a key to play it, another key when done\n", i, i);
					getchar();
					

					int16_t *samples = calloc(sizeof(*samples), mc->rom->melodies[i]->length);
					uint32_t j;

					for (j = 0; j < mc->rom->melodies[i]->length; j++)
						samples[j] = 64 * (int8_t)mc->rom->melodies[i]->data[j];

					SDL_QueueAudio(mc->audioDev, samples, j * sizeof(*samples));
					free(samples);

					getchar();
					SDL_ClearQueuedAudio(mc->audioDev);
				}
				else {
					fprintf(stderr, "melody %u (0x%04x) does not exist\n", i, i);
				}
			}
		}
	}
	return mc;
}

bool melodyChipIsPlaying(struct MelodyChip *mc)
{
	return mc->playState != PlaybackIdle;
}

//play up to MAX_MELODIES_AT_ONCE melodies mixed together. Longest duration is the duration of playback. Return true if anything started playing
static bool melodyChipPrvPlayMelodies(struct MelodyChip *mc, const uint32_t melodyID[static MAX_MELODIES_AT_ONCE])
{
	const int8_t *voices[MAX_MELODIES_AT_ONCE];
	uint32_t lengths[MAX_MELODIES_AT_ONCE];
	uint32_t maxLen = 0, nMelodies = 0;
	int32_t volScale = 32 * (mc->lastVolume + 1);
	int16_t *samples;

	if (DEBUG)
		fprintf(stderr, "MELODYCHIP: play %u(%xh) %u(%xh) %u(%xh) %u(%xh)\n",
				melodyID[0], melodyID[0], melodyID[1], melodyID[1],
				melodyID[2], melodyID[2], melodyID[3], melodyID[3]);

	for (uint32_t i = 0; i < MAX_MELODIES_AT_ONCE; i++) {
		if (melodyID[i] >= mc->rom->numMelodyIndices || !mc->rom->melodies[melodyID[i]] ||
			!mc->rom->melodies[melodyID[i]]->length)
			continue;
		voices[nMelodies] = (const int8_t *)mc->rom->melodies[melodyID[i]]->data;
		lengths[nMelodies] = mc->rom->melodies[melodyID[i]]->length;
		if (lengths[nMelodies] > maxLen)
			maxLen = lengths[nMelodies];
		nMelodies++;
	}
	if (!nMelodies)
		return false;

	samples = malloc((size_t)maxLen * sizeof(*samples));
	if (!samples) {
		fprintf(stderr, "MELODYCHIP: cannot allocate mixed audio\n");
		return false;
	}
	for (uint32_t i = 0; i < maxLen; i++) {
		int32_t mixed = 0;
		for (uint32_t j = 0; j < nMelodies; j++)
			if (i < lengths[j])
				mixed += volScale * voices[j][i];
		/* Keep both the accumulator and divisor signed. An unsigned divisor
		* folded negative samples into large positive spikes with 3 voices.
		* Mixing in 32 bits also prevents overflow before averaging 4 voices. */
		mixed /= (int32_t)nMelodies;
		if (mixed < INT16_MIN) mixed = INT16_MIN;
		if (mixed > INT16_MAX) mixed = INT16_MAX;
		samples[i] = (int16_t)mixed;
	}
	bool queued = !SDL_QueueAudio(mc->audioDev, samples, maxLen * sizeof(*samples));
	if (!queued)
		fprintf(stderr, "MELODYCHIP: audio queue error: %s\n", SDL_GetError());
	free(samples);
	return queued;
}

static void melodyChipPrvNormalControl(struct MelodyChip *mc, uint32_t cmd)
{
	uint32_t mel[4] = {cmd & 0x3f, -1, -1, -1};
	bool repeat = !!(cmd & 0x100);
	
	mc->lastValidCommand = cmd;
	mc->lastVolume = 3 & (cmd >> 6);

	SDL_ClearQueuedAudio(mc->audioDev);
	mc->playState = PlaybackIdle;

	if (mel[0] == MELODY_IDX_SILENT) {

		//nothing
	}
	else if (mc->chipType == MelodyChipAdvanced && mel[0] == MELODY_IDX_GO_ADVANCED) {

		mc->isInAdvancedMode = true;
		if (DEBUG)
			fprintf(stderr, "MELODYCHIP: going advanced mode\n");
	}
	else if (melodyChipPrvPlayMelodies(mc, mel)) {

		mc->playState = repeat ? PlaybackPlayingLooped : PlaybackPlayingOnce;
	}
	else {

		if (DEBUG)
			fprintf(stderr, "MELODYCHIP: no such melody %u\n", mel[0]);
	}
}

static void melodyChipPrvAdvancedControl(struct MelodyChip *mc, uint32_t cmd)
{
	SDL_ClearQueuedAudio(mc->audioDev);
	if (cmd == ADVANCED_INSTR_GO_BASIC) {

		SDL_ClearQueuedAudio(mc->audioDev);
		mc->playState = PlaybackIdle;
		mc->isInAdvancedMode = false;
		if (DEBUG)
			fprintf(stderr, "MELODYCHIP: going normal mode\n");
	}
	else {

		uint32_t mel[4] = {
			ADVANCED_INDEX_BASE + (cmd / ADVANCED_MAX_INSTRS / ADVANCED_MAX_INSTRS / ADVANCED_MAX_INSTRS) % ADVANCED_MAX_INSTRS,
			ADVANCED_INDEX_BASE + (cmd / ADVANCED_MAX_INSTRS / ADVANCED_MAX_INSTRS) % ADVANCED_MAX_INSTRS,
			ADVANCED_INDEX_BASE + (cmd / ADVANCED_MAX_INSTRS) % ADVANCED_MAX_INSTRS,
			ADVANCED_INDEX_BASE + cmd % ADVANCED_MAX_INSTRS,
		};

		mc->playState = melodyChipPrvPlayMelodies(mc, mel) ? PlaybackPlayingOnce : PlaybackIdle;
	}
}

void melodyChipPeriodic(struct MelodyChip *mc)
{
	mc->timestamp++;
	if (mc->commsState == CommsRxing && mc->timestamp - mc->lastBitTime >= MELODY_CHIP_TIMEOUT && mc->commsNumBits) {

		uint32_t numBits = mc->commsNumBits;
		uint32_t cmd = mc->commsInput & ((1 << numBits) - 1);

		if (DEBUG)
			fprintf(stderr, "MELODYCHIP: command: %u bits: 0x%08x\n", mc->commsNumBits, cmd);
		mc->commsNumBits = 0;
		mc->commsState = CommsIdle;
		
		if (numBits == 9) {	//normal melody chip
			
			if (mc->isInAdvancedMode)
				fprintf(stderr, "MELODYCHIP: refusing normal command in advanced mode\n");
			else
				melodyChipPrvNormalControl(mc, cmd);
		}
		else if (numBits == 24) {	//fancy chip for music composer
			
			if (!mc->isInAdvancedMode)
				fprintf(stderr, "MELODYCHIP: refusing advanced command in normal mode\n");
			else
				melodyChipPrvAdvancedControl(mc, cmd);
		}
		else if (numBits) {

			fprintf(stderr, "MELODYCHIP: not sure how to interpret a melody chip command 0x%08x with %u bits, ignoring\n", cmd, numBits);
		}
	}

	if (mc->playState != PlaybackIdle && !SDL_GetQueuedAudioSize(mc->audioDev)) {			//we were playing and i =t just ended...decide what to do

		
		if (mc->playState == PlaybackPlayingOnce) {

			mc->playState = PlaybackIdle;
		}
		else if (mc->playState == PlaybackPlayingLooped) {

			melodyChipPrvNormalControl(mc, mc->lastValidCommand);
		}
	}
}

void melodyChipControlGpioStateChange(struct MelodyChip *mc, bool dataHi, bool clockHi)
{
	bool startSeen;

	//if nothing changed, do nothing
	if (!mc->prevClock == !clockHi && !mc->prevData == !dataHi)
		return;

	startSeen = mc->lastTransitionWasStartOfStart && !mc->prevClock && !clockHi && mc->prevData && !dataHi;		//start is if the last edge we saw was data going up, and this edge is data going wodn while clock is low
	mc->lastTransitionWasStartOfStart = !mc->prevClock && !clockHi && !mc->prevData && dataHi;					//maybe this is the start of a start?

	if (startSeen) {	//start pulse
		
		mc->commsNumBits = 0;
		mc->commsInput = 0;
		mc->lastBitTime = mc->timestamp;
		mc->commsState = CommsRxing;

		if (DEBUG)
			fprintf(stderr, "MELODY START\n");
	}
	
	//data input - capture on rising edge
	if (mc->commsState == CommsRxing && !mc->prevClock && !mc->prevData && clockHi) {
		
		if (DEBUG)
			fprintf(stderr, "MELODY BIT %u\n", !!dataHi);

		mc->commsInput <<= 1;
		if (dataHi)
			mc->commsInput++;
		mc->commsNumBits++;

		mc->lastBitTime = mc->timestamp;
	}

	mc->prevClock = clockHi;
	mc->prevData = dataHi;
}








