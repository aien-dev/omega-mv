/* om_oracle -- TEST-ONLY ORACLE AND GLUE for the seeded .om GPU lowering (seed/oscgpu, C1 of docs/OMEGA.md).
 * It adds no back end: every mode calls src/omega_osc_gpu.c as it stands.
 *
 *   om_oracle dump   <unit.ir> <kernel-name> <out.bwir> <mutant>         the C lowering (osc_gpu_lower), as the .om writes it
 *   om_oracle build  <unit.ir> <kernel-name> <out.code> <out.man> <mutant> the whole C back end (osc_gpu_build)
 *   om_oracle encode <in.bwir> <out.code> <out.man>                       a lowering dump (the .om's or the C's) through the C
 *                                                                          register allocator and encoder (osc_gpu_encode)
 *   om_oracle names    <unit.ir>                                       the unit's function names, one per line
 *   om_oracle variants <unit.ir> <out-dir>                             writes <out-dir>/vNNNNN.ir, the differential's variants
 *
 * The unit is read and the kernel picked exactly as tools/osc_gpu/osc_gpu_build does (at most 1 MiB, the last
 * function of that name), and refusals are the same lines on stdout. Exit 0 written; 1 refused; 2 usage or I/O.
 *
 * The dump (.bwir) is text, one record per line, decimal:
 *   bwir 1 / name N / threads / shared_bytes / nparams / param i kind elem off (each) / status_off / blocks_off /
 *   batches_off / arg_words / reads_block / reads_batch / regions / region_bars / count / num_vregs / num_uvregs /
 *   ra peak_gpr peak_ugpr gpr_limit /
 *   v i vreg first_def last_use phys_reg bundle_size is_pair is_quad active vreg_to_phys (each vreg) /
 *   u i ... uvreg_to_phys (each uniform vreg) /
 *   i idx op dst src1 src2 src3 ureg imm control is_uniform p0 p_not dst_sub src1_sub src2_sub src3_sub bar_reg (each insn) / end
 * encode reads a dump back only if writing what it read gives the same bytes: anything else is refused (fail closed).
 * The manifest (.man) is: name, threads, shared_bytes, nparams, param i kind elem off, status_off, blocks_off,
 * batches_off, arg_words, reads_block, reads_batch, regions, region_bars, code_size, insn_count, gpr_count, code_sha256. */
#include "omega_osc_gpu.h"
#include "sha256.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *create(const char *path, const char *mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, mode);
    if (!f) close(fd);
    return f;
}

static void dump(FILE *o, const OscGpuKernel *k, const BlackwellIRProgram *p) {
    const OmegaRegAlloc *ra = &p->regalloc;
    fprintf(o, "bwir 1\nname %s\nthreads %u\nshared_bytes %u\nnparams %u\n", k->name, k->threads, k->shared_bytes, k->nparams);
    for (unsigned i = 0; i < k->nparams; i++) fprintf(o, "param %u %u %u %u\n", i, k->params[i].kind, k->params[i].elem, k->params[i].off);
    fprintf(o, "status_off %u\nblocks_off %u\nbatches_off %u\narg_words %u\nreads_block %d\nreads_batch %d\nregions %u\nregion_bars %u\n",
            k->status_off, k->blocks_off, k->batches_off, k->arg_words, k->reads_block, k->reads_batch, k->regions, k->region_bars);
    fprintf(o, "count %zu\nnum_vregs %d\nnum_uvregs %d\nra %u %u %u\n", p->count, ra->num_vregs, ra->num_uvregs, ra->peak_gpr_usage,
            ra->peak_ugpr_usage, ra->gpr_limit);
    for (int i = 0; i < ra->num_vregs; i++) {
        const OmegaLiveInterval *v = &ra->intervals[i];
        fprintf(o, "v %d %d %d %d %d %u %d %d %d %d\n", i, v->vreg, v->first_def, v->last_use, v->phys_reg, v->bundle_size, v->is_pair,
                v->is_quad, v->active, ra->vreg_to_phys[i]);
    }
    for (int i = 0; i < ra->num_uvregs; i++) {
        const OmegaLiveInterval *v = &ra->uintervals[i];
        fprintf(o, "u %d %d %d %d %d %u %d %d %d %d\n", i, v->vreg, v->first_def, v->last_use, v->phys_reg, v->bundle_size, v->is_pair,
                v->is_quad, v->active, ra->uvreg_to_phys[i]);
    }
    for (size_t i = 0; i < p->count; i++) {
        const BlackwellIRInsn *s = &p->insns[i];
        fprintf(o, "i %zu %u %d %d %d %d %d %u %u %d %d %d %u %u %u %u %u\n", i, (unsigned)s->op, s->dst_vreg, s->src1_vreg, s->src2_vreg,
                s->src3_vreg, s->ureg, s->imm, s->control, s->is_uniform, s->predicate_p0, s->predicate_not, s->dst_subreg, s->src1_subreg,
                s->src2_subreg, s->src3_subreg, s->bar_reg);
    }
    fprintf(o, "end\n");
}

/* one dump line: the keyword, then up to 17 integers */
static int line(char **cur, const char *key, long long *x, int nx) {
    char *nl = strchr(*cur, '\n');
    if (!nl) return -1;
    *nl = 0;
    char *s = *cur;
    *cur = nl + 1;
    size_t kl = strlen(key);
    if (strncmp(s, key, kl) != 0) return -1;
    s += kl;
    for (int i = 0; i < nx; i++) {
        if (*s != ' ') return -1;
        char *e;
        x[i] = strtoll(s + 1, &e, 10);
        if (e == s + 1) return -1;
        s = e;
    }
    return *s ? -1 : 0;
}

static int in(long long v, long long lo, long long hi) { return v >= lo && v <= hi; }

/* the dump in buf (NUL-terminated) to k and p; -1 when it is not a dump this oracle writes */
static int load(char *buf, OscGpuKernel *k, BlackwellIRProgram *p) {
    memset(k, 0, sizeof *k);
    omega_bw_ir_init(p);
    OmegaRegAlloc *ra = &p->regalloc;
    char *c = buf;
    long long x[17];
    if (line(&c, "bwir", x, 1) || x[0] != 1) return -1;
    char *nl = strchr(c, '\n');
    if (!nl || strncmp(c, "name ", 5) || (size_t)(nl - c - 5) >= sizeof k->name) return -1;
    memcpy(k->name, c + 5, (size_t)(nl - c - 5));
    c = nl + 1;
    if (line(&c, "threads", x, 1) || !in(x[0], 1, 65535)) return -1;
    k->threads = (uint16_t)x[0];
    if (line(&c, "shared_bytes", x, 1) || !in(x[0], 0, 0xffffffffll)) return -1;
    k->shared_bytes = (uint32_t)x[0];
    if (line(&c, "nparams", x, 1) || !in(x[0], 0, OSC_MAX_KPARAMS)) return -1;
    k->nparams = (uint8_t)x[0];
    for (unsigned i = 0; i < k->nparams; i++) {
        if (line(&c, "param", x, 4) || x[0] != i || !in(x[1], 0, 255) || !in(x[2], 0, 255) || !in(x[3], 0, 65535)) return -1;
        k->params[i].kind = (uint8_t)x[1]; k->params[i].elem = (uint8_t)x[2]; k->params[i].off = (uint16_t)x[3];
    }
    const char *u16k[] = { "status_off", "blocks_off", "batches_off", "arg_words" };
    uint16_t *u16v[] = { &k->status_off, &k->blocks_off, &k->batches_off, &k->arg_words };
    for (int i = 0; i < 4; i++) { if (line(&c, u16k[i], x, 1) || !in(x[0], 0, 65535)) return -1; *u16v[i] = (uint16_t)x[0]; }
    if (line(&c, "reads_block", x, 1) || !in(x[0], 0, 1)) return -1;
    k->reads_block = x[0];
    if (line(&c, "reads_batch", x, 1) || !in(x[0], 0, 1)) return -1;
    k->reads_batch = x[0];
    if (line(&c, "regions", x, 1) || !in(x[0], 0, 0xffffffffll)) return -1;
    k->regions = (uint32_t)x[0];
    if (line(&c, "region_bars", x, 1) || !in(x[0], 0, 0xffffffffll)) return -1;
    k->region_bars = (uint32_t)x[0];
    if (line(&c, "count", x, 1) || !in(x[0], 0, BW_MAX_IR_INSNS)) return -1;
    p->count = (size_t)x[0];
    if (line(&c, "num_vregs", x, 1) || !in(x[0], 0, BW_MAX_VREGS)) return -1;
    ra->num_vregs = (int)x[0];
    if (line(&c, "num_uvregs", x, 1) || !in(x[0], 0, BW_MAX_UVREGS)) return -1;
    ra->num_uvregs = (int)x[0];
    if (line(&c, "ra", x, 3) || !in(x[0], 0, 0xffffffffll) || !in(x[1], 0, 0xffffffffll) || !in(x[2], 0, 0xffffffffll)) return -1;
    ra->peak_gpr_usage = (uint32_t)x[0]; ra->peak_ugpr_usage = (uint32_t)x[1]; ra->gpr_limit = (uint32_t)x[2];
    for (int pass = 0; pass < 2; pass++) {
        unsigned n = (unsigned)(pass ? ra->num_uvregs : ra->num_vregs);
        for (unsigned i = 0; i < n; i++) {
            OmegaLiveInterval *v = pass ? &ra->uintervals[i] : &ra->intervals[i];
            if (line(&c, pass ? "u" : "v", x, 10) || x[0] != i) return -1;
            for (int j = 1; j < 10; j++) if (!in(x[j], -2147483648ll, 2147483647ll)) return -1;
            if (!in(x[5], 0, 0xffffffffll) || !in(x[6], 0, 1) || !in(x[7], 0, 1) || !in(x[8], 0, 1)) return -1;
            v->vreg = (int)x[1]; v->first_def = (int)x[2]; v->last_use = (int)x[3]; v->phys_reg = (int)x[4];
            v->bundle_size = (uint32_t)x[5]; v->is_pair = x[6]; v->is_quad = x[7]; v->active = x[8];
            if (pass) ra->uvreg_to_phys[i] = (int)x[9]; else ra->vreg_to_phys[i] = (int)x[9];
        }
    }
    for (size_t i = 0; i < p->count; i++) {
        BlackwellIRInsn *s = &p->insns[i];
        if (line(&c, "i", x, 17) || x[0] != (long long)i || !in(x[1], 0, 255)) return -1;
        for (int j = 2; j < 7; j++) if (!in(x[j], -2147483648ll, 2147483647ll)) return -1;
        if (!in(x[7], 0, 0xffffffffll) || !in(x[8], 0, 0xffffffffll) || !in(x[9], 0, 1) || !in(x[10], 0, 1) || !in(x[11], 0, 1)) return -1;
        for (int j = 12; j < 17; j++) if (!in(x[j], 0, 255)) return -1;
        s->op = (BlackwellIROpcode)x[1]; s->dst_vreg = (int)x[2]; s->src1_vreg = (int)x[3]; s->src2_vreg = (int)x[4]; s->src3_vreg = (int)x[5];
        s->ureg = (int)x[6]; s->imm = (uint32_t)x[7]; s->control = (uint32_t)x[8]; s->is_uniform = x[9]; s->predicate_p0 = x[10];
        s->predicate_not = x[11]; s->dst_subreg = (uint8_t)x[12]; s->src1_subreg = (uint8_t)x[13]; s->src2_subreg = (uint8_t)x[14];
        s->src3_subreg = (uint8_t)x[15]; s->bar_reg = (uint8_t)x[16];
    }
    if (strcmp(c, "end\n") != 0) return -1;
    return 0;
}

static int man(const char *path, const OscGpuKernel *k) {
    FILE *o = create(path, "w");
    if (!o) return -1;
    fprintf(o, "name %s\nthreads %u\nshared_bytes %u\nnparams %u\n", k->name, k->threads, k->shared_bytes, k->nparams);
    for (unsigned i = 0; i < k->nparams; i++) fprintf(o, "param %u %u %u %u\n", i, k->params[i].kind, k->params[i].elem, k->params[i].off);
    fprintf(o, "status_off %u\nblocks_off %u\nbatches_off %u\narg_words %u\nreads_block %d\nreads_batch %d\nregions %u\nregion_bars %u\n",
            k->status_off, k->blocks_off, k->batches_off, k->arg_words, k->reads_block, k->reads_batch, k->regions, k->region_bars);
    fprintf(o, "code_size %zu\ninsn_count %u\ngpr_count %u\ncode_sha256 ", k->code_size, k->insn_count, k->gpr_count);
    for (int i = 0; i < 32; i++) fprintf(o, "%02x", k->code_sha256[i]);
    fprintf(o, "\n");
    return fclose(o) ? -1 : 0;
}

static int code(const char *path, const OscGpuKernel *k) {
    FILE *o = create(path, "wb");
    if (!o) return -1;
    if (fwrite(k->code, 1, k->code_size, o) != k->code_size) { fclose(o); return -1; }
    return fclose(o) ? -1 : 0;
}

static int mutant(const char *s) {
    char *e;
    long m = strtol(s, &e, 10);
    if (e == s || *e || m < 0 || m > 8) { fprintf(stderr, "om_oracle: mutant must be 0..8\n"); exit(2); }
    return (int)m;
}

/* the unit and the kernel exactly as osc_gpu_build picks them; -1 refused (printed), -2 I/O */
static int unit(const char *path, const char *kname, OscUnit *u) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return -2; }
    static uint8_t buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    int rc = osc_ir_decode(buf, n, u);
    if (rc) { printf("REFUSED decode: %s\n", osc_ir_refusal_name(rc)); return -1; }
    int fn = -1;
    for (int i = 0; i < u->nfuncs; i++) if (!strcmp(u->funcs[i].name, kname)) fn = i;
    if (fn < 0) { printf("REFUSED: no function %s in %s\n", kname, path); return -1; }
    return fn;
}

/* variants: units one field away from a corpus unit (an instruction's sub, imm, operand register or branch target,
 * a function's kind or thread count, a register's type), plus the unit with one host function appended, kept when
 * osc_ir_decode still admits them (so osc_ir_validate passes them) and their bytes are new. They reach the back
 * end's own refusals and shapes no corpus kernel has. Deterministic order. */
static OscUnit g_var, g_chk;
static uint8_t g_vbuf[1 << 20];
static uint8_t g_seen[1 << 16][32];
static int g_nseen, g_nvar;
static const char *g_vdir;
static void vkeep(void) {
    size_t len;
    uint8_t h[32];
    if (osc_ir_encode(&g_var, g_vbuf, sizeof g_vbuf, &len) != 0) return;
    if (osc_ir_decode(g_vbuf, len, &g_chk) != 0) return;
    sha256_hash(g_vbuf, len, h);
    for (int i = 0; i < g_nseen; i++) if (!memcmp(g_seen[i], h, 32)) return;
    if (g_nseen == (int)(sizeof g_seen / sizeof *g_seen)) { fprintf(stderr, "om_oracle: too many variants\n"); exit(2); }
    memcpy(g_seen[g_nseen++], h, 32);
    if (g_nseen == 1) return; /* the unit itself */
    char path[512];
    snprintf(path, sizeof path, "%s/v%05d.ir", g_vdir, g_nvar);
    FILE *o = create(path, "wb");
    if (!o || fwrite(g_vbuf, 1, len, o) != len || fclose(o)) { perror(path); exit(2); }
    g_nvar++;
}
/* set a field, keep the unit if it is a new admitted one, restore the field */
#define V(lv, val) do { __typeof__(lv) sv_ = (lv); (lv) = (val); vkeep(); (lv) = sv_; } while (0)
static void variants(void) {
    static const uint16_t thr[] = { 0, 16, 32, 48, 64, 992, 1024, 1056, 2048 };
    static const uint64_t imm[] = { 0, 1, 2, 3, 15, 16, 31, 32, 33, 255, 256, 0x7fffffffu, 0x80000000u, 0xffffffffu };
    static const uint8_t sc[] = { OSC_T_BOOL, OSC_T_U32, OSC_T_F32 };
    static const uint16_t lens[] = { 1, 2, 31, 32, 4096, 12288, 12289 };
    vkeep(); /* the unit itself, so no variant repeats it */
    for (unsigned f = 0; f < g_var.nfuncs; f++) {
        OscFunc *F = &g_var.funcs[f];
        V(F->kind, (uint8_t)(F->kind ^ 1));
        for (unsigned t = 0; t < sizeof thr / sizeof *thr; t++) V(F->threads, thr[t]);
        for (unsigned i = 0; i < F->ninsns; i++) {
            OscInsn *x = &F->insns[i];
            for (unsigned s = 0; s < 16; s++) V(x->sub, (uint8_t)s);
            for (unsigned m = 0; m < sizeof imm / sizeof *imm; m++) V(x->imm, imm[m]);
            for (int v = -1; v < (int)F->nvregs; v++) { V(x->a, (int16_t)v); V(x->b, (int16_t)v); V(x->c, (int16_t)v); }
            for (int b = 0; b < (int)F->nblocks; b++) { V(x->blk_t, (int16_t)b); V(x->blk_f, (int16_t)b); }
        }
        for (unsigned v = 0; v < F->nvregs; v++) {
            OscType *t = &F->vtype[v];
            for (unsigned s = 0; s < sizeof sc / sizeof *sc; s++) V(t->s, sc[s]);
            for (unsigned l = 0; l < sizeof lens / sizeof *lens; l++) V(t->len, lens[l]);
            for (unsigned s = 0; s < sizeof sc / sizeof *sc; s++) V(t->elem, sc[s]);
        }
    }
    if (g_var.nfuncs < OSC_MAX_FUNCS) { /* a host function `host` returning nothing */
        OscFunc *H = &g_var.funcs[g_var.nfuncs];
        memset(H, 0, sizeof *H);
        snprintf(H->name, sizeof H->name, "host");
        H->kind = OSC_FN_HOST;
        H->ninsns = 1; H->insns[0].op = OSC_I_RET;
        H->insns[0].dst = H->insns[0].a = H->insns[0].b = H->insns[0].c = H->insns[0].blk_t = H->insns[0].blk_f = H->insns[0].callee = -1;
        H->nblocks = 1; H->blocks[0].first = 0; H->blocks[0].count = 1;
        g_var.nfuncs++;
        vkeep();
        g_var.nfuncs--;
    }
}
#undef V

int main(int argc, char **argv) {
    static OscUnit u;
    static BlackwellIRProgram p;
    OscGpuKernel k;
    char err[256];
    if (argc == 3 && !strcmp(argv[1], "names")) {
        FILE *fp = fopen(argv[2], "rb");
        if (!fp) { perror(argv[2]); return 2; }
        static uint8_t buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf, fp);
        fclose(fp);
        if (osc_ir_decode(buf, n, &u) != 0) return 1;
        for (unsigned i = 0; i < u.nfuncs; i++) printf("%s\n", u.funcs[i].name);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "variants")) {
        FILE *fp = fopen(argv[2], "rb");
        if (!fp) { perror(argv[2]); return 2; }
        static uint8_t buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf, fp);
        fclose(fp);
        if (osc_ir_decode(buf, n, &g_var) != 0) { printf("REFUSED decode\n"); return 1; }
        g_vdir = argv[3];
        variants();
        printf("%d\n", g_nvar);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "dump")) {
        osc_gpu_mutant = mutant(argv[5]);
        int fn = unit(argv[2], argv[3], &u);
        if (fn < 0) return fn == -1 ? 1 : 2;
        if (osc_gpu_lower(&u, (unsigned)fn, &k, &p, err, sizeof err) != 0) { printf("REFUSED build: %s\n", err); return 1; }
        FILE *o = create(argv[4], "w");
        if (!o) { perror(argv[4]); return 2; }
        dump(o, &k, &p);
        if (fclose(o)) { perror(argv[4]); return 2; }
        return 0;
    }
    if (argc == 7 && !strcmp(argv[1], "build")) {
        osc_gpu_mutant = mutant(argv[6]);
        int fn = unit(argv[2], argv[3], &u);
        if (fn < 0) return fn == -1 ? 1 : 2;
        if (osc_gpu_build(&u, (unsigned)fn, &k, err, sizeof err) != 0) { printf("REFUSED build: %s\n", err); return 1; }
        if (code(argv[4], &k) || man(argv[5], &k)) { perror("om_oracle"); return 2; }
        osc_gpu_free(&k);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "encode")) {
        FILE *fp = fopen(argv[2], "rb");
        if (!fp) { perror(argv[2]); return 2; }
        static char buf[1 << 22], again[1 << 22];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        int big = !feof(fp);
        fclose(fp);
        buf[n] = 0;
        memcpy(again, buf, n + 1);
        if (big || strlen(buf) != n || load(again, &k, &p) != 0) { printf("REFUSED dump: %s is not a lowering dump\n", argv[2]); return 1; }
        /* canonical: writing what was read gives the same bytes */
        char *w = NULL; size_t wn = 0;
        FILE *m = open_memstream(&w, &wn);
        if (!m) { perror("om_oracle"); return 2; }
        dump(m, &k, &p);
        fclose(m);
        int same = wn == n && memcmp(w, buf, n) == 0;
        free(w);
        if (!same) { printf("REFUSED dump: %s is not a lowering dump\n", argv[2]); return 1; }
        if (osc_gpu_encode(&k, &p, err, sizeof err) != 0) { printf("REFUSED build: %s\n", err); free(k.code); return 1; }
        if (code(argv[3], &k) || man(argv[4], &k)) { perror("om_oracle"); return 2; }
        free(k.code);
        return 0;
    }
    fprintf(stderr, "usage: om_oracle dump <unit.ir> <kernel-name> <out.bwir> <mutant>\n"
                    "       om_oracle build <unit.ir> <kernel-name> <out.code> <out.man> <mutant>\n"
                    "       om_oracle encode <in.bwir> <out.code> <out.man>\n"
                    "       om_oracle names <unit.ir>\n"
                    "       om_oracle variants <unit.ir> <out-dir>\n");
    return 2;
}
