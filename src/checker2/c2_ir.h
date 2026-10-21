/* c2_ir: checker2's own decoder and acceptance rules for canonical OSC IR bytes.
 *
 * INDEPENDENCE. Written from the byte layout of the encoder (src/compiler/osc_ir.c, the
 * canonical-encoding section only, lines 537-729 at d58023e6) and the meaning stated in
 * docs/osc/OSC-1-DESIGN.md, OSC-2-DESIGN.md, OSC-3-DESIGN.md, OSC-EXT-BYTES-DESIGN.md and the
 * comments of src/compiler/osc_ir.h. It includes no compiler header, links no compiler object and
 * never read osc_check.* or osc_ir_validate. Every constant below is restated here, not imported.
 * The rule list is in spec/one-layer-receipts.md section 9. GPU kernels (encoding version 6) are
 * restated from docs/omegese/GPU.md sections 6-9 and the version-6 byte layout stated there.
 *
 * Fails closed: anything not understood is a rejection with a stable rule name. */
#ifndef C2_IR_H
#define C2_IR_H

#include <stddef.h>
#include <stdint.h>

/* limits restated from the OSC design (osc_ir.h comments) */
#define C2_MAX_FUNCS 32
#define C2_MAX_PARAMS 6
#define C2_MAX_VREGS 480
#define C2_MAX_INSNS 4096
#define C2_MAX_BLOCKS 512
#define C2_MAX_ARRAY 64
#define C2_NAME_MAX 63      /* bytes, excluding the terminator the producer keeps */
#define C2_CLAUSE_MAX 255
#define C2_MAX_STRUCTS 16
#define C2_MAX_FIELDS 16
#define C2_POOL_SLOTS 16
#define C2_TRAP_MAX 14
#define C2_MAX_KPARAMS 32          /* GPU kernel parameter registers (GPU.md section 2) */
#define C2_MIN_THREADS 32          /* threads per block of a kernel: 32..1024, a multiple of the warp */
#define C2_MAX_THREADS 1024
#define C2_WARP 32
#define C2_MAX_SYNCS 63            /* sync lines per kernel (GPU.md section 8) */
#define C2_MAX_NEST 16             /* nesting of varying regions (GPU.md section 6) */
#define C2_MAX_SHARED_LEN 12288    /* elements of one block-shared array */
#define C2_MAX_SHARED_BYTES 49152  /* block-shared bytes of one kernel, 4 per element */

/* scalar codes on the wire */
enum { S_VOID = 0, S_BOOL, S_U8, S_U16, S_U32, S_U64, S_I8, S_I16, S_I32, S_I64, S_REF, S_BYTES, S_CELLS,
       S_F32, S_GVIEW, S_GEDIT, /* version 6: binary32; global buffer pointers (one elem byte follows) */
       S_LIMIT };
enum { K_NONE = 0, K_OWN, K_SHARED, K_MUT, K_BLOCK /* version 6: block-shared array */ };
enum { FN_HOST = 0, FN_KERNEL = 1 }; /* function kind byte, version 6 */

/* opcodes on the wire */
enum {
    C2_CONST = 1, C2_MOV, C2_BIN, C2_UN, C2_CMP, C2_CAST, C2_ALLOC, C2_RELEASE, C2_LOAD, C2_STORE,
    C2_CALL, C2_TRAP, C2_BR, C2_CBR, C2_RET, C2_FLOAD, C2_FSTORE, C2_AOPEN, C2_AALLOC, C2_ADESTROY,
    C2_POPEN, C2_PCLOSE, C2_HALLOC, C2_HGEN, C2_HFREE, C2_HLOAD, C2_HSTORE, C2_SLOAD, C2_SSTORE,
    C2_GPU, C2_SHARED, C2_GLOAD, C2_GSTORE, C2_SYNC, C2_SHFL, C2_MAD, /* version 6, kernels only */
    C2_OP_LIMIT
};
enum { B_ADD = 1, B_SUB, B_MUL, B_DIV, B_REM, B_AND, B_OR, B_XOR, B_SHL, B_SHR, B_MIN, B_MAX, B_LIMIT };
enum { U_NEG = 1, U_BNOT, U_LNOT, U_EXP, U_INV, U_RSQ, U_SQRT, U_LIMIT };
enum { G_THREAD = 1, G_BLOCK, G_BLOCKS, G_BATCH, G_BATCHES, G_LIMIT }; /* C2_GPU sub */
enum { SH_DOWN = 1 };                                                              /* C2_SHFL sub */
enum { CC_EQ = 1, CC_NE, CC_LT, CC_LE, CC_GT, CC_GE, CC_LIMIT };

typedef struct {
    uint8_t s, kind, elem, sid;
    uint16_t len;
} C2Type;

typedef struct {
    uint8_t op, sub, nargs;
    int16_t dst, a, b, c;
    int16_t args[C2_MAX_PARAMS];
    uint16_t t, f, callee;
    uint64_t imm;
} C2Insn;

typedef struct { uint32_t first, count; } C2Block;

typedef struct {
    char name[C2_NAME_MAX + 1];
    uint8_t kind;     /* FN_HOST, FN_KERNEL; on the wire only from version 6 */
    uint16_t threads; /* kernel: threads per block (u16 after the kind byte); host: 0 */
    uint8_t nparams;
    C2Type ret;
    uint16_t nvregs, nblocks;
    uint32_t ninsns;
    C2Type *vt;       /* nvregs */
    C2Block *blk;     /* nblocks */
    C2Insn *in;       /* ninsns */
    uint16_t req_len, ens_len;
    char req[C2_CLAUSE_MAX + 1], ens[C2_CLAUSE_MAX + 1];
} C2Func;

typedef struct {
    char name[C2_NAME_MAX + 1];
    uint8_t s;
    uint16_t alen, off;
} C2Field;

typedef struct {
    char name[C2_NAME_MAX + 1];
    uint8_t nfields;
    uint16_t ncells;
    C2Field f[C2_MAX_FIELDS];
} C2Struct;

typedef struct {
    uint8_t version;
    uint8_t nstructs;
    C2Struct st[C2_MAX_STRUCTS];
    uint16_t nfuncs;
    C2Func fn[C2_MAX_FUNCS];
} C2Unit;

/* Verdict. rule is a stable name ("C2-..."), func / insn locate it (-1 when not applicable). */
typedef struct {
    int ok;
    const char *rule;
    int func, insn;
    char detail[160];
} C2Verdict;

/* Strict decode: exact byte layout, canonical version byte, limits, no trailing bytes.
 * 0 ok, -1 rejected with v filled. Call c2_unit_free afterwards in both cases. */
int c2_decode(const uint8_t *buf, size_t len, C2Unit *u, C2Verdict *v);
void c2_unit_free(C2Unit *u);
/* Acceptance rules over a decoded unit (types, operands, CFG, calls, definite assignment,
 * handle disciplines, ownership). 0 accept, -1 reject. */
int c2_check_unit(const C2Unit *u, C2Verdict *v);
/* decode + check. */
int c2_check_bytes(const uint8_t *buf, size_t len, C2Verdict *v);

/* checker2's own canonical re-encoding (used by the tests to plant defects structurally and to
 * prove decode is the exact inverse on accepted inputs). Returns the length, or 0 if cap is too
 * small. */
size_t c2_encode(const C2Unit *u, uint8_t *buf, size_t cap);

#endif /* C2_IR_H */
