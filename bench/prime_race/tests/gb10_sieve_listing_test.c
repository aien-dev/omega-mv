/* SPDX-License-Identifier: MIT */
/* Exercise the actual listing check without the full sieve sweep or a GPU. */
#define main gb10_sieve_full_suite_main
#include "gb10_sieve_host_test.c"
#undef main
#include <sys/stat.h>

int main(void) {
    char target[] = "/tmp/l478-sieve-target-XXXXXX";
    int fd = mkstemp(target);
    if (fd < 0) return 2;
    static const char sentinel[] = "preserve this file";
    if (write(fd, sentinel, sizeof sentinel) != sizeof sentinel) { close(fd); unlink(target); return 2; }
    close(fd);
    char predictable[96];
    snprintf(predictable, sizeof predictable, "/tmp/pr_gb10_sieve_%d.sass", (int)getpid());
    if (symlink(target, predictable) != 0) { unlink(target); return 2; }
    BlackwellIRProgram *p = calloc(1, sizeof *p);
    OmegaBlackwellKernel k = {0};
    int rc = p ? omega_gpu_elementwise_codegen_ir(OMEGA_GPU_EW_PRIME_SIEVE, 0, p, &k) : -1;
    CHECK(rc == OMEGA_GPU_EW_OK, "real sieve kernel builds");
    if (rc == OMEGA_GPU_EW_OK) sm121dis_check(&k, p, MUST_GLOBAL, "private listing regression");
    char bytes[sizeof sentinel + 1] = {0};
    FILE *f = fopen(target, "rb");
    size_t n = f ? fread(bytes, 1, sizeof bytes, f) : 0;
    if (f) fclose(f);
    CHECK(n == sizeof sentinel && memcmp(bytes, sentinel, sizeof sentinel) == 0, "predictable listing symlink target preserved");
    struct stat st;
    CHECK(lstat(predictable, &st) == 0 && S_ISLNK(st.st_mode), "predictable listing path untouched");
    unlink(predictable); unlink(target); free(k.code); free(p);
    printf("listing security: checks=%d failed=%d\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
