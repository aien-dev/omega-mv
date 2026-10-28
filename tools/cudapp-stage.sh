#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Host I/O/build glue only. Pin policy is the seeded om/consumer.om program.
set -eu
export GIT_NO_REPLACE_OBJECTS=1 GIT_GRAFT_FILE=/nonexistent/cudapp-stage-grafts
if [ "$#" != 3 ]; then
    echo 'usage: cudapp-stage.sh LOCK CHECKOUT OUTPUT_LINK | --verify LOCK STAGE | --source LOCK STAGE' >&2
    exit 3
fi
: "${OMEGA_OMC:?set OMEGA_OMC to the pinned seeded compiler}"
: "${OMEGA_BRIDGE:?set OMEGA_BRIDGE to the pinned bridge checkout}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
printf '%s  %s\n' 2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f "$OMEGA_OMC" | sha256sum -c - >&2
# Reuse the existing raw-object read-only checker, including every actual and
# expected top-level entry. Build outputs live outside this pristine source.
verify_source() {
    checked_lock=$1 checked_stage=$2 checked_work=$3
    IFS= read -r repository < "$checked_stage/repository"
    IFS= read -r revision < "$checked_stage/observed"
    git -C "$repository" ls-tree -z --name-only "$revision" > "$checked_work/expected-roots"
    find "$checked_stage/source" -mindepth 1 -maxdepth 1 -printf '%f\0' > "$checked_work/actual-roots"
    LC_ALL=C sort -zu "$checked_work/expected-roots" "$checked_work/actual-roots" > "$checked_work/roots"
    test -s "$checked_work/roots"
    xargs -0 env AIENOS_LOCK= AIENOS_LOCK_FILE="$checked_lock" AIENOS_LOCK_REPO="$repository" \
        bash "$root/tools/aienos_lock_source.sh" verify-dir "$checked_stage/source" < "$checked_work/roots"
}
if [ "$1" = --verify ] || [ "$1" = --source ]; then
    mode=$1
    lock=$(realpath -- "$2")
    stage=$(realpath -- "$3")
    work=$(mktemp -d)
    trap 'rm -rf -- "$work"' EXIT HUP INT TERM
    "$OMEGA_OMC" < "$root/om/consumer.om" > "$work/pincheck"
    chmod 700 "$work/pincheck"
    cat "$lock" "$stage/pin" > "$work/pair"
    "$work/pincheck" < "$work/pair"
    cat "$lock" "$stage/observed" > "$work/pair"
    "$work/pincheck" < "$work/pair"
    verify_source "$lock" "$stage" "$work" >&2
    sha256sum -c "$stage/source.sha256" "$stage/guard.sha256" "$stage/archive.sha256" >&2
    if [ "$mode" = --source ]; then
        printf '%s\n' "$stage/source"
    else
        "$OMEGA_OMC" < "$root/om/consumer_receipt.om" > "$work/receipt"
        chmod 700 "$work/receipt"
        { cat "$lock"; sha256sum "$stage/source.tar" "$work/pincheck" "$stage/build/libcudapp.a" | cut -c1-64;
          printf '%s\n' "$stage/source"; } | "$work/receipt"
    fi
    exit 0
fi
lock=$1
checkout=$2
output=$3
mkdir -p -- "$(dirname -- "$output")"
parent=$(CDPATH= cd -- "$(dirname -- "$output")" && pwd)
output=$parent/$(basename -- "$output")
if [ -e "$output" ] || [ -L "$output" ]; then
    if [ ! -L "$output" ]; then
        echo 'cudapp-stage: output exists and is not a generated snapshot link' >&2
        exit 3
    fi
fi
stage=$(mktemp -d "$parent/.cudapp-stage.XXXXXX")
link=$stage.publish
trap 'rm -f -- "$link"; rm -rf -- "$stage"' EXIT HUP INT TERM
cp -- "$lock" "$stage/pin"
printf '%s\n' "$(CDPATH= cd -- "$checkout" && pwd)" > "$stage/repository"
git -C "$checkout" rev-parse HEAD > "$stage/observed"
cat "$stage/pin" "$stage/observed" > "$stage/guard-input"
"$OMEGA_OMC" < "$root/om/consumer.om" > "$stage/pincheck"
chmod 700 "$stage/pincheck"
"$stage/pincheck" < "$stage/guard-input"
# Read only the revision already frozen and accepted by the .om guard.
IFS= read -r revision < "$stage/observed"
git -C "$checkout" archive --format=tar "$revision" > "$stage/source.tar"
mkdir "$stage/source"
tar -xf "$stage/source.tar" -C "$stage/source"
verify_source "$stage/pin" "$stage" "$stage" >&2
sh "$stage/source/om/units.sh" --check
# Build only the authenticated tree: no inherited MAKEFLAGS/PHYSICS_DIR/CFLAGS/CC may
# substitute another driver client or flags (a parent make passes PHYSICS_DIR via MAKEFLAGS).
env -i PATH="$PATH" make -C "$stage/source" PHYSICS_DIR=. OUT_DIR="$stage/build" CC=cc CUDAPP_ENCODER=om all
sha256sum "$stage/source.tar" > "$stage/source.sha256"
sha256sum "$stage/pincheck" > "$stage/guard.sha256"
sha256sum "$stage/build/libcudapp.a" > "$stage/archive.sha256"
cat "$stage/source.sha256" "$stage/guard.sha256" "$stage/archive.sha256" > "$stage/hashes"
ln -s -- "$stage" "$link"
mv -T -- "$link" "$output"
trap - EXIT HUP INT TERM
printf '%s\n' "$stage"
