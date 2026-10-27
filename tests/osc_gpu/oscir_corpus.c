/* TEST-ONLY INPUT GENERATOR for tests/osc_gpu/oscir_diff.sh (not an implementation).
 * Writes encoded OSC IR inputs, one file each, into DIR:
 *   corpus-*    every unit of the omega#444 decoder corpus (tests/compiler/test_osc_ir_decode.c:
 *               the compiled .osc units plus its two hand-built GPU kernel units)
 *   class-*     that test's hand-built refusal counterexamples (independent writer)
 *   valid-*     seeded valid variants (that test's mutate_unit), canonical encodings
 *   bad-*       seeded INVALID units: one or more field mutations, written with the
 *               canonical writer but without the validate step, so the decoder sees a
 *               well-formed unit that fails osc_ir_validate (every validator rule class)
 *   edit-*      seeded random byte edits of corpus encodings (that test's edit mix)
 * Deterministic (the test's seeded PRNG plus a separate one here).
 * Usage: oscir_corpus DIR   (run from the repository root)
 * Build: see tests/osc_gpu/oscir_diff.sh. */
#include "../../src/compiler/osc_ir.c"
#define main test_osc_ir_decode_main
#include "../compiler/test_osc_ir_decode.c"
#undef main

static const char *g_dir;
static unsigned g_nout;

static void out(const char *tag, unsigned k, const uint8_t *b, size_t n) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s-%05u.ir", g_dir, tag, k);
    FILE *fp = fopen(path, "wb");
    if (!fp || fwrite(b, 1, n, fp) != n) {
        printf("cannot write %s\n", path);
        exit(1);
    }
    fclose(fp);
    g_nout++;
}

/* canonical bytes of u without the validate step (encode_unit is osc_ir.c's writer) */
static size_t raw_encode(const OscUnit *u, uint8_t *buf, size_t cap) {
    W w = {buf, cap, 0, NULL, NULL, 0, false};
    encode_unit(u, &w);
    return w.len <= cap ? w.len : 0;
}

static uint64_t g_r2 = 0x0c1a2d0e5eedf00dULL;
static uint64_t r2(void) { g_r2 ^= g_r2 << 13; g_r2 ^= g_r2 >> 7; g_r2 ^= g_r2 << 17; return g_r2; }
static uint64_t r2n(uint64_t n) { return n ? r2() % n : 0; }
static int16_t r2vr(unsigned n) { return (int16_t)((int)r2n(n + 2) - 1); }

/* one field mutation; every index stays inside the OscUnit arrays */
static void mutate_bad(OscUnit *u) {
    if (!u->nfuncs) return;
    OscFunc *f = &u->funcs[r2n(u->nfuncs)];
    OscInsn *x = f->ninsns ? &f->insns[r2n(f->ninsns)] : NULL;
    OscType *t = f->nvregs ? &f->vtype[r2n(f->nvregs)] : NULL;
    switch (r2n(22)) {
    case 0: if (x) x->dst = r2vr(f->nvregs); break;
    case 1: if (x) x->a = r2vr(f->nvregs); break;
    case 2: if (x) x->b = r2vr(f->nvregs); break;
    case 3: if (x) x->c = r2vr(f->nvregs); break;
    case 4: if (x) x->sub = (uint8_t)r2n(16); break;
    case 5: if (x) x->op = (uint8_t)(1 + r2n(OSC_I_MAD)); break;
    case 6: if (t) t->s = (OscScalar)r2n(OSC_T_LAST + 1); break;
    case 7: if (t) t->ref = (OscRefKind)r2n(5); break;
    case 8: if (t) t->elem = (OscScalar)r2n(OSC_T_LAST + 1); break;
    case 9: if (t) t->len = (uint16_t)r2n(70); break;
    case 10: if (f->nblocks) { OscBlock *b = &f->blocks[r2n(f->nblocks)]; if (r2n(2)) b->count = (uint32_t)r2n(b->count + 2); else b->first = (uint32_t)r2n(f->ninsns + 1); } break;
    case 11: if (x) { x->blk_t = (int16_t)((int)r2n(f->nblocks + 2) - 1); x->blk_f = (int16_t)((int)r2n(f->nblocks + 2) - 1); } break;
    case 12: if (x) { x->callee = (int16_t)((int)r2n(u->nfuncs + 1) - 1); x->nargs = (uint8_t)r2n(OSC_MAX_PARAMS + 1); } break;
    case 13: if (x) x->args[r2n(OSC_MAX_PARAMS)] = r2vr(f->nvregs); break;
    case 14: if (x) x->imm = r2n(3) ? r2n(300) : r2(); break;
    case 15: f->nparams = (uint8_t)r2n((f->nvregs < OSC_MAX_PARAMS ? f->nvregs : OSC_MAX_PARAMS) + 2); if (f->nparams > f->nvregs) f->nparams = (uint8_t)f->nvregs; break;
    case 16: f->ret.s = (OscScalar)r2n(OSC_T_LAST + 1); f->ret.ref = (OscRefKind)r2n(2); break;
    case 17: f->kind = (uint8_t)r2n(2); f->threads = (uint16_t)(f->kind ? r2n(4) ? 32u << r2n(6) : r2n(2048) : 0); break;
    case 18:
        if (u->nstructs) {
            OscStruct *s = &u->structs[r2n(u->nstructs)];
            OscField *fd = &s->fields[r2n(s->nfields)];
            switch (r2n(4)) {
            case 0: fd->alen = (uint16_t)r2n(70); break;
            case 1: fd->off = (uint16_t)r2n(70); break;
            case 2: s->ncells = (uint16_t)r2n(70); break;
            default: fd->s = (OscScalar)r2n(OSC_T_LAST + 1); break;
            }
        } else if (x) x->op = OSC_I_TRAP;
        break;
    case 19: if (u->nfuncs > 1) { memcpy(u->funcs[r2n(u->nfuncs)].name, f->name, OSC_NAME_MAX); } else if (t) t->sid = (uint8_t)r2n(3); break;
    case 20: if (x && f->ninsns > 1) { OscInsn y = *x; *x = f->insns[r2n(f->ninsns)]; f->insns[r2n(f->ninsns)] = y; } break;
    default: if (t) { t->s = OSC_T_REF; t->ref = (OscRefKind)(1 + r2n(4)); t->elem = (OscScalar)r2n(OSC_T_LAST + 1); t->len = (uint16_t)r2n(70); } break;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    g_dir = argv[1];
    g_dec = calloc(1, sizeof *g_dec);
    g_tmp = calloc(1, sizeof *g_tmp);
    g_digs = calloc(DIG_CAP, 32);
    g_e1 = malloc(OSC_IR_ENCODED_MAX);
    g_e2 = malloc(OSC_IR_ENCODED_MAX);
    if (!g_dec || !g_tmp || !g_digs || !g_e1 || !g_e2) return 1;
    load_dir("tests/compiler/progs");
    load_dir("tests/compiler/slices");
    load_dir("src/osh");
    load_dir("tests/physics0/osc");
    add_kernel_unit("vadd", 0);
    add_kernel_unit("kall", 1);
    if (g_ncorpus == 0) return 1;
    for (int i = 0; i < g_ncorpus; i++) out("corpus", (unsigned)i, g_corpus[i].enc, g_corpus[i].len);

    /* the refusal counterexamples of test_classes, same knobs */
    {
        Buf m;
        unsigned k = 0;
        Knobs d = knobs_default(), q;
        static const char n64[] = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl";
#define KB(stmt) do { q = d; stmt; build(&m, &q); out("class", k++, m.b, m.n); } while (0)
        KB((void)0);
        KB(q.version = 0);
        KB(q.version = OSC_IR_VERSION_MAX + 1);
        KB(q.version = 7; q.kind = 1);
        KB(q.version = 8; q.kind = 1);
        KB(q.name_len = 64; q.name = n64);
        KB(q.nvregs = OSC_MAX_VREGS + 1);
        KB(q.nvregs = OSC_MAX_VREGS);
        KB(q.req_len = OSC_CLAUSE_MAX);
        KB(q.req_len = OSC_CLAUSE_MAX - 1);
        KB(q.op0 = 0);
        for (unsigned op = 16; op <= 50; op++) KB(q.op0 = op);
        for (unsigned op = 28; op <= 50; op++) KB(q.op0 = op; q.version = 6; q.kind = 1);
        for (unsigned op = 28; op <= 50; op++) KB(q.op0 = op; q.version = 8; q.kind = 1);
        KB(q.ret_tag = OSC_T_LAST + 1);
        KB(q.ret_tag = OSC_T_F32);
        KB(q.dst = 0x8000);
        KB(q.dst = 0xFFFF);
        KB(q.name_len = 3; q.name = "f\0g");
        KB(q.imm = 256);
        KB(q.dst = 1);
        KB(q.version = 2; q.struct_table = 1);
        KB(q.version = 5; q.struct_table = 1);
        KB(q.version = 6; q.struct_table = 1; q.kind = 0);
        KB(q.version = 6; q.struct_table = 1; q.kind = 1);
        KB(q.version = 6; q.struct_table = 1; q.kind = 2);
        KB(q.version = 6; q.struct_table = 1; q.kind = 1; q.ret_tag = OSC_T_VOID);
        KB(q.version = 8; q.struct_table = 1; q.kind = 0);
        KB(q.version = 8; q.struct_table = 1; q.kind = 1);
        KB(q.version = 8; q.struct_table = 1; q.kind = 2);
        KB(q.version = 8; q.struct_table = 1; q.kind = 1; q.ret_tag = OSC_T_VOID);
#undef KB
        build(&m, &d);
        m.b[3] = '2';
        out("class", k++, m.b, m.n);
        build(&m, &d);
        for (size_t l = 0; l <= m.n; l++) out("classpfx", (unsigned)l, m.b, l);
        size_t big = (size_t)OSC_IR_ENCODED_MAX + 1;
        uint8_t *x = calloc(1, big);
        memcpy(x, m.b, m.n);
        out("class", k++, x, big);
        free(x);
        m.b[m.n++] = 0;
        out("class", k++, m.b, m.n);
    }

    /* valid variants */
    for (unsigned v = 0, tries = 0; v < 600 && tries < 20000; tries++) {
        copy_unit(g_tmp, g_corpus[rndn((uint64_t)g_ncorpus)].u);
        unsigned k = 1 + (unsigned)rndn(3);
        for (unsigned j = 0; j < k; j++) mutate_unit(g_tmp);
        size_t n = 0;
        if (osc_ir_encode(g_tmp, g_e1, OSC_IR_ENCODED_MAX, &n)) continue;
        out("valid", v++, g_e1, n);
    }

    /* invalid units, well-formed bytes */
    for (int i = 0; i < g_ncorpus; i++) {
        for (unsigned r = 0; r < 40; r++) {
            copy_unit(g_tmp, g_corpus[i].u);
            unsigned k = 1 + (unsigned)r2n(r < 30 ? 1 : 3);
            for (unsigned j = 0; j < k; j++) mutate_bad(g_tmp);
            size_t n = raw_encode(g_tmp, g_e1, OSC_IR_ENCODED_MAX);
            if (n) out("bad", (unsigned)i * 40 + r, g_e1, n);
        }
    }

    /* random byte edits (test_malformed's edit mix) */
    {
        uint8_t *m = malloc(1 << 20);
        unsigned e = 0;
        for (int i = 0; i < g_ncorpus; i++) {
            const Entry *c = &g_corpus[i];
            if (c->len + 64 > (1u << 20)) continue;
            for (unsigned r = 0; r < 12; r++) {
                size_t n = c->len;
                memcpy(m, c->enc, n);
                unsigned edits = 1 + (unsigned)rndn(4);
                for (unsigned j = 0; j < edits; j++) {
                    size_t p = (size_t)rndn(n ? n : 1);
                    switch (rndn(5)) {
                    case 0: if (n) m[p] ^= (uint8_t)(1u << rndn(8)); break;
                    case 1: if (n) m[p] = (uint8_t)rnd(); break;
                    case 2: if (n) { memmove(m + p, m + p + 1, n - p - 1); n--; } break;
                    case 3: memmove(m + p + 1, m + p, n - p); m[p] = (uint8_t)rnd(); n++; break;
                    case 4: if (n > 2) { uint16_t v = (uint16_t)(rndn(2) ? 0xFFFF : rnd()); size_t q = p < n - 2 ? p : n - 2; memcpy(m + q, &v, 2); } break;
                    }
                }
                out("edit", e++, m, n);
            }
        }
        free(m);
    }
    printf("oscir_corpus: wrote %u files (%d corpus units)\n", g_nout, g_ncorpus);
    return 0;
}
