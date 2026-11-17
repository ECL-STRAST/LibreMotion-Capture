#!/bin/sh
# Guard for the telemetria C ABI: fails the build if an expected entry point is
# missing from the shared library that was just linked (see CMakeLists.txt).
#
# usage: check_exports.sh <shared-library> <dump-tool> [<symbol> ...]
#
# <dump-tool> may be an objdump/llvm-objdump path, a dumpbin path, or empty
# (MSVC leaves CMAKE_OBJDUMP unset). Artifact shapes are all covered:
# ELF (.so, dynamic symbol table via objdump -T), PE via objdump -p and
# via `dumpbin -exports`. When no usable tool exists at all the check is
# SKIPPED (exit 0): a missing analysis tool must never break a good build.
set -eu

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <shared-library> <dump-tool> <symbol>..." >&2
    exit 2
fi

artifact="$1"
tool="$2"
shift 2

# An empty/unusable tool argument must not swallow the first symbol name.
if [ -z "$tool" ] || [ ! -x "$tool" ]; then
    tool="$(command -v objdump 2>/dev/null || true)"
    if [ -z "$tool" ]; then
        tool="$(command -v dumpbin 2>/dev/null || true)"
    fi
fi
if [ -z "$tool" ]; then
    echo "check_exports: no dump tool (objdump/dumpbin) found; export check skipped"
    exit 0
fi

case "$tool" in
    *dumpbin*)
        # "-exports", not "/exports": MSYS shells (Git for Windows sh) rewrite a leading
        # "/" argument into a path, so dumpbin would dump C:\Program Files\Git\exports.
        dump="$("$tool" -exports "$artifact" 2>/dev/null || true)"
        ;;
    *)
        # ELF -> dynamic symbol table (-T); PE/COFF -> export table (-p).
        magic="$(od -An -tx1 -N4 "$artifact" 2>/dev/null | tr -d ' \n')"
        case "$magic" in
            7f454c46) dump="$("$tool" -T "$artifact" 2>/dev/null || true)" ;;
            *)        dump="$("$tool" -p "$artifact" 2>/dev/null || true)" ;;
        esac
        ;;
esac
if [ -z "$dump" ]; then
    echo "check_exports: cannot dump symbols of '$artifact' with '$tool'" >&2
    exit 1
fi

missing=0
for sym in "$@"; do
    if printf '%s\n' "$dump" | grep -Eq "(^|[[:space:]=:])${sym}([[:space:]:=]|$)"; then
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
