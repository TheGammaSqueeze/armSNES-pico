/* armSNES-pico: the heap and the in-memory file of pico_libc.c, for the glue in pico_core.c.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze
 */
#ifndef ARMSNES_PICO_LIBC_H
#define ARMSNES_PICO_LIBC_H

#include <stddef.h>

void     pico_heap_init(void *base, unsigned size);
unsigned pico_heap_used(void);   /* bytes in blocks handed out and not freed */
unsigned pico_heap_top(void);    /* bytes up to the bump top, free holes below it included */
unsigned pico_heap_peak(void);   /* the highest the top has been since pico_heap_init: the arena needed */
unsigned pico_heap_size(void);
void    *pico_heap_mark(void);
void     pico_heap_reset(void *mark);

/* Serve data[0, size) to newlib's fopen at exactly this path (read only, one open at a time);
 * data NULL forgets it. The bytes stay the caller's. */
void pico_memfile_set(const char *path, const void *data, size_t size);

#endif
