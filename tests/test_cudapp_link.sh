#!/bin/sh
# SPDX-License-Identifier: MIT
# Exercise the production linker and Make binding using isolated tracked sources.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
base=$(mktemp -d "${TMPDIR:-/tmp}/omega-cudapp-link.XXXXXX")
trap 'rm -r -- "$base"' EXIT HUP INT TERM
checks=0
failed=0
commit_fixture() {
    git -C "$c" add -A
    git -C "$c" -c user.name=Test -c user.email=test@invalid commit -qm fixture
    git -C "$c" rev-parse HEAD > "$o/cudapp.lock"
}
for case in valid missing-checkout wrong-pin disabled-lock dirty-source dirty-buildfile ignored-source malformed-pin shadow-copy unmapped-gpu unsafe-map source-symlink \
    untracked-header ignored-header external-header assume-unchanged skip-worktree hidden-buildfile tracked-header hidden-header destination-parent destination-dangling duplicate-map make-override make-missing make-valid; do
    o=$base/$case/omega
    c=$base/$case/cudapp
    mkdir -p "$o/tools" "$o/src" "$c/src" "$c/nvrm" "$c/third_party" "$base/$case/outside"
    cp "$root/tools/cudapp-link.sh" "$o/tools/"
    printf 'int canonical_fixture(void) { return 7; }\n' > "$c/src/omega_gpu_fixture.c"
    printf '#define HEADER 1\n' > "$c/nvrm/pinned.h"
    printf 'all:\n\ttrue\n' > "$c/Makefile"
    git -C "$c" init -q
    commit_fixture
    printf 'src/omega_gpu_fixture.c\n' > "$o/cudapp.files"
    printf 'src/omega_blackwell_gates.c\nsrc/omega_blackwell_gates.h\n' > "$o/cudapp.stay"
    target=$c
    lock_check=1
    expect=refuse
    case $case in
        valid|make-valid) expect=accept;;
        missing-checkout|make-missing|make-override) target=$base/absent;;
        wrong-pin) printf '%040d\n' 0 > "$o/cudapp.lock";;
        disabled-lock) lock_check=0;;
        dirty-source) printf 'int divergent;\n' > "$c/src/omega_gpu_fixture.c";;
        dirty-buildfile) printf 'all:\n\tfalse\n' > "$c/Makefile";;
        ignored-source)
            git -C "$c" rm --cached -q src/omega_gpu_fixture.c
            printf '/src/omega_gpu_fixture.c\n' > "$c/.gitignore"
            git -C "$c" add .gitignore
            git -C "$c" -c user.name=Test -c user.email=test@invalid commit -qm ignored
            git -C "$c" rev-parse HEAD > "$o/cudapp.lock";;
        malformed-pin) printf ' %s\n' "$(cat "$o/cudapp.lock")" > "$o/cudapp.lock.new"; mv "$o/cudapp.lock.new" "$o/cudapp.lock";;
        shadow-copy) printf 'int shadow;\n' > "$o/src/omega_gpu_fixture.c";;
        unmapped-gpu) printf 'int hidden;\n' > "$o/src/omega_gpu_hidden.c";;
        unsafe-map) printf '../escape\n' > "$o/cudapp.files";;
        source-symlink)
            rm "$c/src/omega_gpu_fixture.c"
            printf 'int outside;\n' > "$base/$case/outside/source.c"
            ln -s "$base/$case/outside/source.c" "$c/src/omega_gpu_fixture.c"
            commit_fixture;;
        untracked-header) printf '#define HEADER 2\n' > "$c/nvrm/nvos.h";;
        ignored-header)
            printf '/nvrm/nvos.h\n' > "$c/.gitignore"
            commit_fixture
            printf '#define HEADER 2\n' > "$c/nvrm/nvos.h";;
        external-header) ln -s "$base/$case/outside" "$c/nvrm/nvos.h";;
        assume-unchanged|skip-worktree)
            git -C "$c" update-index "--$case" src/omega_gpu_fixture.c
            printf 'int hidden_edit;\n' > "$c/src/omega_gpu_fixture.c";;
        hidden-buildfile)
            git -C "$c" update-index --assume-unchanged Makefile
            printf 'all:\n\tfalse\n' > "$c/Makefile";;
        tracked-header) printf '#define HEADER 2\n' > "$c/nvrm/pinned.h";;
        hidden-header)
            git -C "$c" update-index --skip-worktree nvrm/pinned.h
            printf '#define HEADER 2\n' > "$c/nvrm/pinned.h";;
        destination-parent|destination-dangling)
            if [ "$case" = destination-dangling ]; then dest=$base/$case/not-created; else dest=$base/$case/outside; fi
            ln -s "$dest" "$o/tools/redirected"
            printf 'tools/redirected/file.c\n' > "$o/cudapp.files"
            mkdir -p "$c/tools/redirected"
            printf 'int canonical;\n' > "$c/tools/redirected/file.c"
            commit_fixture;;
        duplicate-map) printf 'src/omega_gpu_fixture.c\nsrc/omega_gpu_fixture.c\n' > "$o/cudapp.files";;
    esac
    rc=0
    case $case in
        make-*)
            # The production prefix ends before unrelated optional module includes.
            sed '/^-include mk\/\*.mk/,$d' "$root/Makefile" > "$o/Makefile"
            printf '\nall: ; @:\n' >> "$o/Makefile"
            if [ "$case" = make-override ]; then
                (cd "$o" && make -s CUDAPP_DIR="$target" CUDAPP_LINK=) > "$base/result" 2>&1 || rc=$?
            else
                (cd "$o" && make -s CUDAPP_DIR="$target") > "$base/result" 2>&1 || rc=$?
            fi;;
        *) CUDAPP_LOCK_CHECK=$lock_check sh "$o/tools/cudapp-link.sh" "$target" > "$base/result" 2>&1 || rc=$?;;
    esac
    ok=0
    if [ "$expect" = accept ] && [ "$rc" = 0 ] && [ -L "$o/src/omega_gpu_fixture.c" ] && [ "$(readlink -f "$o/src/omega_gpu_fixture.c")" = "$c/src/omega_gpu_fixture.c" ]; then ok=1; fi
    if [ "$expect" = refuse ] && [ "$rc" != 0 ] && [ ! -L "$o/src/omega_gpu_fixture.c" ] && [ ! -e "$base/$case/outside/file.c" ] && [ ! -d "$base/$case/not-created" ]; then ok=1; fi
    checks=$((checks+1))
    if [ "$ok" = 1 ]; then printf 'PASS %s (exit %s)\n' "$case" "$rc"; else
        failed=$((failed+1)); printf 'FAIL %s: expected %s, exit %s\n' "$case" "$expect" "$rc"; cat "$base/result"
    fi
done
printf 'CANONICAL LINK: %s checks, %s failed\n' "$checks" "$failed"
[ "$failed" = 0 ]
