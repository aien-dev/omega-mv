/* Oracle entry only; all original eight sim_fused checks are unchanged. */
#define main original_unused_main
#include "tests/gpu_fp8_api_test.c"
#undef main
int main(void) {
    sim_fused();
    printf("L137 original fusion simulator: checks=%d failed=%d\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
