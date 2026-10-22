/* Test-only host adapter. Calls the unchanged OSC encoder/decoder/validator and
 * checker2. This executable is never part of the source-aware production path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osc_front.h"
#include "c2_ir.h"
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    char *source = malloc(1u << 20);
    if (!source) return 2;
    size_t n = fread(source, 1, (1u << 20)-1, f);
    if (ferror(f) || !feof(f)) return 2;
    fclose(f);
    OscUnit *unit = calloc(1, sizeof *unit), *decoded = calloc(1, sizeof *decoded);
    if (!unit || !decoded) return 2;
    OscDiag diag;
    if (osc_compile(source, n, unit, &diag, NULL)) return 3;
    size_t size = 0;
    if (osc_ir_encode(unit, NULL, 0, &size)) return 4;
    unsigned char *bytes = malloc(size), *again = malloc(size);
    if (!bytes || !again || osc_ir_encode(unit, bytes, size, &size)) return 4;
    char err[256]; C2Verdict verdict;
    if (osc_ir_decode(bytes, size, decoded) || osc_ir_validate(decoded, err, sizeof err)) return 5;
    size_t count = size;
    if (osc_ir_encode(decoded, again, size, &count) || size != count || memcmp(bytes, again, size)) return 5;
    if (c2_check_bytes(bytes, size, &verdict)) return 6;
    if (fwrite(bytes, 1, size, stdout) != size) return 7;
    return 0;
}
