/*
 * oscir.c -- `oscir <file.osc> <out.ir>` (L093 gate RT helper): compile an OSC source with the same front
 * end as oscc (osc_compile) and write exactly the IR bytes osc_ir_encode gives, nothing else.
 * Why: oscc writes IR bytes only inside a unit container (OUT.{ir,code,entries}), and the container
 * refuses a function with a struct-reference parameter (OSC_UNIT_ARTIFACT.md section 6.1: kind 10). oscc
 * without OUT still prints ir_sha256 of the same encoding (osc_ir_digest), so gate_structs.sh takes A
 * from oscc and requires sha256 of these bytes to equal it before it lifts them.
 * Prints ir_sha256=<hex>. Exit 0 written; 1 the compiler refused (diagnostic on stdout); 2 usage / I/O.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osc_front.h"
#include "osc_ir.h"
#include "sha256.h"

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: oscir <file.osc> <out.ir>\n"); return 2; }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { fprintf(stderr, "oscir: cannot open %s\n", argv[1]); return 2; }
    static char src[1 << 20];
    size_t len = fread(src, 1, sizeof src, fp);
    int big = !feof(fp);
    fclose(fp);
    if (big) { fprintf(stderr, "oscir: %s is larger than %zu bytes\n", argv[1], sizeof src); return 2; }
    OscUnit *u = malloc(sizeof *u);
    if (!u) { fprintf(stderr, "oscir: out of memory\n"); return 2; }
    OscDiag d;
    if (osc_compile(src, len, u, &d, NULL)) {
        printf("error kind=%s line=%u col=%u message=%s\n", osc_diag_kind_name(d.kind), d.line, d.col, d.message);
        free(u);
        return 1;
    }
    size_t n = 0;
    uint8_t *ir = NULL;
    if (osc_ir_encode(u, NULL, 0, &n) || !(ir = malloc(n ? n : 1)) || osc_ir_encode(u, ir, n, &n)) {
        printf("error kind=encode message=the IR validator refused the unit\n");
        free(ir);
        free(u);
        return 1;
    }
    free(u);
    FILE *out = fopen(argv[2], "wb");
    int bad = !out || fwrite(ir, 1, n, out) != n;
    if (out && fclose(out)) bad = 1;
    if (bad) { fprintf(stderr, "oscir: cannot write %s\n", argv[2]); free(ir); return 2; }
    uint8_t dig[32];
    sha256_hash(ir, n, dig);
    free(ir);
    printf("ir_sha256=");
    for (int i = 0; i < 32; i++) printf("%02x", dig[i]);
    printf("\n");
    return 0;
}
