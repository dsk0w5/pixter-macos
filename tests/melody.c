#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_QueueAudio captureAudio
#define SDL_ClearQueuedAudio clearAudio
#ifdef CHECK_COLOR
#ifndef MELODY_SOURCE
#define MELODY_SOURCE "../uARMpixter/pixterMelodyChip.c"
#endif
#define TEST_GAIN 128
#define TEST_MODEL "Color"
#else
#ifndef MELODY_SOURCE
#define MELODY_SOURCE "../uPixter/melodyChip.c"
#endif
#define TEST_GAIN 64
#define TEST_MODEL "Classic"
#endif
#include MELODY_SOURCE
#undef SDL_QueueAudio
#undef SDL_ClearQueuedAudio

static int16_t output[128];
static size_t outputCount;

static void require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "%s: FAIL: %s\n", TEST_MODEL, message);
		exit(1);
	}
}

int captureAudio(SDL_AudioDeviceID device, const void *data, Uint32 length)
{
	(void)device;
	require(length <= sizeof(output) - outputCount * sizeof(*output), "audio queue capacity");
	memcpy(output + outputCount, data, length);
	outputCount += length / sizeof(*output);
	return 0;
}

void clearAudio(SDL_AudioDeviceID device)
{
	(void)device;
	outputCount = 0;
}

int main(void)
{
#ifdef CHECK_COLOR
	struct PixterRomFile *rom = calloc(1, sizeof(*rom) + 0x140 * sizeof(*rom->melodies));
#else
	struct Rom *rom = calloc(1, sizeof(*rom) + 0x140 * sizeof(*rom->melodies));
#endif
	const int8_t waveform[] = {-128, -127, -64, -1, 0, 1, 64, 127};
	struct MelodyChip chip = {0};
	require(rom != NULL, "allocate synthetic ROM");
	rom->numMelodyIndices = 0x140;
	for (unsigned i = 0; i < 4; i++) {
		rom->melodies[0x101 + i] = malloc(sizeof(*rom->melodies[0]) + sizeof(waveform));
		require(rom->melodies[0x101 + i] != NULL, "allocate synthetic voice");
		rom->melodies[0x101 + i]->length = sizeof(waveform);
		memcpy(rom->melodies[0x101 + i]->data, waveform, sizeof(waveform));
	}
	chip.rom = rom;
	chip.lastVolume = 3;

	/* Averaging identical voices must preserve the signed waveform. */
	for (unsigned voices = 1; voices <= 4; voices++) {
		uint32_t ids[4] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
		for (unsigned i = 0; i < voices; i++) ids[i] = 0x101 + i;
		outputCount = 0;
		require(melodyChipPrvPlayMelodies(&chip, ids), "start mixed audio");
		require(outputCount == sizeof(waveform), "mixed duration");
		for (unsigned i = 0; i < sizeof(waveform); i++)
			require(output[i] == waveform[i] * TEST_GAIN, "signed average with one to four voices");
	}

	/* Replacement commands must leave only the newly requested clip queued. */
	melodyChipPrvAdvancedControl(&chip, 0x0410c4);
	require(outputCount == sizeof(waveform), "replace previous clip");
	require(chip.playState == PlaybackPlayingOnce, "replacement playback state");
	melodyChipPrvAdvancedControl(&chip, ADVANCED_INSTR_GO_BASIC);
	require(outputCount == 0 && chip.playState == PlaybackIdle && !chip.isInAdvancedMode,
		"stop and return to basic mode");

	/* Empty and missing recordings do not dilute the mix; short voices end
	 * without shortening the other voice or changing the averaging divisor. */
	const int8_t first[] = {-12, 12, -6, 6}, second[] = {6, -6};
	const int16_t expected[] = {-3 * TEST_GAIN, 3 * TEST_GAIN, -3 * TEST_GAIN, 3 * TEST_GAIN};
	rom->melodies[0x101]->length = sizeof(first);
	memcpy(rom->melodies[0x101]->data, first, sizeof(first));
	rom->melodies[0x102]->length = sizeof(second);
	memcpy(rom->melodies[0x102]->data, second, sizeof(second));
	rom->melodies[0x103]->length = 0;
	const uint32_t uneven[4] = {0x101, 0x102, 0x103, UINT32_MAX};
	require(melodyChipPrvPlayMelodies(&chip, uneven), "play uneven voices");
	require(outputCount == 4 && !memcmp(output, expected, sizeof(expected)), "uneven voice samples");
	const uint32_t missing[4] = {0x130, 0x103, UINT32_MAX, UINT32_MAX};
	outputCount = 0;
	require(!melodyChipPrvPlayMelodies(&chip, missing) && outputCount == 0, "missing voices stay silent");

	for (unsigned i = 0; i < 4; i++) free(rom->melodies[0x101 + i]);
	free(rom);
	printf("%s melody: signed mixing, replacement, uneven and missing voices PASS\n", TEST_MODEL);
	return 0;
}
