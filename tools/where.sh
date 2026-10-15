#!/bin/bash
# OpenJazz CD32X - which function a PC from the red exception screen belongs to.
#   tools/where.sh 0201A31E
# Uses the engine of the LAST build (mars/openjazz32x.elf): run it before building again.
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PC=$((16#${1#0x}))
for d in -4 -2 0 2; do printf '0x%08X\n' $((PC + d)); done | \
  /opt/toolchains/sega/sh-elf/bin/sh-elf-addr2line -f -C -i -a -e "$HERE/mars/openjazz32x.elf"
