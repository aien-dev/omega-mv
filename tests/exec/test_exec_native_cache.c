/* omega_exec_native_f3 keeps executable pages for recently seen realizations.
 * Contract: same bytes -> same mapping (hit), different bytes -> new mapping (miss),
 * results identical to a fresh mapping, eviction frees without breaking later calls. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "omega_program.h"
#include "omega_exec.h"

static int fails;
#define CHECK(c, ...) do{ if(!(c)){ fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } }while(0)

static void mk(OmegaProgram *p, OpCode op, uint64_t imm) {
    if (omega_program_build_unary_op(p, "f", op, imm) != 0 || omega_program_realize(p) != 0) { puts("build fail"); exit(2); }
}

int main(void) {
    OmegaExecCacheStats s0, s1;
    OmegaProgram a, b;
    uint64_t y = 0;
    mk(&a, OP_ADD, 5);
    mk(&b, OP_MUL, 7);
    omega_exec_native_cache_reset();
    omega_exec_native_cache_stats(&s0);
    CHECK(s0.hits == 0 && s0.misses == 0 && s0.resident == 0, "reset clears counters");

    CHECK(omega_exec_native_f3(&a.realization, 10, 0, 0, &y) == 0 && y == 15, "a first call result");
    CHECK(omega_exec_native_f3(&a.realization, 11, 0, 0, &y) == 0 && y == 16, "a second call result");
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.misses == 1 && s1.hits == 1 && s1.resident == 1, "same bytes: 1 miss then 1 hit (got m=%llu h=%llu r=%llu)",
          (unsigned long long)s1.misses, (unsigned long long)s1.hits, (unsigned long long)s1.resident);

    CHECK(omega_exec_native_f3(&b.realization, 3, 0, 0, &y) == 0 && y == 21, "b result");
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.misses == 2 && s1.resident == 2, "different bytes: a miss, 2 resident");

    /* a copy with identical bytes but a different RealizationObject address still hits */
    RealizationObject copy = a.realization;
    CHECK(omega_exec_native_f3(&copy, 1, 0, 0, &y) == 0 && y == 6, "copy result");
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.hits == 2, "identity is the bytes, not the pointer");

    /* Invalidation. A program of the same length whose bytes differ (ADD #6 vs ADD #5) must map fresh
     * pages and compute its own result; a cache keyed by length or address would return a + 5. */
    OmegaProgram a6;
    mk(&a6, OP_ADD, 6);
    CHECK(a6.realization.code_len == a.realization.code_len &&
          memcmp(a6.realization.code_bytes, a.realization.code_bytes, a.realization.code_len) != 0,
          "fixture: ADD #6 has the same length as ADD #5 and different bytes");
    omega_exec_native_cache_stats(&s0);
    CHECK(omega_exec_native_f3(&a6.realization, 5, 0, 0, &y) == 0 && y == 11, "same length, other bytes: own result (got %llu)",
          (unsigned long long)y);
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.hits == s0.hits && s1.misses == s0.misses + 1, "same length, other bytes: a miss, not a hit");

    /* The same RealizationObject rewritten in place (same address, same length, new bytes) must not run
     * the stale mapping; rewriting it back to the original bytes hits the mapping still resident. */
    RealizationObject *ra = &a.realization;
    mk(&a, OP_ADD, 9);
    CHECK(ra == &a.realization && omega_exec_native_f3(&a.realization, 5, 0, 0, &y) == 0 && y == 14,
          "rewritten in place: new code runs (got %llu, stale mapping gives 10)", (unsigned long long)y);
    mk(&a, OP_ADD, 5);
    omega_exec_native_cache_stats(&s0);
    CHECK(omega_exec_native_f3(&a.realization, 5, 0, 0, &y) == 0 && y == 10, "rewritten back: original result");
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.hits == s0.hits + 1 && s1.misses == s0.misses, "rewritten back: hit on the resident mapping");

    /* fill past capacity: every program still computes right after eviction */
    omega_exec_native_cache_reset();
    for (unsigned i = 0; i < 2 * OMEGA_EXEC_CACHE_SLOTS + 3; i++) {
        OmegaProgram p; mk(&p, OP_ADD, 1000 + i);
        CHECK(omega_exec_native_f3(&p.realization, i, 0, 0, &y) == 0 && y == 1000 + 2ull * i, "evict loop %u", i);
    }
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.resident == OMEGA_EXEC_CACHE_SLOTS, "resident capped at %u (got %llu)", OMEGA_EXEC_CACHE_SLOTS, (unsigned long long)s1.resident);
    CHECK(omega_exec_native_f3(&a.realization, 10, 0, 0, &y) == 0 && y == 15, "a still right after eviction");

    /* LRU victim: fill every slot, touch the oldest, add one more. The second oldest is evicted and
     * the touched one stays resident. */
    static OmegaProgram lru[OMEGA_EXEC_CACHE_SLOTS + 1];
    omega_exec_native_cache_reset();
    for (unsigned i = 0; i <= OMEGA_EXEC_CACHE_SLOTS; i++) mk(&lru[i], OP_ADD, 2000 + i);
    for (unsigned i = 0; i < OMEGA_EXEC_CACHE_SLOTS; i++)
        CHECK(omega_exec_native_f3(&lru[i].realization, 1, 0, 0, &y) == 0 && y == 2001ull + i, "lru fill %u", i);
    CHECK(omega_exec_native_f3(&lru[0].realization, 1, 0, 0, &y) == 0 && y == 2001, "lru touch oldest");
    CHECK(omega_exec_native_f3(&lru[OMEGA_EXEC_CACHE_SLOTS].realization, 1, 0, 0, &y) == 0 && y == 2001ull + OMEGA_EXEC_CACHE_SLOTS,
          "lru one past capacity");
    omega_exec_native_cache_stats(&s0);
    CHECK(s0.evictions == 1 && s0.resident == OMEGA_EXEC_CACHE_SLOTS, "lru: exactly one eviction (got %llu)",
          (unsigned long long)s0.evictions);
    CHECK(omega_exec_native_f3(&lru[0].realization, 1, 0, 0, &y) == 0 && y == 2001, "lru touched entry result");
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.hits == s0.hits + 1 && s1.misses == s0.misses, "lru: the recently touched entry is still resident");
    CHECK(omega_exec_native_f3(&lru[1].realization, 1, 0, 0, &y) == 0 && y == 2002, "lru evicted entry result");
    omega_exec_native_cache_stats(&s0);
    CHECK(s0.misses == s1.misses + 1 && s0.hits == s1.hits, "lru: the least recently used entry was the one evicted");

    printf("EXEC_NATIVE_CACHE: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
