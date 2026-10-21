#!/bin/sh
# L377: the physics lock must hold for explicit make targets, and physics objects must rebuild when
# PHYSICS_DIR (or its commit) changes. L313 built an explicit target against ~/workspace/physics at
# e95e3ed (GPFIFO/USERD allocated GPU-cached): nothing refused it, and the stale nvrm.o was kept by
# later builds at the pin because the pinned nvrm.c had an older mtime than the object.
# Needs a physics clone that holds the physics.lock commit (PHYSICS_REPO, default the pin worktree
# under ~/workspace, else ../physics). Compiles nvrm.c and m16_native.c a few times; no chip.
cd "$(dirname "$0")/.." || exit 1
fail=0
ok() { echo "[+] $1"; }
bad() { echo "[-] $1"; fail=$((fail + 1)); }

pin=$(tr -d '[:space:]' < physics.lock)
repo=${PHYSICS_REPO:-}
if [ -z "$repo" ]; then
    for c in "$HOME"/workspace/physics-q36-6d7cf0d4 ../physics; do
        git -C "$c" cat-file -e "$pin^{commit}" 2>/dev/null && { repo=$c; break; }
    done
fi
git -C "$repo" cat-file -e "$pin^{commit}" 2>/dev/null || { echo "no physics clone holding $pin (set PHYSICS_REPO)"; exit 2; }

W=$(mktemp -d) || exit 1
trap 'rm -rf "$W"' EXIT
# PIN: a clone at the pin. WRONG: the same tree plus one local commit that changes nvrm.c, so its
# HEAD differs from physics.lock and its nvrm.o carries a marker symbol the pinned one lacks.
git -c advice.detachedHead=false clone -q --shared "$repo" "$W/pin" && git -C "$W/pin" -c advice.detachedHead=false checkout -q --detach "$pin" || exit 2
git -c advice.detachedHead=false clone -q --shared "$W/pin" "$W/wrong" && git -C "$W/wrong" -c advice.detachedHead=false checkout -q --detach "$pin" || exit 2
echo 'const int omega_physics_lock_test_marker = 1;' >> "$W/wrong/nvrm/nvrm.c"
git -C "$W/wrong" -c user.name=t -c user.email=t@t commit -q -am 'test: marker' || exit 2
# The pinned sources are older than any object built here, exactly the L313 situation.
touch -d '2001-01-01 00:00:00' "$W/pin/nvrm/nvrm.c" "$W/pin/m16/m16_native.c"

mk() { # OUT_DIR PHYSICS_DIR extra-make-args...
    o=$1; p=$2; shift 2
    make -s OUT_DIR="$o" PHYSICS_DIR="$p" "$@" 2>&1
}
marker() { nm "$1" 2>/dev/null | grep -q omega_physics_lock_test_marker; }

# (a) an explicit target (not `all`) at a PHYSICS_DIR that is not the pin must be refused.
for t in nvrm.o m16_native.o omega_blackwell_submit.o omega_gpu_platform_linux.o libomega_gpu.a; do
    out=$(mk "$W/a" "$W/wrong" "$W/a/$t"); rc=$?
    if [ $rc -ne 0 ] && printf '%s\n' "$out" | grep -q 'physics.lock pins'; then ok "(a) explicit target $t refused at a wrong PHYSICS_DIR"
    else bad "(a) explicit target $t was NOT refused at a wrong PHYSICS_DIR (exit $rc)"; fi
    [ -e "$W/a/$t" ] && bad "(a) $t was built anyway"
done
out=$(mk "$W/a0" "$W/wrong" PHYSICS_LOCK_CHECK=0 "$W/a0/nvrm.o"); rc=$?
if [ $rc -eq 0 ] && marker "$W/a0/nvrm.o"; then ok "(a) PHYSICS_LOCK_CHECK=0 still builds at a wrong PHYSICS_DIR"
else bad "(a) PHYSICS_LOCK_CHECK=0 did not build at a wrong PHYSICS_DIR (exit $rc)"; fi

# (b) objects built from one PHYSICS_DIR rebuild when PHYSICS_DIR changes, even if the new sources are older.
B=$W/b
out=$(mk "$B" "$W/wrong" PHYSICS_LOCK_CHECK=0 "$B/nvrm.o" "$B/m16_native.o"); rc=$?
[ $rc -eq 0 ] && marker "$B/nvrm.o" || bad "(b) setup: nvrm.o from the wrong dir has no marker (exit $rc)"
out=$(mk "$B" "$W/pin" "$B/nvrm.o" "$B/m16_native.o"); rc=$?
if [ $rc -eq 0 ] && ! marker "$B/nvrm.o"; then ok "(b) nvrm.o rebuilt from the pinned PHYSICS_DIR"
else bad "(b) nvrm.o is still the wrong-dir object after switching to the pin (exit $rc)"; fi
mk "$W/fresh" "$W/pin" "$W/fresh/nvrm.o" "$W/fresh/m16_native.o" >/dev/null
for o in nvrm.o m16_native.o; do
    if cmp -s "$B/$o" "$W/fresh/$o"; then ok "(b) $o is identical to a clean build at the pin"
    else bad "(b) $o differs from a clean build at the pin"; fi
done
# an unchanged PHYSICS_DIR must not rebuild.
before=$(ls -l --time-style=full-iso "$B/nvrm.o" "$B/m16_native.o")
sleep 1
mk "$B" "$W/pin" "$B/nvrm.o" "$B/m16_native.o" >/dev/null
after=$(ls -l --time-style=full-iso "$B/nvrm.o" "$B/m16_native.o")
if [ "$before" = "$after" ]; then ok "(b) unchanged PHYSICS_DIR: no rebuild"; else bad "(b) unchanged PHYSICS_DIR rebuilt the objects"; fi

# (c) the list of physics-bearing objects must cover every object that includes a physics header.
list=$(make -s --eval='print-physics-objs: ; @echo $(PHYSICS_OBJS)' OUT_DIR=obj PHYSICS_DIR="$W/pin" print-physics-objs 2>/dev/null)
inc="-Isrc -I$W/pin/m16 -I$W/pin/nvrm"
inc="$inc -I$W/pin/third_party/nvidia-open-580.173.02/src/common/sdk/nvidia/inc"
inc="$inc -I$W/pin/third_party/nvidia-open-580.173.02/kernel-open/common/inc"
inc="$inc -I$W/pin/third_party/nvidia-open-580.173.02/kernel-open/nvidia-uvm"
inc="$inc -I$W/pin/third_party/nvidia-open-580.173.02/src/nvidia/arch/nvalloc/unix/include"
miss=
for f in src/*.c tools/omegatool.c; do
    gcc -std=gnu11 -D_GNU_SOURCE $inc -MM "$f" 2>/dev/null | grep -q "$W/pin" || continue
    n=obj/$(basename "$f" .c).o
    case " $list " in *" $n "*) ;; *) miss="$miss $n" ;; esac
done
if [ -z "$miss" ]; then ok "(c) PHYSICS_OBJS covers every object that includes a physics header"
else bad "(c) PHYSICS_OBJS misses:$miss"; fi

echo "failures $fail"
[ "$fail" = 0 ]
