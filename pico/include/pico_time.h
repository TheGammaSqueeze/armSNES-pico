/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze */
/* armSNES-pico: forced into every file (-include). newlib's <time.h> has no CLOCK_MONOTONIC or
 * clock_gettime for this target, and no sched_yield; the threaded parts read the one and call the
 * other (never on this target, where their threads do not start, but they must compile and link:
 * pico_libc.c). */
#ifndef ARMSNES_PICO_TIME_H
#define ARMSNES_PICO_TIME_H
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t clk, struct timespec *ts);
int sched_yield(void);
#ifdef __cplusplus
}
#endif
#endif
