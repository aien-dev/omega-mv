/* omega_gpu_pfa16.c: W8A16 whole-prompt prefill kernels (W-L1H). See omega_gpu_pfa16.h. */
#include "omega_gpu_pfa16.h"
#include "omega_gpu_prefill.h"
#include "omega_blackwell_codegen_tc.h"
#include "omega_blackwell_codegen_hx.h"
#include <math.h>
#include <string.h>

#define KIND_HGEMM 0x60u
#define KIND_HX 0x61u

/* host mirrors (omega_blackwell_codegen_hx.h) */
uint16_t omega_hx_bf16_rn(float f) {
    uint32_t u; memcpy(&u, &f, 4);
    return (uint16_t)((u + 0x7fffu + ((u >> 16) & 1u)) >> 16);
}
float omega_hx_swiglu(float g, float u) {
    const uint32_t kb = 0xbfb8aa3bu; float k; memcpy(&k, &kb, 4);
    float t = fminf(g * k, 125.0f), ex = exp2f(t), d = ex + 1.0f, r = 1.0f / d, o = g * r;
    return o * u;
}

static struct { uint32_t rt, ct, warps; int gmutant, xmutant; } h;
void omega_gpu_pf_hgemm_set_shape(uint32_t rt, uint32_t ct, uint32_t warps) { h.rt = rt; h.ct = ct; h.warps = warps; }
void omega_gpu_pf_hgemm_test_set_mutant(int m) { h.gmutant = m; }
void omega_gpu_pf_hx_test_set_mutant(int m) { h.xmutant = m; }
static uint32_t HRT(void) { return h.rt ? h.rt : 4u; }
uint32_t omega_gpu_pf_hgemm_bm(void) { return 16u * HRT(); }

static int gen_hgemm(const OmegaPfKey *key, void *prog) { OmegaTcGemmSpec s; memcpy(&s, key->words, sizeof s); return omega_blackwell_codegen_tcgemm_prog(&s, prog); }
static int gen_hx(const OmegaPfKey *key, void *prog) { OmegaHxSpec s; memcpy(&s, key->words, sizeof s); return omega_blackwell_codegen_hx_prog(&s, prog); }

int omega_gpu_pf_hgemm(uint32_t n, uint32_t k, uint64_t tiles_va, uint32_t ntiles, uint64_t rowidx_va, uint64_t xh_va, uint64_t y_va, uint64_t *ns) {
    if (!tiles_va || !rowidx_va || !xh_va || !y_va) return OMEGA_GPU_PF_BAD_ARGS; /* direct rows: pass an identity row index */
    if (ntiles == 0) return OMEGA_GPU_PF_OK;
    if (ntiles > 65535u) return OMEGA_GPU_PF_TOO_LARGE;
    OmegaTcGemmSpec s; memset(&s, 0, sizeof s);
    s.k = k; s.rt = HRT(); s.ct = h.ct ? h.ct : (n % 128u == 0 ? 2u : 1u); s.warps = h.warps ? h.warps : 8u; s.mutant = h.gmutant;
    if (!omega_tcgemm_spec_ok(&s)) return OMEGA_GPU_PF_BAD_ARGS;
    const uint32_t span = omega_tcgemm_span(&s);
    if (n == 0 || n % span || n % 128u) return OMEGA_GPU_PF_BAD_ARGS;
    OmegaPfKey key; memset(&key, 0, sizeof key); key.kind = KIND_HGEMM; memcpy(key.words, &s, sizeof s);
    _Static_assert(sizeof(OmegaTcGemmSpec) <= sizeof key.words, "spec fits the key");
    uint32_t args[OMEGA_TCGEMM_ARGS_WORDS];
    const uint64_t va[4] = { tiles_va, rowidx_va, xh_va, y_va };
    for (int i = 0; i < 4; i++) { args[2 * i] = (uint32_t)va[i]; args[2 * i + 1] = (uint32_t)(va[i] >> 32); }
    args[8] = n;
    omega_gpu_session_lock();
    int rc = omega_gpu_pf_open_locked();
    OmegaPfKernel kern;
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_kernel_locked(&key, gen_hgemm, &kern);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_launch_locked(&kern, 32u * s.warps, n / span, ntiles, omega_tcgemm_shared_bytes(&s), args, OMEGA_TCGEMM_ARGS_WORDS, ns);
    omega_gpu_session_unlock();
    return rc;
}

static int hx_launch(uint32_t mode, uint32_t k, uint32_t in_type, uint32_t rows, uint64_t in_va, uint64_t out_va, uint64_t *ns) {
    if (!in_va || !out_va) return OMEGA_GPU_PF_BAD_ARGS;
    if (rows == 0) return OMEGA_GPU_PF_OK;
    OmegaHxSpec s; memset(&s, 0, sizeof s); s.mode = mode; s.k = k; s.in_type = in_type; s.mutant = h.xmutant;
    if (!omega_hx_spec_ok(&s)) return OMEGA_GPU_PF_BAD_ARGS;
    OmegaPfKey key; memset(&key, 0, sizeof key); key.kind = KIND_HX; memcpy(key.words, &s, sizeof s);
    uint32_t args[OMEGA_HX_ARGS_WORDS] = { (uint32_t)in_va, (uint32_t)(in_va >> 32), (uint32_t)out_va, (uint32_t)(out_va >> 32) };
    omega_gpu_session_lock();
    int rc = omega_gpu_pf_open_locked();
    OmegaPfKernel kern;
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_kernel_locked(&key, gen_hx, &kern);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_launch_locked(&kern, k / 4u, rows, 1, 0, args, OMEGA_HX_ARGS_WORDS, ns);
    omega_gpu_session_unlock();
    return rc;
}
int omega_gpu_pf_hconv(uint32_t k, uint32_t in_type, uint32_t rows, uint64_t x_va, uint64_t xh_va, uint64_t *ns) {
    if (in_type > OMEGA_GPU_PF_HX_BF16) return OMEGA_GPU_PF_BAD_ARGS;
    return hx_launch(OMEGA_HX_CONVERT, k, in_type, rows, x_va, xh_va, ns);
}
int omega_gpu_pf_hswiglu(uint32_t i, uint32_t rows, uint64_t gu_va, uint64_t act_va, uint64_t *ns) {
    return hx_launch(OMEGA_HX_SWIGLU, i, OMEGA_HX_IN_F32, rows, gu_va, act_va, ns);
}
