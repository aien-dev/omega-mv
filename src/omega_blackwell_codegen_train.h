/* omega_blackwell_codegen_train.h: on-chip training probe (aien-train L279, 2026-10-20).
 *
 * The question it answers: can a training step (forward, backward, weight update) stay on the math cores, with the
 * weights never leaving the chip, and what does that buy against the same steps done the normal way (weights read
 * from and written back to memory every step)?
 *
 * The model is one linear layer y = W x with no bias, trained by plain SGD on squared error, one sample per step
 * (the LMS rule): e = y - y*, W <- W - lr * e * x^T, loss += e^2. Each thread owns one output row of W (`in`
 * weights, FP32) and keeps it in registers; ctas * threads rows in all. Rows are independent, so no thread ever
 * needs another thread's weights and no cross-SM traffic exists.
 *
 * Per step a thread reads the sample's `in` inputs (the same for every thread) and its own target y*, both from
 * the data buffers, and nothing else. `batch` = 0 (read once): W is loaded once before the first step and stored
 * once after the last. `batch` = b > 0 (streamed): W is loaded, used for b consecutive samples, and stored again;
 * b = 1 is the normal way at its most memory-hungry, larger b is a micro-batch of b samples per weight read.
 * `ivall` = 1 invalidates the SM's L1 before every weight load (CCTL.IVALL), so a reload cannot be served from
 * L1 and must come from L2 (cached buffer) or memory (uncached buffer). The arithmetic is the same instruction
 * sequence in every mode, so final weights and losses must be bit-identical, and identical to a host fmaf()
 * reference.
 *
 * Layouts (FP32): W chunk q (weights 4q..4q+3) of row g at W + (q * rows + g) * 16 (one LDG.128 per chunk, a warp
 * reads 512 contiguous bytes). X sample s at X + s * in * 4. Y target of row g for sample s at Y + (s * rows + g) * 4.
 * Loss of row g (sum over all steps) at L + g * 4. Step k (0-based) uses sample (steps - k) & data_mask.
 * Kernel arguments at cbank 0x380: W, X, Y, L as four 64-bit addresses. */
#ifndef OMEGA_BLACKWELL_CODEGEN_TRAIN_H
#define OMEGA_BLACKWELL_CODEGEN_TRAIN_H

#include "omega_blackwell_codegen.h"
#include <stdint.h>

typedef struct {
    uint32_t in;        /* weights per row: 8, 16, 24 or 32 */
    uint32_t threads;   /* rows per CTA, multiple of 32, <= 1024 */
    uint32_t ctas;      /* CTAs (rows = ctas * threads) */
    uint32_t steps;     /* SGD steps, >= 1 */
    uint32_t data_mask; /* samples in the data set minus one; samples is a power of two */
    uint32_t lr_bits;   /* FP32 bits of the learning rate (> 0) */
    uint32_t batch;     /* 0 read once (resident); b > 0: load + store W every b samples; steps % b == 0 */
    int ivall;          /* streamed only: invalidate L1 before each weight load */
} OmegaTrainProbeSpec;

int omega_blackwell_codegen_train_lms_prog(const OmegaTrainProbeSpec *spec, BlackwellIRProgram *prog);

#endif
