#include "pixterMelodyChipXL.h"
#include <stdlib.h>


#define DEBUG	 					1

#define XL_INDEX_BASE				0x200			//in rom, XL melody chips will have indices this high or more
#define XL_NUM_VOICES				8
#define XL_INSTRUMENT_MULTIPLE		64
#define XL_SAMPLERATE				22050

#define NOTE_CONTINUE_LEN			15				//how many ticks the note will play if no further request is given

enum CommsState {
	CommsIdle,
	CommsRxing,
};

struct Voice {
	uint8_t noteLengthRequested, noteLengthPlayed, instrument, curNote, nextNoteLength;
	int8_t nextNote;					//negative if not yet requested
};

struct MelodyChipXL {
	const struct PixterRomFile *rom;
	SDL_AudioDeviceID audioDev;

	uint16_t commsInputDDR[8];		//when fully RXed, [0] has the first 9 bits RXed in LSBs, [1] has the next, etc
	uint8_t commsNumBitsDDR;
	bool prevClock, prevData, lastTransitionWasStartOfStart, inMelodyMode, midiPF6state;

	enum CommsState commsState;

	uint8_t ticksPerTimeUnit, ticksElapsed;
	int8_t curMelody;				//for looping melody mode, negative for none
	struct Voice voice[XL_NUM_VOICES];
};

static void melodyChipXLprvSetSpeed(struct MelodyChipXL *mc, uint8_t speedSetting)
{
	static const uint8_t ticksPerTimeUnitAtSpeed[] = {16, 12, 11, 10, 9, 8, 12, };	//others not used by the game

	if (speedSetting >= sizeof(ticksPerTimeUnitAtSpeed))
		speedSetting = 0;

	mc->ticksPerTimeUnit = ticksPerTimeUnitAtSpeed[speedSetting];
	mc->ticksElapsed = 0;
}

bool melodyChipXLisXLromFile(const struct PixterRomFile *rom)
{
	bool advancedIndicesSeen = false, melodyPresentAtAdvancedCmd = false, invalidAdvancedIndexSeen = false, xlIndicesSeen = false;;
	unsigned i;

	if (!rom)
		return false;

	for (i = 0; i < rom->numMelodyIndices; i++) {

		if (!rom->melodies[i] || !rom->melodies[i]->length)
			continue;

		if (i >= XL_INDEX_BASE)
			return true;
	}

	return false;
}

static void melodyChipXLprvResetAllVoices(struct MelodyChipXL *mc, bool alsoResetInstrumentID)
{
	unsigned i;

	for (i = 0; i < XL_NUM_VOICES; i++) {		//init each voice to be doing nothing
		if (alsoResetInstrumentID)
			mc->voice[i].instrument = 0;
		mc->voice[i].noteLengthRequested = 0;
		mc->voice[i].noteLengthPlayed = NOTE_CONTINUE_LEN;
		mc->voice[i].curNote = 0;
		mc->voice[i].nextNote = -1;
	}
}

struct MelodyChipXL* melodyChipXLinit(const struct PixterRomFile *rom, SDL_AudioDeviceID audioDev)
{
	struct MelodyChipXL *mc;

	if (!rom || !melodyChipXLisXLromFile(rom) || !(mc = malloc(sizeof(struct MelodyChipXL))))
		return NULL;

	mc->rom = rom;
	mc->audioDev = audioDev;
	mc->commsState = CommsIdle;
	mc->prevClock = false;
	mc->prevData = false;
	mc->lastTransitionWasStartOfStart = false;
	mc->inMelodyMode = true;
	mc->curMelody = -1;
	melodyChipXLprvResetAllVoices(mc, true);
	melodyChipXLprvSetSpeed(mc, 0);

	return mc;
}

static bool melodyChipXLprvMidiModeIsOutputtingSound(struct MelodyChipXL *mc)					//including post-note sustain of 15 TUs. does not work in melody mode
{
	unsigned i;

	for (i = 0; i < XL_NUM_VOICES; i++) {		//init each voice to be doing nothing
		
		if (mc->voice[i].noteLengthRequested + NOTE_CONTINUE_LEN > mc->voice[i].noteLengthPlayed)
			return true;
	}
	return false;
}

static bool melodyChipXLprvMidiModeIsOutputtingScheduledNotes(struct MelodyChipXL *mc)			//are we only outputting notes that were scheduled properly (will not reutnr true during sustain caused by not getting commands)
{
	unsigned i;

	for (i = 0; i < XL_NUM_VOICES; i++) {		//init each voice to be doing nothing
		
		if (mc->voice[i].noteLengthRequested > mc->voice[i].noteLengthPlayed)
			return true;
	}
	return false;
}

static uint32_t melodyChipXLprvTimeUnitLengthInSamples(struct MelodyChipXL *mc)
{
	return (uint64_t)XL_SAMPLERATE * mc->ticksPerTimeUnit * MELODY_XL_PERIODIC_RATE_NSEC / 1000000000;
}

bool melodyChipXLisPlaying(struct MelodyChipXL *mc)			//this is the PF6 pin and it has different uses in MIDI mode
{
	if (mc->inMelodyMode && mc->curMelody >= 0)				//melody mode in looping mode - always active
		return true;
	else if (mc->inMelodyMode)								//melody mode in one-time-playback mode - active as long as there is data to play
		return !!SDL_GetQueuedAudioSize(mc->audioDev);
	else if (melodyChipXLprvMidiModeIsOutputtingSound(mc))	//internal state as long as audio is being produced (it will stop toggling once scheduled notes end)
		return mc->midiPF6state;
	else
		return false;
}

static bool melodyChipXLprvQueueSounds(struct MelodyChipXL *mc, const int32_t sounds[static XL_NUM_VOICES], const uint32_t offsets[static XL_NUM_VOICES], uint32_t vol, uint32_t nSamp)
{
	int16_t *samples, volScale = 16 * vol;
	uint32_t i, j, maxLen = 0;

	//calculate the max length we could generate
	for (i = 0; i < XL_NUM_VOICES; i++) {
		
		int32_t melID = sounds[i];
		uint32_t lengthAvail = melID >= 0 && (uint32_t)melID < mc->rom->numMelodyIndices && mc->rom->melodies[melID] ? mc->rom->melodies[melID]->length : 0;
		
		if (lengthAvail <= offsets[i])
			continue;

		lengthAvail -= offsets[i];

		if (maxLen < lengthAvail)
			maxLen = lengthAvail;
	}

	if (nSamp > maxLen)
		nSamp = maxLen;

	
	if (DEBUG >= 1) {

		fprintf(stderr, "MELODY sounds:");

		for (i = 0; i < XL_NUM_VOICES; i++) {
		
			int32_t melID = sounds[i];
			uint32_t lengthAvail = melID >= 0 && (uint32_t)melID < mc->rom->numMelodyIndices && mc->rom->melodies[melID] ? mc->rom->melodies[melID]->length : 0;
			if (melID < 0)
				continue;

			fprintf(stderr, " {0x%04x @ %5u%s}", melID, offsets[i], lengthAvail > offsets[i] ? "" : " PAST END");
		}
		fprintf(stderr, "\n");
	}

	if (!nSamp)
		return false;

	samples = calloc(sizeof(int16_t), nSamp);

	for (i = 0; i < XL_NUM_VOICES; i++) {
		int32_t melID = sounds[i];
		uint32_t lengthAvail = melID >= 0 && (uint32_t)melID < mc->rom->numMelodyIndices && mc->rom->melodies[melID] ? mc->rom->melodies[melID]->length : 0;
		const uint8_t *src = lengthAvail ? mc->rom->melodies[melID]->data : NULL;

		if (lengthAvail <= offsets[i])
			continue;

		src += offsets[i];
		lengthAvail -= offsets[i];

		if (lengthAvail > nSamp)
			lengthAvail = nSamp;

		for (j = 0; j < lengthAvail; j++)
			samples[j] += volScale * (int8_t)*src++;
	}

	//play and free
	SDL_QueueAudio(mc->audioDev, samples, nSamp * sizeof(*samples));
	free(samples);

	return true;
}

static bool melodyChipXLprvGenerateOneTimeUnitOfSound(struct MelodyChipXL *mc)	//return true if any voice is outputting anything
{
	uint32_t offsets[XL_NUM_VOICES] = {0, }, tuLength = melodyChipXLprvTimeUnitLengthInSamples(mc);
	int32_t sounds[XL_NUM_VOICES];
	uint_fast8_t i;

	for (i = 0; i < XL_NUM_VOICES; i++) {

		if (mc->voice[i].noteLengthPlayed >= mc->voice[i].noteLengthRequested) {
			if (mc->voice[i].nextNote >= 0) {
				mc->voice[i].noteLengthPlayed = 0;
				mc->voice[i].curNote = mc->voice[i].nextNote;
				mc->voice[i].noteLengthRequested = mc->voice[i].nextNoteLength;
				mc->voice[i].nextNote = -1;
			}
			else {
				sounds[i] = -1;
				continue;
			}
		}
		if (mc->voice[i].noteLengthPlayed < mc->voice[i].noteLengthRequested + NOTE_CONTINUE_LEN) {

			sounds[i] = XL_INDEX_BASE + XL_INSTRUMENT_MULTIPLE * mc->voice[i].instrument + mc->voice[i].curNote;
			offsets[i] = tuLength * mc->voice[i].noteLengthPlayed;
			mc->voice[i].noteLengthPlayed++;
		}
		else {

			sounds[i] = -1;
		}
	}
	return melodyChipXLprvQueueSounds(mc, sounds, offsets, 1, tuLength);
}

//return true on success
static bool melodyChipXLprvQueueSingleMelody(struct MelodyChipXL *mc, uint_fast8_t melodyID)
{
	int32_t sounds[XL_NUM_VOICES] = {melodyID, -1, -1, -1, -1, -1, -1, -1, };
	uint32_t offsets[XL_NUM_VOICES] = {0, };

	return melodyChipXLprvQueueSounds(mc, sounds, offsets, 8, 0xffffffff);
}

static void melodyChipXLprvMidiModeTimeUnitHappened(struct MelodyChipXL *mc)
{
	mc->midiPF6state = melodyChipXLprvGenerateOneTimeUnitOfSound(mc) && !mc->midiPF6state;
}

void melodyChipXLperiodic(struct MelodyChipXL *mc)
{
	if (mc->inMelodyMode && mc->curMelody >= 0 && !SDL_GetQueuedAudioSize(mc->audioDev)) {		//check for looping mode and restart if needed
		
		(void)melodyChipXLprvQueueSingleMelody(mc, mc->curMelody);	//if we get this far, this cannot fail
	}
	else if (!mc->inMelodyMode && ++mc->ticksElapsed == mc->ticksPerTimeUnit) {
		mc->ticksElapsed = 0;
		melodyChipXLprvMidiModeTimeUnitHappened(mc);
	}
}

//to the best of my abilities ot discern:
// if the channel is playing a scheduled note and this is NOT the last time unit of that playback, this enqueue is ignored
// if the channel is playing a scheduled note and this IS the last time unit of that playback, this note is enqueued
// if the channel has finished playing previously scheduled notes (but may be sustaining them), playback starts immediately
static void melodyChipXLprvQueueNote(struct MelodyChipXL *mc, struct Voice *voice, uint_fast8_t note, uint_fast8_t length)
{
	if (voice->noteLengthPlayed >= voice->noteLengthRequested) {

		voice->curNote = note;
		voice->noteLengthPlayed = 0;
		voice->noteLengthRequested = length;
		voice->nextNote = -1;
	}
	else if (voice->noteLengthPlayed == voice->noteLengthRequested - 1) {

		voice->nextNote = note;
		voice->nextNoteLength = length;
	}
}

static void melodyChipXLprvControl(struct MelodyChipXL *mc)
{
	uint_fast8_t cmd = mc->commsInputDDR[0] >> 3, flags = mc->commsInputDDR[0] & 7;
	unsigned i;

	if (DEBUG >= 1)
		fprintf(stderr, "MELODY CMD %03x %03x %03x %03x %03x %03x %03x %03x\n",
				mc->commsInputDDR[0], mc->commsInputDDR[1], mc->commsInputDDR[2], mc->commsInputDDR[3],
				mc->commsInputDDR[4], mc->commsInputDDR[5], mc->commsInputDDR[6], mc->commsInputDDR[7]);

	if (cmd == 0x3f) {		//mode change
		
		bool goToMidiMode = !!flags;

		if (!mc->inMelodyMode == !goToMidiMode) {	//midi -> midi transition is a no-op, melody -> melody transition is a no-op
			
			SDL_ClearQueuedAudio(mc->audioDev);
			mc->inMelodyMode = !goToMidiMode;
			mc->midiPF6state = false;
		}
	}
	else if (mc->inMelodyMode) {

		if (flags == 7) {		//stop

			SDL_ClearQueuedAudio(mc->audioDev);
			mc->curMelody = -1;
		}
		mc->curMelody = (melodyChipXLprvQueueSingleMelody(mc, cmd) && flags == 1) ? cmd : -1;
	}
	else if (flags == 7) {		//midi speed change
		
		melodyChipXLprvSetSpeed(mc, flags);
	}
	else if (flags == 6) {		//midi instrument change
		
		for (i = 0; i < XL_NUM_VOICES; i++) {
			if (DEBUG >= 1)
				fprintf(stderr, "VOICE %u = instrument %u\n", i, mc->commsInputDDR[i] >> 3);
			mc->voice[i].instrument = mc->commsInputDDR[i] >> 3;
		}
	}
	else if (flags == 5) {		//midi stop playback immediately
		
		SDL_ClearQueuedAudio(mc->audioDev);
		melodyChipXLprvResetAllVoices(mc, false);
	}
	else {						//midi note(s)

		bool newStart = !melodyChipXLprvMidiModeIsOutputtingScheduledNotes(mc);

		for (i = 0; i < XL_NUM_VOICES; i++)
			melodyChipXLprvQueueNote(mc, &mc->voice[i], mc->commsInputDDR[i] >> 3, 1 << (mc->commsInputDDR[i] & 7));

		if (newStart) {
			mc->ticksElapsed = 0;
			mc->midiPF6state = true;
			SDL_ClearQueuedAudio(mc->audioDev);
			melodyChipXLprvGenerateOneTimeUnitOfSound(mc);
		}
	}
}

void melodyChipXLcontrolGpioStateChange(struct MelodyChipXL *mc, bool dataHi, bool clockHi)
{
	bool startSeen;

	//if nothing changed, do nothing
	if (!mc->prevClock == !clockHi && !mc->prevData == !dataHi)
		return;

	startSeen = mc->lastTransitionWasStartOfStart && !mc->prevClock && !clockHi && mc->prevData && !dataHi;		//start is if the last edge we saw was data going up, and this edge is data going wodn while clock is low
	mc->lastTransitionWasStartOfStart = !mc->prevClock && !clockHi && !mc->prevData && dataHi;					//maybe this is the start of a start?

	if (startSeen) {	//start pulse
		
		mc->commsNumBitsDDR = 0;
		memset(mc->commsInputDDR, 0, sizeof(mc->commsInputDDR));
		mc->commsState = CommsRxing;

		if (DEBUG >= 2)
			fprintf(stderr, "MELODY START\n");
	}
	
	//DDR input
	if (mc->commsState == CommsRxing && !mc->prevClock != !clockHi) {

		int_fast8_t i, bitIn = dataHi ? 1 : 0;
		
		for (i = sizeof(mc->commsInputDDR) / sizeof(*mc->commsInputDDR) - 1; i >= 0; i--) {
			uint_fast8_t bitOut = mc->commsInputDDR[i] >> 8;

			mc->commsInputDDR[i] = (mc->commsInputDDR[i] & 0xff) * 2 + bitIn;
			bitIn = bitOut;
		}
		mc->commsNumBitsDDR++;

		if (DEBUG >= 2)
			fprintf(stderr, "DDR bit %u (index %u)\n", dataHi, mc->commsNumBitsDDR);

		if (mc->commsNumBitsDDR == 72) {
			
			mc->commsState = CommsIdle;
			melodyChipXLprvControl(mc);
		}
	}

	mc->prevClock = clockHi;
	mc->prevData = dataHi;
}

