/* c2_check.c: checker2 acceptance rules over a decoded unit (spec/one-layer-receipts.md section 9).
 *
 * Written from docs/osc/OSC-{1,2,3}-DESIGN.md, OSC-EXT-BYTES-DESIGN.md and the osc_ir.h comments
 * only. Rules, in the order applied:
 *   unit tables  -> per-function types -> CFG shape -> per-instruction typing -> calls ->
 *   GPU kernel rules -> handle disciplines -> definite assignment -> loop bounds (termination) ->
 *   ownership flow.
 * GPU kernels (version 6) are restated from docs/omegese/GPU.md sections 6-9: their types, the ops
 * each function kind may hold, block-shared arrays, constant divisors, and the uniformity rule for
 * SYNC and SHFL.
 * The ownership flow is a may-analysis that rejects only DEFINITE errors (a state that holds on
 * every path), so it cannot reject a correct program through imprecision; drop-flag guarded
 * releases (OSC-3 item 3) are therefore never refused by it. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c2_ir.h"

static int fail(C2Verdict *v, const char *rule, int func, int insn, const char *fmt, ...)
{
    if (v) {
        v->ok = 0;
        v->rule = rule;
        v->func = func;
        v->insn = insn;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(v->detail, sizeof v->detail, fmt, ap);
        va_end(ap);
    }
    return -1;
}

/* ------------------------------------------------------------------ types */

static int is_value(uint8_t s) { return s >= S_BOOL && s <= S_I64; }
static int is_int(uint8_t s) { return s >= S_U8 && s <= S_I64; }
static int is_signed(uint8_t s) { return s >= S_I8 && s <= S_I64; }
static int is_num(uint8_t s) { return is_value(s) || s == S_F32; } /* a register value: bool, integer or f32 */
static int is_kvalue(uint8_t s) { return s == S_U32 || s == S_F32; } /* GPU buffer and shared elements */
static unsigned width(uint8_t s)
{
    switch (s) {
    case S_BOOL: return 1;
    case S_U8: case S_I8: return 8;
    case S_U16: case S_I16: return 16;
    case S_U32: case S_I32: case S_F32: return 32;
    case S_U64: case S_I64: return 64;
    default: return 0;
    }
}
static int is_scalar_t(const C2Type *t) { return is_num(t->s); }
static int is_gbuf(const C2Type *t) { return t->s == S_GVIEW || t->s == S_GEDIT; }
static int is_block(const C2Type *t) { return t->s == S_REF && t->kind == K_BLOCK; }
static int is_ref(const C2Type *t) { return t->s == S_REF; }
static int is_array_ref(const C2Type *t) { return t->s == S_REF && t->sid == 0; }
static int is_struct_ref(const C2Type *t) { return t->s == S_REF && t->sid != 0; }
static int is_slice(const C2Type *t) { return t->s == S_BYTES || t->s == S_CELLS; }
static int is_u64(const C2Type *t) { return t->s == S_U64; }
static int same_type(const C2Type *a, const C2Type *b)
{
    if (a->s != b->s) return 0;
    if (is_gbuf(a)) return a->elem == b->elem;
    if (a->s != S_REF) return 1;
    return a->kind == b->kind && a->elem == b->elem && a->len == b->len && a->sid == b->sid;
}
/* Same object shape (element, length, struct), any reference kind. */
static int same_shape(const C2Type *a, const C2Type *b)
{
    return a->s == S_REF && b->s == S_REF && a->elem == b->elem && a->len == b->len && a->sid == b->sid;
}
/* A reference of kind `from` may be bound to a destination of kind `to` (move, borrow, reborrow,
 * downgrade): own->own|shared|mut, mut->mut|shared, shared->shared. */
static int bindable(uint8_t to, uint8_t from)
{
    if (to == K_BLOCK || from == K_BLOCK) return 0; /* a shared array is never moved or borrowed */
    if (from == K_OWN) return 1;
    if (from == K_MUT) return to == K_MUT || to == K_SHARED;
    if (from == K_SHARED) return to == K_SHARED;
    return 0;
}
/* imm is the canonical 64-bit value of a constant of scalar s */
static int canonical(uint8_t s, uint64_t imm)
{
    unsigned w = width(s);
    if (s == S_BOOL) return imm <= 1;
    if (w == 64) return 1;
    if (!is_signed(s)) return imm < (1ull << w);
    int64_t x = (int64_t)imm;
    int64_t lo = -(int64_t)(1ull << (w - 1)), hi = (int64_t)(1ull << (w - 1)) - 1;
    return x >= lo && x <= hi;
}

/* ------------------------------------------------------------------ unit tables */

static int check_structs(const C2Unit *u, C2Verdict *v)
{
    for (int k = 0; k < u->nstructs; k++) {
        const C2Struct *s = &u->st[k];
        for (int j = 0; j < k; j++)
            if (strcmp(u->st[j].name, s->name) == 0)
                return fail(v, "C2-STRUCT-NAME", -1, -1, "struct %s defined twice", s->name);
        unsigned off = 0;
        for (int f = 0; f < s->nfields; f++) {
            const C2Field *fd = &s->f[f];
            for (int g = 0; g < f; g++)
                if (strcmp(s->f[g].name, fd->name) == 0)
                    return fail(v, "C2-STRUCT-NAME", -1, -1, "field %s.%s twice", s->name, fd->name);
            if (!is_value(fd->s)) return fail(v, "C2-STRUCT-FIELD-TYPE", -1, -1, "%s.%s", s->name, fd->name);
            if (fd->alen > C2_MAX_ARRAY)
                return fail(v, "C2-STRUCT-FIELD-LEN", -1, -1, "%s.%s length %u", s->name, fd->name, fd->alen);
            if (fd->off != off) /* GUARD:struct-offset */
                return fail(v, "C2-STRUCT-LAYOUT", -1, -1, "%s.%s offset %u, prefix sum %u", s->name, fd->name, fd->off, off);
            off += fd->alen ? fd->alen : 1;
        }
        if (s->ncells != off || off < 1 || off > C2_MAX_ARRAY) /* GUARD:struct-cells */
            return fail(v, "C2-STRUCT-LAYOUT", -1, -1, "%s cells %u, fields need %u", s->name, s->ncells, off);
    }
    return 0;
}

static int check_type(const C2Unit *u, const C2Type *t)
{
    if (t->s >= S_LIMIT || t->s == S_VOID) return 0;
    if (is_gbuf(t)) return is_kvalue(t->elem);
    if (t->s != S_REF) return 1;
    if (t->kind == K_BLOCK) return is_kvalue(t->elem) && t->sid == 0 && t->len >= 1 && t->len <= C2_MAX_SHARED_LEN;
    if (t->kind < K_OWN || t->kind > K_MUT) return 0;
    if (t->elem == S_VOID) { /* struct reference */
        if (t->sid < 1 || t->sid > u->nstructs) return 0;
        return t->len == u->st[t->sid - 1].ncells;
    }
    return is_value(t->elem) && t->sid == 0 && t->len >= 1 && t->len <= C2_MAX_ARRAY;
}

/* ------------------------------------------------------------------ per-function context */

#define BSW 8 /* 512 bits: enough for C2_MAX_VREGS (480) and C2_MAX_BLOCKS (512) */
typedef struct { uint64_t w[BSW]; } Bits;
static void bset(Bits *b, unsigned i) { b->w[i >> 6] |= 1ull << (i & 63); }
static int btest(const Bits *b, unsigned i) { return (int)((b->w[i >> 6] >> (i & 63)) & 1); }
static void ball(Bits *b) { memset(b, 0xff, sizeof *b); }
static void band(Bits *d, const Bits *s) { for (int i = 0; i < BSW; i++) d->w[i] &= s->w[i]; }
static int beq(const Bits *a, const Bits *b) { return memcmp(a, b, sizeof *a) == 0; }

typedef struct {
    const C2Unit *u;
    const C2Func *f;
    int fi;
    C2Verdict *v;
    int nb;
    int *blk_of;          /* insn -> block */
    int succ[C2_MAX_BLOCKS][2];
    int nsucc[C2_MAX_BLOCKS];
    int reach[C2_MAX_BLOCKS];
    Bits dom[C2_MAX_BLOCKS];
    int ndefs[C2_MAX_VREGS];
    int def_insn[C2_MAX_VREGS]; /* the single definition, or -1 */
    int quiet;                  /* ownership fixpoint: apply effects, report nothing */
} Fx;

static int term(uint8_t op) { return op == C2_BR || op == C2_CBR || op == C2_RET; }

/* uses of an instruction (vregs read); returns count */
static int uses(const C2Insn *in, int16_t *o)
{
    int n = 0;
    switch (in->op) {
    case C2_CONST: case C2_TRAP: case C2_BR: case C2_AOPEN: case C2_POPEN: break;
    case C2_MOV: case C2_CAST: case C2_UN: case C2_ALLOC: case C2_RELEASE: case C2_ADESTROY: case C2_PCLOSE:
    case C2_CBR: o[n++] = in->a; break;
    case C2_RET: if (in->a >= 0) o[n++] = in->a; break;
    case C2_BIN: case C2_CMP: case C2_LOAD: case C2_AALLOC: case C2_HALLOC: case C2_HGEN: case C2_SLOAD:
        o[n++] = in->a; o[n++] = in->b; break;
    case C2_STORE: case C2_HFREE: case C2_HLOAD: case C2_SSTORE: o[n++] = in->a; o[n++] = in->b; o[n++] = in->c; break;
    case C2_HSTORE: o[n++] = in->a; o[n++] = in->b; o[n++] = in->c; o[n++] = in->args[0]; break;
    case C2_FLOAD: o[n++] = in->a; if (in->b >= 0) o[n++] = in->b; break;
    case C2_FSTORE: o[n++] = in->a; if (in->b >= 0) o[n++] = in->b; o[n++] = in->c; break;
    case C2_CALL: for (int k = 0; k < in->nargs; k++) o[n++] = in->args[k]; break;
    /* a buffer access also reads the buffer's length register (the next one) */
    case C2_GLOAD: o[n++] = in->a; o[n++] = (int16_t)(in->a + 1); o[n++] = in->b; break;
    case C2_GSTORE: o[n++] = in->a; o[n++] = (int16_t)(in->a + 1); o[n++] = in->b; o[n++] = in->c; break;
    case C2_SHFL: o[n++] = in->a; o[n++] = in->b; break;
    case C2_MAD: o[n++] = in->a; o[n++] = in->b; o[n++] = in->c; break;
    default: break;
    }
    return n;
}

static int defines(const C2Insn *in)
{
    switch (in->op) {
    case C2_CONST: case C2_MOV: case C2_BIN: case C2_UN: case C2_CMP: case C2_CAST: case C2_ALLOC: case C2_LOAD:
    case C2_FLOAD: case C2_AOPEN: case C2_AALLOC: case C2_POPEN: case C2_HALLOC: case C2_HGEN: case C2_HLOAD:
    case C2_SLOAD: case C2_GPU: case C2_SHARED: case C2_GLOAD: case C2_SHFL: case C2_MAD:
        return in->dst;
    case C2_CALL: return in->dst; /* -1 for a void call */
    default: return -1;
    }
}

#define F(rule, ...) return fail(x->v, rule, x->fi, (int)ii, __VA_ARGS__)

static const C2Type *T(const Fx *x, int16_t r) { return &x->f->vt[r]; }

static int check_insn(Fx *x, uint32_t ii)
{
    const C2Func *f = x->f;
    const C2Insn *in = &f->in[ii];
    int nv = f->nvregs;
    int16_t us[8];
    int nu = uses(in, us);
    for (int k = 0; k < nu; k++) {
        if (us[k] < 0 || us[k] >= nv) F("C2-OPERAND-RANGE", "operand v%d out of range", us[k]); /* GUARD:operand-range */
    }
    int d = defines(in);
    int need_dst = !(in->op == C2_CALL && f->in[ii].dst == -1) && d != -1 ? 1 : 0;
    if (in->op != C2_CALL && (in->op == C2_CONST || in->op == C2_MOV || in->op == C2_BIN || in->op == C2_UN ||
                              in->op == C2_CMP || in->op == C2_CAST || in->op == C2_ALLOC || in->op == C2_LOAD ||
                              in->op == C2_FLOAD || in->op == C2_AOPEN || in->op == C2_AALLOC || in->op == C2_POPEN ||
                              in->op == C2_HALLOC || in->op == C2_HGEN || in->op == C2_HLOAD || in->op == C2_SLOAD ||
                              in->op == C2_GPU || in->op == C2_SHARED || in->op == C2_GLOAD || in->op == C2_SHFL ||
                              in->op == C2_MAD) &&
        in->dst < 0)
        F("C2-OPERAND-RANGE", "missing destination");
    if (need_dst && (d < 0 || d >= nv)) F("C2-OPERAND-RANGE", "destination v%d out of range", d); /* GUARD:dst-range */
    if (d >= 0 && is_slice(T(x, d))) F("C2-SLICE-WRITE", "slice register v%d redefined", d);
    if (d >= 0 && d < f->nparams && d > 0 && is_slice(T(x, d - 1)))
        F("C2-SLICE-WRITE", "slice length register v%d redefined", d);
    if (d >= 0 && is_gbuf(T(x, d))) F("C2-GPU-BUFFER", "buffer register v%d redefined", d); /* GUARD:gbuf-write */
    if (d > 0 && d < f->nparams && is_gbuf(T(x, d - 1))) F("C2-GPU-BUFFER", "buffer length register v%d redefined", d); /* GUARD:gbuf-len-write */

    const C2Type *td = d >= 0 ? T(x, d) : NULL;
    const C2Type *ta = in->a >= 0 && in->a < nv ? T(x, in->a) : NULL;
    const C2Type *tb = in->b >= 0 && in->b < nv ? T(x, in->b) : NULL;
    const C2Type *tc = in->c >= 0 && in->c < nv ? T(x, in->c) : NULL;

    switch (in->op) {
    case C2_CONST:
        if (!is_scalar_t(td)) F("C2-TYPE-CONST", "constant into a non-scalar register");
        if (!canonical(td->s, in->imm)) F("C2-CONST-CANONICAL", "constant %llu not canonical for its type", (unsigned long long)in->imm); /* GUARD:const-canonical */
        break;
    case C2_MOV:
        if (is_slice(ta) || is_slice(td)) F("C2-SLICE-ESCAPE", "slice copied");
        if (is_ref(td) || is_ref(ta)) {
            if (!same_shape(td, ta) || !bindable(td->kind, ta->kind)) F("C2-TYPE-MOV", "reference kinds or shapes do not bind"); /* GUARD:mov-ref */
        } else {
            if (!same_type(td, ta) || !is_scalar_t(td)) F("C2-TYPE-MOV", "MOV between different types"); /* GUARD:mov-type */
        }
        break;
    case C2_BIN:
        if (in->sub < B_ADD || in->sub >= B_LIMIT) F("C2-DECODE-SUBOP", "binary op %u", in->sub);
        if (in->sub == B_MIN || in->sub == B_MAX) {
            if (!is_kvalue(td->s) || !same_type(td, ta) || !same_type(td, tb)) F("C2-TYPE-BIN", "min/max need u32 or f32"); /* GUARD:bin-minmax */
        } else if (td->s == S_F32 || ta->s == S_F32) {
            /* f32: add, sub, mul only (GPU.md section 4: no float div/rem, no bit ops on floats) */
            if (!(in->sub == B_ADD || in->sub == B_SUB || in->sub == B_MUL)) F("C2-TYPE-BIN", "f32 binary op %u", in->sub); /* GUARD:bin-f32-op */
            if (!same_type(td, ta) || !same_type(td, tb)) F("C2-TYPE-BIN", "f32 operand types"); /* GUARD:bin-f32-type */
        } else if (in->sub == B_SHL || in->sub == B_SHR) {
            if (!is_int(td->s) || !same_type(td, ta) || !is_int(tb->s)) F("C2-TYPE-BIN", "shift operand types"); /* GUARD:bin-shift */
        } else if (in->sub == B_AND || in->sub == B_OR || in->sub == B_XOR) {
            if (!is_scalar_t(td) || !same_type(td, ta) || !same_type(td, tb)) F("C2-TYPE-BIN", "bitwise operand types");
        } else {
            if (!is_int(td->s) || !same_type(td, ta) || !same_type(td, tb)) F("C2-TYPE-BIN", "arithmetic operand types"); /* GUARD:bin-type */
        }
        break;
    case C2_UN:
        if (in->sub < U_NEG || in->sub >= U_LIMIT) F("C2-DECODE-SUBOP", "unary op %u", in->sub);
        if (!same_type(td, ta)) F("C2-TYPE-UN", "unary result type differs from operand");
        int unbad = in->sub == U_LNOT ? td->s != S_BOOL
                    : in->sub == U_NEG ? !(is_int(td->s) || td->s == S_F32)
                    : in->sub == U_BNOT ? !is_int(td->s)
                    : td->s != S_F32; /* EXP, INV, RSQ, SQRT */
        if (unbad) F("C2-TYPE-UN", "unary operand type"); /* GUARD:un-type */
        break;
    case C2_CMP:
        if (in->sub < CC_EQ || in->sub >= CC_LIMIT) F("C2-DECODE-SUBOP", "comparison %u", in->sub);
        if (td->s != S_BOOL || !is_scalar_t(ta) || !same_type(ta, tb)) F("C2-TYPE-CMP", "comparison operand types"); /* GUARD:cmp-type */
        break;
    case C2_CAST:
        /* integers, or u32 <-> f32 (GPU.md section 4) */
        if (td->s == S_F32 || ta->s == S_F32) {
            if (!((td->s == S_F32 && ta->s == S_U32) || (td->s == S_U32 && ta->s == S_F32))) F("C2-TYPE-CAST", "f32 cast other than u32 <-> f32"); /* GUARD:cast-f32 */
            break;
        }
        if (!is_int(td->s) || !is_int(ta->s)) F("C2-TYPE-CAST", "cast between non-integers (bool casts are refused)"); /* GUARD:cast-type */
        break;
    case C2_ALLOC: case C2_AALLOC:
        if (!is_ref(td) || td->kind != K_OWN) F("C2-TYPE-ALLOC", "allocation into a non-owner"); /* GUARD:alloc-own */
        if (is_array_ref(td) ? ta->s != td->elem : ta->s != S_U64) F("C2-TYPE-ALLOC", "initial value type"); /* GUARD:alloc-init */
        if (in->op == C2_AALLOC && !is_u64(tb)) F("C2-TYPE-ALLOC", "arena handle is not u64");
        break;
    case C2_RELEASE:
        if (!is_ref(ta) || ta->kind != K_OWN) F("C2-TYPE-RELEASE", "release of a non-owner"); /* GUARD:release-own */
        break;
    case C2_LOAD: case C2_STORE:
        if (!is_array_ref(ta)) F("C2-TYPE-INDEX", "indexing a non-array"); /* GUARD:index-array */
        if (!is_int(tb->s)) F("C2-TYPE-INDEX", "index is not an integer");
        if (in->op == C2_LOAD ? td->s != ta->elem : tc->s != ta->elem) F("C2-TYPE-INDEX", "element type"); /* GUARD:index-elem */
        if (in->op == C2_STORE && ta->kind == K_SHARED) F("C2-READ-ONLY", "store through a shared borrow"); /* GUARD:store-shared */
        break;
    case C2_FLOAD: case C2_FSTORE: {
        if (!is_struct_ref(ta)) F("C2-TYPE-FIELD", "field access on a non-struct");
        const C2Struct *s = &x->u->st[ta->sid - 1];
        if (in->imm >= s->nfields) F("C2-TYPE-FIELD", "field %llu of %s", (unsigned long long)in->imm, s->name); /* GUARD:field-index */
        const C2Field *fd = &s->f[in->imm];
        if (fd->alen ? (!tb || !is_int(tb->s)) : in->b != -1) F("C2-TYPE-FIELD", "element index of field %s.%s", s->name, fd->name); /* GUARD:field-elem-index */
        if (in->op == C2_FLOAD ? td->s != fd->s : tc->s != fd->s) F("C2-TYPE-FIELD", "type of field %s.%s", s->name, fd->name); /* GUARD:field-type */
        if (in->op == C2_FSTORE && ta->kind == K_SHARED) F("C2-READ-ONLY", "field store through a shared borrow"); /* GUARD:fstore-shared */
        break;
    }
    case C2_AOPEN:
        if (!is_u64(td)) F("C2-TYPE-HANDLE", "arena handle is not u64");
        if (in->imm < 1 || in->imm > C2_MAX_ARRAY) F("C2-ARENA-CAPACITY", "arena of %llu cells", (unsigned long long)in->imm); /* GUARD:arena-cap */
        break;
    case C2_ADESTROY: case C2_PCLOSE:
        if (!is_u64(ta)) F("C2-TYPE-HANDLE", "handle is not u64");
        break;
    case C2_POPEN:
        if (!is_u64(td)) F("C2-TYPE-HANDLE", "pool id is not u64");
        if (!is_value(in->sub)) F("C2-TYPE-POOL", "pool element type %u", in->sub);
        if (in->nargs < 1 || in->nargs > C2_POOL_SLOTS) F("C2-POOL-CAPACITY", "pool of %u slots", in->nargs); /* GUARD:pool-cap */
        break;
    case C2_HALLOC: if (!is_u64(td) || !is_u64(tb) || !is_value(ta->s)) F("C2-TYPE-POOL", "pool alloc operand types"); break;
    case C2_HGEN: if (!is_u64(td) || !is_u64(ta) || !is_u64(tb)) F("C2-TYPE-POOL", "pool generation operand types"); break;
    case C2_HFREE: if (!is_u64(ta) || !is_u64(tb) || !is_u64(tc)) F("C2-TYPE-POOL", "pool free operand types"); break;
    case C2_HLOAD: if (!is_value(td->s) || !is_u64(ta) || !is_u64(tb) || !is_u64(tc)) F("C2-TYPE-POOL", "pool load operand types"); break;
    case C2_HSTORE:
        if (in->args[0] < 0 || in->args[0] >= nv) F("C2-OPERAND-RANGE", "pool store value");
        if (!is_u64(ta) || !is_u64(tb) || !is_u64(tc)) F("C2-TYPE-POOL", "pool store operand types");
        break;
    case C2_SLOAD: case C2_SSTORE:
        if (!is_slice(ta)) F("C2-TYPE-SLICE", "slice access through a non-slice register"); /* GUARD:slice-ptr */
        if (in->op == C2_SSTORE && ta->s != S_CELLS) F("C2-SLICE-WRITE", "store into a read-only bytes slice"); /* GUARD:slice-bytes-ro */
        if (!is_int(tb->s)) F("C2-TYPE-SLICE", "slice index is not an integer");
        if (in->op == C2_SLOAD ? !is_u64(td) : !is_u64(tc)) F("C2-TYPE-SLICE", "slice element is u64");
        break;
    case C2_TRAP:
        if (in->imm < 1 || in->imm > C2_TRAP_MAX) F("C2-TRAP-CODE", "trap code %llu", (unsigned long long)in->imm); /* GUARD:trap-code */
        break;
    case C2_BR:
        if (in->t >= f->nblocks) F("C2-CFG-TARGET", "branch to block %u", in->t); /* GUARD:br-target */
        break;
    case C2_CBR:
        if (in->t >= f->nblocks || in->f >= f->nblocks) F("C2-CFG-TARGET", "branch to block %u/%u", in->t, in->f); /* GUARD:cbr-target */
        if (ta->s != S_BOOL) F("C2-TYPE-COND", "branch condition is not bool"); /* GUARD:cbr-bool */
        break;
    case C2_RET:
        if (f->ret.s == S_VOID ? in->a != -1 : (in->a < 0 || !same_type(ta, &f->ret))) F("C2-TYPE-RET", "return value type"); /* GUARD:ret-type */
        break;
    case C2_CALL: {
        if (in->callee >= x->fi) F("C2-CALL-ORDER", "call to function %u from function %d (no recursion, no forward calls)", in->callee, x->fi); /* GUARD:call-order */
        const C2Func *g = &x->u->fn[in->callee];
        if (f->kind != FN_HOST || g->kind != FN_HOST) F("C2-GPU-CALL", "call into or out of a kernel"); /* GUARD:gpu-call */
        if (in->nargs != g->nparams) F("C2-CALL-ARITY", "%u arguments for %u parameters", in->nargs, g->nparams); /* GUARD:call-arity */
        if (g->ret.s == S_VOID ? in->dst != -1 : (in->dst < 0 || in->dst >= nv || !same_type(T(x, in->dst), &g->ret)))
            F("C2-TYPE-CALL", "call result type"); /* GUARD:call-ret */
        for (int k = 0; k < in->nargs; k++) {
            const C2Type *pa = T(x, in->args[k]), *pp = &g->vt[k];
            if (is_ref(pp)) {
                if (!same_shape(pa, pp) || !bindable(pp->kind, pa->kind)) F("C2-TYPE-CALL", "argument %d does not bind", k); /* GUARD:call-ref */
            } else if (is_slice(pp)) {
                if (pa->s != pp->s) F("C2-TYPE-CALL", "slice argument %d type", k);
                if (k + 1 >= in->nargs || in->args[k + 1] != in->args[k] + 1)
                    F("C2-TYPE-CALL", "slice argument %d is not passed with its own length", k);
            } else {
                if (!same_type(pa, pp)) F("C2-TYPE-CALL", "argument %d type", k); /* GUARD:call-scalar */
            }
            /* aliasing: the same register twice where either parameter may write or take it */
            for (int j = 0; j < k; j++) {
                int alias = in->args[j] == in->args[k] && (is_ref(pp) || pp->s == S_CELLS) &&
                            (pp->kind != K_SHARED || g->vt[j].kind != K_SHARED);
                if (alias) F("C2-CALL-ALIAS", "register v%d passed twice and written", in->args[k]); /* GUARD:call-alias */
            }
        }
        break;
    }
    case C2_GPU:
        if (in->sub < G_THREAD || in->sub >= G_LIMIT) F("C2-DECODE-SUBOP", "launch value %u", in->sub); /* GUARD:gpu-sub */
        if (td->s != S_U32) F("C2-TYPE-GPU", "launch value into a non-u32 register"); /* GUARD:gpu-u32 */
        break;
    case C2_SHARED:
        if (!is_block(td)) F("C2-TYPE-GPU", "shared array into a non-block register"); /* GUARD:shared-type */
        break;
    case C2_GLOAD: case C2_GSTORE:
        if (!is_gbuf(ta)) F("C2-TYPE-GPU", "buffer access through a non-buffer register"); /* GUARD:gbuf-ptr */
        if (T(x, (int16_t)(in->a + 1))->s != S_U32) F("C2-TYPE-GPU", "buffer v%d has no u32 length register", in->a);
        if (tb->s != S_U32) F("C2-TYPE-GPU", "buffer index is not u32"); /* GUARD:gbuf-index */
        if (in->op == C2_GLOAD ? td->s != ta->elem : tc->s != ta->elem) F("C2-TYPE-GPU", "buffer element type"); /* GUARD:gbuf-elem */
        if (in->op == C2_GSTORE && ta->s != S_GEDIT) F("C2-GPU-STORE-VIEW", "store into a view buffer"); /* GUARD:gstore-view */
        break;
    case C2_SYNC:
        break;
    case C2_SHFL:
        if (in->sub != SH_DOWN) F("C2-DECODE-SUBOP", "shuffle %u", in->sub);
        if (!is_kvalue(ta->s) || !same_type(td, ta)) F("C2-TYPE-GPU", "shuffle value is not u32 or f32"); /* GUARD:shfl-type */
        if (tb->s != S_U32) F("C2-TYPE-GPU", "shuffle distance is not u32");
        break;
    case C2_MAD:
        if (td->s != S_F32 || ta->s != S_F32 || tb->s != S_F32 || tc->s != S_F32) F("C2-TYPE-GPU", "mad needs f32 operands"); /* GUARD:mad-type */
        break;
    default: F("C2-DECODE-OPCODE", "opcode %u", in->op);
    }
    return 0;
}

/* ------------------------------------------------------------------ CFG */

static int build_cfg(Fx *x)
{
    const C2Func *f = x->f;
    uint32_t pos = 0;
    for (int b = 0; b < f->nblocks; b++) {
        const C2Block *bk = &f->blk[b];
        if (bk->count < 1 || bk->first != pos || bk->first + bk->count > f->ninsns) /* GUARD:cfg-layout */
            return fail(x->v, "C2-CFG-LAYOUT", x->fi, -1, "block %d is not the next contiguous range", b);
        for (uint32_t i = bk->first; i < bk->first + bk->count; i++) {
            x->blk_of[i] = b;
            int last = i + 1 == bk->first + bk->count;
            if (term(f->in[i].op) != last) /* GUARD:cfg-terminator */
                return fail(x->v, "C2-CFG-TERMINATOR", x->fi, (int)i, last ? "block %d does not end in a terminator" : "terminator inside block %d", b);
        }
        pos += bk->count;
    }
    if (pos != f->ninsns) return fail(x->v, "C2-CFG-LAYOUT", x->fi, -1, "blocks cover %u of %u instructions", pos, f->ninsns);
    return 0;
}

static void link_cfg(Fx *x)
{
    const C2Func *f = x->f;
    for (int b = 0; b < f->nblocks; b++) {
        const C2Insn *t = &f->in[f->blk[b].first + f->blk[b].count - 1];
        x->nsucc[b] = 0;
        if (t->op == C2_BR) x->succ[b][x->nsucc[b]++] = t->t;
        if (t->op == C2_CBR) {
            x->succ[b][x->nsucc[b]++] = t->t;
            if (t->f != t->t) x->succ[b][x->nsucc[b]++] = t->f;
        }
    }
    int stack[C2_MAX_BLOCKS], sp = 0;
    memset(x->reach, 0, sizeof x->reach);
    x->reach[0] = 1;
    stack[sp++] = 0;
    while (sp) {
        int b = stack[--sp];
        for (int k = 0; k < x->nsucc[b]; k++)
            if (!x->reach[x->succ[b][k]]) { x->reach[x->succ[b][k]] = 1; stack[sp++] = x->succ[b][k]; }
    }
    /* dominators over reachable blocks */
    for (int b = 0; b < f->nblocks; b++) ball(&x->dom[b]);
    memset(&x->dom[0], 0, sizeof x->dom[0]);
    bset(&x->dom[0], 0);
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int b = 1; b < f->nblocks; b++) {
            if (!x->reach[b]) continue;
            Bits n;
            ball(&n);
            for (int p = 0; p < f->nblocks; p++) {
                if (!x->reach[p]) continue;
                for (int k = 0; k < x->nsucc[p]; k++)
                    if (x->succ[p][k] == b) band(&n, &x->dom[p]);
            }
            bset(&n, (unsigned)b);
            if (!beq(&n, &x->dom[b])) { x->dom[b] = n; changed = 1; }
        }
    }
}

static int dominates(const Fx *x, int a, int b) { return btest(&x->dom[b], (unsigned)a); }

/* ------------------------------------------------------------------ disciplines */

static int count_defs(Fx *x)
{
    const C2Func *f = x->f;
    for (int r = 0; r < f->nvregs; r++) { x->ndefs[r] = r < f->nparams ? 1 : 0; x->def_insn[r] = -1; }
    for (uint32_t i = 0; i < f->ninsns; i++) {
        int d = defines(&f->in[i]);
        if (d < 0) continue;
        x->ndefs[d]++;
        x->def_insn[d] = x->ndefs[d] == 1 ? (int)i : -1;
    }
    return 0;
}

/* Arena handles and pool ids: defined exactly once, by AOPEN / POPEN, never a parameter, and used
 * only in their handle positions (OSC-2 section 3.3, OSC-3 item 2). */
static int check_handles(Fx *x)
{
    const C2Func *f = x->f;
    for (uint32_t i = 0; i < f->ninsns; i++) {
        const C2Insn *in = &f->in[i];
        if (in->op != C2_AOPEN && in->op != C2_POPEN) continue;
        int h = in->dst;
        if (h < f->nparams || x->ndefs[h] != 1) /* also implied by handle-origin and the leak rule (untagged) */
            return fail(x->v, "C2-HANDLE-DISCIPLINE", x->fi, (int)i, "handle v%d defined more than once or a parameter", h);
        for (uint32_t j = 0; j < f->ninsns; j++) {
            const C2Insn *o = &f->in[j];
            int16_t us[8];
            int nu = uses(o, us);
            for (int k = 0; k < nu; k++) {
                if (us[k] != h) continue;
                int ok;
                if (in->op == C2_AOPEN)
                    ok = (o->op == C2_AALLOC && o->b == h && o->a != h) || (o->op == C2_ADESTROY && o->a == h);
                else
                    ok = (o->op == C2_PCLOSE && o->a == h) || (o->op == C2_HALLOC && o->b == h && o->a != h) ||
                         ((o->op == C2_HGEN || o->op == C2_HFREE || o->op == C2_HLOAD || o->op == C2_HSTORE) && o->a == h &&
                          o->b != h && o->c != h && o->args[0] != h);
                if (!ok) /* GUARD:handle-use */
                    return fail(x->v, "C2-HANDLE-DISCIPLINE", x->fi, (int)j, "handle v%d used outside its handle position", h);
            }
        }
    }
    /* every handle operand is such a handle, and pool element types agree with the pool */
    for (uint32_t j = 0; j < f->ninsns; j++) {
        const C2Insn *o = &f->in[j];
        int h = -1;
        if (o->op == C2_AALLOC) h = o->b;
        if (o->op == C2_ADESTROY || o->op == C2_PCLOSE || o->op == C2_HGEN || o->op == C2_HFREE || o->op == C2_HLOAD ||
            o->op == C2_HSTORE)
            h = o->a;
        if (o->op == C2_HALLOC) h = o->b;
        if (h < 0) continue;
        int di = x->def_insn[h];
        uint8_t want = (o->op == C2_AALLOC || o->op == C2_ADESTROY) ? C2_AOPEN : C2_POPEN;
        if (di < 0 || f->in[di].op != want) /* GUARD:handle-origin */
            return fail(x->v, "C2-HANDLE-DISCIPLINE", x->fi, (int)j, "v%d is not a handle opened by %s", h, want == C2_AOPEN ? "AOPEN" : "POPEN");
        uint8_t es = f->in[di].sub;
        if (o->op == C2_HALLOC && f->vt[o->a].s != es) return fail(x->v, "C2-TYPE-POOL", x->fi, (int)j, "pool value type");
        if (o->op == C2_HLOAD && f->vt[o->dst].s != es) return fail(x->v, "C2-TYPE-POOL", x->fi, (int)j, "pool value type"); /* GUARD:pool-elem */
        if (o->op == C2_HSTORE && f->vt[o->args[0]].s != es) return fail(x->v, "C2-TYPE-POOL", x->fi, (int)j, "pool value type");
    }
    return 0;
}

/* ------------------------------------------------------------------ definite assignment */

static int check_defined(Fx *x)
{
    const C2Func *f = x->f;
    static Bits in[C2_MAX_BLOCKS], out[C2_MAX_BLOCKS]; /* not reentrant; checker2 is single-threaded */
    Bits params;
    memset(&params, 0, sizeof params);
    for (int r = 0; r < f->nparams; r++) bset(&params, (unsigned)r);
    for (int b = 0; b < f->nblocks; b++) ball(&out[b]);
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int b = 0; b < f->nblocks; b++) {
            if (!x->reach[b]) continue;
            Bits cur;
            if (b == 0) cur = params;
            else {
                ball(&cur);
                for (int p = 0; p < f->nblocks; p++) {
                    if (!x->reach[p]) continue;
                    for (int k = 0; k < x->nsucc[p]; k++)
                        if (x->succ[p][k] == b) band(&cur, &out[p]);
                }
            }
            if (b == 0) {
                /* the entry block may also be a loop header */
                for (int p = 0; p < f->nblocks; p++) {
                    if (!x->reach[p]) continue;
                    for (int k = 0; k < x->nsucc[p]; k++)
                        if (x->succ[p][k] == 0) band(&cur, &out[p]);
                }
            }
            in[b] = cur;
            for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].count; i++) {
                int stop = f->in[i].op == C2_TRAP; /* a host trap ends the path */
                if (f->kind == FN_KERNEL) stop = 0; /* GUARD:kernel-trap-flows */
                if (stop) { ball(&cur); break; }
                int d = defines(&f->in[i]);
                if (d >= 0) bset(&cur, (unsigned)d);
            }
            if (!beq(&cur, &out[b])) { out[b] = cur; changed = 1; }
        }
    }
    for (int b = 0; b < f->nblocks; b++) {
        if (!x->reach[b]) continue;
        Bits cur = in[b];
        for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].count; i++) {
            if (f->in[i].op == C2_TRAP && f->kind == FN_HOST) break; /* a kernel thread runs on (GPU.md section 7) */
            int16_t us[8];
            int nu = uses(&f->in[i], us);
            for (int k = 0; k < nu; k++) {
                if (!btest(&cur, (unsigned)us[k])) /* GUARD:definite-assignment */
                    return fail(x->v, "C2-UNDEFINED-USE", x->fi, (int)i, "v%d may be read before it is written", us[k]);
            }
            int d = defines(&f->in[i]);
            if (d >= 0) bset(&cur, (unsigned)d);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ loop bounds */

/* Every loop must be bounded (OSC-1 section 3 "Loops"; OSC-1 section 4 "loops to blocks with an
 * explicit counter vreg, CMP and CBR on the bound"). checker2 proves it structurally: the CFG is
 * reducible, and each natural loop L contains a guard block G that dominates every latch of L and
 * ends in CBR on cond, where
 *   cond is defined once, in G, by CMP c, K with
 *   K defined exactly once in the function, by CONST, |K| <= 1,000,000;
 *   c an integer register whose only definitions inside L are  c = BIN ADD c, s  with s defined
 *   exactly once in the function by CONST 1, each in a block that dominates every latch of L;
 *   CMP GE with the TRUE successor outside L, or CMP LT with the FALSE successor outside L.
 * Then every iteration raises c by one and the loop is left once c >= K: at most |K - c0| + 2
 * iterations per entry (an unsigned counter can wrap at most once, on the first iteration; a
 * signed one traps on overflow). */
static int single_const(const Fx *x, int r, uint64_t *val)
{
    if (r < 0 || x->ndefs[r] != 1 || x->def_insn[r] < 0) return 0;
    const C2Insn *d = &x->f->in[x->def_insn[r]];
    if (d->op != C2_CONST) return 0;
    *val = d->imm;
    return 1;
}

static int loop_guarded(const Fx *x, const int *inloop, const int *latch, int nlatch)
{
    const C2Func *f = x->f;
    for (int g = 0; g < f->nblocks; g++) {
        if (!inloop[g]) continue;
        int domall = 1;
        for (int k = 0; k < nlatch; k++) domall &= dominates(x, g, latch[k]);
        if (!domall) continue;
        const C2Insn *t = &f->in[f->blk[g].first + f->blk[g].count - 1];
        if (t->op != C2_CBR) continue;
        int ci = x->def_insn[t->a];
        if (ci < 0 || x->blk_of[ci] != g || f->in[ci].op != C2_CMP) continue;
        const C2Insn *cmp = &f->in[ci];
        int exits;
        if (cmp->sub == CC_GE) exits = !inloop[t->t];
        else if (cmp->sub == CC_LT) exits = !inloop[t->f];
        else continue;
        if (!exits) continue;
        uint64_t kv, one;
        if (!single_const(x, cmp->b, &kv)) continue;
        uint8_t ks = f->vt[cmp->b].s;
        int64_t ksig = is_signed(ks) ? (int64_t)kv : (kv > 2000000 ? 2000000 : (int64_t)kv);
        if (ksig > 1000000 || ksig < -1000000) continue;
        int c = cmp->a;
        if (!is_int(f->vt[c].s)) continue;
        int incs = 0, bad = 0;
        for (uint32_t i = 0; i < f->ninsns && !bad; i++) {
            if (!inloop[x->blk_of[i]] || defines(&f->in[i]) != c) continue;
            const C2Insn *d = &f->in[i];
            int ok = d->op == C2_BIN && d->sub == B_ADD && d->a == c && single_const(x, d->b, &one) && one == 1;
            for (int k = 0; ok && k < nlatch; k++) ok = dominates(x, x->blk_of[i], latch[k]);
            if (!ok) bad = 1;
            else incs++;
        }
        if (!bad && incs >= 1) return 1;
    }
    return 0;
}

static int check_loops(Fx *x)
{
    const C2Func *f = x->f;
    /* reducibility: every edge into a block that does not dominate its source must go forward in a
     * DFS without creating a cycle; equivalently, removing back edges (to a dominator) leaves a DAG. */
    static int color[C2_MAX_BLOCKS], stk[C2_MAX_BLOCKS], idx[C2_MAX_BLOCKS];
    memset(color, 0, sizeof color);
    int sp = 0;
    stk[sp] = 0; idx[sp] = 0; sp++;
    color[0] = 1;
    while (sp) {
        int b = stk[sp - 1];
        if (idx[sp - 1] < x->nsucc[b]) {
            int s = x->succ[b][idx[sp - 1]++];
            if (color[s] == 1 && !dominates(x, s, b)) /* GUARD:reducible */
                return fail(x->v, "C2-CFG-IRREDUCIBLE", x->fi, -1, "cycle entered other than through its header (block %d)", s);
            if (color[s] == 0) { color[s] = 1; stk[sp] = s; idx[sp] = 0; sp++; }
        } else {
            color[b] = 2;
            sp--;
        }
    }
    /* natural loops, one per header (all its back edges together) */
    for (int h = 0; h < f->nblocks; h++) {
        if (!x->reach[h]) continue;
        int latch[C2_MAX_BLOCKS], nlatch = 0;
        for (int p = 0; p < f->nblocks; p++) {
            if (!x->reach[p]) continue;
            for (int k = 0; k < x->nsucc[p]; k++)
                if (x->succ[p][k] == h && dominates(x, h, p)) latch[nlatch++] = p;
        }
        if (!nlatch) continue;
        static int inloop[C2_MAX_BLOCKS];
        memset(inloop, 0, sizeof inloop);
        inloop[h] = 1;
        int st[C2_MAX_BLOCKS], n = 0;
        for (int k = 0; k < nlatch; k++)
            if (!inloop[latch[k]]) { inloop[latch[k]] = 1; st[n++] = latch[k]; }
        while (n) {
            int b = st[--n];
            for (int p = 0; p < f->nblocks; p++) {
                if (!x->reach[p] || inloop[p]) continue;
                for (int k = 0; k < x->nsucc[p]; k++)
                    if (x->succ[p][k] == b) { inloop[p] = 1; st[n++] = p; break; }
            }
        }
        if (!loop_guarded(x, inloop, latch, nlatch)) /* GUARD:loop-bound */
            return fail(x->v, "C2-LOOP-UNBOUNDED", x->fi, (int)f->blk[h].first, "loop at block %d has no counter guard", h);
    }
    return 0;
}

/* ------------------------------------------------------------------ GPU kernels */

/* Which ops each function kind may hold, block-shared arrays and constant divisors
 * (GPU.md sections 8 and 9). */
static int kernel_forbidden(uint8_t op)
{
    switch (op) {
    case C2_ALLOC: case C2_RELEASE: case C2_AOPEN: case C2_AALLOC: case C2_ADESTROY: case C2_POPEN:
    case C2_PCLOSE: case C2_HALLOC: case C2_HGEN: case C2_HFREE: case C2_HLOAD: case C2_HSTORE:
    case C2_SLOAD: case C2_SSTORE: case C2_FLOAD: case C2_FSTORE: case C2_CALL:
        return 1;
    default: return 0;
    }
}

static int kernel_ops(Fx *x)
{
    const C2Func *f = x->f;
    int kern = f->kind == FN_KERNEL;
    unsigned shared_bytes = 0;
    int syncs = 0;
    for (uint32_t i = 0; i < f->ninsns; i++) {
        const C2Insn *in = &f->in[i];
        int gpu_op = in->op >= C2_GPU && in->op <= C2_MAD;
        int gpu_sub = (in->op == C2_BIN && (in->sub == B_MIN || in->sub == B_MAX)) ||
                      (in->op == C2_UN && in->sub >= U_EXP);
        if (!kern) {
            if (gpu_op || gpu_sub) /* GUARD:host-gpu-op */
                return fail(x->v, "C2-GPU-OP", x->fi, (int)i, "kernel instruction %u in a host function", in->op);
            continue;
        }
        /* unreachable today: the kernel type rules already refuse every operand these ops need (untagged) */
        if (kernel_forbidden(in->op))
            return fail(x->v, "C2-GPU-OP", x->fi, (int)i, "op %u is not allowed in a kernel", in->op);
        switch (in->op) {
        case C2_SHARED:
            if (x->blk_of[i] != 0) /* GUARD:shared-entry */
                return fail(x->v, "C2-GPU-SHARED", x->fi, (int)i, "shared array outside the entry block");
            if (x->ndefs[in->dst] != 1) /* GUARD:shared-once */
                return fail(x->v, "C2-GPU-SHARED", x->fi, (int)i, "shared array v%d defined more than once", in->dst);
            shared_bytes += 4u * f->vt[in->dst].len;
            if (shared_bytes > C2_MAX_SHARED_BYTES) /* GUARD:shared-bytes */
                return fail(x->v, "C2-GPU-SHARED", x->fi, (int)i, "shared arrays over %u bytes", C2_MAX_SHARED_BYTES);
            break;
        case C2_BIN:
            if (in->sub == B_DIV || in->sub == B_REM) {
                uint64_t k;
                if (in->b < f->nparams || !single_const(x, in->b, &k) || k == 0) /* GUARD:gpu-div */
                    return fail(x->v, "C2-GPU-DIV", x->fi, (int)i, "kernel div/rem needs a nonzero constant divisor");
            }
            break;
        case C2_SHFL: {
            uint64_t k;
            if (!single_const(x, in->b, &k) || k < 1 || k >= C2_WARP) /* GUARD:shfl-const */
                return fail(x->v, "C2-GPU-SHFL", x->fi, (int)i, "shuffle distance is not one constant 1..31");
            break;
        }
        case C2_SYNC:
            if (++syncs > C2_MAX_SYNCS) /* GUARD:sync-limit */
                return fail(x->v, "C2-GPU-SYNC-LIMIT", x->fi, (int)i, "more than %d sync lines", C2_MAX_SYNCS);
            break;
        default: break;
        }
    }
    return 0;
}

/* C2-GPU-SYNC-DIVERGENT and C2-GPU-NEST (GPU.md section 6). A register is varying when it is
 * defined from `thread`, a global or shared load or a shuffle, from any varying operand, or anywhere
 * in a divergent block. A CBR on a varying register makes divergent every block reachable from its
 * successors before a block that post-dominates it (its region); a RET goes to one virtual exit, so
 * a region holding a RET runs to the end of the kernel. A kernel TRAP does not end the thread
 * (GPU.md section 7), so it is an ordinary instruction here. SYNC and SHFL must not sit in a
 * divergent block, and no block may sit inside more than C2_MAX_NEST regions. The analysis only
 * grows its sets, so it terminates. */
static int kernel_uniform(Fx *x)
{
    const C2Func *f = x->f;
    static Bits pdom[C2_MAX_BLOCKS], region[C2_MAX_BLOCKS];
    static uint8_t divb[C2_MAX_BLOCKS], done[C2_MAX_BLOCKS], vary[C2_MAX_VREGS];
    static int st[2 * C2_MAX_BLOCKS + 2];
    for (int b = 0; b < f->nblocks; b++) {
        divb[b] = done[b] = 0;
        memset(&region[b], 0, sizeof region[b]);
        ball(&pdom[b]);
        if (x->nsucc[b] == 0) { /* RET */
            memset(&pdom[b], 0, sizeof pdom[b]);
            bset(&pdom[b], (unsigned)b);
        }
    }
    memset(vary, 0, sizeof vary);
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int b = f->nblocks - 1; b >= 0; b--) {
            if (x->nsucc[b] == 0) continue;
            Bits n;
            ball(&n);
            for (int k = 0; k < x->nsucc[b]; k++) band(&n, &pdom[x->succ[b][k]]);
            bset(&n, (unsigned)b);
            if (!beq(&n, &pdom[b])) { pdom[b] = n; changed = 1; }
        }
    }
    changed = 1;
    while (changed) {
        changed = 0;
        for (int b = 0; b < f->nblocks; b++) {
            if (!x->reach[b]) continue;
            for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].count; i++) {
                const C2Insn *in = &f->in[i];
                int d = defines(in);
                if (d < 0 || vary[d]) continue;
                int v = 0;
                if (divb[b]) v = 1; /* GUARD:vary-divergent-def */
                if ((in->op == C2_GPU && in->sub == G_THREAD) || in->op == C2_GLOAD || in->op == C2_SHFL || in->op == C2_LOAD) v = 1; /* GUARD:vary-source */
                int16_t us[8];
                int nu = uses(in, us);
                for (int k = 0; k < nu && !v; k++) v = vary[us[k]]; /* GUARD:vary-operand */
                if (v) { vary[d] = 1; changed = 1; }
            }
            const C2Insn *t = &f->in[f->blk[b].first + f->blk[b].count - 1];
            if (t->op != C2_CBR || !vary[t->a] || done[b]) continue;
            done[b] = 1;
            changed = 1;
            int sp = 0;
            for (int k = 0; k < x->nsucc[b]; k++) st[sp++] = x->succ[b][k];
            while (sp) {
                int c = st[--sp];
                if (btest(&region[b], (unsigned)c)) continue;
                if (c != b && btest(&pdom[b], (unsigned)c)) continue; /* GUARD:region-stop */
                bset(&region[b], (unsigned)c);
                divb[c] = 1;
                for (int k = 0; k < x->nsucc[c]; k++) st[sp++] = x->succ[c][k];
            }
        }
    }
    for (int b = 0; b < f->nblocks; b++) {
        if (!x->reach[b]) continue;
        int nest = 0;
        for (int c = 0; c < f->nblocks; c++)
            if (done[c] && btest(&region[c], (unsigned)b)) nest++;
        if (done[b] && !btest(&region[b], (unsigned)b)) nest++; /* the branch opens one more level, unless it loops into its own region */
        if (nest > C2_MAX_NEST) /* GUARD:nest */
            return fail(x->v, "C2-GPU-NEST", x->fi, (int)f->blk[b].first, "block %d sits %d varying regions deep (at most %d)", b, nest, C2_MAX_NEST);
        if (!divb[b]) continue;
        for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].count; i++)
            if (f->in[i].op == C2_SYNC || f->in[i].op == C2_SHFL) /* GUARD:sync-divergent */
                return fail(x->v, "C2-GPU-SYNC-DIVERGENT", x->fi, (int)i, "sync or down in block %d, which not every thread reaches", b);
    }
    return 0;
}

/* ------------------------------------------------------------------ races */

/* C2-GPU-GLOBAL-RACE and C2-GPU-SHARED-RACE (GPU.md section 8). An index is read as a tree through
 * registers with one definition. Leaves: numbers, uniform inputs (u32 parameters, `blocks`,
 * `batches`), and digits: `thread`, `block`, `batch`, `rem thread K` (LO), `div thread K` (HI) and
 * loop counters. Inner nodes: u32 add, mul, div, rem. Anything else is opaque and proves nothing.
 * Nodes are hash-consed, so two accesses use "the same tree" when their node ids are equal. */
enum { N_OPAQUE = 0, N_CONST, N_INV, N_THREAD, N_BLOCK, N_BATCH, N_LO, N_HI, N_LOOP, N_ADD, N_MUL, N_DIV, N_REM };
#define RX_NODES 4096
#define RX_FACTS 32
#define RX_DIGITS 16
typedef struct { uint8_t k; int a, b, h; uint64_t v; } Nd; /* LOOP: a counter, b guarded block or -1, h header, v bound */
typedef struct { int n; int lt[RX_FACTS][2]; int nfix; int fix[RX_FACTS]; uint64_t fixv[RX_FACTS]; } Facts;
typedef struct {
    Fx *x;
    unsigned T;
    Nd nd[RX_NODES];
    int nn;
    int memo[C2_MAX_VREGS]; /* -1 not built, -2 being built, else a node */
    int npred[C2_MAX_BLOCKS];
    Bits loop[C2_MAX_BLOCKS]; /* natural loop of each header */
} Rx;

static int mk4(Rx *r, uint8_t k, int a, int b, int h, uint64_t v)
{
    for (int i = 1; i < r->nn; i++)
        if (r->nd[i].k == k && r->nd[i].a == a && r->nd[i].b == b && r->nd[i].h == h && r->nd[i].v == v) return i;
    if (r->nn >= RX_NODES) return 0;
    r->nd[r->nn] = (Nd){ k, a, b, h, v };
    return r->nn++;
}

static int mk(Rx *r, uint8_t k, int a, int b, uint64_t v) { return mk4(r, k, a, b, 0, v); }

static int is_digit(uint8_t k) { return k >= N_THREAD && k <= N_LOOP; }
static int thread_digit(uint8_t k) { return k == N_THREAD || k == N_LO || k == N_HI; }

/* A loop counter: a u32 register with two definitions, a number before its loop and `add c c 1` as
 * the last step of the loop's one latch, which jumps straight back to the header. */
static int loop_digit(Rx *r, int c)
{
    const Fx *x = r->x;
    const C2Func *f = x->f;
    int init = -1, inc = -1;
    uint64_t one;
    for (uint32_t i = 0; i < f->ninsns; i++) {
        if (defines(&f->in[i]) != c) continue;
        const C2Insn *d = &f->in[i];
        if (d->op == C2_BIN && d->sub == B_ADD && d->a == c && single_const(x, d->b, &one) && one == 1) inc = (int)i;
        else if (d->op == C2_CONST || (d->op == C2_MOV && single_const(x, d->a, &one))) init = (int)i;
    }
    if (init < 0 || inc < 0) return 0;
    int lb = x->blk_of[inc];
    const C2Block *bk = &f->blk[lb];
    const C2Insn *t = &f->in[bk->first + bk->count - 1];
    if ((uint32_t)inc + 2 != bk->first + bk->count || t->op != C2_BR) return 0; /* GUARD:loop-digit-last */
    int h = t->t, nlatch = 0;
    if (!dominates(x, h, lb)) return 0;
    for (int p = 0; p < f->nblocks; p++)
        for (int k = 0; k < x->nsucc[p]; k++)
            if (x->reach[p] && x->succ[p][k] == h && dominates(x, h, p)) nlatch++;
    if (nlatch != 1 || btest(&r->loop[h], (unsigned)x->blk_of[init])) return 0; /* also implied by check_loops (untagged) */
    /* the bound: a guard `lt c K` (or `ge c K`) in the loop whose exit side leaves it */
    int gin = -1;
    uint64_t K = 0;
    for (int g = 0; g < f->nblocks && gin < 0; g++) {
        if (!x->reach[g] || !btest(&r->loop[h], (unsigned)g)) continue;
        const C2Insn *gt = &f->in[f->blk[g].first + f->blk[g].count - 1];
        if (gt->op != C2_CBR || gt->t == gt->f) continue;
        int ci = x->def_insn[gt->a];
        if (ci < 0 || x->blk_of[ci] != g || f->in[ci].op != C2_CMP || f->in[ci].a != c || !single_const(x, f->in[ci].b, &K)) continue;
        int in_t = btest(&r->loop[h], gt->t), in_f = btest(&r->loop[h], gt->f);
        if (f->in[ci].sub == CC_LT && in_t && !in_f) gin = gt->t;
        if (f->in[ci].sub == CC_GE && in_f && !in_t) gin = gt->f;
    }
    return mk4(r, N_LOOP, c, gin, h, gin < 0 ? 0 : K);
}

static int tree(Rx *r, int reg);

static int combine(Rx *r, uint8_t sub, int A, int B)
{
    if (!A || !B) return 0;
    const Nd *b = &r->nd[B];
    switch (sub) {
    case B_ADD: case B_MUL:
        return mk(r, sub == B_ADD ? N_ADD : N_MUL, A < B ? A : B, A < B ? B : A, 0);
    case B_DIV: case B_REM:
        if (b->k != N_CONST || b->v == 0) return 0;
        if (r->nd[A].k == N_THREAD) {
            uint64_t K = b->v;
            if (sub == B_DIV) return K == 1 ? A : K >= r->T ? mk(r, N_CONST, 0, 0, 0) : mk(r, N_HI, 0, 0, K);
            return K == 1 ? mk(r, N_CONST, 0, 0, 0) : K >= r->T ? A : mk(r, N_LO, 0, 0, K);
        }
        return mk(r, sub == B_DIV ? N_DIV : N_REM, A, B, 0);
    default: return 0;
    }
}

static int build_tree(Rx *r, int reg)
{
    const Fx *x = r->x;
    const C2Func *f = x->f;
    if (f->vt[reg].s != S_U32) return 0;
    if (reg < f->nparams) return x->ndefs[reg] == 1 ? mk(r, N_INV, reg, 0, 0) : 0;
    if (x->ndefs[reg] == 2) return loop_digit(r, reg);
    if (x->ndefs[reg] != 1 || x->def_insn[reg] < 0) return 0;
    const C2Insn *d = &f->in[x->def_insn[reg]];
    switch (d->op) {
    case C2_CONST: return mk(r, N_CONST, 0, 0, d->imm);
    case C2_MOV: return tree(r, d->a);
    case C2_GPU:
        switch (d->sub) {
        case G_THREAD: return mk(r, N_THREAD, 0, 0, 0);
        case G_BLOCK: return mk(r, N_BLOCK, 0, 0, 0);
        case G_BATCH: return mk(r, N_BATCH, 0, 0, 0);
        default: return mk(r, N_INV, -(int)d->sub, 0, 0); /* blocks, batches */
        }
    case C2_BIN: return combine(r, d->sub, tree(r, d->a), tree(r, d->b));
    default: return 0;
    }
}

static int tree(Rx *r, int reg)
{
    if (reg < 0 || reg >= r->x->f->nvregs || r->memo[reg] == -2) return 0;
    if (r->memo[reg] >= 0) return r->memo[reg];
    r->memo[reg] = -2;
    int n = build_tree(r, reg);
    r->memo[reg] = n;
    return n;
}

/* The digits of a tree, or -1 when it holds an opaque node. */
static int digits(const Rx *r, int n, int *ds, int *nd)
{
    const Nd *p = &r->nd[n];
    if (p->k == N_OPAQUE) return -1;
    if (is_digit(p->k)) {
        for (int i = 0; i < *nd; i++)
            if (ds[i] == n) return 0;
        if (*nd >= RX_DIGITS) return -1;
        ds[(*nd)++] = n;
        return 0;
    }
    if (p->k >= N_ADD) return digits(r, p->a, ds, nd) || digits(r, p->b, ds, nd) ? -1 : 0;
    return 0;
}

static int digit_free(const Rx *r, int n)
{
    int ds[RX_DIGITS], nd = 0;
    return digits(r, n, ds, &nd) == 0 && nd == 0;
}

/* Two trees share no digit. `rem thread K` and `div thread K` with the same K are the only thread
 * digits that are independent of each other. */
static int disjoint(const Rx *r, int a, int b)
{
    int da[RX_DIGITS], db[RX_DIGITS], na = 0, nb = 0;
    if (digits(r, a, da, &na) || digits(r, b, db, &nb)) return 0;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++) {
            const Nd *p = &r->nd[da[i]], *q = &r->nd[db[j]];
            if (da[i] == db[j]) return 0; /* GUARD:disjoint-same */
            if (thread_digit(p->k) && thread_digit(q->k) &&
                !(p->v == q->v && ((p->k == N_LO && q->k == N_HI) || (p->k == N_HI && q->k == N_LO)))) /* GUARD:disjoint-thread */
                return 0;
        }
    return 1;
}

static uint64_t sat_add(uint64_t a, uint64_t b) { return a + b < a ? UINT64_MAX : a + b; }
static uint64_t sat_mul(uint64_t a, uint64_t b) { return a && b > UINT64_MAX / a ? UINT64_MAX : a * b; }

/* The largest value a tree can hold at block sb, or UINT64_MAX when unknown. */
static uint64_t maxval(const Rx *r, int n, int sb)
{
    const Nd *p = &r->nd[n];
    switch (p->k) {
    case N_CONST: return p->v;
    case N_THREAD: return r->T - 1;
    case N_LO: return (p->v < r->T ? p->v : r->T) - 1;
    case N_HI: return (r->T - 1) / p->v;
    case N_LOOP: /* the counter is below K wherever the guard's loop side dominates */
        if (p->b >= 0 && dominates(r->x, p->b, sb)) return p->v ? p->v - 1 : 0; /* GUARD:loop-bound */
        return UINT64_MAX;
    case N_ADD: return sat_add(maxval(r, p->a, sb), maxval(r, p->b, sb));
    case N_MUL: return sat_mul(maxval(r, p->a, sb), maxval(r, p->b, sb));
    case N_DIV: return maxval(r, p->a, sb) / r->nd[p->b].v;
    case N_REM: { uint64_t m = maxval(r, p->a, sb), k = r->nd[p->b].v - 1; return m < k ? m : k; }
    default: return UINT64_MAX;
    }
}

static int less(const Rx *r, int y, int M, int sb, const Facts *fc)
{
    if (r->nd[M].k == N_CONST && maxval(r, y, sb) < r->nd[M].v) return 1;
    for (int i = 0; i < fc->n; i++)
        if (fc->lt[i][0] == y && fc->lt[i][1] == M) return 1; /* GUARD:fact-lt */
    return 0;
}

/* Different digit values give different indexes. */
static int injective(const Rx *r, int n, int sb, const Facts *fc)
{
    const Nd *p = &r->nd[n];
    if (is_digit(p->k) || digit_free(r, n)) return 1;
    if (p->k == N_MUL) {
        if (r->nd[p->a].k == N_CONST && r->nd[p->a].v) return injective(r, p->b, sb, fc);
        if (r->nd[p->b].k == N_CONST && r->nd[p->b].v) return injective(r, p->a, sb, fc);
        return 0;
    }
    if (p->k != N_ADD) return 0;
    for (int o = 0; o < 2; o++) {
        int m = o ? p->b : p->a, y = o ? p->a : p->b;
        if (digit_free(r, y)) return injective(r, m, sb, fc);
        if (r->nd[m].k != N_MUL) continue;
        for (int q = 0; q < 2; q++) {
            int a = q ? r->nd[m].b : r->nd[m].a, M = q ? r->nd[m].a : r->nd[m].b;
            if (!digit_free(r, M)) continue;
            if (!less(r, y, M, sb, fc)) continue; /* GUARD:inj-less */
            if (injective(r, a, sb, fc) && injective(r, y, sb, fc) && disjoint(r, a, y)) return 1;
        }
    }
    return 0;
}

/* A loop digit means "this iteration" only inside its loop. */
static int loops_hold(const Rx *r, int n, int sb)
{
    int ds[RX_DIGITS], nd = 0;
    if (digits(r, n, ds, &nd)) return 0;
    for (int i = 0; i < nd; i++) {
        const Nd *p = &r->nd[ds[i]];
        if (p->k == N_LOOP && !btest(&r->loop[p->h], (unsigned)sb)) return 0; /* GUARD:loop-digit-site */
    }
    return 1;
}

static void add_cond(Rx *r, int reg, int pol, int sb, Facts *fc, int depth)
{
    const Fx *x = r->x;
    const C2Func *f = x->f;
    if (depth > 32 || reg < 0 || x->ndefs[reg] != 1 || x->def_insn[reg] < 0) return;
    const C2Insn *d = &f->in[x->def_insn[reg]];
    if (d->op == C2_MOV) { add_cond(r, d->a, pol, sb, fc, depth + 1); return; }
    if (d->op == C2_UN && d->sub == U_LNOT) { add_cond(r, d->a, !pol, sb, fc, depth + 1); return; }
    if (d->op == C2_BIN && d->sub == (pol ? B_AND : B_OR)) {
        add_cond(r, d->a, pol, sb, fc, depth + 1);
        add_cond(r, d->b, pol, sb, fc, depth + 1);
        return;
    }
    if (d->op != C2_CMP || f->vt[d->a].s != S_U32) return; /* trees are u32 only, so also implied (untagged) */
    int A = tree(r, d->a), B = tree(r, d->b);
    if (!A || !B) return;
    /* a fact about a loop digit holds only when the compare runs in that loop's same iteration */
    if (!loops_hold(r, A, x->blk_of[x->def_insn[reg]]) || !loops_hold(r, B, x->blk_of[x->def_insn[reg]])) return;
    uint8_t cc = d->sub;
    if (!pol) cc = cc == CC_GE ? CC_LT : cc == CC_LE ? CC_GT : cc == CC_NE ? CC_EQ : 0;
    if ((cc == CC_LT || cc == CC_GT) && fc->n < RX_FACTS) {
        fc->lt[fc->n][0] = cc == CC_LT ? A : B;
        fc->lt[fc->n][1] = cc == CC_LT ? B : A;
        fc->n++;
    }
    if (cc == CC_EQ && fc->nfix < RX_FACTS) {
        if (r->nd[A].k == N_CONST) { int t = A; A = B; B = t; }
        if (r->nd[B].k == N_CONST && is_digit(r->nd[A].k) && r->nd[A].k != N_LOOP) {
            fc->fix[fc->nfix] = A;
            fc->fixv[fc->nfix++] = r->nd[B].v;
        }
    }
}

/* Facts at block sb: the condition of every branch side that has the branch as its only
 * predecessor and dominates sb. */
static void facts_at(Rx *r, int sb, Facts *fc)
{
    const Fx *x = r->x;
    const C2Func *f = x->f;
    memset(fc, 0, sizeof *fc);
    for (int g = 0; g < f->nblocks; g++) {
        if (!x->reach[g]) continue;
        const C2Insn *t = &f->in[f->blk[g].first + f->blk[g].count - 1];
        if (t->op != C2_CBR || t->t == t->f) continue;
        for (int side = 0; side < 2; side++) {
            int s = side ? t->f : t->t;
            if (r->npred[s] != 1) continue; /* GUARD:fact-single-pred */
            if (!dominates(x, s, sb)) continue; /* GUARD:fact-dom */
            add_cond(r, t->a, !side, sb, fc, 0);
        }
    }
}

/* digits(E) plus the digits both accesses fix to the same number cover `thread`, and `block` /
 * `batch` when asked. */
static int covers(const Rx *r, int E, const Facts *a, const Facts *b, int want_block, int want_batch)
{
    int ds[RX_DIGITS + 2 * RX_FACTS], nd = 0;
    if (digits(r, E, ds, &nd)) return 0;
    for (int i = 0; i < a->nfix; i++)
        for (int j = 0; j < b->nfix; j++)
            if (a->fix[i] == b->fix[j] && a->fixv[i] == b->fixv[j]) ds[nd++] = a->fix[i]; /* GUARD:cover-fixed */
    int th = 0, bl = !want_block, ba = !want_batch;
    for (int i = 0; i < nd; i++) {
        const Nd *p = &r->nd[ds[i]];
        if (p->k == N_THREAD) th = 1;
        if (p->k == N_BLOCK) bl = 1;
        if (p->k == N_BATCH) ba = 1;
        if (p->k == N_LO)
            for (int j = 0; j < nd; j++)
                if (r->nd[ds[j]].k == N_HI && r->nd[ds[j]].v == p->v) th = 1; /* GUARD:cover-lohi */
    }
    return th && bl && ba;
}

typedef struct { uint32_t i; int ref, E, sb, store; uint64_t mask; Facts fc; } Acc;

static int race_pair(Rx *r, const Acc *p, const Acc *q, const char *rule, int wb, int wa)
{
    const Fx *x = r->x;
    if (!p->E || p->E != q->E) /* GUARD:race-same */
        return fail(x->v, rule, x->fi, (int)q->i, "v%d is accessed at two different indexes (insns %u and %u)", p->ref, p->i, q->i);
    if (!injective(r, q->E, q->sb, &q->fc)) /* GUARD:race-inj */
        return fail(x->v, rule, x->fi, (int)q->i, "the index of v%d is not one-to-one", q->ref);
    if (!loops_hold(r, q->E, q->sb)) /* GUARD:race-loop */
        return fail(x->v, rule, x->fi, (int)q->i, "a loop counter is used as an index outside its loop");
    if (!covers(r, q->E, &p->fc, &q->fc, wb, wa)) /* GUARD:race-cover */
        return fail(x->v, rule, x->fi, (int)q->i, "two threads can reach the same element of v%d", q->ref);
    return 0;
}

static int kernel_races(Fx *x)
{
    const C2Func *f = x->f;
    Rx *r = calloc(1, sizeof *r);
    Acc *acc = calloc(f->ninsns, sizeof *acc);
    int rc = -1, na = 0;
    if (!r || !acc) { fail(x->v, "C2-RESOURCE", x->fi, -1, "out of memory"); goto out; }
    r->x = x;
    r->T = f->threads;
    r->nn = 1; /* node 0 is the opaque tree */
    for (int i = 0; i < f->nvregs; i++) r->memo[i] = -1;
    for (int p = 0; p < f->nblocks; p++)
        for (int k = 0; k < x->nsucc[p]; k++)
            if (x->reach[p]) r->npred[x->succ[p][k]]++;
    for (int h = 0; h < f->nblocks; h++) { /* natural loops, as check_loops builds them */
        int st[C2_MAX_BLOCKS], n = 0;
        for (int p = 0; p < f->nblocks; p++)
            for (int k = 0; k < x->nsucc[p]; k++)
                if (x->reach[p] && x->succ[p][k] == h && dominates(x, h, p) && !btest(&r->loop[h], (unsigned)p)) {
                    bset(&r->loop[h], (unsigned)p);
                    st[n++] = p;
                }
        if (!n) continue;
        bset(&r->loop[h], (unsigned)h);
        while (n) {
            int b = st[--n];
            for (int p = 0; p < f->nblocks; p++) {
                if (!x->reach[p] || btest(&r->loop[h], (unsigned)p)) continue;
                for (int k = 0; k < x->nsucc[p]; k++)
                    if (x->succ[p][k] == b) { bset(&r->loop[h], (unsigned)p); st[n++] = p; break; }
            }
        }
    }
    /* phases: bit 0 is the kernel start, bit k the k-th sync line */
    static uint64_t pin[C2_MAX_BLOCKS], pout[C2_MAX_BLOCKS];
    uint32_t syncno[C2_MAX_SYNCS + 1];
    int nsync = 0;
    for (uint32_t i = 0; i < f->ninsns; i++)
        if (f->in[i].op == C2_SYNC && nsync < C2_MAX_SYNCS) syncno[++nsync] = i;
    memset(pout, 0, sizeof pout);
    for (int changed = 1; changed;) {
        changed = 0;
        for (int b = 0; b < f->nblocks; b++) {
            if (!x->reach[b]) continue;
            uint64_t m = b == 0 ? 1 : 0;
            for (int p = 0; p < f->nblocks; p++)
                for (int k = 0; k < x->nsucc[p]; k++)
                    if (x->reach[p] && x->succ[p][k] == b) m |= pout[p];
            pin[b] = m;
            for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].count; i++)
                if (f->in[i].op == C2_SYNC)
                    for (int s = 1; s <= nsync; s++)
                        if (syncno[s] == i) m = 1ull << s; /* GUARD:phase-sync */
            if (m != pout[b]) { pout[b] = m; changed = 1; }
        }
    }
    int want_block = 0, want_batch = 0;
    for (uint32_t i = 0; i < f->ninsns; i++) {
        const C2Insn *in = &f->in[i];
        if (in->op == C2_GPU && in->sub == G_BLOCK) want_block = 1;
        if (in->op == C2_GPU && in->sub == G_BATCH) want_batch = 1;
    }
    for (int b = 0; b < f->nblocks; b++) {
        if (!x->reach[b]) continue;
        uint64_t m = pin[b];
        for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].count; i++) {
            const C2Insn *in = &f->in[i];
            if (in->op == C2_SYNC)
                for (int s = 1; s <= nsync; s++)
                    if (syncno[s] == i) m = 1ull << s;
            if (in->op != C2_LOAD && in->op != C2_STORE && in->op != C2_GLOAD && in->op != C2_GSTORE) continue;
            /* a shared array register is defined once and never copied (C2-GPU-SHARED, bindable), and a
             * buffer register is a parameter never written, so the register names the array */
            int ref = in->a;
            Acc *a = &acc[na++];
            a->i = i; a->ref = ref; a->sb = b; a->mask = m;
            a->store = in->op == C2_STORE || in->op == C2_GSTORE;
            a->E = tree(r, in->b);
            facts_at(r, b, &a->fc);
        }
    }
    for (int p = 0; p < na; p++)
        for (int q = p; q < na; q++) {
            const Acc *A = &acc[p], *B = &acc[q];
            if (A->ref != B->ref || !(A->store || B->store)) continue;
            int shared = f->in[A->i].op == C2_LOAD || f->in[A->i].op == C2_STORE;
            if (shared) {
                if (!(A->mask & B->mask)) continue; /* GUARD:phase-apart */
                if (race_pair(r, A, B, "C2-GPU-SHARED-RACE", 0, 0) || race_pair(r, B, A, "C2-GPU-SHARED-RACE", 0, 0)) goto out;
            } else if (race_pair(r, A, B, "C2-GPU-GLOBAL-RACE", want_block, want_batch) ||
                       race_pair(r, B, A, "C2-GPU-GLOBAL-RACE", want_block, want_batch))
                goto out;
        }
    rc = 0;
out:
    free(acc);
    free(r);
    return rc;
}

/* ------------------------------------------------------------------ ownership flow */

/* May-states of a reference-owner register: bit 0 not yet holding, bit 1 live, bit 2 dead
 * (released, moved or its arena destroyed). Handles: bit 0 not opened, bit 1 open, bit 2 closed.
 * Only a state that excludes "live" on every path is an error. */
enum { ST_U = 1, ST_L = 2, ST_D = 4 };

typedef struct {
    int arena_obj[C2_MAX_VREGS];   /* defined by AALLOC: its handle register, else -1 */
    int origin[C2_MAX_VREGS];      /* borrow -> root owner register; -1 none/external; -2 mixed */
    uint8_t in[C2_MAX_BLOCKS][C2_MAX_VREGS];
} Own;

static int tracked(const Fx *x, int r)
{
    const C2Type *t = &x->f->vt[r];
    return (t->s == S_REF && t->kind == K_OWN);
}

#define OF(rule, ...) do { if (!x->quiet) return fail(x->v, rule, x->fi, (int)i, __VA_ARGS__); } while (0)

static int own_use(const Fx *x, const Own *o, const uint8_t *st, uint32_t i, int r)
{
    if (tracked(x, r) && !(st[r] & ST_L)) /* GUARD:own-use-dead */
        OF("C2-OWN-USE-DEAD", "owner v%d used after it was released or moved (every path)", r);
    int g = o->origin[r];
    if (g >= 0 && !(st[g] & ST_L)) /* GUARD:borrow-outlives */
        OF("C2-BORROW-OUTLIVES-OWNER", "borrow v%d used after its owner v%d is gone (every path)", r, g);
    return 0;
}

static int own_kill(const Fx *x, uint8_t *st, uint32_t i, int r, const char *what)
{
    (void)what;
    if (!(st[r] & ST_L)) OF("C2-OWN-USE-DEAD", "owner v%d moved after it was released or moved (every path)", r); /* GUARD:own-move-dead */
    st[r] = ST_D;
    return 0;
}

static int own_birth(const Fx *x, uint8_t *st, uint32_t i, int r)
{
    if (st[r] == ST_L) /* GUARD:own-overwrite */
        OF("C2-OWN-LEAK", "owner v%d overwritten while it still holds an object (every path)", r);
    st[r] = ST_L;
    return 0;
}

static int own_step(const Fx *x, const Own *o, uint8_t *st, uint32_t i)
{
    const C2Func *f = x->f;
    const C2Insn *in = &f->in[i];
    switch (in->op) {
    case C2_ALLOC:
        return own_birth(x, st, i, in->dst);
    case C2_AALLOC:
        if (!(st[in->b] & ST_L)) OF("C2-ARENA-DEAD", "allocation from arena v%d after it was destroyed (every path)", in->b); /* GUARD:arena-alloc-dead */
        return own_birth(x, st, i, in->dst);
    case C2_RELEASE:
        if (o->arena_obj[in->a] >= 0) OF("C2-OWN-RELEASE-ARENA", "release of arena object v%d", in->a); /* GUARD:release-arena */
        if (!(st[in->a] & ST_L)) OF("C2-OWN-DOUBLE-RELEASE", "v%d released when it holds nothing (every path)", in->a); /* GUARD:double-release */
        st[in->a] = ST_D;
        return 0;
    case C2_MOV:
        if (tracked(x, in->a)) {
            if (f->vt[in->dst].kind == K_OWN) {
                if (o->arena_obj[in->a] >= 0 || o->arena_obj[in->dst] >= 0)
                    OF("C2-OWN-ARENA-MOVE", "move involving arena object"); /* GUARD:arena-move */
                if (own_kill(x, st, i, in->a, "move")) return -1;
                return own_birth(x, st, i, in->dst);
            }
            return own_use(x, o, st, i, in->a);
        }
        return is_ref(&f->vt[in->a]) ? own_use(x, o, st, i, in->a) : 0;
    case C2_LOAD: case C2_STORE: case C2_FLOAD: case C2_FSTORE:
        return own_use(x, o, st, i, in->a);
    case C2_CALL: {
        const C2Func *g = &x->u->fn[in->callee];
        for (int k = 0; k < in->nargs; k++) {
            int r = in->args[k];
            if (!is_ref(&f->vt[r])) continue;
            if (own_use(x, o, st, i, r)) return -1;
            if (g->vt[k].kind == K_OWN) {
                if (o->arena_obj[r] >= 0) OF("C2-OWN-ARENA-MOVE", "arena object v%d passed to an own parameter", r);
                if (own_kill(x, st, i, r, "move into call")) return -1;
            }
        }
        return 0;
    }
    case C2_AOPEN: case C2_POPEN:
        if (st[in->dst] & ST_L) OF("C2-HANDLE-REOPEN", "handle v%d reopened while open", in->dst);
        st[in->dst] = ST_L;
        return 0;
    case C2_ADESTROY: case C2_PCLOSE:
        if (!(st[in->a] & ST_L)) OF("C2-HANDLE-DOUBLE-CLOSE", "handle v%d closed when not open (every path)", in->a); /* GUARD:double-close */
        st[in->a] = ST_D;
        if (in->op == C2_ADESTROY)
            for (int r = 0; r < f->nvregs; r++)
                if (o->arena_obj[r] == in->a && st[r] != ST_U) st[r] = ST_D;
        return 0;
    case C2_HALLOC: case C2_HGEN: case C2_HFREE: case C2_HLOAD: case C2_HSTORE: {
        int h = in->op == C2_HALLOC ? in->b : in->a;
        if (!(st[h] & ST_L)) OF("C2-HANDLE-DEAD", "pool v%d used when not open (every path)", h); /* GUARD:pool-dead */
        return 0;
    }
    case C2_RET:
        for (int r = 0; r < f->nvregs; r++) {
            int handle = x->def_insn[r] >= 0 && (f->in[x->def_insn[r]].op == C2_AOPEN || f->in[x->def_insn[r]].op == C2_POPEN);
            if (st[r] == ST_L && (handle || (tracked(x, r) && o->arena_obj[r] < 0))) /* GUARD:leak */
                OF(handle ? "C2-HANDLE-LEAK" : "C2-OWN-LEAK", "v%d still %s at return (every path)", r, handle ? "open" : "owns an object");
        }
        return 0;
    default:
        return 0;
    }
}

static int check_ownership(Fx *x)
{
    const C2Func *f = x->f;
    Own *o = malloc(sizeof *o);
    if (!o) return fail(x->v, "C2-RESOURCE", x->fi, -1, "out of memory");
    for (int r = 0; r < f->nvregs; r++) { o->arena_obj[r] = -1; o->origin[r] = -1; }
    for (uint32_t i = 0; i < f->ninsns; i++) {
        const C2Insn *in = &f->in[i];
        if (in->op == C2_AALLOC) o->arena_obj[in->dst] = in->b;
    }
    for (uint32_t i = 0; i < f->ninsns; i++) {
        const C2Insn *in = &f->in[i];
        if ((in->op == C2_ALLOC && o->arena_obj[in->dst] >= 0) ||
            (in->op == C2_AALLOC && o->arena_obj[in->dst] != in->b)) {
            free(o);
            return fail(x->v, "C2-OWN-ARENA-MOVE", x->fi, (int)i, "register v%d holds both arena and pool objects", in->dst);
        }
    }
    /* borrow origins: flow-insensitive, single root owner or unknown */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (uint32_t i = 0; i < f->ninsns; i++) {
            const C2Insn *in = &f->in[i];
            if (in->op != C2_MOV || !is_ref(&f->vt[in->dst]) || f->vt[in->dst].kind == K_OWN) continue;
            int root = f->vt[in->a].kind == K_OWN ? in->a : o->origin[in->a];
            int cur = o->origin[in->dst];
            int nw = cur == -1 && root >= 0 ? root : (cur == root ? cur : -2);
            if (in->dst < f->nparams) nw = -2;
            if (nw != cur && cur != -2) { o->origin[in->dst] = nw; changed = 1; }
        }
    }
    for (int r = 0; r < f->nvregs; r++)
        if (o->origin[r] == -2) o->origin[r] = -1;

    /* Fixpoint of the may-states. In-states only grow (bitwise OR over a finite lattice), so this
     * terminates. Transfer runs quietly (effects applied, no verdict); a TRAP ends its path, so a
     * trapping block propagates nothing. Blocks no non-trapping path reaches are not judged. */
    static int live[C2_MAX_BLOCKS];
    memset(o->in, 0, sizeof o->in);
    memset(live, 0, sizeof live);
    for (int r = 0; r < f->nvregs; r++) o->in[0][r] = (r < f->nparams && tracked(x, r)) ? ST_L : ST_U;
    live[0] = 1;
    x->quiet = 1;
    changed = 1;
    while (changed) {
        changed = 0;
        for (int b = 0; b < f->nblocks; b++) {
            if (!live[b]) continue;
            uint8_t st[C2_MAX_VREGS];
            memcpy(st, o->in[b], f->nvregs);
            int trapped = 0;
            for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].count && !trapped; i++) {
                if (f->in[i].op == C2_TRAP) trapped = 1;
                else (void)own_step(x, o, st, i);
            }
            if (trapped) continue;
            for (int k = 0; k < x->nsucc[b]; k++) {
                int s = x->succ[b][k];
                if (!live[s]) { live[s] = 1; changed = 1; }
                for (int r = 0; r < f->nvregs; r++) {
                    uint8_t m = o->in[s][r] | st[r];
                    if (m != o->in[s][r]) { o->in[s][r] = m; changed = 1; }
                }
            }
        }
    }
    x->quiet = 0;
    /* report the first definite error */
    for (int b = 0; b < f->nblocks; b++) {
        if (!live[b]) continue;
        uint8_t st[C2_MAX_VREGS];
        memcpy(st, o->in[b], f->nvregs);
        for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].count; i++) {
            if (f->in[i].op == C2_TRAP) break;
            if (own_step(x, o, st, i)) { free(o); return -1; }
        }
    }
    free(o);
    return 0;
}

/* ------------------------------------------------------------------ driver */

static int check_func(const C2Unit *u, int fi, C2Verdict *v)
{
    const C2Func *f = &u->fn[fi];
    for (int g = 0; g < fi; g++)
        if (strcmp(u->fn[g].name, f->name) == 0) return fail(v, "C2-FUNC-NAME", fi, -1, "function %s defined twice", f->name); /* GUARD:func-name */
    int kern = f->kind == FN_KERNEL;
    if (f->nparams > (kern ? C2_MAX_KPARAMS : C2_MAX_PARAMS) || f->nparams > f->nvregs) /* GUARD:params */
        return fail(v, "C2-FUNC-PARAMS", fi, -1, "%u parameters, %u registers", f->nparams, f->nvregs);
    if (kern && (f->ret.s != S_VOID || f->req_len || f->ens_len)) /* GUARD:kernel-void */
        return fail(v, "C2-GPU-KERNEL", fi, -1, "a kernel returns nothing and carries no requires/ensures");
    if (kern && (f->threads < C2_MIN_THREADS || f->threads > C2_MAX_THREADS || f->threads % C2_WARP)) /* GUARD:kernel-threads */
        return fail(v, "C2-GPU-KERNEL", fi, -1, "%u threads per block (32..1024, a multiple of 32)", f->threads);
    if (!(f->ret.s == S_VOID || is_value(f->ret.s))) /* GUARD:ret-scalar */
        return fail(v, "C2-FUNC-RETURN", fi, -1, "function returns a non-scalar");
    for (int r = 0; r < f->nvregs; r++) {
        const C2Type *t = &f->vt[r];
        if (!check_type(u, t)) return fail(v, "C2-TYPE-INVALID", fi, -1, "register v%d has an invalid type", r); /* GUARD:vreg-type */
        int ktype = is_gbuf(t) || is_block(t) || t->s == S_F32;
        if (kern ? !(ktype || t->s == S_BOOL || t->s == S_U32) : ktype) /* GUARD:gpu-type */
            return fail(v, "C2-GPU-TYPE", fi, -1, "register v%d: type %u is not allowed in a %s", r, t->s, kern ? "kernel" : "host function");
        if (is_block(t) && r < f->nparams) /* GUARD:block-param */
            return fail(v, "C2-GPU-SHARED", fi, -1, "shared array v%d is a parameter", r);
        if (is_gbuf(t)) {
            if (r + 1 >= f->nparams || f->vt[r + 1].s != S_U32) /* GUARD:gbuf-param */
                return fail(v, "C2-GPU-BUFFER", fi, -1, "buffer register v%d is not a parameter followed by its u32 length", r);
            if (r > 0 && is_gbuf(&f->vt[r - 1]))
                return fail(v, "C2-GPU-BUFFER", fi, -1, "buffer v%d sits in the length register of v%d", r, r - 1);
        }
        if (is_slice(t)) {
            if (r + 1 >= f->nparams || f->vt[r + 1].s != S_U64) /* GUARD:slice-param */
                return fail(v, "C2-SLICE-PARAM", fi, -1, "slice register v%d is not a parameter followed by its u64 length", r);
            if (r > 0 && is_slice(&f->vt[r - 1]))
                return fail(v, "C2-SLICE-PARAM", fi, -1, "slice v%d sits in the length register of v%d", r, r - 1);
        }
    }
    Fx *x = calloc(1, sizeof *x);
    if (!x) return fail(v, "C2-RESOURCE", fi, -1, "out of memory");
    x->u = u; x->f = f; x->fi = fi; x->v = v; x->nb = f->nblocks;
    x->blk_of = calloc(f->ninsns, sizeof *x->blk_of);
    int rc = -1;
    if (!x->blk_of) { fail(v, "C2-RESOURCE", fi, -1, "out of memory"); goto out; }
    if (build_cfg(x)) goto out;
    for (uint32_t i = 0; i < f->ninsns; i++)
        if (check_insn(x, i)) goto out;
    link_cfg(x);
    count_defs(x);
    if (kernel_ops(x)) goto out;
    if (kern && kernel_uniform(x)) goto out;
    if (check_handles(x)) goto out;
    if (check_defined(x)) goto out;
    if (check_loops(x)) goto out;
    if (check_ownership(x)) goto out;
    if (kern && kernel_races(x)) goto out;
    rc = 0;
out:
    free(x->blk_of);
    free(x);
    return rc;
}

int c2_check_unit(const C2Unit *u, C2Verdict *v)
{
    if (v) { memset(v, 0, sizeof *v); v->ok = 1; v->func = v->insn = -1; v->rule = "ACCEPT"; }
    if (check_structs(u, v)) return -1;
    for (int fi = 0; fi < u->nfuncs; fi++)
        if (check_func(u, fi, v)) return -1;
    return 0;
}
