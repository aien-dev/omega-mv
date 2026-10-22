#!/bin/bash
set -u
mkdir -p build/L585
flags=(-std=gnu11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I. -Isrc -Isrc/compiler -Isrc/osh -Isrc/osh/host -Isrc/runtime -Wl,--gc-sections -Wl,--wrap=read -Wl,--wrap=pselect)
quiet-build gcc "${flags[@]}" evidence/L585-PROMPT-REVIEW/probe.c -o build/L585/probe > evidence/L585-PROMPT-REVIEW/raw/probe-build.log 2>&1 || exit 99
quiet-build build/L585/probe > evidence/L585-PROMPT-REVIEW/raw/probe-run.log 2>&1
rc=$?
printf 'final run exit=%s\n' "$rc"
if [ "$rc" != 0 ]; then exit 99; fi
git show 7569e8d2664ced00ec792ae25a0483e0cfe4f2e7:src/osh/host/osh_shell.c > build/L585/m1-shell.c
quiet-build gcc "${flags[@]}" '-DREVIEW_SHELL="build/L585/m1-shell.c"' evidence/L585-PROMPT-REVIEW/probe.c -o build/L585/probe-m1 > evidence/L585-PROMPT-REVIEW/raw/probe-m1-build.log 2>&1 || exit 99
quiet-build build/L585/probe-m1 > evidence/L585-PROMPT-REVIEW/raw/probe-m1-run.log 2>&1
rc=$?
printf 'M1 red-control exit=%s\n' "$rc"
[ "$rc" = 1 ] || exit 99
