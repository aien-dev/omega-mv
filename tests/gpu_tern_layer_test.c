/* gpu_tern_layer_test.c (L284 own-fp4, 2026-10-20): a trained ternary layer through Omega's native W2A8 GEMV on the chip.
 * Input file (written by aien-train streams/own-fp4/export_tern.py): "TRN1", u32 n, u32 k, u32 m, then int8 W[n][k] in
 * {-1,0,+1}, int8 X[m][k] (real activations of the trained model, absmax-quantized), int32 Yref[m][n] = exact W x on the
 * host. Each of the m vectors goes through omega_gpu_tern_gemv_i32 (caller holds flock /tmp/aien-gb10.lock); every output
 * must equal Yref exactly. Verdict last: "GPU_TERN_LAYER: PASS|FAIL". */
#include "omega_gpu_tern_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s layer.trn\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb"); char mg[4]; uint32_t h[3];
    if (!f || fread(mg, 1, 4, f) != 4 || memcmp(mg, "TRN1", 4) || fread(h, 4, 3, f) != 3) { printf("bad file\n"); return 2; }
    uint32_t n = h[0], k = h[1], m = h[2];
    int8_t *w = malloc((size_t)n * k), *x = malloc((size_t)m * k); int32_t *yr = malloc((size_t)m * n * 4), *y = malloc((size_t)n * 4);
    if (!w || !x || !yr || !y || fread(w, 1, (size_t)n * k, f) != (size_t)n * k || fread(x, 1, (size_t)m * k, f) != (size_t)m * k ||
        fread(yr, 4, (size_t)m * n, f) != (size_t)m * n) { printf("short file\n"); return 2; }
    fclose(f);
    size_t nz = 0; for (size_t i = 0; i < (size_t)n * k; i++) nz += w[i] != 0;
    printf("layer %ux%u, %u activation vectors, nonzero weights %.4f\n", n, k, m, (double)nz / ((double)n * k));
    OmegaGpuTernTensor *t = NULL; int rc = omega_gpu_tern_tensor_upload(n, k, w, NULL, &t);
    if (rc != 0) { printf("upload rc %d\n", rc); printf("GPU_TERN_LAYER: FAIL\n"); return 1; }
    size_t bad = 0; uint64_t ns = 0; OmegaGpuTernInfo info;
    for (uint32_t j = 0; j < m; j++) {
        memset(&info, 0, sizeof info);
        if ((rc = omega_gpu_tern_gemv_i32(t, x + (size_t)j * k, y, &info)) != 0) { printf("gemv rc %d on vector %u\n", rc, j); bad++; break; }
        for (uint32_t r = 0; r < n; r++) if (y[r] != yr[(size_t)j * n + r]) { if (bad < 5) printf("vector %u row %u: chip %d host %d\n", j, r, y[r], yr[(size_t)j * n + r]); bad++; }
        ns += info.elapsed_ns;
    }
    printf("chip %s sm %u: %zu mismatches over %u x %u outputs; packed weight bytes %llu; mean chip time %.1f us\n", info.target_chip,
           info.sm_architecture, bad, m, n, (unsigned long long)info.weight_bytes, m ? ns / 1e3 / m : 0.0);
    omega_gpu_tern_tensor_free(t);
    printf("GPU_TERN_LAYER: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
