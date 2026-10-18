#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define main emulatorMain
#ifdef CHECK_COLOR
#define deviceTouch captureTouch
#define deviceKey captureKey
#include "../uARMpixter/main.c"
#undef deviceTouch
#undef deviceKey
#define TEST_MODEL "Color"
#define TOUCH_COORD(value) (value)
static struct SoC testSoc;
static int penX = -1, penY = -1;
static bool calibrationPressed;
void captureTouch(struct Device *device, int x, int y)
{
	(void)device;
	penX = x;
	penY = y;
}
void captureKey(struct Device *device, uint32_t key, bool down)
{
	(void)device;
	if (key == SDLK_RETURN) calibrationPressed = down;
}
static void pollInput(void) { socPrvPollEvents(&testSoc); }
#else
#ifndef CLASSIC_SOURCE
#define CLASSIC_SOURCE "../uPixter/main.c"
#endif
#include CLASSIC_SOURCE
#define TEST_MODEL "Classic"
#define TOUCH_COORD(value) ((value) / 2)
#define penX mPenX
#define penY mPenY
#define calibrationPressed mCalibBtnPressed
static void pollInput(void) { renderScreen(); }
#endif
#undef main

static void require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "%s: FAIL: %s\n", TEST_MODEL, message);
		exit(1);
	}
}

static void pushMouse(Uint32 type, Uint8 button, int x, int y)
{
	SDL_Event event = {0};
	event.type = type;
	if (type == SDL_MOUSEMOTION) {
		event.motion.x = x;
		event.motion.y = y;
	} else {
		event.button.button = button;
		event.button.x = x;
		event.button.y = y;
	}
	require(SDL_PushEvent(&event) == 1, "enqueue mouse event");
}

int main(void)
{
	require(SDL_Init(SDL_INIT_VIDEO) == 0, "initialize dummy video");
#ifndef CHECK_COLOR
	mIO[0x05] = 8;
	mIO[0x0e] = 5;
#endif
	pollInput();
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
	for (unsigned i = 0; i < 256; i++) pushMouse(SDL_MOUSEMOTION, 0, 20, 20);
	SDL_Event marker = {0};
	marker.type = SDL_KEYDOWN;
	marker.key.keysym.sym = SDLK_RETURN;
	require(SDL_PushEvent(&marker) == 1, "enqueue marker after motion backlog");
	pollInput();
	require(calibrationPressed, "process the marker in the same poll as 256 motion events");

	pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, 80, 80);
	pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, 80, 80);
	pushMouse(SDL_MOUSEMOTION, 0, 140, 120);
	pollInput();
	require(penX == TOUCH_COORD(80) && penY == TOUCH_COORD(80), "keep a complete queued tap for one frame");
	pollInput();
	require(penX == -1 && penY == -1, "release the queued tap next frame");

	pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, 80, 80);
	pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, 80, 80);
	pollInput();
	pollInput();
	require(penX == TOUCH_COORD(80), "ignore another mouse button's release");
	pushMouse(SDL_MOUSEMOTION, 0, 100, 120);
	pollInput();
	require(penX == TOUCH_COORD(100) && penY == TOUCH_COORD(120), "drag a held stylus");
	pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, 100, 120);
	pollInput();
	require(penX == -1 && penY == -1, "release a held stylus");

	pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, 80, 80);
	SDL_Event focus = {0};
	focus.type = SDL_WINDOWEVENT;
	focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
	require(SDL_PushEvent(&focus) == 1, "enqueue focus loss");
	pollInput();
	require(penX == -1 && penY == -1, "release the stylus on focus loss");
	SDL_Quit();
	printf("%s input: motion backlog, queued tap, drag, button and focus handling PASS\n", TEST_MODEL);
	return 0;
}
