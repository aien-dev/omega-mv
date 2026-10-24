/* SPDX-License-Identifier: AGPL-3.0-only */
/* Test-only issuer/verifier for the cross-runtime envelope's C2 receipt.
 *
 * It does not change the receipt format or the trust policy: it fills the
 * existing 416-byte C2Receipt with the envelope's bound values and signs with
 * the public TEST key through c2_receipt.c. The signature attests provenance
 * only; it is not a correctness claim.
 *
 *   receipt_tool issue  ENVELOPE_ID CUBIN_SHA INPUT_SHA ARTIFACT_ID OUT
 *   receipt_tool verify RECEIPT ENVELOPE_ID CUBIN_SHA INPUT_SHA ARTIFACT_ID [EPOCH_FLOOR]
 *
 * Exit: 0 ok, 2 usage/hex, 3 refused (named c2r reason printed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c2_receipt.h"
#include "sha256.h"

static int hex2(const char *s, uint8_t out[32]) {
    if (!s || strlen(s) != 64) return -1;
    for (int i = 0; i < 32; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}

static void sha_text(const char *s, uint8_t out[32]) {
    sha256_hash((const uint8_t *)s, strlen(s), out);
}

static int read_all(const char *p, uint8_t *buf, size_t cap, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, cap, f);
    int ok = fclose(f) == 0;
    *n = got;
    return ok ? 0 : -1;
}

static void fill(C2Receipt *r, const uint8_t eid[32], const uint8_t cub[32],
                 const uint8_t in[32], const uint8_t art[32]) {
    memset(r, 0, sizeof *r);
    r->version = C2R_FORMAT_VERSION;
    r->signer_class = C2R_CLASS_TEST;
    r->alg = C2R_ALG_ED25519;
    r->evidence_class = C2R_EV_IR_CHECKED;
    {
        uint8_t x[32];
        sha_text("omega-cross-runtime-envelope/1", x);
        sha256_ctx c;
        sha256_init(&c);
        sha256_update(&c, x, 32);
        sha256_update(&c, eid, 32);
        sha256_final(&c, r->requirements);
    }
    memcpy(r->program, eid, 32);
    memcpy(r->executable, cub, 32);
    {
        uint8_t ids[1][32];
        memcpy(ids[0], art, 32);
        c2r_deps_digest((const uint8_t (*)[32])ids, 1, r->dependencies);
    }
    sha_text("omega-envelope-checker-v1", r->checker);
    sha_text("cudapp-artifact-export/1", r->producer_checker);
    c2r_target_id("gpu-sm121-max-interop", r->target);
    /* No firmware image was read: zero means "none", not a hash of a label. */
    memset(r->firmware, 0, sizeof r->firmware);
    memcpy(r->input, in, 32);
    r->policy_epoch = 1;
    r->expiry_epoch = 1000000;
}


int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (strcmp(argv[1], "issue") == 0) {
        uint8_t eid[32], cub[32], in[32], art[32], seed[32], out[C2R_SIZE];
        if (argc != 7) return 2;
        if (hex2(argv[2], eid) || hex2(argv[3], cub) || hex2(argv[4], in) || hex2(argv[5], art)) return 2;
        C2Receipt r;
        fill(&r, eid, cub, in, art);
        c2r_test_seed(seed);
        if (c2r_sign_test(&r, seed, out) != 0) return 3;
        FILE *f = fopen(argv[6], "wb");
        if (!f) return 3;
        int ok = fwrite(out, 1, C2R_SIZE, f) == C2R_SIZE && fclose(f) == 0;
        if (!ok) return 3;
        printf("OK receipt_id=");
        uint8_t rid[32];
        c2r_id(out, rid);
        for (int i = 0; i < 32; i++) printf("%02x", rid[i]);
        printf("\n");
        return 0;
    }
    if (strcmp(argv[1], "verify") == 0) {
        uint8_t eid[32], cub[32], in[32], art[32], buf[C2R_SIZE];
        size_t n = 0;
        if (argc < 7) return 2;
        if (hex2(argv[3], eid) || hex2(argv[4], cub) || hex2(argv[5], in) || hex2(argv[6], art)) return 2;
        if (read_all(argv[2], buf, sizeof buf, &n) != 0 || n != C2R_SIZE) return 2;
        C2ReceiptPolicy pol;
        memset(&pol, 0, sizeof pol);
        uint8_t checker[32];
        sha_text("omega-envelope-checker-v1", checker);
        const uint8_t (*anchors)[32] = &c2r_test_pk;
        const uint8_t (*checkers)[32] = &checker;
        pol.test_anchors = anchors;
        pol.n_test = 1;
        pol.checkers = checkers;
        pol.n_checkers = 1;
        pol.current_epoch = 10;
        pol.epoch_floor = argc > 7 ? (uint64_t)strtoull(argv[7], NULL, 10) : 1;
        C2ReceiptExpect ex;
        memset(&ex, 0, sizeof ex);
        uint8_t target[32], deps[32];
        c2r_target_id("gpu-sm121-max-interop", target);
        {
            uint8_t ids[1][32];
            memcpy(ids[0], art, 32);
            c2r_deps_digest((const uint8_t (*)[32])ids, 1, deps);
        }
        ex.target = target;
        ex.program = eid;
        ex.executable = cub;
        ex.dependencies = deps;
        ex.producer_checker = NULL;
        C2Receipt out;
        int rc = c2r_verify(buf, n, &pol, &ex, &out);
        if (rc == C2R_OK && memcmp(out.input, in, 32) != 0) rc = C2R_E_DEPS_MISMATCH;
        if (rc != C2R_OK) {
            printf("REFUSED %d %s\n", rc, c2r_reason(rc));
            return 3;
        }
        printf("OK receipt_id=");
        uint8_t rid[32];
        c2r_id(buf, rid);
        for (int i = 0; i < 32; i++) printf("%02x", rid[i]);
        printf("\n");
        return 0;
    }
    return 2;
}
