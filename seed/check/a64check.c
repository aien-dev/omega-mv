/*
 * a64check.c -- REFERENCE CHECK ONLY for the seed chain (seed/README.md). It is never used to make a
 * byte of seed/: it runs the repo's own C decoder (src/aarch64_decoder.c) and C encoder
 * (src/aarch64_encoder.c) so the hand-written words can be compared against them.
 *
 *   a64check decode            stdin: one hex word per line; stdout: the decoder's fields per word
 *   a64check encode <form> <args...>   one word from the C encoder for that form, as 8 hex digits
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aarch64_decoder.h"
#include "aarch64_encoder.h"

static const char *opname(DecodedOp op)
{
    static const char *n[] = {"INVALID", "ADD", "SUB", "MUL", "AND", "ORR", "EOR", "MOVZ", "RET", "B",
                              "B_COND", "CBZ", "CBNZ", "MOVK", "ADR", "LDR", "STR", "LDRB", "STRB", "SUBS"};
    return (unsigned)op < sizeof n / sizeof n[0] ? n[op] : "?";
}

static int decode(void)
{
    char line[128];
    while (fgets(line, sizeof line, stdin)) {
        unsigned long w = strtoul(line, NULL, 16);
        DecodedInsn d;
        memset(&d, 0, sizeof d);
        if (aarch64_decode_instruction((uint32_t)w, &d) != 0) d.op = DECODED_INVALID;
        printf("op=%s sf=%d rd=%u rn=%u rm=%u imm16=%u hw=%u bimm=%ld cond=%d\n", opname(d.op), d.sf ? 1 : 0,
               d.rd, d.rn, d.rm, d.imm16, d.hw, (long)d.branch_imm, (int)d.cond);
    }
    return 0;
}

static long a(char **v, int i) { return strtol(v[i], NULL, 0); }

static int encode(int c, char **v)
{
    uint8_t b[4];
    size_t pos = 0;
    int rc = -1;
    const char *f = v[2];
    if (!strcmp(f, "add_reg") && c == 7) rc = aarch64_emit_add_reg(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "sub_reg") && c == 7) rc = aarch64_emit_sub_reg(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "mul_reg") && c == 7) rc = aarch64_emit_mul_reg(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "and_reg") && c == 7) rc = aarch64_emit_and_reg(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "orr_reg") && c == 7) rc = aarch64_emit_orr_reg(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "eor_reg") && c == 7) rc = aarch64_emit_eor_reg(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "subs_reg") && c == 7) rc = aarch64_emit_subs_reg(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "subs_imm") && c == 7) rc = aarch64_emit_subs_imm(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "mov_reg") && c == 6) rc = aarch64_emit_mov_reg(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else if (!strcmp(f, "movz") && c == 7) rc = aarch64_emit_movz(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "movk") && c == 7) rc = aarch64_emit_movk(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "ret") && c == 3) rc = aarch64_emit_ret(b, &pos, 4);
    else if (!strcmp(f, "b") && c == 4) rc = aarch64_emit_b(b, &pos, 4, a(v, 3));
    else if (!strcmp(f, "b_cond") && c == 5) rc = aarch64_emit_b_cond(b, &pos, 4, (Aarch64Cond)a(v, 3), a(v, 4));
    else if (!strcmp(f, "cbz") && c == 6) rc = aarch64_emit_cbz(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else if (!strcmp(f, "cbnz") && c == 6) rc = aarch64_emit_cbnz(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else if (!strcmp(f, "adr") && c == 5) rc = aarch64_emit_adr(b, &pos, 4, a(v, 3), a(v, 4));
    else if (!strcmp(f, "ldr_uoff") && c == 7) rc = aarch64_emit_ldr_uoff(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "str_uoff") && c == 7) rc = aarch64_emit_str_uoff(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5), a(v, 6));
    else if (!strcmp(f, "ldrb_uoff") && c == 6) rc = aarch64_emit_ldrb_uoff(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else if (!strcmp(f, "strb_uoff") && c == 6) rc = aarch64_emit_strb_uoff(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else if (!strcmp(f, "ldr_post") && c == 6) rc = aarch64_emit_ldr_post(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else if (!strcmp(f, "str_post") && c == 6) rc = aarch64_emit_str_post(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else if (!strcmp(f, "ldr_x_post") && c == 6) rc = aarch64_emit_ldr_x_post(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else if (!strcmp(f, "str_x_post") && c == 6) rc = aarch64_emit_str_x_post(b, &pos, 4, a(v, 3), a(v, 4), a(v, 5));
    else { fprintf(stderr, "a64check: unknown form or argument count: %s\n", f); return 2; }
    if (rc != 0 || pos != 4) { printf("REFUSED\n"); return 1; }
    printf("%02x%02x%02x%02x\n", b[3], b[2], b[1], b[0]);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "decode")) return decode();
    if (argc >= 3 && !strcmp(argv[1], "encode")) return encode(argc, argv);
    fprintf(stderr, "usage: a64check decode | a64check encode <form> <args...>\n");
    return 2;
}
