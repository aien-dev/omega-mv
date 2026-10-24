/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Drake Stapleton (AIEN / aien-dev)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * rx_experience_test.c -- omega #699 verified experience records.
 *
 * Real World, J-Space, AEGIS verifier, commit binder, Cortex journal and
 * AIENOS authority; nothing is mocked. Checks:
 *   1. walk1d: the O(1) noise word equals the sequential PD0 stream; refusals.
 *   2. accept chain: an action becomes a CX_K_EXPERIENCE linked to its World
 *      commit, AEGIS evidence and promotion; the prediction is an unqualified
 *      hypothesis claim.
 *   3. generated-but-unobserved records are refused as observations (no
 *      receipt, hypothesis/evidence as receipt, forged and replayed commit
 *      records, altered next state, duplicate, stale, refused action);
 *      nothing is written on refusal.
 *   4. consent: revoked experiences (single and owner-wide) leave the dataset
 *      view, a non-owner cannot revoke, nothing is deleted, the view is
 *      rebuilt identically after restart, tampering is excluded.
 *   5. determinism: same seed -> byte-identical experience payloads in fresh
 *      environments and after reset; different seeds -> different trajectories.
 */
#include "physics0/pd0_rng.h"
#include "runtime/rx_cortex_record.h"
#include "runtime/rx_experience.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_checks, g_fail;

#define CHECK(cond, ...) do {                                            \
        g_checks++;                                                      \
        if (!(cond)) {                                                   \
            g_fail++;                                                    \
            fprintf(stderr, "  FAIL %s:%d ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                                \
            fputc('\n', stderr);                                         \
        }                                                                \
    } while (0)

static const RxExpFamily *F = &rx_exp_walk1d;
static AienosCapAdmin *g_admin;
static AienosCapView *g_view;
static AienMachineId g_self;
static char g_base[64];
static RxExpEnv g_e, g_e2;          /* large: static */

enum { OWNER_A = 0xA11CE, OWNER_B = 0xB0B, PREDICTOR = 0x7ED };

static int rm_one(const char *p, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb; (void)flag; (void)ftw;
    return remove(p);
}
static void rmtree(const char *p) { nftw(p, rm_one, 16, FTW_DEPTH | FTW_PHYS); }

static void dir_for(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", g_base, name);
    rmtree(out);
}

static int64_t field(uint64_t s, uint32_t i) {
    int64_t f[RX_EXP_MAX_FIELDS];
    F->unpack(s, f);
    return f[i];
}

/* Closed-loop policy: thrust toward the target, brake near it. */
static uint64_t policy(uint64_t s) {
    int64_t x = field(s, RX_EXP_WALK_X), v = field(s, RX_EXP_WALK_V), t = field(s, RX_EXP_WALK_TARGET);
    int64_t want = (t - x) / 4 - v;
    if (want > 2) want = 2;
    if (want < -2) want = -2;
    return RX_EXP_WALK_THRUST(want);
}

static RxExpDraft draft(uint64_t owner, uint64_t consent, uint64_t split, uint64_t hyp,
                        const RxExpObservation *o) {
    RxExpDraft d;
    memset(&d, 0, sizeof d);
    d.prior = o->prior;
    d.action = o->action;
    d.hypothesis = hyp;
    d.obs = *o;
    d.owner = owner;
    d.consent = consent;
    d.license = 1;
    d.privacy = RX_EXP_PRIV_SYNTHETIC;
    d.split = split;
    return d;
}

/* One full step: hypothesis (from `pred`), act, record. Returns the experience id. */
static uint64_t step_record(RxExpEnv *e, uint64_t action, uint64_t pred, uint64_t owner,
                            uint64_t consent, uint64_t split) {
    uint64_t hyp = 0, id = 0;
    RxExpObservation o;
    if (rx_exp_hypothesis(e, PREDICTOR, action, pred, &hyp) != RX_EXP_OK) return 0;
    if (rx_exp_act(e, action, &o) != RX_EXP_OK) return 0;
    RxExpDraft d = draft(owner, consent, split, hyp, &o);
    int rc = rx_exp_record(e, &d, &id);
    return rc == RX_EXP_OK ? id : 0;
}

/* A model that knows the physics but not the noise: predicts noise 0. */
static uint64_t noise_blind_prediction(uint64_t prior, uint64_t action) {
    uint64_t next = 0, cost = 0;
    if (F->step(F->encode(prior, action), &next, &cost) != 0) return 0;
    int64_t noise = field(next, RX_EXP_WALK_NOISE) - 2;
    int64_t v = field(prior, RX_EXP_WALK_V) + (int64_t)action - 3;
    if (v > 8) v = 8;
    if (v < -8) v = -8;
    if (noise == 0) return next;
    /* rebuild with noise 0: v, x and the noise code change */
    int64_t x = field(prior, RX_EXP_WALK_X) + v;
    uint64_t m = next;
    m &= ~(((1ull << 12) - 1) << 34) & ~(((1ull << 5) - 1) << 46) & ~(3ull << 60);
    m |= ((uint64_t)x & 0xfff) << 34 | ((uint64_t)v & 0x1f) << 46 | 2ull << 60;
    return m;
}

static int open_env(RxExpEnv *e, const char *dir, uint64_t session) {
    return rx_exp_open(e, F, dir, &g_self, session, g_admin, g_view);
}

/* ---- 1 ---------------------------------------------------------------------- */
static void t_family(void) {
    printf("[*] walk1d: O(1) noise word = sequential PD0 stream; refusals\n");
    uint64_t key = rx_exp_walk_key(0x5eed), next = 0, cost = 0;
    CHECK(key != 0 && key < (1u << 24), "key in range");
    uint64_t s;
    CHECK(F->step(F->reset_input(0x5eed), &s, &cost) == 0, "reset");
    pd0_rng ref;
    pd0_stream(&ref, key, "omega.exp.walk1d.noise");
    int same = 1, count[3] = { 0, 0, 0 };
    for (uint32_t k = 0; k < 600; k++) {
        int64_t want = (int64_t)(pd0_next(&ref) % 3) - 1;
        if (F->step(F->encode(s, RX_EXP_WALK_THRUST(0)), &next, &cost) != 0) { same = 0; break; }
        int64_t got = field(next, RX_EXP_WALK_NOISE) - 2;
        same &= got == want && field(next, RX_EXP_WALK_K) == (int64_t)k + 1 && cost == 1;
        count[got + 1]++;
        s = next;
    }
    CHECK(same, "noise of step k is word k of the sequential stream");
    printf("    noise counts over 600 steps: -1:%d 0:%d +1:%d\n", count[0], count[1], count[2]);
    CHECK(count[0] && count[1] && count[2], "all three noise values occur");
    CHECK(F->step(F->encode(s, 0), &next, &cost) != 0, "action code 0 refused");
    CHECK(F->step(F->encode(s, 6), &next, &cost) != 0, "action code 6 refused");
    CHECK(F->step(F->encode(s, 99), &next, &cost) != 0, "action out of range refused");
    CHECK(F->step(F->encode(0, RX_EXP_WALK_THRUST(1)), &next, &cost) != 0, "no episode: refused");
    CHECK(F->step(F->reset_input(1) | (1ull << 30), &next, &cost) != 0, "reset with state bits refused");
    CHECK(F->contract(F->encode(s, 4), next ^ 1) == 0, "contract refuses a wrong next state");
}

/* ---- 2 + 3 -------------------------------------------------------------------- */
static void t_chain_and_unobserved(void) {
    printf("[*] accept chain -> experience; unobserved / forged / stale refused\n");
    char d[200];
    dir_for(d, sizeof d, "chain");
    RxExpEnv *e = &g_e;
    CHECK(open_env(e, d, 1) == RX_EXP_OK, "open");
    CHECK(rx_exp_reset(e, 0xC0FFEE) == RX_EXP_OK, "reset through the accept chain");
    uint64_t s0 = rx_exp_state(e);
    CHECK(field(s0, RX_EXP_WALK_LIVE) == 1 && field(s0, RX_EXP_WALK_K) == 0, "episode live");

    /* happy path with a noise-blind model prediction */
    uint64_t a = policy(s0), pred = noise_blind_prediction(s0, a), hyp = 0, id = 0;
    CHECK(rx_exp_hypothesis(e, PREDICTOR, a, pred, &hyp) == RX_EXP_OK, "hypothesis");
    const CxObject *ho = cx_get(&e->c.cx, hyp);
    CHECK(ho && ho->cls == CX_CLAIM && ho->kind == CX_K_CANDIDATE && ho->tag == RX_EXP_TAG_HYPOTHESIS,
          "a prediction is a claim, never an observation");
    RxExpObservation o;
    CHECK(rx_exp_act(e, a, &o) == RX_EXP_OK, "act");
    CHECK(o.prior == s0 && o.next == rx_exp_state(e) && o.commit && o.evidence && o.promotion,
          "observation names the World state and its receipts");

    /* 3a. the model's generated next state offered with no receipt */
    uint64_t n_before = e->c.cx.n;
    RxExpObservation gen = { s0, a, pred, 0, 0, 0 };
    RxExpDraft g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, hyp, &gen);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_UNOBSERVED, "generated, no receipt: refused");
    /* 3b. the hypothesis itself offered as the receipt */
    gen.commit = hyp; gen.evidence = hyp; gen.promotion = hyp;
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, hyp, &gen);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_UNOBSERVED, "hypothesis as receipt: refused");
    /* 3c. the verification evidence offered as the commit receipt */
    gen = o;
    gen.commit = o.evidence;
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, hyp, &gen);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_UNOBSERVED, "evidence as commit receipt: refused");
    /* 3d. a forged commit record carrying the model's state, appended through
     * the writer link (only the World recorder may speak for a crumb) */
    CxRecord cr;
    CHECK(cx_recall_id(&e->c.cx, o.commit, &cr) == CX_OK, "recall real commit");
    uint64_t fake[CX_WREC_WORDS];
    memcpy(fake, cr.payload, sizeof fake);
    CxHeader fh;
    memset(&fh, 0, sizeof fh);
    fh.cls = CX_EXECUTION;
    fh.kind = CX_K_EXEC_COMMIT;
    fh.subject = RX_EXP_SUBJECT;
    fh.protect = CX_PROT_COMMIT_RECEIPT;
    fh.tag = cr.hdr.tag;
    fh.t = rx_cortex_next_t_in(e->c.world, &e->c.cx);
    uint64_t replayed = 0, forged = 0;
    CHECK(rx_cortex_append_in(e->c.world, &e->c.cx, &fh, fake, CX_WREC_WORDS, &replayed) == RX_OK,
          "append replayed copy");
    fake[CX_WREC_FIELD0 + RXC_S_RESULT] = pred ^ (1ull << 34);
    fh.t = rx_cortex_next_t_in(e->c.world, &e->c.cx);
    CHECK(rx_cortex_append_in(e->c.world, &e->c.cx, &fh, fake, CX_WREC_WORDS, &forged) == RX_OK,
          "append fabricated commit");
    n_before = e->c.cx.n;
    gen = o;
    gen.commit = replayed;
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, hyp, &gen);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_FORGED, "replayed commit record: refused");
    gen.commit = forged;
    gen.next = pred ^ (1ull << 34);
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, hyp, &gen);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_FORGED, "fabricated commit record: refused");
    /* 3e. real receipts, next state replaced by a different (predicted) one */
    gen = o;
    gen.next = o.next ^ (1ull << 34);
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, hyp, &gen);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_MISMATCH, "altered next state: refused");
    /* hypothesis about another action */
    uint64_t other = 0;
    CHECK(rx_exp_hypothesis(e, PREDICTOR, a == 5 ? 4 : a + 1, pred, &other) == RX_EXP_OK, "other hyp");
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, other, &o);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_HYPOTHESIS, "hypothesis for another action: refused");
    g = draft(0, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, hyp, &o);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_CONSENT, "no owner: refused");
    n_before = e->c.cx.n;
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, 7, hyp, &o);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_CONSENT && e->c.cx.n == n_before,
          "bad split: refused, nothing written");

    /* the real observation is accepted */
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, hyp, &o);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_OK, "real observation recorded");
    RxExpRecord r;
    CHECK(rx_exp_decode(&e->c.cx, id, &r) == RX_EXP_OK, "decode");
    const CxObject *xo = cx_get(&e->c.cx, id);
    CHECK(xo && xo->cls == CX_EVIDENCE && xo->kind == CX_K_EXPERIENCE &&
          (xo->protect & CX_PROT_VERIFY_EVIDENCE), "experience is protected verification evidence");
    CHECK(r.w[RX_EXP_W_VERIFY] == RX_EXP_V_ALL && r.commit == o.commit && r.evidence == o.evidence &&
          r.promotion == o.promotion && r.hypothesis == hyp, "receipts linked");
    CHECK(r.w[RX_EXP_W_PRIOR] == s0 && r.w[RX_EXP_W_ACTION] == a && r.w[RX_EXP_W_NEXT] == o.next,
          "state / action / next state");
    CHECK(r.w[RX_EXP_W_PRED_STATUS] == RX_EXP_PRED_UNQUALIFIED && r.w[RX_EXP_W_PREDICTED] == pred &&
          r.w[RX_EXP_W_PREDICTOR] == PREDICTOR, "prediction kept as an unqualified annotation");
    int noisy = field(o.next, RX_EXP_WALK_NOISE) != 2;
    CHECK((r.w[RX_EXP_W_ERR_FIELDS] != 0) == (pred != o.next) && (pred != o.next) == noisy,
          "prediction error counted (noise-blind model wrong iff noise != 0)");
    CHECK(r.w[RX_EXP_W_COST] == (uint64_t)llabs((int64_t)a - 3) + 1 &&
          r.w[RX_EXP_W_COST_UNIT] == RX_EXP_COST_DECLARED_UNITS, "declared cost");
    CHECK(r.w[RX_EXP_W_OWNER] == OWNER_A && r.w[RX_EXP_W_CONSENT] == RX_EXP_CONSENT_ALL &&
          r.w[RX_EXP_W_APPLY_OP] == F->op && r.w[RX_EXP_W_FAMILY] == F->family, "owner, consent, applicability");
    uint64_t anc[16];
    uint32_t na = cx_provenance(&e->c.cx, id, anc, 16);
    int has_goal = 0;
    for (uint32_t i = 0; i < na; i++) {
        const CxObject *ao = cx_get(&e->c.cx, anc[i]);
        has_goal |= ao && ao->kind == CX_K_WORK_ACCEPTED;
    }
    CHECK(has_goal, "provenance reaches the accepted goal");
    /* 3f. the same receipts again */
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_DUPLICATE, "duplicate: refused");

    /* 3g. stale: act twice, record the first */
    RxExpObservation o1, o2;
    uint64_t s1 = rx_exp_state(e);
    CHECK(rx_exp_act(e, policy(s1), &o1) == RX_EXP_OK, "act 1");
    CHECK(rx_exp_act(e, policy(rx_exp_state(e)), &o2) == RX_EXP_OK, "act 2");
    CHECK(e->dropped == 1, "unrecorded transition counted as dropped (%llu)", (unsigned long long)e->dropped);
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, 0, &o1);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_ERR_ORDER, "stale observation: refused");
    g = draft(OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN, 0, &o2);
    CHECK(rx_exp_record(e, &g, &id) == RX_EXP_OK, "current observation recorded");

    /* 3h. the environment refuses an inadmissible action: no new state,
     * the refusal is in Cortex as AEGIS evidence with no winner */
    uint64_t before = rx_exp_state(e), ev_before = 0, ev_after = 0;
    for (uint64_t i = 1; i <= e->c.cx.n; i++) ev_before += cx_get(&e->c.cx, i)->kind == CX_K_EVIDENCE_REF;
    RxExpObservation bad;
    CHECK(rx_exp_act(e, 0, &bad) == RX_EXP_ERR_REFUSED, "inadmissible action refused");
    CHECK(rx_exp_state(e) == before, "World state unchanged");
    uint64_t last_ev = 0;
    for (uint64_t i = 1; i <= e->c.cx.n; i++)
        if (cx_get(&e->c.cx, i)->kind == CX_K_EVIDENCE_REF) { ev_after++; last_ev = i; }
    const uint64_t *lp = last_ev ? cx_payload(&e->c.cx, cx_get(&e->c.cx, last_ev)) : NULL;
    CHECK(ev_after == ev_before + 1 && lp && lp[RXC_EP_WINNER] == RXC_NONE,
          "refusal recorded as no-winner evidence");
    CHECK(cx_verify_chain(&e->c.cx) == CX_OK, "Cortex chain verifies");
    rx_exp_close(e);
}

/* ---- 4 ----------------------------------------------------------------------- */
static int same_view(const RxExpRecord *a, uint32_t na, const RxExpRecord *b, uint32_t nb) {
    if (na != nb) return 0;
    for (uint32_t i = 0; i < na; i++)
        if (a[i].id != b[i].id || memcmp(a[i].w, b[i].w, sizeof a[i].w)) return 0;
    return 1;
}

static void t_consent(void) {
    printf("[*] consent: revocation excludes, never deletes; view rebuilt after restart\n");
    char d[200];
    dir_for(d, sizeof d, "consent");
    RxExpEnv *e = &g_e;
    CHECK(open_env(e, d, 1) == RX_EXP_OK, "open");
    CHECK(rx_exp_reset(e, 0xBEEF) == RX_EXP_OK, "reset");
    uint64_t a[6], b[2];
    for (int i = 0; i < 6; i++) {
        uint64_t s = rx_exp_state(e), act = policy(s);
        a[i] = step_record(e, act, noise_blind_prediction(s, act), OWNER_A, RX_EXP_CONSENT_ALL,
                           i == 5 ? RX_EXP_SPLIT_HOLDOUT : RX_EXP_SPLIT_TRAIN);
        CHECK(a[i], "owner A experience %d", i);
    }
    for (int i = 0; i < 2; i++) {
        uint64_t s = rx_exp_state(e), act = policy(s);
        b[i] = step_record(e, act, 0, OWNER_B, i ? RX_EXP_CONSENT_RETRIEVE : RX_EXP_CONSENT_ALL,
                           RX_EXP_SPLIT_TRAIN);
        CHECK(b[i], "owner B experience %d", i);
    }
    static RxExpRecord v0[32], v1[32], v2[32];
    RxExpViewStats st;
    uint32_t n0 = rx_exp_dataset(&e->c.cx, F, RX_EXP_CONSENT_TRAIN, RX_EXP_SPLIT_TRAIN, v0, 32, &st);
    CHECK(n0 == 6 && st.seen == 8 && st.split == 1 && st.no_consent == 1 && st.invalid == 0,
          "train view: 5 A + 1 B (holdout and retrieve-only excluded) n=%u seen=%llu", n0,
          (unsigned long long)st.seen);
    for (uint32_t i = 1; i < n0; i++) CHECK(v0[i].t > v0[i - 1].t, "view ordered by time");

    uint64_t rid = 0, n_before = e->c.cx.n;
    CHECK(rx_exp_revoke(e, OWNER_B, a[1], RX_EXP_CONSENT_TRAIN, 1, &rid) == RX_EXP_ERR_OWNER &&
          e->c.cx.n == n_before, "non-owner cannot revoke; nothing written");
    CHECK(rx_exp_revoke(e, OWNER_A, a[1], RX_EXP_CONSENT_SHARE, 1, &rid) == RX_EXP_OK, "revoke share only");
    uint32_t n1 = rx_exp_dataset(&e->c.cx, F, RX_EXP_CONSENT_TRAIN, RX_EXP_SPLIT_TRAIN, v1, 32, &st);
    CHECK(n1 == 6 && st.revoked == 0, "share revocation leaves training view");
    CHECK(rx_exp_revoke(e, OWNER_A, a[1], RX_EXP_CONSENT_TRAIN, 2, &rid) == RX_EXP_OK, "revoke train");
    const CxObject *ro = cx_get(&e->c.cx, rid);
    CHECK(ro && ro->kind == CX_K_CONSENT_REVOKED && (ro->protect & CX_PROT_AUTHORITY) &&
          ro->links[0] == a[1], "revocation is a protected tombstone linking the experience");
    n1 = rx_exp_dataset(&e->c.cx, F, RX_EXP_CONSENT_TRAIN, RX_EXP_SPLIT_TRAIN, v1, 32, &st);
    int gone = 1;
    for (uint32_t i = 0; i < n1; i++) gone &= v1[i].id != a[1];
    CHECK(n1 == 5 && st.revoked == 1 && gone, "revoked experience excluded (n=%u)", n1);
    RxExpRecord kept;
    CHECK(rx_exp_decode(&e->c.cx, a[1], &kept) == RX_EXP_OK, "nothing deleted: record still verifies");
    uint32_t nr = rx_exp_dataset(&e->c.cx, F, RX_EXP_CONSENT_RETRIEVE, RX_EXP_SPLIT_TRAIN, v2, 32, &st);
    CHECK(nr == 7, "retrieval view keeps it (only train was withdrawn) n=%u", nr);

    /* owner-wide: every A experience so far; a later one is unaffected */
    CHECK(rx_exp_revoke(e, OWNER_A, 0, RX_EXP_CONSENT_TRAIN, 3, &rid) == RX_EXP_OK, "owner-wide revoke");
    uint64_t s = rx_exp_state(e), act = policy(s);
    uint64_t later = step_record(e, act, 0, OWNER_A, RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN);
    CHECK(later, "later experience");
    n1 = rx_exp_dataset(&e->c.cx, F, RX_EXP_CONSENT_TRAIN, RX_EXP_SPLIT_TRAIN, v1, 32, &st);
    /* revoked counts all six A experiences, the heldout one included (consent is checked before split) */
    CHECK(n1 == 2 && v1[0].id == b[0] && v1[1].id == later && st.revoked == 6,
          "owner-wide revocation: B's and the later A experience remain (n=%u revoked=%llu)", n1,
          (unsigned long long)st.revoked);
    CHECK(cx_verify_chain(&e->c.cx) == CX_OK, "chain");
    rx_exp_close(e);

    /* restart: the same ordered view from the journal alone */
    CHECK(open_env(e, d, 2) == RX_EXP_OK, "reopen");
    uint32_t n2 = rx_exp_dataset(&e->c.cx, F, RX_EXP_CONSENT_TRAIN, RX_EXP_SPLIT_TRAIN, v2, 32, &st);
    CHECK(same_view(v1, n1, v2, n2), "view identical after restart (%u vs %u)", n1, n2);
    uint32_t nh = rx_exp_dataset(&e->c.cx, F, RX_EXP_CONSENT_RETRIEVE, RX_EXP_SPLIT_HOLDOUT, v2, 32, &st);
    CHECK(nh == 1 && v2[0].id == a[5], "holdout view holds only the heldout experience");
    /* a tampered receipt takes its experience out of every view */
    cx_tamper(&e->c.cx, b[0], RX_EXP_W_NEXT, 0);
    n2 = rx_exp_dataset(&e->c.cx, F, RX_EXP_CONSENT_TRAIN, RX_EXP_SPLIT_TRAIN, v2, 32, &st);
    CHECK(n2 == 1 && v2[0].id == later && st.invalid == 1, "tampered experience excluded as invalid");
    rx_exp_close(e);
    rmtree(d);
}

/* ---- 5 ------------------------------------------------------------------------ */
#define DET_STEPS 24
typedef struct { uint64_t w[DET_STEPS][RX_EXP_W_WORDS]; uint64_t tr[DET_STEPS][3]; } Trace;

static int run_trace(RxExpEnv *e, uint64_t seed, Trace *t) {
    if (rx_exp_reset(e, seed) != RX_EXP_OK) return -1;
    for (int i = 0; i < DET_STEPS; i++) {
        uint64_t s = rx_exp_state(e), act = policy(s);
        uint64_t id = step_record(e, act, noise_blind_prediction(s, act), OWNER_A,
                                  RX_EXP_CONSENT_ALL, RX_EXP_SPLIT_TRAIN);
        RxExpRecord r;
        if (!id || rx_exp_decode(&e->c.cx, id, &r) != RX_EXP_OK) return -1;
        memcpy(t->w[i], r.w, sizeof r.w);
        t->tr[i][0] = r.w[RX_EXP_W_PRIOR];
        t->tr[i][1] = r.w[RX_EXP_W_ACTION];
        t->tr[i][2] = r.w[RX_EXP_W_NEXT];
    }
    return 0;
}

static void t_determinism(void) {
    printf("[*] determinism: same seed -> identical experiences; seeds differ\n");
    static const uint64_t seeds[4] = { 1, 2, 0xC0FFEE, 0xFFFFFFFFFFFFFFFFull };
    static Trace first[4], again;
    char d[200];
    for (int i = 0; i < 4; i++) {
        char leaf[32];
        snprintf(leaf, sizeof leaf, "det%d_a", i);
        dir_for(d, sizeof d, leaf);
        CHECK(open_env(&g_e, d, 1) == RX_EXP_OK, "open a");
        CHECK(run_trace(&g_e, seeds[i], &first[i]) == 0, "trace seed %d", i);
        /* reset to the same seed inside the same environment: same transitions */
        CHECK(run_trace(&g_e, seeds[i], &again) == 0, "trace after reset");
        CHECK(memcmp(first[i].tr, again.tr, sizeof again.tr) == 0,
              "seed %d: reset reproduces every (prior, action, next)", i);
        rx_exp_close(&g_e);
        rmtree(d);
        snprintf(leaf, sizeof leaf, "det%d_b", i);
        dir_for(d, sizeof d, leaf);
        CHECK(open_env(&g_e2, d, 1) == RX_EXP_OK, "open b");
        CHECK(run_trace(&g_e2, seeds[i], &again) == 0, "trace b");
        CHECK(memcmp(first[i].w, again.w, sizeof again.w) == 0,
              "seed %d: fresh environment -> byte-identical experience payloads", i);
        rx_exp_close(&g_e2);
        rmtree(d);
    }
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) {
            CHECK(memcmp(first[i].tr, first[j].tr, sizeof first[i].tr) != 0,
                  "seeds %d and %d give different trajectories", i, j);
            CHECK(memcmp(first[i].w[0] + RX_EXP_W_SEED_COMMIT0, first[j].w[0] + RX_EXP_W_SEED_COMMIT0,
                         32) != 0, "seed commitments differ");
        }
    int wrong = 0, total = 0;
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < DET_STEPS; k++) {
            total++;
            wrong += first[i].w[k][RX_EXP_W_ERR_FIELDS] != 0;
        }
    printf("    noise-blind predictor wrong on %d of %d transitions (unqualified annotation)\n",
           wrong, total);
}

int main(void) {
    snprintf(g_base, sizeof g_base, "/tmp/rx_exp_test.XXXXXX");
    if (!mkdtemp(g_base)) { perror("mkdtemp"); return 2; }
    uint8_t root[8] = { 'x', '1', 'e', 'x', 'p', 6, 9, 9 };
    if (aien_mid_derive(AIEN_MID_ROOT_PROVISIONED, root, sizeof root, &g_self) != 0) return 2;
    if (aienos_cap_start(&g_admin, &g_view) != 0) { fprintf(stderr, "aienos authority\n"); return 2; }
    t_family();
    t_chain_and_unobserved();
    t_consent();
    t_determinism();
    aienos_cap_stop(g_admin, g_view);
    rmtree(g_base);
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
    if (!g_fail) printf("OMEGA #699 EXPERIENCE TESTS: PASS\n");
    return g_fail ? 1 : 0;
}
