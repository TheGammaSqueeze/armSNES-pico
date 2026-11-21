/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze */
/* armSNES-pico: armSNES puts its fast line renderer, its sound processor and its Super FX and ST018
 * line work on threads of their own when it can. The LK gives the core one CPU and newlib has no
 * threads for this target: these declarations let those files compile, pthread_create fails (as on
 * a system out of threads), and each falls back to doing the work on the calling thread. */
#ifndef ARMSNES_PICO_PTHREAD_H
#define ARMSNES_PICO_PTHREAD_H
#include <stddef.h>
#include <sys/types.h>   /* newlib defines the pthread types (sys/_pthreadtypes.h) */
#ifndef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) 0xFFFFFFFF)
#endif
#ifndef PTHREAD_COND_INITIALIZER
#define PTHREAD_COND_INITIALIZER ((pthread_cond_t) 0xFFFFFFFF)
#endif
#ifdef __cplusplus
extern "C" {
#endif
int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg);
int pthread_join(pthread_t t, void **ret);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
#ifdef __cplusplus
}
#endif
#endif
