/* sm121dis -- Omega's sm_121 disassembler on the command line (C6 of docs/OMEGA.md; the decoder is
 * src/omega_sm121_disasm.c).
 *
 *   sm121dis <code.bin>          a listing in nvdisasm's layout: the address in a comment, then TEXT ;
 *   sm121dis --pairs <code.bin>  one line per word: <addr> <lo hex> <hi hex> <text> ;  (what check.sh compares)
 *
 * Exit 0 when every word was a known form, 1 when some were not (printed "?? ..."), 2 for a usage or file error. */
#include "omega_sm121_disasm.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int pairs = argc == 3 && strcmp(argv[1], "--pairs") == 0;
    if (argc != 2 + pairs) { fprintf(stderr, "usage: sm121dis [--pairs] <code.bin>\n"); return 2; }
    FILE *f = fopen(argv[1 + pairs], "rb");
    if (!f) { perror(argv[1 + pairs]); return 2; }
    unsigned char b[16];
    size_t n;
    uint64_t pc = 0;
    int unknown = 0;
    while ((n = fread(b, 1, 16, f)) == 16) {
        uint64_t lo = 0, hi = 0;
        for (int i = 7; i >= 0; i--) { lo = lo << 8 | b[i]; hi = hi << 8 | b[8 + i]; }
        char t[256];
        int rc = omega_sm121_disasm(lo, hi, pc, t, sizeof t);
        if (rc == -2) { fprintf(stderr, "sm121dis: text too long at 0x%llx\n", (unsigned long long)pc); fclose(f); return 2; }
        if (rc) unknown++;
        if (pairs) printf("%04llx %016llx %016llx %s ;\n", (unsigned long long)pc, (unsigned long long)lo, (unsigned long long)hi, t);
        else printf("        /*%04llx*/ %s ;\n", (unsigned long long)pc, t);
        pc += 16;
    }
    fclose(f);
    if (n != 0) { fprintf(stderr, "sm121dis: %s is not a whole number of 16-byte words\n", argv[1 + pairs]); return 2; }
    return unknown ? 1 : 0;
}
