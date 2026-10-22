/* test_syscall.c -- OSC-4 slice D: the gated syscall/addr words.
 * The gate refuses by default; with it open, the interpreter and the native
 * SVC path must agree, and the bytes must really land in the file. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include "osc_alloc.h"
#include "osc_diag.h"
#include "osc_ir.h"
#include "osc_front.h"
#include "osc_interp.h"
#include "osc_rt.h"
#include "osc_cg.h"
#include "osc_native.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* openat(AT_FDCWD, path, O_WRONLY|O_CREAT|O_TRUNC, 0600); write(fd, path, len);
 * close(fd); return bytes written. AT_FDCWD is -100. */
static const char *PROG =
    "fn emit(path: bytes) -> u64 {\n"
    "    let fd: u64 = syscall(56, 18446744073709551516, addr(path), 577, 384);\n"
    "    if fd >= 9223372036854775808 { return 18446744073709551615; }\n"
    "    let n: u64 = syscall(64, fd, addr(path), path.len);\n"
    "    let c: u64 = syscall(57, fd);\n"
    "    if c != 0 { return 18446744073709551615; }\n"
    "    return n;\n"
    "}\n";

static const char *PATH = "/tmp/osc_syscall_probe.txt";

int main(void)
{
    /* 1. gate: refused while closed */
    osc_parse_allow_syscall(0);
    static OscUnit u0;
    static OscDiag d0;
    memset(&u0, 0, sizeof u0);
    memset(&d0, 0, sizeof d0);
    int rc0 = osc_compile(PROG, strlen(PROG), &u0, &d0, NULL);
    CHECK(rc0 == -1, "syscall must be refused with the gate closed (rc=%d)", rc0);

    /* 2. open the gate and compile */
    osc_parse_allow_syscall(1);
    static OscUnit u;
    static OscDiag d;
    memset(&u, 0, sizeof u);
    memset(&d, 0, sizeof d);
    int rc = osc_compile(PROG, strlen(PROG), &u, &d, NULL);
    CHECK(rc == 0, "compile with the gate open (rc=%d, diag: %s | %s)", rc, d.object, d.message);
    if (rc != 0) { printf("test_syscall.c: %d failures\n", fails); return 1; }
    int fi = 0;
    CHECK(u.funcs[fi].nparams == 2, "entry takes a bytes (2 registers), got %u", u.funcs[fi].nparams);

    unlink(PATH);
    /* the buffer doubles as the bytes written: openat reads the NUL, write copies len bytes */
    char pbuf[64];
    snprintf(pbuf, sizeof pbuf, "%s", PATH);
    const size_t plen = strlen(PATH);
    uint64_t args[2] = { (uint64_t)(uintptr_t)pbuf, (uint64_t)plen };
    CHECK(osc_ir_slice_args_ok(&u.funcs[fi], args, 2) == 0, "slice args ok");

    OscRt *ri = malloc(sizeof *ri);
    CHECK(ri != NULL, "rt");
    osc_rt_init(ri);
    uint64_t vr = 12345;
    int ti = osc_interp_run(&u, fi, args, 2, ri, &vr);

    /* 3. native run */
    uint64_t nr = 12345;
    int tn = -1;
    OscCode c;
    memset(&c, 0, sizeof c);
    char err[160] = "";
    int cg = osc_cg_compile(&u, &c, err, sizeof err);
    CHECK(cg == 0, "cg compile: %s", err);
    if (cg == 0) {
        OscNative nm;
        if (osc_native_map(&nm, c.code, c.len) == 0) {
            OscRt *rn = malloc(sizeof *rn);
            osc_rt_init(rn);
            tn = osc_rt_call_native(rn, osc_native_at(&nm, c.entry[fi]), args, 2, &nr);
            osc_rt_reset(rn);
            osc_native_unmap(&nm);
        } else {
            CHECK(0, "native map");
        }
        osc_cg_free(&c);
    }

    CHECK(ti == 0, "interp run rc=%d", ti);
    CHECK(vr == (uint64_t)plen, "interp wrote %llu, want %zu", (unsigned long long)vr, plen);
    CHECK(tn == 0, "native run rc=%d", tn);
    CHECK(nr == vr, "native %llu != interp %llu", (unsigned long long)nr, (unsigned long long)vr);

    /* 4. the bytes really landed */
    FILE *f = fopen(PATH, "rb");
    CHECK(f != NULL, "probe file exists");
    if (f) {
        char got[64] = {0};
        size_t n = fread(got, 1, sizeof got - 1, f);
        fclose(f);
        CHECK(n == plen, "file bytes %zu want %zu", n, plen);
        CHECK(memcmp(got, pbuf, plen) == 0, "file content matches");
    }
    unlink(PATH);
    osc_rt_reset(ri);

    printf("test_syscall.c: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
