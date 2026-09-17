#!/bin/sh
# Guard for the telemetria C ABI: fails the build if an expected entry point is
# missing from the shared library that was just linked (see CMakeLists.txt).
#
# usage: check_exports.sh <shared-library> <objdump> <symbol> [<symbol> ...]
#
# Works for both artifact shapes: ELF (.so, readelf-style dynamic table via -T)
# and PE (.dll, export table via -p).
set -eu

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <shared-library> <objdump> <symbol>..." >&2
    exit 2
fi

artifact="$1"
objd="$2"
shift 2

if [ ! -x "$objd" ]; then
    objd=objdump
fi

# ELF -> dynamic symbol table (-T); PE/COFF -> export table (-p).
magic="$(od -An -tx1 -N4 "$artifact" 2>/dev/null | tr -d ' \n')"
elf_dump=""
pe_dump=""
case "$magic" in
    7f454c46) elf_dump="$("$objd" -T "$artifact" 2>/dev/null || true)" ;;
    *)        pe_dump="$("$objd" -p "$artifact" 2>/dev/null || true)" ;;
esac
dump="$elf_dump$pe_dump"
if [ -z "$dump" ]; then
    echo "check_exports: cannot dump symbols of '$artifact' with '$objd'" >&2
    exit 1
fi

missing=0
for sym in "$@"; do
    if printf '%s\n' "$dump" | grep -qw "${sym}"; then
        echo "  export ok: ${sym}"
    else
        echo "  MISSING EXPORT: ${sym}" >&2
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    echo "check_exports: ${artifact} does not export the full C ABI (link loss?)" >&2
fi
exit "$missing"
