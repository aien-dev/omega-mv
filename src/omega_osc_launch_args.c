/* omega_osc_launch_args.c -- every check of an Omegese kernel launch and the argument words it writes
 * (omega_kernel_launch_words, see omega_osc_launch.h and docs/omegese/GPU.md s11). No device calls, so
 * the host tests (tests/osc_gpu) link this file alone and prove each refusal. */
#include "omega_osc_launch.h"
#include "sha256.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int refuse(char *err, size_t errn, const char *fmt, ...) {
    if (err && errn) { va_list ap; va_start(ap, fmt); vsnprintf(err, errn, fmt, ap); va_end(ap); }
    return OSC_LAUNCH_REFUSED;
}

static const char *kind_name(unsigned kind) {
    switch (kind) {
    case OSC_GPU_P_VIEW: return "view"; case OSC_GPU_P_EDIT: return "edit"; case OSC_GPU_P_U32: return "uint";
    case OSC_GPU_P_F32: return "float"; case OSC_GPU_P_BOOL: return "bool"; default: return "unknown";
    }
}

static bool is_buf(unsigned kind) { return kind == OSC_GPU_P_VIEW || kind == OSC_GPU_P_EDIT; }

/* Checks a launch and fills the argument words. Exported for the host tests (no device needed). */
int omega_kernel_launch_words(const OscGpuKernel *k, const OmegaGpuMem *code, const OscGpuArg *args, unsigned nargs,
                              uint32_t blocks, uint32_t batches, uint64_t status_va,
                              uint32_t words[OSC_GPU_MAX_ARG_WORDS], char *err, size_t errn) {
    if (!k || !k->code || !code || !words || (nargs && !args)) return refuse(err, errn, "missing kernel, code or arguments");
    /* the receipt: the bytes in the code buffer are the bytes the build hashed */
    if (code->kind != OMEGA_GPU_HANDLE_BUF_CODE || !code->cpu || !code->va || code->size < k->code_size)
        return refuse(err, errn, "%s: code buffer is not a loaded code allocation of %zu bytes", k->name, k->code_size);
    uint8_t h[32];
    sha256_hash((const uint8_t *)code->cpu, k->code_size, h);
    if (memcmp(h, k->code_sha256, 32) != 0) return refuse(err, errn, "%s: code in the buffer does not match the receipt", k->name);
    if (nargs != k->nparams) return refuse(err, errn, "%s takes %u arguments, the launch gives %u", k->name, k->nparams, nargs);
    if (blocks == 0 || batches == 0) return refuse(err, errn, "%s: zero blocks or batches", k->name);
    if (blocks > 1 && !k->reads_block) return refuse(err, errn, "%s never reads block; launch with 1 block, not %u", k->name, blocks);
    if (batches > 1 && !k->reads_batch) return refuse(err, errn, "%s never reads batch; launch with 1 batch, not %u", k->name, batches);
    if (blocks > 0x7fffffffu || batches > 65535u) return refuse(err, errn, "%s: grid %u x %u past the chip's limits", k->name, blocks, batches);
    if (k->arg_words > OSC_GPU_MAX_ARG_WORDS || k->status_off + 8u > 4u * k->arg_words || k->batches_off + 4u > 4u * k->arg_words)
        return refuse(err, errn, "%s: argument layout past %u words", k->name, OSC_GPU_MAX_ARG_WORDS);
    memset(words, 0, OSC_GPU_MAX_ARG_WORDS * sizeof words[0]);
    for (unsigned i = 0; i < nargs; i++) {
        const OscGpuParam *p = &k->params[i];
        const OscGpuArg *a = &args[i];
        if (a->kind != p->kind)
            return refuse(err, errn, "%s argument %u: the kernel line says %s, the launch gives %s", k->name, i, kind_name(p->kind), kind_name(a->kind));
        unsigned w = p->off / 4u;
        if (is_buf(p->kind)) {
            const OmegaGpuMem *m = a->mem;
            if (!m || m->kind != OMEGA_GPU_HANDLE_BUF_DATA || !m->va || !m->size)
                return refuse(err, errn, "%s argument %u: not a session data allocation", k->name, i);
            uint64_t bytes = 4ull * a->len;
            if (a->byte_off % 4u || a->byte_off > m->size || bytes > m->size - a->byte_off)
                return refuse(err, errn, "%s argument %u: %u elements at byte %llu are not inside its allocation of %llu bytes",
                              k->name, i, a->len, (unsigned long long)a->byte_off, (unsigned long long)m->size);
            uint64_t va = m->va + a->byte_off;
            words[w] = (uint32_t)va; words[w + 1] = (uint32_t)(va >> 32); words[w + 2] = a->len;
        } else {
            if (p->kind == OSC_GPU_P_BOOL && a->word > 1u) return refuse(err, errn, "%s argument %u: a bool is 0 or 1, not %u", k->name, i, a->word);
            words[w] = a->word;
        }
    }
    /* no edit buffer may share a byte with any other buffer */
    for (unsigned i = 0; i < nargs; i++) {
        if (!is_buf(k->params[i].kind) || !args[i].len) continue;
        uint64_t s0 = args[i].mem->va + args[i].byte_off, e0 = s0 + 4ull * args[i].len;
        for (unsigned j = i + 1; j < nargs; j++) {
            if (!is_buf(k->params[j].kind) || !args[j].len) continue;
            if (k->params[i].kind != OSC_GPU_P_EDIT && k->params[j].kind != OSC_GPU_P_EDIT) continue;
            uint64_t s1 = args[j].mem->va + args[j].byte_off, e1 = s1 + 4ull * args[j].len;
            if (s0 < e1 && s1 < e0) return refuse(err, errn, "%s arguments %u and %u overlap and one is an edit buffer", k->name, i, j);
        }
    }
    unsigned sw = k->status_off / 4u;
    words[sw] = (uint32_t)status_va; words[sw + 1] = (uint32_t)(status_va >> 32);
    words[k->blocks_off / 4u] = blocks;
    words[k->batches_off / 4u] = batches;
    return OSC_LAUNCH_OK;
}

