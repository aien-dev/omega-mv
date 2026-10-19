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

    /* a one-byte change in the code is a different program: must not hit */
    RealizationObject tweaked = a.realization;
    tweaked.code_bytes[0] ^= 0x01;
    omega_exec_native_cache_stats(&s0);
    (void)omega_exec_native_f3(&tweaked, 1, 0, 0, &y);   /* may fault-free execute garbage or refuse; only the counter matters */
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.hits == s0.hits, "changed byte never hits");

    /* fill past capacity: every program still computes right after eviction */
    omega_exec_native_cache_reset();
    for (unsigned i = 0; i < 2 * OMEGA_EXEC_CACHE_SLOTS + 3; i++) {
        OmegaProgram p; mk(&p, OP_ADD, 1000 + i);
        CHECK(omega_exec_native_f3(&p.realization, i, 0, 0, &y) == 0 && y == 1000 + 2ull * i, "evict loop %u", i);
    }
    omega_exec_native_cache_stats(&s1);
    CHECK(s1.resident == OMEGA_EXEC_CACHE_SLOTS, "resident capped at %u (got %llu)", OMEGA_EXEC_CACHE_SLOTS, (unsigned long long)s1.resident);
    CHECK(omega_exec_native_f3(&a.realization, 10, 0, 0, &y) == 0 && y == 15, "a still right after eviction");

    printf("EXEC_NATIVE_CACHE: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
