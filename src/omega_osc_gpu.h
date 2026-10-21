/* omega_osc_gpu.h -- the GPU back end for Omegese kernels (L386, C1 of docs/OMEGA.md).
 *
 * Reads one kernel of a decoded OSC IR v6 unit (src/compiler/osc_ir.h) and builds sm_121 machine
 * code through the existing builder: BlackwellIRProgram (src/omega_blackwell_codegen.h), the
 * emitter helpers of src/omega_bw_em.h, omega_bw_regalloc_solve and omega_bw_encode_program. There
 * is no second encoder. The meaning of every operation is docs/omegese/GPU.md; section 11 there
 * states the argument layout and the code shape this file produces.
 *
 * The unit is expected to have been accepted by checker2 first (the driver runs it on the same
 * bytes). This file does not repeat checker2's proofs (races, types, loop bounds); it refuses, with
 * a message, any shape it does not know how to lower, so it never emits code for a case it was not
 * written for. */
#ifndef OMEGA_OSC_GPU_H
#define OMEGA_OSC_GPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "compiler/osc_ir.h"
#include "omega_blackwell_codegen.h"

#define OSC_GPU_ARG_BASE 0x380u      /* constant bank 0 byte offset of the first argument word */
#define OSC_GPU_MAX_ARG_WORDS 32u    /* OmegaGpuLaunch.args */
#define OSC_GPU_STATUS_WORDS 16u     /* hidden status buffer: word N = 1 when a thread trapped with code N */

typedef enum { OSC_GPU_P_VIEW = 1, OSC_GPU_P_EDIT, OSC_GPU_P_U32, OSC_GPU_P_F32, OSC_GPU_P_BOOL } OscGpuParamKind;

typedef struct {
    uint8_t kind;   /* OscGpuParamKind */
    uint8_t elem;   /* buffers: OSC_T_U32 or OSC_T_F32 */
    uint16_t off;   /* byte offset from OSC_GPU_ARG_BASE: a buffer's 64-bit address, then its u32 length at off + 8 */
} OscGpuParam;

typedef struct {
    char name[OSC_NAME_MAX];
    uint16_t threads;           /* from the kernel line */
    uint32_t shared_bytes;      /* every shared array, 4 bytes per element */
    uint8_t nparams;            /* source parameters (a buffer is one, its length register is not counted) */
    OscGpuParam params[OSC_MAX_KPARAMS];
    uint16_t status_off, blocks_off, batches_off; /* hidden arguments, byte offsets from OSC_GPU_ARG_BASE */
    uint16_t arg_words;         /* argument words the launcher writes (<= OSC_GPU_MAX_ARG_WORDS) */
    bool reads_block, reads_batch;
    uint8_t *code;              /* encoded sm_121 instructions, 16 bytes each (malloc) */
    size_t code_size;
    uint32_t insn_count, gpr_count;
    uint8_t code_sha256[32];
    uint32_t regions, region_bars; /* reconvergence regions opened, barrier registers used */
    BlackwellIRProgram *prog;   /* the allocated program (for the host simulator); malloc */
} OscGpuKernel;

/* Builds kernel function fn of u. 0 on success; -1 with a message in err when the kernel is refused. */
int osc_gpu_build(const OscUnit *u, unsigned fn, OscGpuKernel *k, char *err, size_t errn);
void osc_gpu_free(OscGpuKernel *k);

/* The unsigned division the back end emits for a constant divisor d != 0, run on the host with the
 * same steps (multiply-high by the magic number, shifts, add). Tests compare it with n / d. */
uint32_t osc_gpu_udiv_model(uint32_t n, uint32_t d);

/* tests only: a nonzero value makes the build break one rule on purpose (see omega_osc_gpu.c) */
extern int osc_gpu_mutant;

#endif /* OMEGA_OSC_GPU_H */
