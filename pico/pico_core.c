/* armSNES-pico: the LK's SNES core interface (snes_core_abi.h) over armSNES's libretro port.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze
 *
 * The blob is armSNES's own libretro port (upstream libretro/libretro.c) with the frontend's side of
 * libretro played here: the environment, picture, sound and input callbacks are installed at init,
 * and each run() is one retro_run(). It fills the same table as the snes9x blob, so the LK's SNES
 * runner drives either one; what differs is met here:
 *
 *   - One thread. armSNES's Fast mode puts its line renderer and its sound processor on threads of
 *     their own when it can make them; the LK gives a core one CPU, so the Fast Mode Threads option
 *     is held at Off (and pthread_create fails anyway, pico_libc.c), and both run in line.
 *   - The LK's own picture and sound rows speak snes9x's option keys (snes9x_aspect,
 *     snes9x_overscan, snes9x_audio_interpolation, snes9x_hires_blend). set_option takes them and
 *     sets armSNES's nearest ones; armSNES's own keys from the Core Settings page go straight
 *     through. Before the LK sets any, the options stand where the snes9x rows begin (4:3, the
 *     overscan lines hidden, linear interpolation, no hi-res blending).
 *   - Options otherwise as in armNES-pico: a table GET_VARIABLE answers from, GET_VARIABLE_UPDATE
 *     reporting a change once, the port's version 2 definitions kept for the Core Settings page.
 *   - Run ahead's quick snapshot (state_save_ra) is snes9x's own: here it says "unsupported" and the
 *     LK uses the ordinary state calls, which armSNES keeps small and fast.
 *   - Frames the LK will not show: GET_AUDIO_VIDEO_ENABLE without the video bit, and GET_CAN_DUPE,
 *     so the port skips the picture and hands back none. Skipped sound is dropped here.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "libretro.h"
#include "snes.h"
#include "snes_core_abi.h"
#include "pico_libc.h"

/* ---- libretro.c ---- */
void     retro_set_environment(retro_environment_t);
void     retro_set_video_refresh(retro_video_refresh_t);
void     retro_set_audio_sample(retro_audio_sample_t);
void     retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void     retro_set_input_poll(retro_input_poll_t);
void     retro_set_input_state(retro_input_state_t);
void     retro_init(void);
void     retro_deinit(void);
bool     retro_load_game(const struct retro_game_info *);
void     retro_unload_game(void);
void     retro_run(void);
void     retro_reset(void);
void     retro_get_system_av_info(struct retro_system_av_info *);
void    *retro_get_memory_data(unsigned);
size_t   retro_get_memory_size(unsigned);
size_t   retro_serialize_size(void);
bool     retro_serialize(void *, size_t);
bool     retro_unserialize(const void *, size_t);

/* The system folder the port is told about: nothing is in it, so a game that needs a chip's
 * firmware the dump does not carry (DSP-1 to DSP-4, ST010, ST011, Cx4, ST018) does not start. */
#define PICO_SYSTEM_DIR "/pico"

static const struct snes_core_imports *g_imp;
static unsigned (*g_pad_reader)(unsigned port);

long pico_host_seconds(void) { return g_imp && g_imp->host_time ? g_imp->host_time() : 0; }

/* ---- options ---- */

#define OPT_MAX 96
static struct { char key[40]; char value[40]; } g_opt[OPT_MAX];
static unsigned g_opt_count;
static bool g_opt_changed;
static const struct retro_core_options_v2 *g_defs;

static void copy_str(char *d, const char *s, size_t cap)
{
	size_t i = 0;
	for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
	d[i] = 0;
}

/* whether value is one of key's values in the port's list (true while there is no list yet) */
static bool option_valid(const char *key, const char *value)
{
	const struct retro_core_option_v2_definition *d;
	if (!g_defs || !g_defs->definitions) return true;
	for (d = g_defs->definitions; d->key; d++) {
		unsigned k;
		if (strcmp(d->key, key)) continue;
		for (k = 0; k < RETRO_NUM_CORE_OPTION_VALUES_MAX && d->values[k].value; k++)
			if (!strcmp(d->values[k].value, value)) return true;
		return false;
	}
	return false;
}

static int option_put(const char *key, const char *value)
{
	unsigned i;
	if (!key || !value || strlen(key) >= sizeof g_opt[0].key || strlen(value) >= sizeof g_opt[0].value)
		return -1;
	if (!option_valid(key, value)) return -1;
	for (i = 0; i < g_opt_count; i++) {
		if (!strcmp(g_opt[i].key, key)) {
			if (strcmp(g_opt[i].value, value)) {
				copy_str(g_opt[i].value, value, sizeof g_opt[i].value);
				g_opt_changed = true;
			}
			return 0;
		}
	}
	if (g_opt_count >= OPT_MAX) return -1;
	copy_str(g_opt[g_opt_count].key, key, sizeof g_opt[0].key);
	copy_str(g_opt[g_opt_count].value, value, sizeof g_opt[0].value);
	g_opt_count++;
	g_opt_changed = true;
	return 0;
}

/* the LK's snes9x rows, as armSNES's options: {snes9x key, its value, armSNES key, value, and a
 * second armSNES key and value or 0} */
static const struct { const char *key, *value, *k1, *v1, *k2, *v2; } k_from_snes9x[] = {
	{ "snes9x_aspect", "4:3", "armsnes_aspect", "4:3", 0, 0 },
	{ "snes9x_aspect", "uncorrected", "armsnes_aspect", "1:1", 0, 0 },
	{ "snes9x_aspect", "ntsc", "armsnes_aspect", "8:7 PAR", 0, 0 },
	{ "snes9x_aspect", "pal", "armsnes_aspect", "8:7 PAR", 0, 0 },
	/* snes9x crops the 239 line picture to 224 ("enabled"), then 12 or 16 lines more in all */
	{ "snes9x_overscan", "enabled", "armsnes_overscan", "Hidden", "armsnes_crop_vertical", "0" },
	{ "snes9x_overscan", "12_pixels", "armsnes_overscan", "Hidden", "armsnes_crop_vertical", "4" },
	{ "snes9x_overscan", "16_pixels", "armsnes_overscan", "Hidden", "armsnes_crop_vertical", "8" },
	{ "snes9x_overscan", "disabled", "armsnes_overscan", "Shown", "armsnes_crop_vertical", "0" },
	{ "snes9x_audio_interpolation", "linear", "armsnes_interpolation", "Linear", 0, 0 },
	{ "snes9x_audio_interpolation", "gaussian", "armsnes_interpolation", "Gaussian", 0, 0 },
	{ "snes9x_audio_interpolation", "cubic", "armsnes_interpolation", "Cubic", 0, 0 },
	{ "snes9x_audio_interpolation", "sinc", "armsnes_interpolation", "Gaussian", 0, 0 },   /* armSNES has no sinc */
	{ "snes9x_audio_interpolation", "none", "armsnes_interpolation", "None", 0, 0 },
	{ "snes9x_hires_blend", "disabled", "armsnes_hires_blend", "Off", 0, 0 },
	{ "snes9x_hires_blend", "merge", "armsnes_hires_blend", "Merge", 0, 0 },
	{ "snes9x_hires_blend", "blur", "armsnes_hires_blend", "On", 0, 0 },
};

static int snes_set_option(const char *key, const char *value)
{
	unsigned i;
	if (!key || !value) return -1;
	if (!strncmp(key, "snes9x_", 7)) {
		for (i = 0; i < sizeof k_from_snes9x / sizeof k_from_snes9x[0]; i++) {
			if (strcmp(k_from_snes9x[i].key, key) || strcmp(k_from_snes9x[i].value, value)) continue;
			if (option_put(k_from_snes9x[i].k1, k_from_snes9x[i].v1) != 0) return -1;
			if (k_from_snes9x[i].k2 && option_put(k_from_snes9x[i].k2, k_from_snes9x[i].v2) != 0) return -1;
			return 0;
		}
		return -1;
	}
	return option_put(key, value);
}

/* where the options stand before the LK sets any: the snes9x rows' first values, and one thread */
static void options_start(void)
{
	g_opt_count = 0;
	snes_set_option("snes9x_aspect", "4:3");
	snes_set_option("snes9x_overscan", "enabled");
	snes_set_option("snes9x_audio_interpolation", "linear");
	snes_set_option("snes9x_hires_blend", "disabled");
	option_put("armsnes_threads", "Off");
}

/* ---- the core's own options, for the LK's Core Settings page ----
 * The definitions as struct pico_core_option (pointers into the port's constant tables), less the
 * options the LK sets itself and the ones these consoles cannot use. */
#define COPT_MAX   96
#define COPT_VALS  2048
static struct pico_core_option g_copt[COPT_MAX];
static const char *g_copt_val[COPT_VALS], *g_copt_lab[COPT_VALS];
static int g_copt_n, g_copt_vn;

static bool starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }
static bool copt_hidden(const char *key)
{
	return !strcmp(key, "armsnes_aspect") || !strcmp(key, "armsnes_overscan") ||		/* the LK's own rows */
	       !strcmp(key, "armsnes_crop_vertical") || !strcmp(key, "armsnes_interpolation") ||
	       !strcmp(key, "armsnes_hires_blend") ||
	       !strcmp(key, "armsnes_threads") ||						/* one CPU */
	       !strcmp(key, "armsnes_turbo_buttons") || !strcmp(key, "armsnes_turbo_period") ||	/* the LK's turbo */
	       !strcmp(key, "armsnes_left_stick_dpad") || !strcmp(key, "armsnes_stick_invert") ||	/* the LK maps the pad */
	       starts(key, "armsnes_port") || !strcmp(key, "armsnes_gun_aim") ||		/* no mouse, gun or keypad */
	       !strcmp(key, "armsnes_aim_sensitivity") || !strcmp(key, "armsnes_crosshair") ||
	       starts(key, "armsnes_motion_") || !strcmp(key, "armsnes_rumble") ||
	       !strcmp(key, "armsnes_sgb_border") || !strcmp(key, "armsnes_volume_gameboy") ||	/* no Super Game Boy */
	       !strcmp(key, "armsnes_channel_gameboy");
}

static void copt_take(const struct retro_core_options_v2 *o)
{
	const struct retro_core_option_v2_definition *d;
	g_defs = o;
	g_copt_n = g_copt_vn = 0;
	if (!o || !o->definitions) return;
	for (d = o->definitions; d->key && g_copt_n < COPT_MAX; d++) {
		struct pico_core_option *e = &g_copt[g_copt_n];
		int nv = 0, k;
		if (copt_hidden(d->key)) continue;
		while (nv < RETRO_NUM_CORE_OPTION_VALUES_MAX && d->values[nv].value) nv++;
		if (!nv || g_copt_vn + nv > COPT_VALS) continue;
		e->key = d->key;
		e->label = d->desc ? d->desc : d->key;
		e->category = 0;
		if (d->category_key && o->categories) {
			const struct retro_core_option_v2_category *c;
			for (c = o->categories; c->key; c++)
				if (!strcmp(c->key, d->category_key)) { e->category = c->desc; break; }
		}
		e->values = &g_copt_val[g_copt_vn];
		e->labels = &g_copt_lab[g_copt_vn];
		e->nvalues = nv;
		e->def = 0;
		for (k = 0; k < nv; k++) {
			g_copt_val[g_copt_vn + k] = d->values[k].value;
			g_copt_lab[g_copt_vn + k] = d->values[k].label ? d->values[k].label : d->values[k].value;
			if (d->default_value && !strcmp(d->values[k].value, d->default_value)) e->def = k;
		}
		g_copt_vn += nv;
		g_copt_n++;
	}
}
static int snes_opt_count(void) { return g_copt_n; }
static const struct pico_core_option *snes_opt_info(int i) { return i >= 0 && i < g_copt_n ? &g_copt[i] : NULL; }

/* ---- the frame being run ---- */

#define AUDIO_MAX 4096u   /* stereo frames; one frame is about 800 at 48 kHz */
static int16_t g_audio[AUDIO_MAX * 2];
static unsigned g_audio_frames;
static const void *g_video;
static unsigned g_video_w, g_video_h, g_video_pitch;
static bool g_skip_video, g_skip_audio;
static unsigned g_frames_run;

/* ---- libretro callbacks ---- */

#define MEM_DESCS 32
static struct retro_memory_descriptor g_mem_desc[MEM_DESCS];
static unsigned g_mem_ndesc;

static bool env_cb(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return data && *(const enum retro_pixel_format *) data == RETRO_PIXEL_FORMAT_RGB565;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		if (!data) return false;
		*(const char **) data = PICO_SYSTEM_DIR;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		if (!data) return false;
		*(unsigned *) data = 2;		/* the definitions, with their value names and defaults */
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
		copt_take((const struct retro_core_options_v2 *) data);
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = (struct retro_variable *) data;
		unsigned i;
		if (!v || !v->key) return false;
		v->value = NULL;
		for (i = 0; i < g_opt_count; i++) {
			if (!strcmp(g_opt[i].key, v->key)) { v->value = g_opt[i].value; return true; }
		}
		return false;   /* not set: the port's default */
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		if (!data) return false;
		*(bool *) data = g_opt_changed;
		g_opt_changed = false;
		return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
		if (!data) return false;
		*(int *) data = (g_skip_video ? 0 : RETRO_AV_ENABLE_VIDEO) | (g_skip_audio ? 0 : RETRO_AV_ENABLE_AUDIO);
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		if (!data) return false;
		*(bool *) data = true;
		return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
	case RETRO_ENVIRONMENT_SET_MESSAGE:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
	case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
		return true;    /* noted; the LK reads geometry and timing back through av_info */
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
		const struct retro_memory_map *m = (const struct retro_memory_map *) data;
		g_mem_ndesc = 0;
		if (m && m->descriptors)
			for (; g_mem_ndesc < m->num_descriptors && g_mem_ndesc < MEM_DESCS; g_mem_ndesc++)
				g_mem_desc[g_mem_ndesc] = m->descriptors[g_mem_ndesc];
		return true;
	}
	default:
		return false;   /* everything else declined: sensors, rumble, fast forward, logs */
	}
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
	g_video = data;
	g_video_w = width;
	g_video_h = height;
	g_video_pitch = (unsigned) pitch;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
	size_t room, n;
	if (g_skip_audio) return frames;
	room = AUDIO_MAX - g_audio_frames;
	n = frames < room ? frames : room;
	memcpy(g_audio + g_audio_frames * 2u, data, n * 2u * sizeof(int16_t));
	g_audio_frames += (unsigned) n;
	return frames;
}

static void audio_sample_cb(int16_t left, int16_t right)
{
	int16_t pair[2] = { left, right };
	audio_batch_cb(pair, 1);
}

static void input_poll_cb(void) { }

/* a port's buttons: the netplay reader's for both ports when the LK set one, else the console's own
 * pad on port 0 */
static unsigned pad(unsigned port)
{
	if (g_pad_reader) return g_pad_reader(port);
	if (port || !g_imp || !g_imp->read_buttons) return 0;
	return g_imp->read_buttons();
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
	unsigned buttons;
	(void) index;
	if ((device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD || port > 1) return 0;
	buttons = pad(port);
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t) (buttons & 0xFFFFu);
	return id < 16 ? (int16_t) ((buttons >> id) & 1u) : 0;
}

/* ---- exports ---- */

static void snes_heap_init(void *base, unsigned size) { pico_heap_init(base, size); }

/* the C++ renderer's static constructors, if any: the link script keeps them in .init_array */
extern void (*__init_array_start[])(void), (*__init_array_end[])(void);

static void snes_init(void)
{
	static bool constructed;
	void (**f)(void);
	if (!constructed) {
		for (f = __init_array_start; f < __init_array_end; f++) (*f)();
		constructed = true;
	}
	g_skip_video = g_skip_audio = false;
	g_frames_run = 0;
	g_pad_reader = NULL;
	options_start();
	retro_set_environment(env_cb);
	retro_set_video_refresh(video_cb);
	retro_set_audio_sample(audio_sample_cb);
	retro_set_audio_sample_batch(audio_batch_cb);
	retro_set_input_poll(input_poll_cb);
	retro_set_input_state(input_state_cb);
	retro_init();
}

static int snes_load(const void *rom, unsigned size)
{
	struct retro_game_info info;
	if (!rom || !size) return -1;
	memset(&info, 0, sizeof info);
	info.path = NULL;
	info.data = rom;
	info.size = size;
	g_frames_run = 0;
	return retro_load_game(&info) ? 0 : -1;
}

static void snes_reset(void) { retro_reset(); }

static void snes_unload(void)
{
	retro_unload_game();
	retro_deinit();
}

static void snes_run(struct snes_frame *out)
{
	g_video = NULL;
	g_audio_frames = 0;
	retro_run();
	g_frames_run++;
	if (!out) return;
	out->video = g_video;
	out->width = g_video_w;
	out->height = g_video_h;
	out->pitch = g_video_pitch;
	out->audio = g_audio;
	out->frames = g_audio_frames;
}

static void snes_av_info(unsigned *base_w, unsigned *base_h, unsigned *max_w, unsigned *max_h, unsigned *sample_rate)
{
	struct retro_system_av_info av;
	retro_get_system_av_info(&av);
	if (base_w) *base_w = av.geometry.base_width;
	if (base_h) *base_h = av.geometry.base_height;
	if (max_w) *max_w = av.geometry.max_width;
	if (max_h) *max_h = av.geometry.max_height;
	if (sample_rate) *sample_rate = (unsigned) (av.timing.sample_rate + 0.5);
}

static unsigned snes_fps_milli(void)
{
	struct retro_system_av_info av;
	retro_get_system_av_info(&av);
	return (unsigned) (av.timing.fps * 1000.0 + 0.5);
}

static unsigned snes_aspect_x1000(void)
{
	struct retro_system_av_info av;
	float a;
	retro_get_system_av_info(&av);
	a = av.geometry.aspect_ratio;
	if (a <= 0.0f && av.geometry.base_height)
		a = (float) av.geometry.base_width / (float) av.geometry.base_height;
	return (unsigned) (a * 1000.0f + 0.5f);
}

static void *snes_sram_ptr(void) { return retro_get_memory_data(RETRO_MEMORY_SAVE_RAM); }
static unsigned snes_sram_size(void) { return (unsigned) retro_get_memory_size(RETRO_MEMORY_SAVE_RAM); }

static unsigned snes_state_size(void) { return (unsigned) retro_serialize_size(); }
static int snes_state_save(void *buf, unsigned size) { return buf && retro_serialize(buf, size) ? 0 : -1; }
static int snes_state_load(const void *buf, unsigned size) { return buf && retro_unserialize(buf, size) ? 0 : -1; }

static void snes_set_av_skip(int skip_video, int skip_audio)
{
	g_skip_video = skip_video != 0;
	g_skip_audio = skip_audio != 0;
}

/* snes9x's quick run ahead snapshot: not here (the LK falls back to state_save / state_load) */
static void snes_set_ra_fast(int on) { (void) on; }
static unsigned snes_state_save_ra(void *buf, unsigned cap) { (void) buf; (void) cap; return 0; }
static int snes_state_load_ra(const void *buf) { (void) buf; return -1; }

static void snes_set_pad_reader(unsigned (*reader)(unsigned port)) { g_pad_reader = reader; }

static unsigned snes_dbg_pc(void) { return cpu.r.pc.d & 0xFFFFFFu; }

/* 0: heap bytes in use, 1: the heap's high water mark (the arena a game needs), 2: the arena's size,
 * 3: the heap's top now, 4: frames run since load, 5: the last run()'s audio frames */
static unsigned snes_dbg_get(unsigned idx)
{
	switch (idx) {
	case 0: return pico_heap_used();
	case 1: return pico_heap_peak();
	case 2: return pico_heap_size();
	case 3: return pico_heap_top();
	case 4: return g_frames_run;
	case 5: return g_audio_frames;
	default: return 0;
	}
}

/* v5: the game's memory for RetroAchievements: the port's blocks and the map it announced */
_Static_assert(sizeof(struct pico_memdesc) == sizeof(struct retro_memory_descriptor), "pico_memdesc layout");
static void snes_get_memory(struct pico_core_memory *out)
{
	memset(out, 0, sizeof *out);
	out->system_ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	out->system_ram_size = out->system_ram ? (unsigned) retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM) : 0;
	out->save_ram = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	out->save_ram_size = out->save_ram ? (unsigned) retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) : 0;
	out->descriptors = g_mem_ndesc ? (const struct pico_memdesc *) g_mem_desc : NULL;
	out->num_descriptors = g_mem_ndesc;
}

/* v6: cheat codes, armSNES's own (src/cheat.c: Game Genie, Pro Action Replay and address:value
 * codes, several joined by '+'); it says whether it read any */
static void snes_cheat_reset(void) { cheatReset(); }
static int snes_cheat_add(const char *code) { return code && cheatAdd(code) ? 0 : -1; }

static const struct snes_core_exports g_exports = {
	SNES_CORE_ABI_MAGIC,
	SNES_CORE_ABI_V6,
	snes_heap_init,
	snes_init,
	snes_load,
	snes_reset,
	snes_unload,
	snes_run,
	snes_av_info,
	snes_sram_ptr,
	snes_sram_size,
	snes_state_size,
	snes_state_save,
	snes_state_load,
	snes_dbg_pc,
	snes_dbg_get,
	snes_set_option,
	snes_aspect_x1000,
	pico_heap_mark,
	pico_heap_reset,
	snes_set_ra_fast,
	snes_set_av_skip,
	snes_state_save_ra,
	snes_state_load_ra,
	snes_fps_milli,
	snes_set_pad_reader,
	snes_opt_count,
	snes_opt_info,
	snes_get_memory,
	snes_cheat_reset,
	snes_cheat_add,
};

__attribute__((used, section(".text.snes_core_blob_init")))
const struct snes_core_exports *snes_core_blob_init(const struct snes_core_imports *imp)
{
	g_imp = imp;
	return &g_exports;
}
