# armSNES-pico

> **Enjoying armSNES-pico? Please support the upstream project.** All of the emulation here is
> Magnominus's work on [armSNES](https://github.com/MagnominusGit/armSNES). If this core makes your
> games better, consider buying them a coffee on [Ko-fi](https://ko-fi.com/magnominus).

[armSNES](https://github.com/MagnominusGit/armSNES) by Magnominus, built as a loadable Super Nintendo
and Super Famicom core for the GammaOS bootloader emulator on the Konkr Pocket Advance and the Konkr
Pocket Block (`lk-mt6785`). In GammaOS Pico it is the second SNES core, beside snes9x, as gpSP is
beside armGBA: the in-game menu's **SNES Core** row (Gameplay) picks it for the games that follow.

All of the emulation is armSNES's. This repository adds no emulation code: it holds the upstream
tree as a submodule, the glue that lets armSNES's libretro port run inside a bootloader with no
operating system, and a build script.

**This is an experimental project and does not accept issues.** Please do not report problems you
see with armSNES-pico to armSNES upstream either: this build runs armSNES in a bootloader on hardware
upstream does not target, so its behaviour here says nothing about armSNES itself.

## What differs from armSNES in RetroArch

- **One thread.** The LK gives a core one CPU. armSNES's Fast mode puts its line renderer and its
  sound processor on threads of their own when it can; here it cannot (`pthread_create` fails), and
  both run on the game's thread, as upstream does when threads are not to be had.
- **No Super Game Boy.** The LK hands the SNES core cartridges only, so the Game Boy inside a Super
  Game Boy (upstream's `src/chips/gb.cpp`, with Gambatte) is not built (`pico/pico_nogb.c`).
- **No HD Mode 7, widescreen or NTSC/PAL filter picture** (`patches/0001-no-hd-pictures.patch`):
  the LK shows a picture up to the console's own hires size, and those three are larger. Their
  options read Off and are not on the Core Settings page, and the memory upstream sets aside for
  them when a game starts is not taken.
- **No SA-1 recompiler.** Upstream's is for x86-64 and 64 bit ARM; the LK runs the core as 32 bit
  ARM code, where armSNES runs the SA-1 the plain way and has no such option.
- **No MSU-1 files.** The LK hands over a game's bytes with no path, so there is nothing to look
  beside; the port's zip reader for them is not built (`pico/pico_nozip.c`).
- **Turbo is the LK's.** armSNES's own turbo options are not on the Core Settings page, and L3 and
  R3 (its turbo L and R) are never passed to it.
- **No firmware files.** A game whose chip needs a firmware file the dump does not carry (DSP-1 to
  DSP-4, ST010, ST011, Cx4, ST018) does not start; snes9x runs those.
- The LK's own picture and sound rows (aspect, overscan, audio filter, hi-res blend) are snes9x's;
  the glue sets armSNES's nearest options for them. Every other armSNES option is on the Core
  Settings page, saved apart from snes9x's (`/gammaos/coreopt-armsnes.cfg`), and save states are
  kept apart too (`/states/armsnes`): the two cores' states do not load in each other.

## Credits

- **armSNES** (Magnominus, GPL-2.0-or-later, [support them on Ko-fi](https://ko-fi.com/magnominus)):
  the SNES emulator, its three modes, its chips and the libretro port this blob drives.
- armSNES builds on the work of others, which this blob carries too (see upstream's README):
  **ares** (Near and the ares team, ISC), whose emulation armSNES is a port of to C; **snes_faust**
  (Mednafen Team, GPL-2.0-or-later), whose line renderer is Fast mode's.

## License

GPL-2.0 as a whole (`LICENSE`). armSNES's own files are GPL-2.0-or-later, the code ported from ares
keeps ares's ISC notice (`upstream/LICENSES/ISC-ares.txt`), and the `libretro.h` header is MIT. The
glue in `pico/` and the build script are GPL-2.0-or-later. Each file keeps its own notice.
`THIRD_PARTY_NOTICES.md` lists everything the blob is made of. The GPL asks that whoever receives the
blob can get its complete source: every release carries this repository with its submodule at the
commit the blob was built from.

## Layout

| Path | What it is |
| --- | --- |
| `upstream/` | armSNES, as a git submodule, never modified in place |
| `patches/` | changes to upstream, applied by `build.sh` to a copy in `out/src` |
| `pico/pico_core.c` | the LK's SNES core interface (`snes_core_abi.h`) filled in for armSNES's libretro port |
| `pico/pico_libc.c` | the heap over the LK's arena, newlib's system calls, the thread calls that start none |
| `pico/pico_nogb.c` | the Super Game Boy's Game Boy, answered as one with no game |
| `pico/pico_nozip.c` | the port's zip reader (MSU-1 files in a zip), answered as an archive that is not there |
| `pico/snes_core_abi.h` | a copy of `lk-mt6785/emu/snes9x/snes_core_abi.h`; the LK's copy is the reference |
| `pico/blob.ld` | link script: flat image at 0x4F000000, up to 16 MiB with its static memory |
| `build.sh` | builds `out/armsnes-pico.blob` |
