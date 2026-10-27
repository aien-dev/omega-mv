/* SPDX-License-Identifier: Apache-2.0
 * OM-2 qualification, lane s5s7 (#748, ledger L836): ABI v2 ORACLE driver for contract section 6
 * (spec/omegese-om2.md lines 180-210) and errata E1/E3. Outside the delivered closure.
 *
 * Written from the contract only, against the API it describes (ASSUMPTIONS, README A10-A13):
 *   uint32_t omega2_<export>(uint64_t *result, ...)      v1 shape (omega-bridge docs/NATIVE-ABI.md)
 *   unsized view/edit -> (const void *ptr, uint64_t len) / (void *ptr, uint64_t len), len in elements
 *   layout, out       -> one pointer, no count (contract 156-157, erratum E1)
 *   const char *omega2_ident_<unit>(void)                 NUL-terminated text holding the source
 *                                                          sha256, the omc pin and "OM-2" (E1)
 * Units: sec/s5s7/abi/abiv.om (unit abiv), abiw.om (unit abiw), abi1.om (OM-1, v1 omega_madd1).
 * Expected values are computed here in C from the unit's stated semantics, never from Omega.
 * Usage: driver SHA_ABIV SHA_ABIW OMC_PIN   (hex digests computed by /usr/bin/sha256sum)
 * Output: one PASS/FAIL line per case id (ids match candidate.cases abi-* rows); exit 0 iff no FAIL. */
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

uint32_t omega2_vsum(uint64_t *result, const void *v, uint64_t v_len);
uint32_t omega2_bcnt(uint64_t *result, const void *b, uint64_t b_len);
uint32_t omega2_vcnt(uint64_t *result, const void *v, uint64_t v_len);
uint32_t omega2_ecpy(uint64_t *result, void *d, uint64_t d_len, const void *s, uint64_t s_len);
uint32_t omega2_rev(uint64_t *result, const void *s, uint64_t s_len, void *r);
uint32_t omega2_revtrap(uint64_t *result, const void *s, uint64_t s_len, void *r);
uint32_t omega2_lay(uint64_t *result, void *p, uint64_t v);
uint32_t omega2_spin(uint64_t *result, uint64_t n);
uint32_t omega2_madd2(uint64_t *result, uint64_t a, uint64_t b);
uint32_t omega_madd1(uint64_t *result, uint64_t a, uint64_t b);
const char *omega2_ident_abiv(void);
const char *omega2_ident_abiw(void);

#define SENT UINT64_C(0x5555555555555555)
static int fails;
static void check(const char *id, int ok, const char *what)
{
    printf("%s %s %s\n", ok ? "PASS" : "FAIL", id, what);
    if (!ok)
        fails++;
}

static volatile uint32_t inner = 99;
static void on_alarm(int sig)
{
    uint64_t r = SENT;
    uint8_t b[2] = {0, 0};
    (void)sig;
    inner = omega2_bcnt(&r, b, 2);
}

static int ident_ok(const char *s, const char *src, const char *pin)
{
    return s && strstr(s, src) && strstr(s, pin) && strstr(s, "OM-2") && !strstr(s, "OM-1");
}

int main(int argc, char **argv)
{
    static uint64_t w[8] __attribute__((aligned(16)));
    static uint8_t buf[64] __attribute__((aligned(16)));
    uint64_t r;
    uint32_t st;
    int i;

    if (argc != 4) {
        fprintf(stderr, "usage: driver SHA_ABIV SHA_ABIW OMC_PIN\n");
        return 90;
    }
    /* success baselines */
    w[0] = 1; w[1] = 2; w[2] = 3;
    r = SENT; st = omega2_vsum(&r, w, 3);
    check("abi-vsum-ok", st == 0 && r == 6, "vsum {1,2,3} = 6");
    r = SENT; st = omega2_bcnt(&r, buf, 64);
    check("abi-bcnt-ok", st == 0 && r == 64, "bcnt len 64");
    /* status 32 refusals (contract 187-193) */
    r = SENT; st = omega2_bcnt(&r, NULL, 1);
    check("abi-null-len1", st == 32 && r == SENT, "len 1, ptr NULL -> 32, result untouched");
    r = SENT; st = omega2_vcnt(&r, (const uint8_t *)w + 1, 1);
    check("abi-misaligned", st == 32 && r == SENT, "ulong view at w+1 byte -> 32 (mutant: 0, result 1)");
    r = SENT; st = omega2_vcnt(&r, w, UINT64_C(1) << 61);
    check("abi-size-overflow", st == 32 && r == SENT, "ulong len 2^61: len*8 overflows -> 32 (mutant: 0, result 2^61)");
    r = SENT; st = omega2_bcnt(&r, buf, UINT64_MAX);
    check("abi-golden-wrap", st == 32 && r == SENT, "uchar len 2^64-1 at buf: ptr+len wraps -> 32 (#748 golden extent)");
    r = SENT; st = omega2_bcnt(&r, (const void *)(uintptr_t)(UINTPTR_MAX - 7), 16);
    check("abi-wrap-high", st == 32 && r == SENT, "ptr 2^64-8, len 16 wraps -> 32");
    /* empty views (contract 193-194) */
    r = SENT; st = omega2_bcnt(&r, NULL, 0);
    check("abi-null-len0", st == 0 && r == 0, "len 0, ptr NULL -> empty view, status 0");
    r = SENT; st = omega2_vsum(&r, NULL, 0);
    check("abi-null-len0-ulong", st == 0 && r == 0, "ulong len 0, NULL -> sum 0");
    r = SENT; st = omega2_bcnt(&r, (const void *)(uintptr_t)1, 0);
    check("abi-bogus-len0", st == 0 && r == 0, "uchar len 0, ptr 1 -> empty view");
    /* edit overlap (contract 192-193) */
    for (i = 0; i < 16; i++)
        buf[i] = (uint8_t)(i + 1);
    r = SENT; st = omega2_ecpy(&r, buf, 8, buf + 4, 8);
    check("abi-edit-overlap", st == 32 && r == SENT && buf[0] == 1 && buf[7] == 8,
          "edit [0,8) overlaps view [4,12) -> 32, nothing written");
    r = SENT; st = omega2_ecpy(&r, buf, 4, buf + 4, 4);
    check("abi-edit-adjacent", st == 0 && r == 4 && buf[0] == 5 && buf[3] == 8,
          "edit [0,4) and view [4,8) are disjoint (half-open) -> copy");
    r = SENT; st = omega2_ecpy(&r, buf + 2, 0, buf, 8);
    check("abi-edit-empty", st == 0 && r == 0, "empty edit inside a view overlaps nothing");
    r = SENT; st = omega2_ecpy((uint64_t *)(void *)(buf + 8), buf, 16, buf + 32, 4);
    check("abi-edit-result", st == 32, "result buffer inside the edit range -> 32");
    /* out: memmove semantics, untouched on trap (contract 196-198) */
    buf[0] = 1; buf[1] = 2; buf[2] = 3; buf[3] = 4;
    r = SENT; st = omega2_rev(&r, buf, 4, buf);
    check("abi-out-overlap", st == 0 && buf[0] == 4 && buf[1] == 3 && buf[2] == 2 && buf[3] == 1,
          "out overlapping its input: computed from the original input (4,3,2,1; direct writes give 4,3,3,4)");
    memset(buf + 16, 0xAA, 4);
    buf[0] = 1; buf[1] = 2; buf[2] = 3; buf[3] = 4;
    r = SENT; st = omega2_revtrap(&r, buf, 4, buf + 16);
    check("abi-out-trap", st == 9 && r == SENT && buf[16] == 0xAA && buf[19] == 0xAA,
          "trap 9 after writing out: status 9, out and result untouched");
    memset(buf + 16, 0xAA, 4);
    r = SENT; st = omega2_rev(&r, buf, 3, buf + 16);
    check("abi-out-bounds", st == 3 && buf[16] == 0xAA, "len 3 view read at index 3 -> trap 3, out untouched");
    /* layout parameter: one pointer, fixed SIZE 16 (contract 156-158) */
    memset(buf, 0, 16);
    r = SENT; st = omega2_lay(&r, buf, UINT64_C(0x0102030405060708));
    {
        static const uint8_t want[16] = {8, 7, 6, 5, 4, 3, 2, 1, 1, 2, 3, 4, 5, 6, 7, 8};
        check("abi-layout", st == 0 && memcmp(buf, want, 16) == 0, "lo little-endian at 0, hi big-endian at 8");
    }
    r = SENT; st = omega2_lay(&r, NULL, 1);
    check("abi-layout-null", st == 32, "layout pointer NULL -> 32 (v1 null-pointer rule)");
    r = SENT; st = omega2_madd2(NULL, 1, 2);
    check("abi-null-result", st == 32, "result pointer NULL -> 32 (v1 rule kept)");
    /* status 33 reentry (contract 195) */
    {
        struct sigaction sa;
        struct itimerval it;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_alarm;
        sigaction(SIGALRM, &sa, NULL);
        memset(&it, 0, sizeof it);
        it.it_value.tv_usec = 20000;
        setitimer(ITIMER_REAL, &it, NULL);
        r = SENT; st = omega2_spin(&r, UINT64_C(4000000000));
        check("abi-reentry", st == 0 && inner == 33, "bcnt from SIGALRM during spin -> 33; spin still 0");
    }
    /* two units in one binary, interleaved (contract 207-210); OM-1 unit stays v1 (E1) */
    {
        uint64_t a = SENT, b = SENT, c = SENT, a1;
        uint32_t s1 = omega2_madd2(&a, 5, 7), s2, s3, s4;
        a1 = a;
        s2 = omega2_bcnt(&b, buf, 5);
        s3 = omega_madd1(&c, 5, 7);
        s4 = omega2_madd2(&a, 6, 1);
        check("abi-two-units", s1 == 0 && s2 == 0 && s3 == 0 && s4 == 0 && a1 == 17 && a == 13 && b == 5 && c == 22,
              "abiw, abiv and the OM-1 unit interleaved: 5*2+7=17 then 6*2+1=13, len 5, 5*3+7=22");
    }
    /* identities (erratum E1) */
    check("abi-ident-abiv", ident_ok(omega2_ident_abiv(), argv[1], argv[3]), "abiv: source sha256, omc pin, OM-2");
    check("abi-ident-abiw", ident_ok(omega2_ident_abiw(), argv[2], argv[3]), "abiw: source sha256, omc pin, OM-2");
    check("abi-ident-distinct", strcmp(omega2_ident_abiv(), omega2_ident_abiw()) != 0, "per-unit identities differ");
    printf("%s ABI v2 driver: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
