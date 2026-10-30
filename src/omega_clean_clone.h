#ifndef OMEGA_CLEAN_CLONE_H
#define OMEGA_CLEAN_CLONE_H

#include <stdbool.h>

/* Clean-clone reproduction shared by the M18 and M19 CLEAN_CLONE gates.
 *
 * Clones the repository this process runs in (cwd, the repo root under make)
 * at its exact HEAD into a fresh mkdtemp directory /tmp/omega_clean_<tag>_XXXXXX,
 * builds it from scratch (make clean, then make -j) against the pinned
 * PHYSICS_DIR this process resolves, and runs
 * `OMEGA_IN_CLEAN_CLONE=1 ./build/omegatool <gates_flag>` inside the clone.
 *
 * The clone always builds into its own default build/ directory: OUT_DIR,
 * MAKEFLAGS, MFLAGS and MAKELEVEL inherited from the calling make (for example
 * tests/gates/run.sh exports OUT_DIR=build/step-<step>) are removed, so the
 * nested build cannot land somewhere the run step does not look (omega #511
 * moved the gate builds into per-step OUT_DIRs, and the clone then ran a
 * ./build/omegatool that was never built).
 *
 * Refuses a dirty source tree. Returns true only when every step exits 0 and
 * the built clone's HEAD equals the source HEAD; out_sha (41 bytes) then holds
 * that revision. On failure the failing step, its exit status, its log path
 * and the last lines of that log are printed to stderr and the clone
 * directory is kept for inspection; on success it is removed. */
bool omega_clean_clone_run(const char *tag, const char *gates_flag, char out_sha[41]);

#endif
