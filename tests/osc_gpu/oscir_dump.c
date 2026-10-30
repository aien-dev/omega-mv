/* TEST-ONLY ORACLE (not an implementation): prints what the C decoder and
 * validator of src/compiler/osc_ir.c give for one encoded unit, in the exact
 * text format seed/oscgpu/oscirdump.om prints for the seeded .om port
 * seed/oscgpu/oscir.om. tests/osc_gpu/oscir_diff.sh compares the two byte for
 * byte. Lines:
 *   DECODE rc=N            osc_ir_decode
 *   UNIT/STRUCT/FIELD/FUNC/VREG/BLOCK/INSN   the decoded unit (entries within counts)
 *   NVDECODE rc=N          osc_ir_decode with its validate step off (decoder mutant 4)
 *   VALIDATE rc=N msg      osc_ir_validate of that unit, message as written
 * Build: cc -std=gnu11 -O2 -Isrc -Isrc/compiler tests/osc_gpu/oscir_dump.c \
 *           src/compiler/osc_ir.c src/compiler/osc_alloc.c src/omega_om_units.c */
#include "osc_ir.h"

/* second copy of osc_ir.c with the validate call inside decode switched off
 * (the file's own OSC_IR_DECODE_MUTANT 4 hook), public names suffixed _nv */
#define osc_scalar_width osc_scalar_width_nv
#define osc_scalar_signed osc_scalar_signed_nv
#define osc_scalar_is_int osc_scalar_is_int_nv
#define osc_ir_has_kernel osc_ir_has_kernel_nv
#define osc_scalar_name osc_scalar_name_nv
#define osc_ir_validate_a osc_ir_validate_a_nv
#define osc_ir_validate osc_ir_validate_nv
#define osc_ir_encoding_version osc_ir_encoding_version_nv
#define osc_ir_slice_args_ok osc_ir_slice_args_ok_nv
#define osc_ir_encode osc_ir_encode_nv
#define osc_ir_digest osc_ir_digest_nv
#define osc_ir_decode osc_ir_decode_nv
#define osc_ir_refusal_name osc_ir_refusal_name_nv
#define OSC_IR_DECODE_MUTANT 4
#include "../../src/compiler/osc_ir.c"
#undef osc_scalar_width
#undef osc_scalar_signed
#undef osc_scalar_is_int
#undef osc_ir_has_kernel
#undef osc_scalar_name
#undef osc_ir_validate_a
#undef osc_ir_validate
#undef osc_ir_encoding_version
#undef osc_ir_slice_args_ok
#undef osc_ir_encode
#undef osc_ir_digest
#undef osc_ir_decode
#undef osc_ir_refusal_name

static void hex(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", (unsigned char)s[i]);
}

static void dump_unit(const OscUnit *u) {
    printf("UNIT nfn=%u nst=%u\n", u->nfuncs, u->nstructs);
    for (unsigned k = 0; k < u->nstructs && k < OSC_MAX_STRUCTS; k++) {
        const OscStruct *s = &u->structs[k];
        printf("STRUCT %u name=", k);
        hex(s->name, strnlen(s->name, OSC_NAME_MAX));
        printf(" %u %u\n", s->nfields, s->ncells);
        for (unsigned g = 0; g < s->nfields && g < OSC_MAX_FIELDS; g++) {
            const OscField *fd = &s->fields[g];
            printf("FIELD %u %u name=", k, g);
            hex(fd->name, strnlen(fd->name, OSC_NAME_MAX));
            printf(" %u %u %u\n", (unsigned)fd->s, fd->alen, fd->off);
        }
    }
    for (unsigned f = 0; f < u->nfuncs && f < OSC_MAX_FUNCS; f++) {
        const OscFunc *fn = &u->funcs[f];
        printf("FUNC %u %u %u %u %u %u %u name=", f, fn->kind, fn->threads, fn->nparams, fn->nvregs,
               fn->ninsns, fn->nblocks);
        hex(fn->name, strnlen(fn->name, OSC_NAME_MAX));
        printf(" %u %u %u %u %u req=", (unsigned)fn->ret.s, (unsigned)fn->ret.ref, (unsigned)fn->ret.elem,
               (unsigned)fn->ret.len, (unsigned)fn->ret.sid);
        hex(fn->requires_text, strnlen(fn->requires_text, OSC_CLAUSE_MAX));
        printf(" ens=");
        hex(fn->ensures_text, strnlen(fn->ensures_text, OSC_CLAUSE_MAX));
        printf("\n");
        for (unsigned v = 0; v < fn->nvregs && v < OSC_MAX_VREGS; v++) {
            const OscType *t = &fn->vtype[v];
            printf("VREG %u %u %u %u %u %u %u\n", f, v, (unsigned)t->s, (unsigned)t->ref, (unsigned)t->elem,
                   (unsigned)t->len, (unsigned)t->sid);
        }
        for (unsigned b = 0; b < fn->nblocks && b < OSC_MAX_BLOCKS; b++)
            printf("BLOCK %u %u %u %u\n", f, b, fn->blocks[b].first, fn->blocks[b].count);
        for (unsigned i = 0; i < fn->ninsns && i < OSC_MAX_INSNS; i++) {
            const OscInsn *x = &fn->insns[i];
            printf("INSN %u %u %u %u %d %d %d %d %d %d %d %u", f, i, x->op, x->sub, x->dst, x->a, x->b, x->c,
                   x->blk_t, x->blk_f, x->callee, x->nargs);
            for (int k = 0; k < OSC_MAX_PARAMS; k++) printf(" %d", x->args[k]);
            printf(" %llu %u\n", (unsigned long long)x->imm, x->line);
        }
    }
}

static OscUnit g_dump_unit;
static uint8_t g_dump_buf[OSC_IR_ENCODED_MAX + 2];

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        printf("READFAIL\n");
        return 2;
    }
    size_t n = fread(g_dump_buf, 1, sizeof g_dump_buf, fp);
    fclose(fp);
    int rc = osc_ir_decode(g_dump_buf, n, &g_dump_unit);
    printf("DECODE rc=%d\n", rc);
    dump_unit(&g_dump_unit);
    rc = osc_ir_decode_nv(g_dump_buf, n, &g_dump_unit);
    printf("NVDECODE rc=%d\n", rc);
    char err[256];
    err[0] = 0;
    rc = osc_ir_validate(&g_dump_unit, err, sizeof err);
    printf("VALIDATE rc=%d %s\n", rc, rc ? err : "");
    return 0;
}
