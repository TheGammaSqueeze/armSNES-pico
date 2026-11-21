/* armSNES-pico: the Super Game Boy's Game Boy, left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze
 *
 * armSNES runs a Super Game Boy cartridge's Game Boy with Gambatte (upstream src/chips/gb.cpp), C++
 * that needs its standard library. The LK only ever hands the SNES core a cartridge from the SNES
 * folder, never a Game Boy game in a Super Game Boy, so that part is not built: these calls answer
 * as a Game Boy with no game would. A Super Game Boy cartridge itself still starts, and shows that it
 * has no game.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gb.h"

bool gbLoad(const unsigned char *rom, size_t size, const unsigned char *boot, size_t bootSize)
{
	(void) rom; (void) size; (void) boot; (void) bootSize;
	return false;
}
void gbUnload(void) { }
bool gbLoaded(void) { return false; }
void gbReset(void) { }
unsigned gbRun(unsigned dots, unsigned *samples, const unsigned **audio)
{
	(void) dots;
	if (samples) *samples = 0;
	if (audio) *audio = NULL;
	return 0;
}
unsigned char *gbSaveRam(unsigned *size) { if (size) *size = 0; return NULL; }
unsigned char *gbRtc(size_t *size) { if (size) *size = 0; return NULL; }
void gbRtcRead(void) { }
void gbRtcWrite(void) { }
size_t gbStateSize(void) { return 0; }
void gbStateSave(void *data) { (void) data; }
bool gbStateLoad(const void *data, size_t size) { (void) data; return size == 0; }
