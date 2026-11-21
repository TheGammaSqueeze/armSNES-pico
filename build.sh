#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 TheGammaSqueeze
# armSNES-pico: build the armSNES core blob for the GammaOS LK (lk-mt6785, Konkr Pocket Advance and
# Konkr Pocket Block).
#
#   ./build.sh            -> out/armsnes-pico.blob (copy it to the SD card as /cores/armsnes-pico.blob)
#   ./build.sh src        -> only out/src: the upstream sources with patches/ applied
#   ./build.sh clean
#
# The upstream tree (submodule upstream/, MagnominusGit/armSNES) is never modified: its src/,
# libretro/ and shared/ are copied to out/src, the patches in patches/ (if any) are applied there,
# and that copy is compiled together with the glue in pico/. Built: armSNES's core (src/ and
# src/chips/, its fast line renderer src/fastppu.cpp included), its libretro port
# (libretro/libretro.c) and the options helper it shares with the Armulators cores
# (shared/libretro/options.c). Not built: the Super Game Boy's Game Boy (src/chips/gb.cpp and the
# Gambatte tree it uses; the LK hands the SNES core cartridges only, pico/pico_nogb.c answers its
# calls), the standalone app and the test tools.
#
# Target: the LK runs the core in 32 bit ARM state on CPU 0 of the MT6785 (a Cortex-A55) or the
# MT8786. Built for ARMv8-A with NEON and hardware floating point (softfp calling convention, as the
# LK's own NEON code and the other blobs), tuned for the A55, -O3 and link time optimisation.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
OUT=$ROOT/out
CC=${CC:-arm-none-eabi-gcc}
CXX=${CXX:-arm-none-eabi-g++}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
# the source list below is sorted: sort the same everywhere, so the link order is too
export LC_ALL=C

if [ "${1:-}" = clean ]; then rm -rf "$OUT"; exit 0; fi

if [ ! -f upstream/src/snes.h ]; then
	git submodule update --init upstream
fi

UPSTREAM_REV=$(git -C upstream describe --tags --always 2>/dev/null || git -C upstream rev-parse --short HEAD)
PICO_REV=$(git describe --always --dirty 2>/dev/null || echo unknown)
VERSION="armSNES ${UPSTREAM_REV} (pico ${PICO_REV})"

rm -rf "$OUT/src" "$OUT/obj"
mkdir -p "$OUT/src" "$OUT/obj"
cp -a upstream/src upstream/libretro upstream/shared "$OUT/src/"
# EXTRA_PATCHES: more patches for experiments, applied after these; releases use none
for p in patches/*.patch ${EXTRA_PATCHES:-}; do
	[ -e "$p" ] || continue
	echo ">> patch $(basename "$p")"
	case "$p" in /*) pp=$p;; *) pp=$ROOT/$p;; esac
	(cd "$OUT/src" && patch -p1 --quiet --forward < "$pp")
done
if [ "${1:-}" = src ]; then echo "out/src ready"; exit 0; fi
# the version the LK shows (the core's name in its menus)
printf '#define ARMSNES_PICO_VERSION "%s"\n' "$VERSION" > "$OUT/pico_version.h"

SRC=$OUT/src
TUNE=${TUNE:-cortex-a55}
ARCH="-march=armv8-a+simd -mtune=$TUNE -marm -mfpu=neon-fp-armv8 -mfloat-abi=softfp"
# upstream builds with -fwrapv (CMakeLists.txt): its arithmetic counts on wrapping
BASE="$ARCH ${OPT:--O3} -fwrapv -DARMSNES_HOST_NO_HD -flto -ffunction-sections -fdata-sections -fno-common ${EXTRA_CFLAGS:-}"
# pico/include first: the headers newlib lacks for this target (pthread.h, sched.h's sched_yield)
INC="-I$ROOT/pico/include -I$SRC/shared/libretro -I$SRC/src -I$SRC/src/chips -include $ROOT/pico/include/pico_time.h"
CORE_FLAGS="$BASE -std=gnu11 -Wall -Wno-unused-parameter $INC"
# the fast renderer is C++ with no library behind it: no exceptions, no type information, no
# guarded statics (one thread)
CXX_FLAGS="$BASE -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -w $INC"
PORT_FLAGS="$BASE -std=gnu11 -Wall -Wno-unused-parameter $INC -I$SRC/libretro"
GLUE_FLAGS="$BASE -std=gnu11 -Wall -Wextra -Wno-unused-parameter $INC -I$ROOT/pico -I$OUT"
# The C runtime is compiled outside LTO: newlib's archive members reference its symbols (_read,
# _calloc_r, ...) only at the final link, after LTO would already have dropped them. -fno-builtin as
# any malloc must be: GCC otherwise turns malloc + memset back into a call to calloc.
RT_FLAGS="${BASE/-flto/} -std=gnu11 -fno-builtin -Wall -Wextra -I$ROOT/pico"

# upstream CMakeLists.txt's armsnes library, less src/chips/gb.cpp
CORE="src/bus.c src/cart.c src/cheat.c src/chips/bsx.c src/chips/competition.c src/chips/cx4.c
	src/chips/gsu.c src/chips/icd.c src/chips/msu1.c src/chips/necdsp.c src/chips/obc1.c
	src/chips/pirate.c src/chips/rtc.c src/chips/sa1.c src/chips/sa1fast.c src/chips/sdd1.c
	src/chips/spc7110.c src/chips/st018.c src/chips/sufami.c src/cpu.c src/dsp.c src/input.c
	src/mix.c src/ppu.c src/sha256.c src/smp.c src/state.c src/system.c"
CXXSRC="src/fastppu.cpp"
PORT="libretro/libretro.c shared/libretro/options.c"

OBJS=()
obj() { echo "$OUT/obj/$(echo "${1%.*}" | tr '/' '_').o"; }
for f in $CORE; do
	o=$(obj "$f")
	$CC $CORE_FLAGS -c "$SRC/$f" -o "$o"
	OBJS+=("$o")
done
for f in $CXXSRC; do
	o=$(obj "$f")
	$CXX $CXX_FLAGS -c "$SRC/$f" -o "$o"
	OBJS+=("$o")
done
for f in $PORT; do
	o=$(obj "$f")
	$CC $PORT_FLAGS -c "$SRC/$f" -o "$o"
	OBJS+=("$o")
done
$CC $GLUE_FLAGS -c "$ROOT/pico/pico_core.c" -o "$OUT/obj/pico_core.o"
$CC $GLUE_FLAGS -c "$ROOT/pico/pico_nogb.c" -o "$OUT/obj/pico_nogb.o"
$CC $RT_FLAGS -c "$ROOT/pico/pico_libc.c" -o "$OUT/obj/pico_libc.o"
OBJS+=("$OUT/obj/pico_core.o" "$OUT/obj/pico_nogb.o" "$OUT/obj/pico_libc.o")

$CXX $BASE -nostartfiles -fno-exceptions -fno-rtti -Wl,--gc-sections -Wl,-T,"$ROOT/pico/blob.ld" \
	-Wl,-Map,"$OUT/armsnes-pico.map" -o "$OUT/armsnes-pico.elf" "${OBJS[@]}" -lm -lc -lgcc
$OBJCOPY -O binary "$OUT/armsnes-pico.elf" "$OUT/armsnes-pico.blob"

python3 - "$OUT/armsnes-pico.blob" "$VERSION" <<'EOF'
import struct, sys, hashlib
d = open(sys.argv[1], 'rb').read()
magic, ver, entry, load, span = struct.unpack_from('<5I', d)
assert magic == 0x31534E53 and ver == 6, "bad header"
assert load == len(d), "load size %d != file %d" % (load, len(d))
assert entry < load, "entry outside the image"
slot = 0x50000000 - 0x4F000000
assert span <= slot, "span %d does not fit the 16 MiB of the SNES and Genesis slots" % span
print("armsnes-pico.blob: %s" % sys.argv[2])
print("  file %d bytes, span %d bytes (%.2f MiB of 16), entry +0x%x" % (len(d), span, span / 1048576.0, entry))
print("  sha256 %s" % hashlib.sha256(d).hexdigest())
EOF
