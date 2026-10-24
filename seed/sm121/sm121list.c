/*
 * sm121list.c -- REFERENCE CHECK ONLY for rung 6 (seed/sm121/README.md). It never makes a byte of seed/.
 * It runs the repo's own GPU code generators and C encoder (src/omega_blackwell_codegen*.c, the referee)
 * and writes, for every program they produce, two files side by side:
 *   NAME.s2     the program as a text listing in asm2's language, one line per 32-bit word, using the names
 *               of seed/sm121/sm121.tbl (instruction name and its registers and numbers, read from the
 *               generator's instruction list, never from the encoded words);
 *   NAME.c.bin  the bytes the C encoder made for the same program (after the two patch steps the callers
 *               apply), the answer asm2 must reproduce.
 * gate6.sh then feeds table + listing to asm2 and compares. Modes:
 *   sm121list kernels DIR   every generator over a sweep of its settings (refused settings are skipped)
 *   sm121list edges DIR     every instruction form with registers and numbers at the edges of their fields
 * Notes on what the C encoder silently ignores or truncates go to DIR/notes.tsv.
 */
#include <stdarg.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../src/omega_blackwell_codegen.h"
#include "../../src/omega_blackwell_codegen_attn.h"
#include "../../src/omega_blackwell_codegen_bf16.h"
#include "../../src/omega_blackwell_codegen_dec.h"
#include "../../src/omega_blackwell_codegen_gemm.h"
#include "../../src/omega_blackwell_codegen_moe.h"
#include "../../src/omega_blackwell_codegen_moe2.h"
#include "../../src/omega_blackwell_codegen_pattn.h"
#include "../../src/omega_blackwell_codegen_pf.h"
#include "../../src/omega_blackwell_codegen_pfhost.h"
#include "../../src/omega_blackwell_codegen_qg.h"
#include "../../src/omega_blackwell_codegen_qmma.h"
#include "../../src/omega_blackwell_codegen_router.h"
#include "../../src/omega_blackwell_codegen_tc.h"
#include "../../src/omega_blackwell_codegen_tc2.h"
#include "../../src/omega_blackwell_codegen_tern.h"
#include "../../src/omega_blackwell_encoder.h"

static const char *g_dir;
static FILE *g_notes;
static unsigned long g_kernels, g_dups, g_refused, g_insns;

/* Generated outputs require fresh paths; never truncate an existing file or
 * follow a final-component symlink. gate6.sh cleans only its build outputs. */
static FILE *private_output(const char *path, const char *mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, mode);
    if (!f) close(fd);
    return f;
}

static void note(const char *kernel, size_t i, const char *what) {
    fprintf(g_notes, "%s\t%zu\t%s\n", kernel, i, what);
}

/* physical operands exactly as encode_single_insn derives them (codegen.c:265-273) */
typedef struct { int d, a, b, c, u; } Ops;
static Ops ops_of(const BlackwellIRInsn *in, const OmegaRegAlloc *ra) {
    Ops o;
    o.d = in->dst_vreg >= 0 ? ra->vreg_to_phys[in->dst_vreg] + in->dst_subreg : 0;
    o.a = in->src1_vreg >= 0 ? ra->vreg_to_phys[in->src1_vreg] + in->src1_subreg : 0;
    o.b = in->src2_vreg >= 0 ? ra->vreg_to_phys[in->src2_vreg] + in->src2_subreg : 0;
    o.c = in->src3_vreg >= 0 ? ra->vreg_to_phys[in->src3_vreg] + in->src3_subreg : 0xff;
    o.u = in->ureg >= 0 ? ra->uvreg_to_phys[in->ureg] : 0;
    if (in->is_uniform && in->dst_vreg >= 0) o.d = ra->uvreg_to_phys[in->dst_vreg];
    return o;
}

static void L(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fputs("  ", f); vfprintf(f, fmt, ap); fputc('\n', f);
    va_end(ap);
}
/* word 3: the given control word, or the instruction's default line */
static void ctl(FILE *f, const BlackwellIRInsn *in, const char *dflt) {
    if (in->control) L(f, "ctl 0x%08x", in->control); else L(f, "%s", dflt);
}
static int pred_of(const BlackwellIRInsn *in) { return in->predicate_p0 ? (in->predicate_not ? 8 : 0) : 7; }

/* patched: 1 when the program went through omega_tc_patch_code / omega_qmma_patch_code */
static int list_insn(FILE *f, const BlackwellIRInsn *in, const OmegaRegAlloc *ra, int patched, const char *k, size_t i) {
    Ops o = ops_of(in, ra);
    switch (in->op) {
    case BW_IR_NOP: L(f, "nop.0"); L(f, "nop.1"); L(f, "nop.2"); ctl(f, in, "nop.3"); break;
    case BW_IR_MOV_RZ: L(f, "mov.rz.0 %d", o.d); L(f, "mov.rz.1"); L(f, "mov.rz.2"); ctl(f, in, "mov.rz.3"); break;
    case BW_IR_MOV_IMM: L(f, "mov.i.0 %d", o.d); L(f, "mov.i.1 0x%08x", in->imm); L(f, "mov.i.2"); ctl(f, in, "mov.i.3"); break;
    case BW_IR_S2R: L(f, "s2r.0 %d", o.d); L(f, "s2r.1"); L(f, "s2r.2 %u", in->imm); ctl(f, in, "s2r.3"); break;
    case BW_IR_LDC: case BW_IR_LDC64: {
        const char *n = in->op == BW_IR_LDC ? "ldc" : "ldc.64";
        if (in->imm & 3u) note(k, i, "LDC offset not a multiple of 4: the C encoder drops the low bits");
        L(f, "%s.0 %d", n, o.d); L(f, "%s.1 %u", n, in->imm >> 2); L(f, "%s.2", n); ctl(f, in, in->op == BW_IR_LDC ? "ldc.3" : "ldc.64.3");
        break;
    }
    case BW_IR_LDCU: case BW_IR_LDCU64: {
        const char *n = in->op == BW_IR_LDCU ? "ldcu" : "ldcu.64";
        if (in->imm & 7u) note(k, i, "LDCU offset not a multiple of 8: the C encoder drops the low bits");
        L(f, "%s.0 %d", n, o.d); L(f, "%s.1 %u", n, in->imm >> 3); L(f, "%s.2", n); ctl(f, in, in->op == BW_IR_LDCU ? "ldcu.3" : "ldcu.64.3");
        break;
    }
    case BW_IR_IMAD:
        if (in->src2_vreg >= 0) { L(f, "imad.0 %d %d", o.d, o.a); L(f, "imad.1 %d", o.b); L(f, "imad.2 %d", o.c); ctl(f, in, "imad.3"); }
        else if (in->is_uniform && in->ureg >= 0) { L(f, "imad.u.0 %d %d", o.d, o.a); L(f, "imad.u.1 %d", o.u); L(f, "imad.u.2 %d", o.c); ctl(f, in, "imad.u.3"); }
        else { L(f, "imad.i.0 %d %d", o.d, o.a); L(f, "imad.i.1 0x%08x", in->imm); L(f, "imad.i.2 %d", o.c); ctl(f, in, "imad.i.3"); }
        break;
    case BW_IR_IMAD_WIDE: L(f, "imad.wide.0 %d %d", o.d, o.a); L(f, "imad.wide.1 0x%08x", in->imm); L(f, "imad.wide.2 %d", o.c); ctl(f, in, "imad.wide.3"); break;
    case BW_IR_ISETP_GE:
        if (in->control) note(k, i, "ISETP.GE: the C encoder ignores the given control word");
        if (in->is_uniform && in->ureg >= 0) { L(f, "isetp.ge.u.0 %d %d", o.d, o.a); L(f, "isetp.ge.u.1 %d", o.u); L(f, "isetp.ge.u.2"); L(f, "isetp.ge.u.3"); }
        else { L(f, "isetp.ge.i.0 %d %d", o.d, o.a); L(f, "isetp.ge.i.1 0x%08x", in->imm); L(f, "isetp.ge.i.2"); L(f, "isetp.ge.i.3"); }
        break;
    case BW_IR_LDG_E: case BW_IR_LDG_E_U16: case BW_IR_LDG_STRONG_SYS: case BW_IR_LDG_MMIO: {
        const char *w2 = in->op == BW_IR_LDG_E ? "ldg.2" : in->op == BW_IR_LDG_E_U16 ? "ldg.u16.2" : in->op == BW_IR_LDG_STRONG_SYS ? "ldg.strong.sys.2" : "ldg.mmio.2";
        L(f, "ldg.0 %d %d", o.d, o.a); L(f, "ldg.1 %d", o.u); L(f, "%s", w2); ctl(f, in, "ldg.3");
        break;
    }
    case BW_IR_LDG_E_64: case BW_IR_LDG_E_128:
        L(f, "ldg.0 %d %d", o.d, o.a); L(f, "ldg.w.1 %d %d", o.u, (int32_t)in->imm);
        L(f, "%s", in->op == BW_IR_LDG_E_64 ? "ldg.64.2" : "ldg.128.2"); ctl(f, in, "ldg.3");
        break;
    case BW_IR_STG_E: L(f, "stg.0 %d", o.a); L(f, "stg.1 %d %d", o.b, (int32_t)in->imm); L(f, "stg.2 %d", o.u); ctl(f, in, "stg.3"); break;
    case BW_IR_STG_EF: L(f, "stg.0 %d", o.a); L(f, "stg.ef.1 %d", o.b); L(f, "stg.ef.2 %d", o.u); ctl(f, in, "w.3"); break;
    case BW_IR_STG_STRONG_SYS:
        L(f, "stg.strong.sys.0 %d %d", pred_of(in), o.a); L(f, "r.1 %d", o.b); L(f, "stg.strong.sys.2 %d", o.u); ctl(f, in, "w.3");
        break;
    case BW_IR_MEMBAR_ALL_SYS: L(f, "membar.all.sys.0"); L(f, "z.1"); L(f, "membar.all.sys.2"); ctl(f, in, "membar.3"); break;
    case BW_IR_MEMBAR_SC_SYS: L(f, "membar.all.sys.0"); L(f, "z.1"); L(f, "membar.sc.sys.2"); ctl(f, in, "membar.3"); break;
    case BW_IR_CCTL_IVALL: L(f, "cctl.ivall.0"); L(f, "z.1"); L(f, "cctl.ivall.2"); ctl(f, in, "cctl.ivall.3"); break;
    case BW_IR_ATOMG_ADD_STRONG_SYS: case BW_IR_ATOMG_EXCH_STRONG_SYS:
        L(f, "atomg.0 %d %d", o.d, o.a); L(f, "atomg.1 %d", o.b);
        L(f, "%s %d", in->op == BW_IR_ATOMG_ADD_STRONG_SYS ? "atomg.add.2" : "atomg.exch.2", o.u); ctl(f, in, "atomg.3");
        break;
    case BW_IR_ISETP_GE_U32: L(f, "isetp.ge.u32.0 %d", o.a); L(f, "r.1 %d", o.b); L(f, "isetp.ge.u32.2"); ctl(f, in, "isetp.ge.u32.3"); break;
    case BW_IR_IADD3:
        if (in->control) note(k, i, "IADD3: the C encoder ignores the given control word");
        if (in->src3_vreg >= 0) note(k, i, "IADD3: the C encoder ignores the third source (always RZ)");
        L(f, "iadd3.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "iadd3.2"); L(f, "iadd3.3");
        break;
    case BW_IR_HMMA_F16: case BW_IR_HMMA_BF16:
        if (patched && omega_qmma_is_mark(in)) { L(f, "qmma.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "qmma.2 %d", o.c); }
        else { L(f, "hmma.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "%s %d", in->op == BW_IR_HMMA_BF16 ? "hmma.bf16.2" : "hmma.f16.2", o.c); }
        ctl(f, in, "w.3");
        break;
    case BW_IR_SHF_R: L(f, "shf.r.hi.0 %d", o.d); L(f, "n.1 0x%08x", in->imm); L(f, "shf.r.hi.2 %d", o.a); ctl(f, in, "alu.3"); break;
    case BW_IR_LOP3_AND: L(f, "lop3.and.i.0 %d %d", o.d, o.a); L(f, "n.1 0x%08x", in->imm); L(f, "lop3.and.i.2"); ctl(f, in, "alu.3"); break;
    case BW_IR_LOP3_XOR: L(f, "lop3.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "lop3.xor.2"); ctl(f, in, "alu.3"); break;
    case BW_IR_LOP3_LUT: case BW_IR_SHF_L_U32: case BW_IR_IMAD_HI_U32: {
        int a = in->src1_vreg >= 0 ? o.a : 0xff, b = in->src2_vreg >= 0 ? o.b : 0xff;
        const char *n0 = in->op == BW_IR_LOP3_LUT ? "lop3.0" : in->op == BW_IR_SHF_L_U32 ? "shf.l.u32.0" : "imad.hi.0";
        L(f, "%s %d %d", n0, o.d, a); L(f, "r.1 %d", b);
        if (in->op == BW_IR_LOP3_LUT) L(f, "lop3.lut.2 %u %d", in->imm & 0xffu, o.c);
        else L(f, "%s %d", in->op == BW_IR_SHF_L_U32 ? "shf.l.u32.2" : "imad.hi.2", o.c);
        if (in->op == BW_IR_LOP3_LUT && in->imm > 0xffu) note(k, i, "LOP3.LUT: truth table above 8 bits, the C encoder keeps the low 8");
        ctl(f, in, "alu.3");
        break;
    }
    case BW_IR_FADD: L(f, "fadd.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "f.2"); ctl(f, in, "f.3"); break;
    case BW_IR_FSUB: L(f, "fadd.0 %d %d", o.d, o.a); L(f, "fsub.1 %d", o.b); L(f, "f.2"); ctl(f, in, "f.3"); break;
    case BW_IR_FMUL: L(f, "fmul.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "fmul.2"); ctl(f, in, "f.3"); break;
    case BW_IR_FFMA: L(f, "ffma.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "r.2 %d", o.c); ctl(f, in, "f.3"); break;
    case BW_IR_FSETP:
        L(f, "fsetp.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b);
        if (in->imm) L(f, "fsetp.2 0x%08x", in->imm); else L(f, "fsetp.ge.2");
        ctl(f, in, "f.3");
        break;
    case BW_IR_FSEL: L(f, "fsel.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "fsel.2 %d", o.c & (in->src3_vreg >= 0 ? 0xff : 7)); ctl(f, in, "fsel.3"); break;
    case BW_IR_FMNMX_MIN: L(f, "fmnmx.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "fmnmx.min.2"); ctl(f, in, "f.3"); break;
    case BW_IR_FMNMX_MAX:
        if (patched && in->imm == OMEGA_TC_MARK_F2FP) { L(f, "f2fp.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "f2fp.bf16.2"); }
        else if (patched && in->imm == OMEGA_QMMA_MARK_F2E4) { L(f, "f2fp.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "f2fp.e4m3.2"); }
        else { L(f, "fmnmx.0 %d %d", o.d, o.a); L(f, "r.1 %d", o.b); L(f, "fmnmx.max.2"); }
        ctl(f, in, "f.3");
        break;
    case BW_IR_I2FP: L(f, "i2fp.0 %d", o.d); L(f, "r.1 %d", o.a); L(f, "i2fp.2"); ctl(f, in, "i2fp.3"); break;
    case BW_IR_F2I: L(f, "f2i.0 %d", o.d); L(f, "r.1 %d", o.a); L(f, "f2i.2"); ctl(f, in, "f2i.3"); break;
    case BW_IR_MUFU_RCP: case BW_IR_MUFU_RSQ: case BW_IR_MUFU_EX2:
        L(f, "mufu.0 %d", o.d); L(f, "r.1 %d", o.a);
        L(f, "%s", in->op == BW_IR_MUFU_RCP ? "mufu.rcp.2" : in->op == BW_IR_MUFU_RSQ ? "mufu.rsq.2" : "mufu.ex2.2"); ctl(f, in, "mufu.3");
        break;
    case BW_IR_SHFL_DOWN: {
        int bfly = patched && (in->imm & OMEGA_TC_MARK_BFLY);
        uint32_t lane = in->imm & ~OMEGA_TC_MARK_BFLY;   /* the mark is a flag for the patch step, not part of the word */
        L(f, "shfl.0 %d %d", o.d, o.a); L(f, "%s %u", bfly ? "shfl.bfly.1" : "shfl.down.1", lane); L(f, "shfl.2"); ctl(f, in, "shfl.3");
        break;
    }
    case BW_IR_LDS: L(f, "lds.0 %d %d", o.d, o.a); L(f, "z.1"); L(f, "lds.u8.2 %u", in->imm); ctl(f, in, "w.3"); break;
    case BW_IR_STS: L(f, "sts.u8.0 %d", o.a); L(f, "r.1 %d", o.b); L(f, "sts.u8.2 %u", in->imm); ctl(f, in, "w.3"); break;
    case BW_IR_LDS128: L(f, "lds.0 %d %d", o.d, o.a); L(f, "lds.128.1 %u", in->imm); L(f, "lds.128.2"); ctl(f, in, "lds.128.3"); break;
    case BW_IR_LDS32: L(f, "lds.0 %d %d", o.d, o.a); L(f, "lds.32.1"); L(f, "lds.32.2"); ctl(f, in, "lds.32.3"); break;
    case BW_IR_STS32: L(f, "sts.32.0 %d", o.a); L(f, "r.1 %d", o.b); L(f, "sts.32.2"); ctl(f, in, "w.3"); break;
    case BW_IR_EXIT:
        if (in->control) note(k, i, "EXIT: the C encoder ignores the given control word");
        if (in->predicate_p0 && in->predicate_not) note(k, i, "EXIT: @!P0 asked, the C encoder writes @P0");
        L(f, "%s", in->predicate_p0 ? "exit.p0.0" : "exit.0"); L(f, "z.1"); L(f, "exit.2"); L(f, "exit.3");
        break;
    case BW_IR_BRA: {
        int32_t delta = (int32_t)in->imm, rel = delta - 1;
        L(f, "bra.0 %d %d", pred_of(in), rel & 0x3f); L(f, "bra.1 %d", rel >> 6); L(f, "%s", rel < 0 ? "bra.back.2" : "bra.fwd.2");
        if (in->control) L(f, "ctl 0x%08x", in->control);
        else L(f, "%s", delta == 0 && !in->predicate_p0 ? "bra.self.3" : "bra.3");
        break;
    }
    case BW_IR_BSSY:
        L(f, "bssy.0 %d %u", pred_of(in), in->bar_reg); L(f, "bssy.1 %d", (int32_t)in->imm - 1); L(f, "b.2"); ctl(f, in, "w.3");
        break;
    case BW_IR_BSYNC: L(f, "bsync.0 %d %u", pred_of(in), in->bar_reg); L(f, "z.1"); L(f, "b.2"); ctl(f, in, "jmp.3"); break;
    case BW_IR_BAR_SYNC: L(f, "bar.sync.0"); L(f, "z.1"); L(f, "bar.sync.2"); ctl(f, in, "membar.3"); break;
    default: return -1;
    }
    return 0;
}

/* ---- kernels ---- */
#define NSEEN 65536
static uint64_t g_seen[NSEEN];
static size_t g_nseen;
static uint64_t fnv(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
static uint8_t g_code[OMEGA_BW_CODE_CAP_MAX];
static FILE *g_tsv;

static void dump(const char *name, const BlackwellIRProgram *p) {
    size_t len = 0;
    if (omega_bw_encode_program(p, g_code, sizeof g_code, &len) != 0) { note(name, 0, "C encoder refused the program"); return; }
    int nt = omega_tc_patch_code(p, g_code, len), nq = omega_qmma_patch_code(p, g_code, len);
    if (nt < 0 || nq < 0) { note(name, 0, "patch step refused the program"); return; }
    uint64_t h = fnv(g_code, len) ^ (uint64_t)len;
    for (size_t i = 0; i < g_nseen; i++) if (g_seen[i] == h) { g_dups++; return; }
    if (g_nseen < NSEEN) g_seen[g_nseen++] = h;
    char path[512];
    snprintf(path, sizeof path, "%s/k/%s.s2", g_dir, name);
    FILE *f = private_output(path, "w");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "; %s: %zu instructions (+ NOP padding), listing written by sm121list from the generator's instruction list\n", name, p->count);
    size_t padded = (p->count + 7) & ~(size_t)7;
    if (padded < 32) padded = 32;
    for (size_t i = 0; i < padded; i++) {
        if (i < p->count) {
            if (list_insn(f, &p->insns[i], &p->regalloc, nt > 0 || nq > 0, name, i) != 0) { note(name, i, "opcode the listing writer does not know"); }
        } else { L(f, "nop.0"); L(f, "nop.1"); L(f, "nop.2"); L(f, "nop.3"); }
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/k/%s.c.bin", g_dir, name);
    f = private_output(path, "wb");
    if (!f || fwrite(g_code, 1, len, f) != len) { perror(path); exit(2); }
    fclose(f);
    fprintf(g_tsv, "%s\t%zu\t%zu\t%d\t%d\n", name, p->count, len, nt, nq);
    g_kernels++; g_insns += p->count;
}

static BlackwellIRProgram g_p;
#define TRY(call, ...) do { char nm_[256]; snprintf(nm_, sizeof nm_, __VA_ARGS__); omega_bw_ir_init(&g_p); \
    if ((call) == 0) dump(nm_, &g_p); else g_refused++; } while (0)

static OmegaMatMulSpec mm(uint32_t m, uint32_t k, uint32_t n, OmegaMatMulPrecision pr) {
    OmegaMatMulSpec s; memset(&s, 0, sizeof s); s.m = m; s.k = k; s.n = n; s.precision = pr; return s;
}

static void kernels(void) {
    static const uint32_t W6[] = { 1, 2, 4, 8, 16, 32 };
    /* integer matmul, single-tile and looped tensor matmuls */
    { static const uint32_t ks[] = { 1, 2, 8, 32 };
      for (int a = 0; a < 4; a++) for (uint32_t m = 1; m <= 16; m *= 4) for (uint32_t n = 1; n <= 16; n *= 4) {
          OmegaMatMulSpec s = mm(m, ks[a], n, OMEGA_MATMUL_PRECISION_INT32);
          TRY(omega_blackwell_codegen_matmul_i32_prog(&s, &g_p), "i32_m%u_k%u_n%u", m, ks[a], n); } }
    for (int pr = 1; pr <= 2; pr++) {
        OmegaMatMulSpec s = mm(16, 16, 8, (OmegaMatMulPrecision)pr);
        TRY(omega_blackwell_codegen_matmul_tensor_prog(&s, &g_p), "tensor_p%d", pr);
        static const uint32_t ks[] = { 16, 64, 256, 4096 }, ns[] = { 8, 64 }, gx[] = { 1, 2, 8 };
        for (int a = 0; a < 4; a++) for (int b = 0; b < 2; b++) for (int c = 0; c < 3; c++) for (int mu = 0; mu < 2; mu++) {
            OmegaMatMulSpec t = mm(16, ks[a], ns[b], (OmegaMatMulPrecision)pr);
            TRY(omega_blackwell_codegen_matmul_tensor_loop_prog(&t, gx[c], mu, &g_p), "tloop_p%d_k%u_n%u_g%u_m%d", pr, ks[a], ns[b], gx[c], mu);
            for (uint32_t un = 1; un <= 8; un *= 2) {
                TRY(omega_blackwell_codegen_matmul_frag_prog(&t, gx[c], un, mu, &g_p), "frag_p%d_k%u_n%u_g%u_u%u_m%d", pr, ks[a], ns[b], gx[c], un, mu);
                for (uint32_t rt = 1; rt <= 4; rt *= 2) for (uint32_t ct = 1; ct <= 4; ct *= 2)
                    TRY(omega_blackwell_codegen_matmul_frag_block_prog(&t, gx[c], un, rt, ct, mu, &g_p),
                        "fragb_p%d_k%u_n%u_g%u_u%u_r%u_c%u_m%d", pr, ks[a], ns[b], gx[c], un, rt, ct, mu);
            }
        }
    }
    for (uint32_t d = 1; d <= 4; d++) for (int mu = 0; mu <= 4; mu++) {
        OmegaDecodeAttnSpec s; memset(&s, 0, sizeof s); s.depth = d; s.mutant = mu;
        TRY(omega_blackwell_codegen_decode_attn_prog(&s, &g_p), "dattn_d%u_m%d", d, mu); }
    { static const uint32_t ks[] = { 256, 1024, 4096, 16384 };
      for (int a = 0; a < 4; a++) for (uint32_t r = 1; r <= 4; r *= 2) for (int w = 0; w < 6; w++) for (uint32_t d = 1; d <= 16; d *= 2) for (int mu = 0; mu <= 3; mu++) {
          OmegaBf16GemvSpec s; memset(&s, 0, sizeof s); s.k = ks[a]; s.rows = r; s.warps = W6[w]; s.depth = d; s.mutant = mu;
          TRY(omega_blackwell_codegen_bf16_gemv_prog(&s, &g_p), "bf16_k%u_r%u_w%u_d%u_m%d", ks[a], r, W6[w], d, mu); } }
    { static const uint32_t st[] = { 256, 1024, 8192 };
      for (int kd = 1; kd <= 4; kd++) for (uint32_t mo = 0; mo < 8; mo++) for (int a = 0; a < 3; a++) for (int mu = 0; mu <= 2; mu++) {
          OmegaDecSpec s; memset(&s, 0, sizeof s); s.kind = (OmegaDecKind)kd; s.mode = mo; s.stride = st[a]; s.mutant = mu;
          TRY(omega_blackwell_codegen_dec_prog(&s, &g_p), "dec_k%d_mo%u_s%u_m%d", kd, mo, st[a], mu); } }
    { static const uint32_t ks[] = { 128, 256, 512, 2048, 16384 };
      for (int a = 0; a < 5; a++) for (uint32_t lb = 8; lb <= 16; lb *= 2) for (uint32_t mt = 1; mt <= 16; mt *= 2) for (int w = 0; w < 6; w++) for (int mu = 0; mu <= 3; mu++) {
          OmegaFp8GemmSpec s; memset(&s, 0, sizeof s); s.k = ks[a]; s.lane_bytes = lb; s.mt = mt; s.warps = W6[w]; s.mutant = mu;
          TRY(omega_blackwell_codegen_fp8gemm_prog(&s, &g_p), "fp8gemm_k%u_l%u_t%u_w%u_m%d", ks[a], lb, mt, W6[w], mu); } }
    { static const uint32_t ks[] = { 256, 512, 2048, 4096, 16384 }, ds[] = { 0, 1, 2, 4, 8 };
      for (int a = 0; a < 5; a++) for (uint32_t lb = 8; lb <= 16; lb *= 2) for (uint32_t r = 1; r <= 8; r *= 2) for (int w = 0; w < 6; w++)
      for (int d = 0; d < 5; d++) for (int mu = 0; mu <= 3; mu++) {
          OmegaFp8GemvSpec s; memset(&s, 0, sizeof s); s.k = ks[a]; s.lane_bytes = lb; s.rows = r; s.warps = W6[w]; s.depth = ds[d]; s.mutant = mu;
          TRY(omega_blackwell_codegen_fp8_gemv_prog(&s, &g_p), "fp8gemv_k%u_l%u_r%u_w%u_d%u_m%d", ks[a], lb, r, W6[w], ds[d], mu);
          TRY(omega_blackwell_codegen_fp8_gemv2_prog(&s, &g_p), "fp8gemv2_k%u_l%u_r%u_w%u_d%u_m%d", ks[a], lb, r, W6[w], ds[d], mu); } }
    for (uint32_t w = 4; w <= 8; w *= 2) for (int mu = 0; mu <= 4; mu++) for (uint32_t mo = 0; mo <= 2; mo++) for (uint32_t mi = 0; mi <= 1; mi++) {
        OmegaPrefillAttnSpec s; memset(&s, 0, sizeof s); s.warps = w; s.mutant = mu; s.mode = mo; s.mirror = mi;
        TRY(omega_blackwell_codegen_prefill_attn_prog(&s, &g_p), "pattn_w%u_m%d_mo%u_mi%u", w, mu, mo, mi); }
    { static const uint32_t ks[] = { 128, 256, 2048, 4096 }, es[] = { 8, 64, 128 }, ts[] = { 1, 8, 16, 64 };
      for (int a = 0; a < 4; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 4; c++) for (int mu = 0; mu <= 2; mu++) {
          OmegaRlSpec s = { ks[a], es[b], ts[c], mu };
          if (omega_rl_spec_ok(&s)) TRY(omega_blackwell_codegen_rl_prog(&s, &g_p), "rl_k%u_e%u_t%u_m%d", ks[a], es[b], ts[c], mu); } }
    { static const uint32_t hs[] = { 512, 2048, 4096 };
      for (int a = 0; a < 3; a++) for (uint32_t tk = 1; tk <= 8; tk *= 2) for (int mu = 0; mu <= 2; mu++) {
          OmegaMcSpec s = { hs[a], tk, mu };
          if (omega_mc_spec_ok(&s)) TRY(omega_blackwell_codegen_mc_prog(&s, &g_p), "mc_h%u_t%u_m%d", hs[a], tk, mu); } }
    TRY(omega_blackwell_codegen_pf_copy_prog(&g_p), "pf_copy");
    { static const uint32_t wn[] = { 0, 1, 4, 15 };
      for (int mu = 0; mu <= 3; mu++) for (int a = 0; a < 4; a++) TRY(omega_blackwell_codegen_qmma_probe_prog(mu, wn[a], &g_p), "qmma_probe_m%d_w%u", mu, wn[a]); }
    { static const uint32_t its[] = { 1, 64 }, th[] = { 32, 128, 256 };
      for (int a = 0; a < 2; a++) for (uint32_t ch = 1; ch <= 8; ch *= 2) for (uint32_t sl = 0; sl <= 15; sl += 15) for (int t = 0; t < 3; t++) {
          TRY(omega_blackwell_codegen_qmma_peak_prog(its[a], ch, sl, th[t], &g_p), "qmma_peak_i%u_c%u_s%u_t%u", its[a], ch, sl, th[t]);
          TRY(omega_blackwell_codegen_tc_peak_prog(its[a], ch, sl, th[t], &g_p), "tc_peak_i%u_c%u_s%u_t%u", its[a], ch, sl, th[t]); }
      for (int a = 0; a < 2; a++) for (uint32_t g = 1; g <= 4; g *= 4) for (int t = 0; t < 3; t++)
          TRY(omega_blackwell_codegen_tc_bw_prog(its[a], g, th[t], &g_p), "tc_bw_i%u_g%u_t%u", its[a], g, th[t]); }
    TRY(omega_blackwell_codegen_f2e4_probe_prog(&g_p), "f2e4_probe");
    for (int mu = 0; mu <= 3; mu++) TRY(omega_blackwell_codegen_tc_probe_prog(mu, &g_p), "tc_probe_m%d", mu);
    { static const uint32_t ks[] = { 128, 512, 4096, 16384 };
      for (int a = 0; a < 4; a++) for (uint32_t rt = 1; rt <= 4; rt *= 2) for (uint32_t ct = 1; ct <= 4; ct *= 2) for (uint32_t w = 1; w <= 8; w *= 2) for (int mu = 0; mu <= 3; mu++) {
          OmegaTcGemmSpec s; memset(&s, 0, sizeof s); s.k = ks[a]; s.rt = rt; s.ct = ct; s.warps = w; s.mutant = mu;
          /* the generator accepts 4*rt/warps staging slots up to 16 but keeps them in 8-long arrays (gx, sa, stg):
           * rt=4 with one warp overruns them and crashes. Fixer ledger L149. Skipped and named, never hidden. */
          if (omega_tcgemm_spec_ok(&s) && 4u * rt / w > 8u) {
              char nm[96]; snprintf(nm, sizeof nm, "tcgemm_k%u_r%u_c%u_w%u_m%d", ks[a], rt, ct, w, mu);
              note(nm, 0, "skipped: generator overruns its 8-slot staging arrays for this setting (fixer L149)"); continue; }
          TRY(omega_blackwell_codegen_tcgemm_prog(&s, &g_p), "tcgemm_k%u_r%u_c%u_w%u_m%d", ks[a], rt, ct, w, mu); } }
    { static const uint32_t ks[] = { 256, 512, 4096, 16384 }, ws[] = { 1, 4, 8, 32 }; static const int ms[] = { 0, 1, 3 };
      for (int a = 0; a < 4; a++) for (uint32_t lay = 0; lay <= 1; lay++) for (uint32_t r = 1; r <= 4; r *= 2) for (int w = 0; w < 4; w++)
      for (uint32_t d = 1; d <= 8; d *= 2) for (uint32_t ep = 0; ep <= 1; ep++) for (int mu = 0; mu < 3; mu++) {
          OmegaTernGemvSpec s; memset(&s, 0, sizeof s); s.k = ks[a]; s.layout = lay; s.rows = r; s.warps = ws[w]; s.depth = d; s.epi = ep; s.mutant = ms[mu];
          TRY(omega_blackwell_codegen_tern_gemv_prog(&s, &g_p), "tern_k%u_l%u_r%u_w%u_d%u_e%u_m%d", ks[a], lay, r, ws[w], d, ep, ms[mu]); } }
    { static const uint32_t ks[] = { 128, 2048, 16384 };
      for (int a = 0; a < 3; a++) for (uint32_t it = 0; it <= 3; it++) for (int mu = 0; mu <= 2; mu++) {
          OmegaPqSpec s = { ks[a], it, mu };
          if (omega_pq_spec_ok(&s)) TRY(omega_blackwell_codegen_pq_prog(&s, &g_p), "pq_k%u_i%u_m%d", ks[a], it, mu); } }
    { static const uint32_t ks[] = { 128, 2048 }, ns[] = { 256, 768 };
      for (int a = 0; a < 2; a++) for (uint32_t w = 8; w <= 16; w *= 2) for (uint32_t ep = 0; ep <= 1; ep++) for (uint32_t di = 0; di <= 1; di++)
      for (int b = 0; b < 2; b++) for (uint32_t wt = 0; wt <= 8; wt += 8) for (int mu = 0; mu <= 4; mu++) {
          OmegaQgSpec s; memset(&s, 0, sizeof s); s.k = ks[a]; s.rt = 4; s.warps = w; s.epi = ep; s.direct = di; s.n = ns[b]; s.wait = wt; s.mutant = mu;
          if (omega_qg_spec_ok(&s)) TRY(omega_blackwell_codegen_qgemm_prog(&s, &g_p), "qgemm_k%u_w%u_e%u_d%u_n%u_t%u_m%d", ks[a], w, ep, di, ns[b], wt, mu); } }
    { static const uint32_t ks[] = { 128, 2048 }, ns[] = { 512, 2048 };
      for (int a = 0; a < 2; a++) for (uint32_t rt = 2; rt <= 8; rt *= 2) for (uint32_t w = 4; w <= 16; w *= 2) for (uint32_t aq = 1; aq <= 2; aq++)
      for (uint32_t ep = 0; ep <= 1; ep++) for (uint32_t di = 0; di <= 1; di++) for (int b = 0; b < 2; b++) for (int mu = 0; mu <= 2; mu++) {
          OmegaTc2Spec s; memset(&s, 0, sizeof s); s.k = ks[a]; s.rt = rt; s.ct = 2; s.warps = w; s.aqn = aq; s.epi = ep; s.direct = di; s.n = ns[b]; s.mutant = mu;
          TRY(omega_blackwell_codegen_tc2_prog(&s, &g_p), "tc2_k%u_r%u_w%u_a%u_e%u_d%u_n%u_m%d", ks[a], rt, w, aq, ep, di, ns[b], mu); } }
    for (uint32_t w = 8; w <= 32; w *= 2) for (uint32_t d = 1; d <= 8; d *= 2) for (int mu = 0; mu <= 4; mu++) {
        OmegaRouterSpec s; memset(&s, 0, sizeof s); s.warps = w; s.depth = d; s.mutant = mu;
        TRY(omega_blackwell_codegen_router_prog(&s, &g_p), "router_w%u_d%u_m%d", w, d, mu); }
}

#ifdef SM121_WITH_GPU_API
/* elementwise kernels: built inside omega_gpu_elementwise_api.c; omega_gpu_elementwise_codegen_ir hands out the
 * program and the kernel bytes the API itself made (the oracle here is the API's own code buffer) */
#include "../../src/omega_gpu_elementwise_api.h"
static void ew_kernels(void) {
    for (int op = OMEGA_GPU_EW_EX2; op <= OMEGA_GPU_EW_RC_REFUSE_JOIN_ONLY; op++)
        for (int mu = 0; mu <= 3; mu++) {
            OmegaBlackwellKernel kern; memset(&kern, 0, sizeof kern);
            omega_bw_ir_init(&g_p);
            if (omega_gpu_elementwise_codegen_ir((OmegaGpuEwOp)op, mu, &g_p, &kern) != 0 || !kern.code) { g_refused++; continue; }
            char nm[64]; snprintf(nm, sizeof nm, "ew_op%d_m%d", op, mu);
            size_t len = 0;
            if (omega_bw_encode_program(&g_p, g_code, sizeof g_code, &len) != 0 || len != kern.code_size || memcmp(g_code, kern.code, len) != 0)
                note(nm, 0, "elementwise API bytes differ from encoding its own program again (listing check skipped)");
            else dump(nm, &g_p);
            omega_blackwell_kernel_free(&kern);
        }
}
#else
static void ew_kernels(void) { fprintf(g_notes, "-\t0\telementwise kernels not in this build (needs -DSM121_WITH_GPU_API + libomega_gpu.a)\n"); }
#endif

/* ---- edges: every form, registers and numbers at the edges of their fields; words from omega_blackwell_encode_one ---- */
static FILE *g_es2, *g_ebin;
static unsigned long g_edges;
static void edge(BlackwellIRInsn in) {
    static BlackwellIRProgram p;
    uint32_t w[4];
    if (omega_blackwell_encode_one(&in, w) != 0) return;   /* the C encoder refuses it: not a case */
    memset(&p.regalloc, 0, sizeof p.regalloc);
    for (int i = 0; i < BW_MAX_VREGS; i++) p.regalloc.vreg_to_phys[i] = i;
    for (int i = 0; i < BW_MAX_UVREGS; i++) p.regalloc.uvreg_to_phys[i] = i;
    p.insns[0] = in; p.count = 1;
    uint8_t *b = (uint8_t *)w;   /* host is little-endian AArch64, the encoder's byte order */
    int nt = omega_tc_patch_code(&p, b, 16), nq = omega_qmma_patch_code(&p, b, 16);
    if (nt < 0 || nq < 0) return;
    char k[32]; snprintf(k, sizeof k, "edge%lu", g_edges);
    if (list_insn(g_es2, &in, &p.regalloc, nt > 0 || nq > 0, k, 0) != 0) { note(k, 0, "opcode the listing writer does not know"); return; }
    fwrite(w, 4, 4, g_ebin);
    g_edges++;
}
static BlackwellIRInsn I(BlackwellIROpcode op, int d, int a, int b, int c, int u, uint32_t imm) {
    BlackwellIRInsn in; memset(&in, 0, sizeof in);
    in.op = op; in.dst_vreg = d; in.src1_vreg = a; in.src2_vreg = b; in.src3_vreg = c; in.ureg = u; in.imm = imm;
    return in;
}
static void edges(void) {
    static const int R[] = { 0, 1, 2, 85, 170, 254, 255 };
    static const int U[] = { -1, 0, 1, 31 };
    static const uint32_t N32[] = { 0, 1, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u };
    static const uint32_t CTL[] = { 0, 0x000fe200u, 0xffffffffu };
    for (int op = BW_IR_NOP; op <= BW_IR_LDS128; op++)
    for (int ci = 0; ci < 3; ci++)
    for (int ri = 0; ri < 7; ri++) {
        int r = R[ri], s = R[6 - ri], t = R[(ri + 3) % 7];
        for (int ui = 0; ui < 4; ui++)
        for (int vi = 0; vi < 2; vi++) {            /* vi 1: absent sources (-1) where the form allows */
            int a = vi ? -1 : s, b = vi ? -1 : t, c = vi ? -1 : r;
            for (int ni = 0; ni < 6; ni++) {
                uint32_t imm = N32[ni];
                switch (op) {   /* numbers kept inside each field's range (the C refusal or masking is a separate note) */
                case BW_IR_S2R: imm &= 0xffu; break;
                case BW_IR_LDC: case BW_IR_LDC64: imm = (imm & 0xffffu) << 2; break;
                case BW_IR_LDCU: case BW_IR_LDCU64: imm = (imm & 0xffffu) << 3; break;
                case BW_IR_STG_E: case BW_IR_LDG_E_64: case BW_IR_LDG_E_128: imm = (uint32_t)((int32_t)(imm << 8) >> 8); break;
                case BW_IR_LDS128: imm &= 0x7fffffu; break;
                case BW_IR_LDS: case BW_IR_STS: imm &= 0xffffu; break;
                case BW_IR_SHFL_DOWN: imm &= 0x1fu; if (ni == 3) imm |= OMEGA_TC_MARK_BFLY; break;
                case BW_IR_LOP3_LUT: imm &= 0xffu; break;
                case BW_IR_FMNMX_MAX: if (ni == 4) imm = OMEGA_TC_MARK_F2FP; else if (ni == 5) imm = OMEGA_QMMA_MARK_F2E4; break;
                case BW_IR_BSSY: imm = ni == 0 ? 2u : ni == 1 ? 3u : ni == 2 ? 0x10000000u : ni == 3 ? 64u : ni == 4 ? 65u : 1000u; break;
                case BW_IR_BRA: { static const int32_t D[] = { 0, 1, -1, 64, -63, 0x7fffffff }; imm = (uint32_t)D[ni]; break; }
                default: break;
                }
                BlackwellIRInsn in = I((BlackwellIROpcode)op, (op == BW_IR_FSETP && r > 7) ? r & 7 : r, a, b, op == BW_IR_FSEL && c > 7 ? -1 : c,
                                       U[ui], imm);
                in.control = CTL[ci];
                in.is_uniform = (ui & 1) != 0 && r < BW_MAX_UVREGS;   /* a uniform destination indexes the 32 uniform registers */
                in.bar_reg = (uint8_t)(ri & 15);
                if (op == BW_IR_IMAD && ui == 3) in.src2_vreg = -1;            /* reach the uniform and immediate forms */
                if (op == BW_IR_IADD3 || op == BW_IR_EXIT) in.src3_vreg = -1, in.control = 0;
                if (op == BW_IR_ISETP_GE) in.control = 0;
                if (op == BW_IR_HMMA_F16 || op == BW_IR_HMMA_BF16) in.imm = ni == 5 ? OMEGA_QMMA_MARK : 0;
                for (int pi = 0; pi < 3; pi++) {
                    if (pi && op != BW_IR_BRA && op != BW_IR_BSSY && op != BW_IR_BSYNC && op != BW_IR_STG_STRONG_SYS && op != BW_IR_EXIT) break;
                    in.predicate_p0 = pi != 0; in.predicate_not = pi == 2 && op != BW_IR_EXIT;
                    edge(in);
                }
                if (op == BW_IR_NOP || op == BW_IR_MEMBAR_ALL_SYS || op == BW_IR_MEMBAR_SC_SYS || op == BW_IR_CCTL_IVALL || op == BW_IR_BAR_SYNC) break;
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: sm121list kernels|edges DIR\n"); return 2; }
    g_dir = argv[2];
    char path[512];
    snprintf(path, sizeof path, "%s/notes.tsv", g_dir);
    g_notes = fopen(path, strcmp(argv[1], "edges") ? "w" : "a");
    if (!g_notes) { perror(path); return 2; }
    if (!strcmp(argv[1], "kernels")) {
        snprintf(path, sizeof path, "%s/kernels.tsv", g_dir);
        g_tsv = private_output(path, "w");
        if (!g_tsv) { perror(path); return 2; }
        fprintf(g_tsv, "kernel\tinstructions\tcode_bytes\ttc_patched\tqmma_patched\n");
        kernels();
        ew_kernels();
        { uint8_t va[OMEGA_BW_VECADD_CODE_SIZE]; size_t vl = 0;   /* the chip-proven vector add, the rung-6 chip kernel */
          snprintf(path, sizeof path, "%s/vecadd.c.bin", g_dir); FILE *vf = private_output(path, "wb");
          if (!vf || omega_blackwell_encode_vecadd(va, sizeof va, &vl) != 0 || fwrite(va, 1, vl, vf) != vl) { perror(path); return 2; }
          fclose(vf); }
        fclose(g_tsv);
        printf("kernels %lu (instructions %lu), identical repeats skipped %lu, settings refused %lu\n", g_kernels, g_insns, g_dups, g_refused);
    } else if (!strcmp(argv[1], "edges")) {
        snprintf(path, sizeof path, "%s/edges.s2", g_dir); g_es2 = private_output(path, "w");
        snprintf(path, sizeof path, "%s/edges.c.bin", g_dir); g_ebin = private_output(path, "wb");
        if (!g_es2 || !g_ebin) { perror(path); return 2; }
        edges();
        fclose(g_es2); fclose(g_ebin);
        printf("edge cases %lu\n", g_edges);
    } else return 2;
    fclose(g_notes);
    return 0;
}
