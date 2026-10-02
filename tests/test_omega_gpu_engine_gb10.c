/* Queued physical qualification only, through tools/chip_run.sh.
 * Never run concurrently with E1. No external timeout or forced termination.
 * A retained context parks this process to keep its driver mappings alive.
 * This is a pending experiment, not a chip qualification receipt. */
#include "omega_blackwell_submit.h"
#include "omega_blackwell_engine.h"
#include "omega_blackwell_encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void keep_uncertain_job_alive(const OmegaGpuResult *r) {
    if (!r->resource_retained) return;
    fprintf(stderr, "GPU_ENGINE: FAIL uncertain execution; resources retained, process parked; do not kill\n");
    fflush(NULL);
    for (;;) pause();
}

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--chip")) {
        puts("GPU_ENGINE: NOT_RUN (requires queued --chip execution)");
        return 2;
    }
    const size_t sizes[] = {1, 63, 64, 65, 4096, 65537};
    const size_t max = sizes[sizeof(sizes) / sizeof(sizes[0]) - 1];
    uint32_t *a = malloc(max * sizeof(*a));
    uint32_t *b = malloc(max * sizeof(*b));
    uint32_t *out = malloc(max * sizeof(*out));
    uint32_t *poison = malloc(max * sizeof(*poison));
    if (!a || !b || !out || !poison) {
        free(a); free(b); free(out); free(poison);
        puts("GPU_ENGINE: FAIL allocation");
        return 1;
    }
    for (size_t i = 0; i < max; ++i) {
        a[i] = UINT32_MAX - (uint32_t)i;
        b[i] = (uint32_t)(i * 17u + 2u);
        /* Include the old fixed poison as a legitimate sum. */
        if (i == 0) { a[i] = OMEGA_VECTOR_POISON_VALUE; b[i] = 0; }
        poison[i] = ~(a[i] + b[i]);
    }
    unsigned runs = 0;
    int failed = 0;
    for (unsigned repeat = 0; repeat < 10 && !failed; ++repeat) {
        for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
            OmegaVectorSpec spec;
            OmegaBlackwellRealization real = {.sm_architecture = OMEGA_BW_SM_ARCH_121};
            OmegaBlackwellVectorExecution info;
            if (omega_vector_spec_init(&spec, "engine_qualification", (uint32_t)sizes[s])) {
                failed = 1;
                break;
            }
            int rc = omega_blackwell_execute_vector(&spec, &real, a, b, out, &info);
            keep_uncertain_job_alive(&info.execution);
            if (rc || !info.parity_verified || info.execution.state != OMEGA_GPU_SUCCESS) {
                fprintf(stderr, "vector run %u size %zu failed rc=%d stage=%d native=%d\n",
                        repeat, sizes[s], rc, info.execution.failed_stage, info.execution.native_rc);
                failed = 1;
                break;
            }
            ++runs;
        }
    }
    /* Negative control: replace only the calibrated STG instruction with the
     * immediately following calibrated EXIT. All lanes terminate without a
     * store. Offsets are fixture locations in omega_blackwell_encoder.c.
     * Completion and release must still arrive; output production must fail. */
    uint8_t code[OMEGA_BW_VECADD_CODE_SIZE];
    size_t code_bytes = 0;
    if (!failed && (omega_blackwell_verify_encoder_fixtures() ||
                   omega_blackwell_encode_vecadd(code, sizeof(code), &code_bytes))) failed = 1;
    if (!failed) {
        memcpy(code + 0x120, code + 0x130, OMEGA_BW_INSTRUCTION_BYTES);
        OmegaGpuJob job = {.code = code, .code_bytes = code_bytes,
            .inputs = {a, b}, .output = out, .output_poison = poison,
            .word_count = 65, .timeout_ms = 5000};
        OmegaGpuResult result;
        int rc = omega_gpu_execute(&job, &result);
        keep_uncertain_job_alive(&result);
        if (rc != OMEGA_GPU_ERR_OUTPUT_NOT_WRITTEN || result.poison_count != 65 ||
            result.state != OMEGA_GPU_OUTPUT_VISIBLE || result.cleanup_rc != 0) {
            fprintf(stderr, "no-store control failed rc=%d poison=%zu state=%d\n",
                    rc, result.poison_count, result.state);
            failed = 1;
        }
    }
    free(a); free(b); free(out); free(poison);
    if (failed) { puts("GPU_ENGINE: FAIL"); return 1; }
    printf("GPU_ENGINE_CASES vector_runs=%u no_store_control=detected\n", runs);
    puts("GPU_ENGINE: PASS");
    return 0;
}
