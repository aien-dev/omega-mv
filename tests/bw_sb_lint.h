/* bw_sb_lint.h: scoreboard check for a register-allocated IR program (speed3/w4-gemv, 2026-10-20).
 *
 * The warp simulator (bw_warp_sim.h) completes every load at once, so it cannot see a missing wait. This check walks
 * the control words the way the chip does: a variable-latency instruction with write barrier b leaves its destination
 * registers pending on b until some later instruction waits on b; an instruction with read barrier b leaves its source
 * registers pending on b the same way (the late operand read of loads and stores). It reports the first instruction
 * that reads or overwrites a register still pending on a write barrier, or overwrites one still pending on a read
 * barrier. Branches are followed: the state at a branch target is the union over every way in (fixed point), so loop
 * back edges count. Uniform registers and predicates are not tracked.
 *
 * Control word (omega_blackwell_codegen_*.c CW): wbar bits 14..16, rbar 17..19, wait mask 20..25; barrier 7 = none.
 */
#ifndef BW_SB_LINT_H
#define BW_SB_LINT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "omega_blackwell_codegen.h"

#define SBL_REGS (BW_MAX_VREGS + 8)

static void sbl_span(const BlackwellIRProgram *p, int v, int *lo, int *cnt) {
    *lo = -1; *cnt = 0;
    if (v < 0 || v >= p->regalloc.num_vregs) return;
    const int ph = p->regalloc.vreg_to_phys[v];
    if (ph < 0) return;
    *lo = ph; *cnt = (int)(p->regalloc.intervals[v].bundle_size ? p->regalloc.intervals[v].bundle_size : 1u);
}

/* 0: clean; -1: a hazard (msg says where); -2: the program has a branch outside itself */
static int bw_sb_lint(const BlackwellIRProgram *p, char *msg, size_t ml) {
    const size_t n = p->count;
    typedef struct { uint8_t w[SBL_REGS], r[SBL_REGS]; } St;
    St *in = calloc(n + 1, sizeof *in);
    unsigned char *reach = calloc(n + 1, 1);
    if (!in || !reach) { free(in); free(reach); snprintf(msg, ml, "out of memory"); return -2; }
    reach[0] = 1;
    int rc = 0, changed = 1, rounds = 0;
    while (changed && rc == 0 && rounds++ < 64) {
        changed = 0;
        for (size_t i = 0; i < n && rc == 0; i++) {
            if (!reach[i]) continue;
            const BlackwellIRInsn *x = &p->insns[i];
            St s = in[i];
            const uint32_t wait = (x->control >> 20) & 0x3fu, wb = (x->control >> 14) & 7u, rb = (x->control >> 17) & 7u;
            for (int g = 0; g < SBL_REGS; g++) { s.w[g] &= (uint8_t)~wait; s.r[g] &= (uint8_t)~wait; }
            const int uni = x->is_uniform;
            const int srcs[3] = { x->src1_vreg, x->src2_vreg, x->src3_vreg };
            for (int q = 0; q < 3 && !uni; q++) {
                int lo, c; sbl_span(p, srcs[q], &lo, &c);
                for (int k = 0; k < c; k++) if (s.w[lo + k]) {
                    snprintf(msg, ml, "insn %zu (op %d) reads R%d still pending on barrier mask 0x%x", i, (int)x->op, lo + k, s.w[lo + k]);
                    rc = -1;
                }
            }
            int dlo = -1, dc = 0;
            if (!uni) sbl_span(p, x->dst_vreg, &dlo, &dc);
            for (int k = 0; k < dc && rc == 0; k++) if (s.w[dlo + k] || s.r[dlo + k]) {
                snprintf(msg, ml, "insn %zu (op %d) overwrites R%d still pending (write 0x%x, read 0x%x)", i, (int)x->op, dlo + k, s.w[dlo + k], s.r[dlo + k]);
                rc = -1;
            }
            if (x->op == BW_IR_EXIT && !x->predicate_p0)
                for (int g = 0; g < SBL_REGS && rc == 0; g++) if (s.w[g]) {
                    snprintf(msg, ml, "insn %zu EXIT with R%d still pending on barrier mask 0x%x", i, g, s.w[g]); rc = -1;
                }
            if (rc) break;
            if (wb != 7u) for (int k = 0; k < dc; k++) s.w[dlo + k] |= (uint8_t)(1u << wb);
            if (rb != 7u)
                for (int q = 0; q < 3 && !uni; q++) { int lo, c; sbl_span(p, srcs[q], &lo, &c); for (int k = 0; k < c; k++) s.r[lo + k] |= (uint8_t)(1u << rb); }
            /* successors: fall through unless an unpredicated BRA or EXIT; a BRA also goes to its target */
            size_t succ[2]; int ns = 0;
            const int uncond_end = (x->op == BW_IR_BRA || x->op == BW_IR_EXIT) && !x->predicate_p0;
            if (!uncond_end && i + 1 <= n) succ[ns++] = i + 1;
            if (x->op == BW_IR_BRA) {
                const long t = (long)i + (long)(int32_t)x->imm;
                if (t < 0 || t > (long)n) { snprintf(msg, ml, "insn %zu branches outside the program", i); rc = -2; break; }
                if ((size_t)t != i) succ[ns++] = (size_t)t; /* the final self-branch is never reached */
            }
            for (int q = 0; q < ns; q++) {
                const size_t t = succ[q];
                if (!reach[t]) { reach[t] = 1; in[t] = s; changed = 1; continue; }
                for (int g = 0; g < SBL_REGS; g++) {
                    const uint8_t w = in[t].w[g] | s.w[g], r = in[t].r[g] | s.r[g];
                    if (w != in[t].w[g] || r != in[t].r[g]) { in[t].w[g] = w; in[t].r[g] = r; changed = 1; }
                }
            }
        }
    }
    if (rc == 0 && changed) { snprintf(msg, ml, "no fixed point after 64 rounds"); rc = -2; }
    if (rc == 0) snprintf(msg, ml, "clean (%zu insns)", n);
    free(in); free(reach);
    return rc;
}
#endif
