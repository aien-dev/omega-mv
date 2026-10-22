/* Omega #522: host-only, post-hoc qualification/replay boundary.
 * No GPU API, runtime selector, prediction, or cost composition. */
#ifndef TURING_GPU_REPLAY_H
#define TURING_GPU_REPLAY_H
#include <stddef.h>
#include <stdint.h>
#define TGR_MAX 16
#define TGR_BINDINGS 12
/* Every digest is over a canonical, externally declared identity. In order:
 * contract, Omega source, Physics source, machine, driver, toolchain/config,
 * model/input quality, exact shape+batch+sequence, precision, variant family,
 * residency, pressure. No nearest-shape interpolation. */
typedef struct { uint8_t id[TGR_BINDINGS][32]; uint64_t reuse; } tgr_binding;
typedef struct {
    tgr_binding binding;
    uint8_t binary[32];
    uint64_t measured_at, total_ns, submit_host_ns, device_ns;
    uint64_t resident_bytes, scratch_bytes;
    uint32_t registers, samples;
} tgr_observation;
typedef struct {
    uint8_t id[32], receipt_sha256[32], binary[32];
    const uint8_t *bytes;
    size_t size;
} tgr_candidate;
typedef struct {
    tgr_binding binding;
    uint64_t now, max_age, max_resident_bytes, max_scratch_bytes;
    uint32_t max_registers;
    size_t incumbent;
} tgr_query;
/* TRUST BOUNDARY: a verifier must authenticate/qualify the exact byte snapshot
 * and decode its normalized observation. Return 0 ONLY for independently
 * qualified clean-source PASS evidence, including correctness/resource,
 * platform, measurement protocol and all binding fields. Structural receipt
 * checks, self-asserted VERIFIED, or a matching digest alone are insufficient.
 * No real GPU verifier is supplied here. A NULL verifier fails closed.
 * Verifiers must not mutate the snapshot or perform execution. */
typedef int (*tgr_verifier)(const uint8_t *, size_t, tgr_observation *, void *);
typedef enum {
    TGR_ELIGIBLE, TGR_DIGEST, TGR_UNQUALIFIED, TGR_BINDING, TGR_BINARY,
    TGR_STALE, TGR_RESOURCE, TGR_COST, TGR_INVALID
} tgr_reason;
typedef struct {
    int chosen; /* original candidate index, -1 without qualified incumbent */
    int fallback;
    tgr_reason reason[TGR_MAX];
    uint64_t total_ns[TGR_MAX]; /* zero means UNKNOWN, never passed to ranking */
} tgr_decision;
/* Fixed declared candidate order, incumbent wins exact cost ties; otherwise
 * existing turing_rank_min_cost's lowest-index tie. Returns 0 for a qualified
 * comparison/fallback, 1 without qualified incumbent, -1 malformed query/set.
 * One receipt per candidate per replay; callers pin this set and query. */
int tgr_replay(const tgr_query *, const tgr_candidate *, size_t, tgr_verifier, void *, tgr_decision *);
#endif
