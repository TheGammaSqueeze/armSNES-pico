/* armSNES-pico: the zip reader of armSNES's libretro port, left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze
 *
 * armSNES reads an MSU-1 game's data file and sound tracks from the zip the game came in, when the
 * frontend names one (upstream libretro/zip.c, with zlib). The LK hands the core a game's bytes with
 * no path at all, so the port never looks for those files, in a zip or beside the game: zip.c and
 * zlib are not built, and these calls answer as an archive that is not there.
 */
#include <stddef.h>

#include "zip.h"

struct ZipFile *zipOpen(const char *archive, const char *name, size_t *size)
{
	(void) archive; (void) name;
	if (size) *size = 0;
	return NULL;
}
size_t zipRead(struct ZipFile *file, size_t offset, unsigned char *to, size_t bytes)
{
	(void) file; (void) offset; (void) to; (void) bytes;
	return 0;
}
void zipClose(struct ZipFile *file) { (void) file; }
