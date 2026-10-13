/* fp4_convert: FP8 e4m3 + bf16 block-128 checkpoint -> OFP4v1 expert store (OWN-30B cut L3). No Python.
 * Usage: fp4_convert <checkpoint_dir> <out_file> [layers] [threads-via-OMP_NUM_THREADS]
 * Per expert, gate rows then up rows form one 1536 x 2048 matrix (one tensor scale), down is 2048 x 768 (one tensor scale).
 * Store (little endian): header 4096 bytes "AFP4v1\0\0" + u32 {layers, experts, gu_n, gu_k, dn_n, dn_k} + u64 source digest note;
 * then per layer (layer stride = LS): gu_w[E][gu_n*gu_k/2], gu_s[E][gu_n*gu_k/16], gu_t[E] f32, dn_w[E][dn_n*dn_k/2], dn_s[E][dn_n*dn_k/16], dn_t[E] f32.
 * Also writes per-layer error statistics to stdout (weight relative RMS error per projection, mean over experts). */
#include "fp4_st.h"
#include "omega_gpu_fp4_api.h"
#include <math.h>
#include <omp.h>

enum { E = 128, GU_N = 1536, GU_K = 2048, DN_N = 2048, DN_K = 768, HDR = 4096 };
#define GU_W ((size_t)GU_N * GU_K / 2)
#define GU_S ((size_t)GU_N * GU_K / 16)
#define DN_W ((size_t)DN_N * DN_K / 2)
#define DN_S ((size_t)DN_N * DN_K / 16)
#define LAYER_STRIDE ((size_t)E * (GU_W + GU_S + 4 + DN_W + DN_S + 4))

/* dequantise one FP8 weight matrix [rows][cols] with bf16 scales [rows/128][cols/128] into dst (row stride dst_stride) */
static void fp8_dequant(const StTensor *w, const StTensor *s, float *dst, size_t dst_stride) {
    uint32_t rows = w->shape[0], cols = w->shape[1], sb = (cols + 127) / 128;
    const uint16_t *sc = (const uint16_t *)s->data;
    for (uint32_t r = 0; r < rows; r++) for (uint32_t c = 0; c < cols; c++)
        dst[(size_t)r * dst_stride + c] = st_e4m3(w->data[(size_t)r * cols + c]) * st_bf16(sc[(r / 128) * sb + c / 128]);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: fp4_convert <checkpoint_dir> <out_file> [layers]\n"); return 2; }
    int layers = argc > 3 ? atoi(argv[3]) : 48;
    StIndex ix = { 0 }; if (st_open_dir(&ix, argv[1])) return 1;
    FILE *out = fopen(argv[2], "wb"); if (!out) { perror(argv[2]); return 1; }
    uint8_t hdr[HDR] = { 0 }; memcpy(hdr, "AFP4v1\0\0", 8);
    uint32_t dims[6] = { (uint32_t)layers, E, GU_N, GU_K, DN_N, DN_K }; memcpy(hdr + 8, dims, sizeof dims);
    fwrite(hdr, 1, HDR, out);
    uint8_t *buf = malloc(LAYER_STRIDE); if (!buf) return 1;
    for (int L = 0; L < layers; L++) {
        double gu_err[E], dn_err[E], gu_den[E], dn_den[E];
        uint8_t *gw = buf, *gs = gw + (size_t)E * GU_W; float *gt = (float *)(gs + (size_t)E * GU_S);
        uint8_t *dw = (uint8_t *)(gt + E), *ds = dw + (size_t)E * DN_W; float *dt = (float *)(ds + (size_t)E * DN_S);
        int bad = 0;
        #pragma omp parallel
        {
            float *mat = malloc((size_t)GU_N * GU_K * sizeof(float)), *dq = malloc((size_t)GU_N * GU_K * sizeof(float));
            #pragma omp for schedule(dynamic, 1)
            for (int e = 0; e < E; e++) {
                char nm[160]; const StTensor *w, *s;
                for (int proj = 0; proj < 2; proj++) {
                    static const char *pn[3] = { "gate_proj", "up_proj", "down_proj" };
                    snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.%s.weight", L, e, pn[proj]); w = st_get(&ix, nm);
                    snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.%s.weight_scale_inv", L, e, pn[proj]); s = st_get(&ix, nm);
                    if (!w || !s) { bad = 1; continue; }
                    fp8_dequant(w, s, mat + (size_t)proj * 768 * GU_K, GU_K);
                }
                if (bad) continue;
                gt[e] = omega_fp4_quantize(mat, GU_N, GU_K, gw + (size_t)e * GU_W, gs + (size_t)e * GU_S);
                { float *src = malloc((size_t)GU_N * GU_K * 4); memcpy(src, mat, (size_t)GU_N * GU_K * 4);
                  omega_fp4_dequantize(gw + (size_t)e * GU_W, gs + (size_t)e * GU_S, gt[e], GU_N, GU_K, dq);
                  double n = 0, d = 0; for (size_t i = 0; i < (size_t)GU_N * GU_K; i++) { double df = (double)src[i] - dq[i]; n += df * df; d += (double)src[i] * src[i]; }
                  gu_err[e] = n; gu_den[e] = d; free(src); }
                snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.down_proj.weight", L, e); w = st_get(&ix, nm);
                snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.down_proj.weight_scale_inv", L, e); s = st_get(&ix, nm);
                fp8_dequant(w, s, mat, DN_K);
                dt[e] = omega_fp4_quantize(mat, DN_N, DN_K, dw + (size_t)e * DN_W, ds + (size_t)e * DN_S);
                omega_fp4_dequantize(dw + (size_t)e * DN_W, ds + (size_t)e * DN_S, dt[e], DN_N, DN_K, dq);
                { double n = 0, d = 0; for (size_t i = 0; i < (size_t)DN_N * DN_K; i++) { double df = (double)mat[i] - dq[i]; n += df * df; d += (double)mat[i] * mat[i]; }
                  dn_err[e] = n; dn_den[e] = d; }
            }
            free(mat); free(dq);
        }
        if (bad) { fprintf(stderr, "layer %d: missing tensors\n", L); return 1; }
        if (omega_fp4_scan_scales(gs, (size_t)E * GU_S) || omega_fp4_scan_scales(ds, (size_t)E * DN_S)) { fprintf(stderr, "layer %d: illegal scale byte\n", L); return 1; }
        fwrite(buf, 1, LAYER_STRIDE, out);
        double sg = 0, sd = 0, mg = 0, md = 0;
        for (int e = 0; e < E; e++) { double a = sqrt(gu_err[e] / gu_den[e]), b = sqrt(dn_err[e] / dn_den[e]); sg += a; sd += b; if (a > mg) mg = a; if (b > md) md = b; }
        printf("layer %2d gate_up rel-RMS mean %.4f max %.4f | down rel-RMS mean %.4f max %.4f\n", L, sg / E, mg, sd / E, md);
        fflush(stdout);
    }
    fclose(out);
    return 0;
}
