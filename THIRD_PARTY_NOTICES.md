# Third party notices

The armSNES-pico blob (`armsnes-pico.blob`) is built from the following. As a whole it is under the
GNU General Public License, version 2 (`LICENSE`). The ISC, MIT and newlib parts listed below are
under permissive licences that only ask for their notices to be kept, and libgcc under the GCC
Runtime Library Exception.

## armSNES (GPL-2.0-or-later)

- Source: <https://github.com/MagnominusGit/armSNES>, the `upstream/` submodule, at the commit
  recorded in this repository.
- Copyright (c) 2026 Magnominus. armSNES's own files are GPL-2.0-or-later (`upstream/LICENSE`).
- Compiled: the core (`src/`, `src/chips/` less `gb.cpp`, the line renderer `src/fastppu.cpp`), the
  libretro port (`libretro/libretro.c`, and its NTSC/PAL filter `libretro/composite.c`, which the
  link leaves out as the filter is held off) and the options helper (`shared/libretro/options.c`),
  with `patches/0001-no-hd-pictures.patch` applied. Not compiled: the Super Game Boy's Game Boy
  (`src/chips/gb.cpp`, `gambatte/`), the port's zip reader (`libretro/zip.c`, and so no zlib), the
  standalone app (`app/`, the `armulators` submodule) and the test tools (`tests/`).
- Parts of it under other licences, as upstream states:
  - **ares** (Copyright (c) 2004-2025 ares team, Near et al., ISC, `upstream/LICENSES/ISC-ares.txt`):
    the emulation armSNES ports to C keeps its notice.
  - **snes_faust** (Copyright (C) 2015-2019 Mednafen Team, GPL-2.0-or-later): Fast mode's line
    renderer in `src/fastppu.cpp` and `src/fastppu_render.inc`.
  - **libretro.h** (`shared/libretro/libretro.h`, Copyright (C) 2010-2024 The RetroArch team, MIT):
    the libretro interface header the port and the glue are compiled with.

## newlib (BSD style licences)

- The C library and libm of the `arm-none-eabi` toolchain: string, formatting, stdio and maths
  functions, linked as the toolchain ships them. Their notices are in newlib's `COPYING.NEWLIB`.

## libgcc (GPL-3.0 with the GCC Runtime Library Exception)

- The compiler's helpers for arithmetic the target has no instructions for.
