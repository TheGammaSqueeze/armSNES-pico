/* armSNES-pico: what the C library needs from an operating system, inside the bootloader.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze
 *
 * The blob links newlib for strings, formatting, stdio and maths. This file is the rest:
 *
 *   - the heap, over the arena the LK hands to heap_init. armSNES allocates when a game loads (the
 *     cartridge and its chips' memories) and frees some of it again, and little while a game runs. A
 *     first fit, address ordered free list with coalescing over a bump top is plenty, every block 16
 *     byte aligned and handed out zeroed so a game starts the same every time. heap_mark /
 *     heap_reset give run ahead a high water mark to go back to.
 *   - one file kept in memory (pico_memfile_set), for a file the port reads with fopen; every other
 *     open fails (ENOENT), so firmware, MSU-1 tracks and patches beside a game are simply absent.
 *   - newlib's other system calls, as stubs: no console, no clock beyond the LK's wall clock.
 *   - the libretro-common archive calls, never reached: games arrive unpacked.
 *   - threads: none (the end of this file); armSNES does the work in line when it cannot start one.
 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "pico_libc.h"

/* ---- heap ------------------------------------------------------------------------------------- */

#define HEAP_ALIGN 16u

/* Every block starts with this header, 16 bytes so payloads stay aligned. size is the payload's
 * capacity; next links the free list (address ordered) and is unused while the block is in use. */
struct block {
	uint32_t size;
	uint32_t magic;
	struct block *next;
	uint32_t pad;
};

#define HDR        ((uint32_t) sizeof(struct block))
#define MAGIC_USED 0x4E45534Du
#define MAGIC_FREE 0x46524545u

static uint8_t *h_base, *h_top, *h_end;
static struct block *h_free;
static uint32_t h_peak, h_live;

void pico_heap_init(void *base, unsigned size)
{
	uintptr_t b = ((uintptr_t) base + HEAP_ALIGN - 1u) & ~(uintptr_t) (HEAP_ALIGN - 1u);
	h_base = h_top = (uint8_t *) b;
	h_end = (uint8_t *) base + size;
	if (h_end < h_base) h_end = h_base;
	h_free = NULL;
	h_peak = h_live = 0;
}

unsigned pico_heap_used(void) { return h_live; }
unsigned pico_heap_top(void) { return (unsigned) (h_top - h_base); }
unsigned pico_heap_peak(void) { return h_peak; }
unsigned pico_heap_size(void) { return (unsigned) (h_end - h_base); }

static int heap_owns(const void *p)
{
	return (const uint8_t *) p >= h_base + HDR && (const uint8_t *) p < h_top;
}

/* a free block that ends at the top goes back to the bump pointer */
static void heap_trim_top(void)
{
	struct block **pp = &h_free, **last = NULL;
	while (*pp) { last = pp; pp = &(*pp)->next; }
	if (last && (uint8_t *) *last + HDR + (*last)->size == h_top) {
		h_top = (uint8_t *) *last;
		*last = NULL;
	}
}

static struct block *heap_take(uint32_t cap)
{
	struct block **pp, *b;
	for (pp = &h_free; (b = *pp) != NULL; pp = &b->next) {
		if (b->size < cap) continue;
		if (b->size >= cap + HDR + HEAP_ALIGN) {   /* split: the tail stays free */
			struct block *t = (struct block *) ((uint8_t *) b + HDR + cap);
			t->size = b->size - cap - HDR;
			t->magic = MAGIC_FREE;
			t->next = b->next;
			*pp = t;
			b->size = cap;
		} else {
			*pp = b->next;
		}
		return b;
	}
	if ((size_t) (h_end - h_top) < (size_t) cap + HDR) return NULL;
	b = (struct block *) h_top;
	h_top += HDR + cap;
	if ((uint32_t) (h_top - h_base) > h_peak) h_peak = (uint32_t) (h_top - h_base);
	b->size = cap;
	return b;
}

void *malloc(size_t n)
{
	uint32_t cap;
	struct block *b;
	if (n > 0x7FFFFFF0u) { errno = ENOMEM; return NULL; }
	cap = ((uint32_t) n + HEAP_ALIGN - 1u) & ~(HEAP_ALIGN - 1u);
	if (!cap) cap = HEAP_ALIGN;
	b = heap_take(cap);
	if (!b) { errno = ENOMEM; return NULL; }
	b->magic = MAGIC_USED;
	b->next = NULL;
	h_live += b->size;
	memset((uint8_t *) b + HDR, 0, cap);
	return (uint8_t *) b + HDR;
}

void free(void *p)
{
	struct block *b, **pp, *prev = NULL;
	if (!p || !heap_owns(p)) return;
	b = (struct block *) ((uint8_t *) p - HDR);
	if (b->magic != MAGIC_USED) return;
	b->magic = MAGIC_FREE;
	h_live -= b->size;
	for (pp = &h_free; *pp && *pp < b; pp = &(*pp)->next) prev = *pp;
	b->next = *pp;
	*pp = b;
	if (b->next && (uint8_t *) b + HDR + b->size == (uint8_t *) b->next) {   /* merge the next */
		b->size += HDR + b->next->size;
		b->next = b->next->next;
	}
	if (prev && (uint8_t *) prev + HDR + prev->size == (uint8_t *) b) {      /* and the previous */
		prev->size += HDR + b->size;
		prev->next = b->next;
	}
	heap_trim_top();
}

void *calloc(size_t a, size_t b)
{
	if (b && a > 0x7FFFFFF0u / b) { errno = ENOMEM; return NULL; }
	return malloc(a * b);   /* malloc hands out zeroed memory */
}

void *realloc(void *p, size_t n)
{
	struct block *b;
	void *q;
	if (!p) return malloc(n);
	if (!n) { free(p); return NULL; }
	if (!heap_owns(p)) return NULL;
	b = (struct block *) ((uint8_t *) p - HDR);
	if (b->magic != MAGIC_USED) return NULL;
	if (n <= b->size) return p;
	q = malloc(n);
	if (!q) return NULL;
	memcpy(q, p, b->size);
	free(p);
	return q;
}

/* Run ahead's save and load pair: the top before the save, and back to it after the load. Free
 * blocks above the mark go with it; one that straddles it is cut at the mark. */
void *pico_heap_mark(void) { return h_top; }

void pico_heap_reset(void *mark)
{
	uint8_t *m = (uint8_t *) mark;
	struct block **pp = &h_free, *b;
	if (m < h_base || m > h_top) return;
	while ((b = *pp) != NULL) {
		if ((uint8_t *) b >= m) { *pp = NULL; break; }   /* address ordered: the rest is above too */
		if ((uint8_t *) b + HDR + b->size > m) b->size = (uint32_t) (m - (uint8_t *) b) - HDR;
		pp = &b->next;
	}
	h_top = m;
	heap_trim_top();
}

/* ---- one file in memory ------------------------------------------------------------------------ */

#define MEMFILE_FD 3

static char mf_path[96];
static const uint8_t *mf_data;
static size_t mf_size, mf_pos;
static bool mf_open;

void pico_memfile_set(const char *path, const void *data, size_t size)
{
	size_t i;
	mf_data = data;
	mf_size = data ? size : 0;
	mf_pos = 0;
	mf_open = false;
	mf_path[0] = 0;
	if (!data || !path) return;
	for (i = 0; path[i] && i < sizeof mf_path - 1u; i++) mf_path[i] = path[i];
	mf_path[i] = 0;
}

int _open(const char *path, int flags, int mode)
{
	(void) mode;
	if (mf_data && path && !strcmp(path, mf_path) && (flags & 3) == 0 /* O_RDONLY */ && !mf_open) {
		mf_open = true;
		mf_pos = 0;
		return MEMFILE_FD;
	}
	errno = ENOENT;
	return -1;
}

int _read(int fd, void *buf, size_t n)
{
	size_t left;
	if (fd != MEMFILE_FD || !mf_open) { errno = EBADF; return -1; }
	left = mf_pos < mf_size ? mf_size - mf_pos : 0;
	if (n > left) n = left;
	memcpy(buf, mf_data + mf_pos, n);
	mf_pos += n;
	return (int) n;
}

off_t _lseek(int fd, off_t off, int whence)
{
	long long at;
	if (fd != MEMFILE_FD || !mf_open) { errno = EBADF; return -1; }
	switch (whence) {
	case 0: at = off; break;                              /* SEEK_SET */
	case 1: at = (long long) mf_pos + off; break;         /* SEEK_CUR */
	case 2: at = (long long) mf_size + off; break;        /* SEEK_END */
	default: errno = EINVAL; return -1;
	}
	if (at < 0) { errno = EINVAL; return -1; }
	mf_pos = (size_t) at;
	return (off_t) at;
}

int _close(int fd)
{
	if (fd != MEMFILE_FD || !mf_open) { errno = EBADF; return -1; }
	mf_open = false;
	return 0;
}

int _fstat(int fd, struct stat *st)
{
	memset(st, 0, sizeof *st);
	if (fd == MEMFILE_FD && mf_open) {
		st->st_mode = S_IFREG | 0444;
		st->st_size = (off_t) mf_size;
		st->st_blksize = 1024;
		return 0;
	}
	st->st_mode = S_IFCHR;
	return 0;
}

/* stat on a path (armNES 1.2's port looks for folders and file times beside a game): nothing is
 * there but what _open serves, and that is not looked up by stat */
int _stat(const char *path, struct stat *st)
{
	(void)path; (void)st;
	errno = ENOENT;
	return -1;
}

int _isatty(int fd) { return fd != MEMFILE_FD; }

/* ---- the other system calls -------------------------------------------------------------------- */

/* Output goes nowhere in the LK. Under the PC harness (qemu-arm user mode) a debug build may print:
 * a write system call when the CPU is in user mode. */
int _write(int fd, const void *buf, size_t n)
{
	unsigned cpsr;
	__asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
	if ((cpsr & 0x1Fu) == 0x10u) {
		register int r0 __asm__("r0") = fd;
		register const void *r1 __asm__("r1") = buf;
		register size_t r2 __asm__("r2") = n;
		register int r7 __asm__("r7") = 4;   /* write */
		__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
		return r0;
	}
	(void) fd; (void) buf;
	return (int) n;
}

extern long pico_host_seconds(void);   /* pico_core.c: the LK's wall clock */

int _gettimeofday(struct timeval *tv, void *tz)
{
	(void) tz;
	if (tv) { tv->tv_sec = (time_t) pico_host_seconds(); tv->tv_usec = 0; }
	return 0;
}
int _kill(int pid, int sig) { (void) pid; (void) sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
void *_sbrk(ptrdiff_t incr) { (void) incr; errno = ENOMEM; return (void *) -1; }   /* malloc is ours */
void _exit(int code) { (void) code; for (;;) __asm__ volatile("wfi"); }
void abort(void) { _exit(1); }
char *getenv(const char *name) { (void) name; return NULL; }

/* newlib's reentrant entry points, so its own malloc (which would need _sbrk) is never linked */
struct _reent;
void *_malloc_r(struct _reent *r, size_t n) { (void) r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void) r; free(p); }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { (void) r; return calloc(a, b); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void) r; return realloc(p, n); }

/* ---- archives: never, games come unpacked -------------------------------------------------------- */

struct string_list;
bool path_is_compressed_file(const char *path) { (void) path; return false; }
struct string_list *file_archive_get_file_list(const char *path, const char *exts)
{ (void) path; (void) exts; return NULL; }
int file_archive_compressed_read(const char *path, void **buf, const char *optional_filename, int64_t *length)
{
	(void) path; (void) optional_filename;
	if (buf) *buf = NULL;
	if (length) *length = -1;
	return 0;
}
void string_list_free(struct string_list *list) { (void) list; }

/* ---- threads: none on this target ----------------------------------------------------------------
 * armSNES tries to start helper threads and does the work in line when it cannot (include/pthread.h);
 * the locks are only ever taken by the one thread there is. */
#include "include/pthread.h"
#include "include/pico_time.h"
#include <unistd.h>
int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg)
{ (void) t; (void) a; (void) fn; (void) arg; return EAGAIN; }
int pthread_join(pthread_t t, void **ret) { (void) t; (void) ret; return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { (void) m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { (void) m; return 0; }
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void) c; (void) m; return 0; }
int pthread_cond_signal(pthread_cond_t *c) { (void) c; return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { (void) c; return 0; }
int sched_yield(void) { return 0; }
long sysconf(int name) { return name == _SC_NPROCESSORS_ONLN ? 1 : -1; }   /* one CPU */
int clock_gettime(clockid_t clk, struct timespec *ts)
{
	struct timeval tv;
	extern int _gettimeofday(struct timeval *tv, void *tz);
	(void) clk;
	_gettimeofday(&tv, 0);
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * 1000;
	return 0;
}
