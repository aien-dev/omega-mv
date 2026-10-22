/* osc_gpu_build -- build one kernel of a checked OSC IR unit to sm_121 code (L386, C1 of docs/OMEGA.md).
 *
 *   osc_gpu_build <unit.ir> <kernel-name> <out.bin> <out.h> <c-prefix>
 *
 * The unit must already be accepted by checker2 (the chain: omegese emit, omega_emit emit, checker2 check).
 * Writes the machine code to out.bin and a C header to out.h that embeds the same bytes with the facts a
 * launcher needs: thread count, registers, shared bytes, every argument offset (bytes from 0x380), and the
 * SHA-256 of the IR and of the code. Exit 0 written; 1 refused (one line on stdout); 2 usage or I/O. */
#include "omega_osc_gpu.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hex(const uint8_t *d, size_t n, char *out) { for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", d[i]); }

int main(int argc, char **argv) {
    if (argc != 6) { fprintf(stderr, "usage: osc_gpu_build <unit.ir> <kernel-name> <out.bin> <out.h> <c-prefix>\n"); return 2; }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 2; }
    static uint8_t buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    static OscUnit u;
    int rc = osc_ir_decode(buf, n, &u);
    if (rc) { printf("REFUSED decode: %s\n", osc_ir_refusal_name(rc)); return 1; }
    int fn = -1;
    for (int i = 0; i < u.nfuncs; i++) if (!strcmp(u.funcs[i].name, argv[2])) fn = i;
    if (fn < 0) { printf("REFUSED: no function %s in %s\n", argv[2], argv[1]); return 1; }
    OscGpuKernel k;
    char err[256];
    if (osc_gpu_build(&u, (unsigned)fn, &k, err, sizeof err) != 0) { printf("REFUSED build: %s\n", err); return 1; }
    uint8_t irsha[32];
    sha256_hash(buf, n, irsha);
    char irh[65], codeh[65];
    hex(irsha, 32, irh);
    hex(k.code_sha256, 32, codeh);
    FILE *ob = fopen(argv[3], "wb");
    if (!ob || fwrite(k.code, 1, k.code_size, ob) != k.code_size || fclose(ob)) { perror(argv[3]); return 2; }
    FILE *oh = fopen(argv[4], "w");
    if (!oh) { perror(argv[4]); return 2; }
    const char *P = argv[5];
    fprintf(oh, "/* %s: kernel %s built by tools/osc_gpu/osc_gpu_build (omega src/omega_osc_gpu.c, L386). Generated: do not edit.\n", P, k.name);
    fprintf(oh, " * IR (accepted by checker2) sha256 %s\n * code sha256 %s */\n", irh, codeh);
    fprintf(oh, "#define %s_THREADS %uu\n#define %s_GPR %uu\n#define %s_SHARED_BYTES %uu\n#define %s_ARG_WORDS %uu\n", P, k.threads, P, k.gpr_count, P,
            k.shared_bytes, P, k.arg_words);
    for (unsigned i = 0; i < k.nparams; i++) fprintf(oh, "#define %s_P%u_OFF %uu /* bytes from 0x380 */\n", P, i, k.params[i].off);
    fprintf(oh, "#define %s_STATUS_OFF %uu\n#define %s_BLOCKS_OFF %uu\n#define %s_BATCHES_OFF %uu\n", P, k.status_off, P, k.blocks_off, P, k.batches_off);
    fprintf(oh, "#define %s_STATUS_WORDS %uu\n#define %s_READS_BLOCK %d\n#define %s_READS_BATCH %d\n", P, OSC_GPU_STATUS_WORDS, P, k.reads_block, P, k.reads_batch);
    fprintf(oh, "#define %s_IR_SHA256 \"%s\"\n#define %s_CODE_SHA256 \"%s\"\n", P, irh, P, codeh);
    fprintf(oh, "#define %s_CODE_SIZE %zuu\nstatic const unsigned char %s_code[%zu] __attribute__((aligned(16))) = {", P, k.code_size, P, k.code_size);
    for (size_t i = 0; i < k.code_size; i++) fprintf(oh, "%s0x%02x,", i % 16 ? " " : "\n    ", k.code[i]);
    fprintf(oh, "\n};\n");
    if (fclose(oh)) { perror(argv[4]); return 2; }
    printf("%s: %zu bytes, %u instructions, %u registers, ir sha256 %s, code sha256 %s\n", k.name, k.code_size, k.insn_count, k.gpr_count, irh, codeh);
    osc_gpu_free(&k);
    return 0;
}
