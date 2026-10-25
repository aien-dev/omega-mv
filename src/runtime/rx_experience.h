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
 * rx_experience.h -- verified state/action/next-state experience (omega #699).
 *
 * An experience is one transition of a seeded, resettable environment, taken
 * through the existing accept chain and recorded in canonical Cortex:
 *
 *   action -> goal (World outside input, CX_K_WORK_ACCEPTED)
 *          -> compose.candidate: the environment Skill runs on a staged
 *             J-Space branch
 *          -> compose.verify (AEGIS): the family contract replays the
 *             transition; only a passing result can win
 *          -> compose.commit + World binder: the state object names the
 *             sealed branch (CX_K_EXEC_COMMIT, the World commit receipt)
 *          -> settle: claim, verification evidence, promotion (rx_compose)
 *          -> rx_exp_record: CX_K_EXPERIENCE through the same Cortex writer
 *             link (rx_cortex_append_in), never a second writer.
 *
 * PREDICTED vs OBSERVED. A prediction (any model output) is only a
 * hypothesis: a CX_CLAIM / CX_K_CANDIDATE record tagged RX_EXP_TAG_HYPOTHESIS.
 * An observation is the environment's committed next state, and is accepted
 * only with its receipts: the World link's own commit record for a live
 * commit crumb, the AEGIS evidence that passed it, the promotion naming that
 * evidence, the accepted goal whose input is prior+action, and a replay of
 * the family transition. Anything else (no receipt, a hypothesis offered as a
 * receipt, a receipt appended by someone other than the World recorder, a
 * next state that differs from the receipt) is refused and nothing is
 * written.
 *
 * The prediction kept inside an experience is an UNQUALIFIED annotation
 * (RX_EXP_PRED_UNQUALIFIED): the Belief/Estimation gate EST-3 has not passed,
 * so predicted state and its error never become an authority input.
 *
 * Consent revocation never deletes (Cortex is append-only): it appends a
 * CX_K_CONSENT_REVOKED record, and every dataset view built afterwards
 * excludes the revoked experiences. Copies and trained weights made earlier
 * are out of scope here.
 *
 * Who may act and own. An action runs only for a tenant that presents a live
 * AIENOS capability (the view rx_exp_open was given) with subject = the
 * tenant, resource RX_EXP_RESOURCE(family) and right RX_EXP_RIGHT_ACT; the
 * experience it yields can be owned by that tenant alone. Reset and
 * hypothesis are not capability-gated.
 *
 * Bounds. An episode is a bounded microjob: at most RX_EXP_MAX_STEPS
 * committed transitions (recorded or dropped) between resets, counted in
 * this process (like `episode`). The next action is refused with
 * RX_EXP_ERR_TRACE; nothing is truncated silently. Longer objectives are
 * chunked by the caller into several episodes.
 *
 * Taint. A hypothesis is a claim and may name its basis in its links. The
 * basis is walked through claims: a non-authoritative import
 * (CX_K_IMPORTED) anywhere in it, a PRIVATE experience in it while the
 * draft is not PRIVATE, or a basis larger than RX_EXP_BASIS_MAX records
 * refuses the record (RX_EXP_ERR_TAINTED). An experience in the basis is not
 * descended into: its lineage was checked when it was recorded.
 *
 * Experience, hypothesis and revocation records are about the composition
 * state object: subject RXC_CX_SUBJECT(RXC_SLOT_STATE). A retired state
 * object takes no further actions or experiences (RX_EXP_ERR_RETIRED).
 */
#ifndef RX_EXPERIENCE_H
#define RX_EXPERIENCE_H

#include "aien_machine_id.h"
#include "aienos_cap.h"
#include "rx_capq.h"
#include "rx_compose.h"
#include "rx_cortex.h"
#include "rx_graph.h"
#include "rx_skillroute.h"

#define RX_EXP_VERSION 1u
#define RX_EXP_SUBJECT RXC_CX_SUBJECT(RXC_SLOT_STATE)
#define RX_EXP_MAX_FIELDS 8u
/* CX_K_CANDIDATE.tag of a hypothesis ("exp.hyp1"); composition claims carry
 * the id of their goal record instead. */
#define RX_EXP_TAG_HYPOTHESIS 0x6578702e68797031ull
/* Transitions per episode (rx_exp_act refuses the next with RX_EXP_ERR_TRACE). */
#define RX_EXP_MAX_STEPS 64u
/* Records a hypothesis basis walk may read before it refuses as tainted. */
#define RX_EXP_BASIS_MAX 32u
/* AIENOS capability an actor presents to rx_exp_act: subject = tenant. */
#define RX_EXP_RESOURCE(fam) ((fam)->family)
#define RX_EXP_RIGHT_ACT RX_RIGHT_WRITE

/* A transition family: pure functions of one 64-bit packed state. The same
 * `step` is the environment Skill (via `skill`) and the AEGIS contract (via
 * `contract`) and the recorder's replay. `skill` and `contract` exist only
 * because the composition function types carry no context pointer. */
typedef struct RxExpFamily {
    uint64_t family;           /* RX_EXP_FAMILY_* */
    uint32_t version;
    uint32_t op;               /* semantic operation id in the environment's own Capability
                                  Graph (cq_op_define ids are dense from 1) */
    const char *name;
    uint64_t domain;           /* applicability word the family declares */
    /* Next state and declared cost for a goal input; nonzero = refused. */
    int (*step)(uint64_t input, uint64_t *next, uint64_t *cost);
    /* Goal input that applies `action` to state `prior`. */
    uint64_t (*encode)(uint64_t prior, uint64_t action);
    /* Goal input that resets the environment for `seed`. */
    uint64_t (*reset_input)(uint64_t seed);
    /* Signed fields of a state (for error accounting); returns the count. */
    uint32_t (*unpack)(uint64_t state, int64_t out[RX_EXP_MAX_FIELDS]);
    /* Digest of the objective a state carries. */
    void (*objective)(uint64_t state, uint8_t out[32]);
    AgSkillFn skill;
    RxcContract contract;
} RxExpFamily;

/* ---- the walk1d family (rx_exp_walk.c) ------------------------------------
 * A noisy 1-D point mass that must reach a target. Packed state, LSB first:
 *   key 24 | k 10 | x 12 (signed) | v 5 (signed) | target 6 (signed) |
 *   act 3 | noise 2 | live 1 | op 1
 * key: stream key derived from the seed; k: steps taken (episode bound 1023);
 * act: last action code (1..5 = thrust -2..+2); noise: last noise + 2
 * (1..3 = -1..+1, 0 = none); live: episode started; op (goal input only):
 * 1 = reset. Step: noise = (pd0 stream(key, "omega.exp.walk1d.noise") word
 * k) mod 3 - 1, v' = clamp(v + thrust + noise, -8, 8), x' = clamp(x + v',
 * -2000, 2000), k' = k + 1; declared cost = |thrust| + 1 units. Reset: x = v
 * = k = 0, target = (stream(key, "omega.exp.walk1d.target") word 0) mod 41
 * - 20. Action word for rx_exp_act: the thrust code 1..5. */
#define RX_EXP_FAMILY_WALK1D 0x77616c6b31640001ull
extern const RxExpFamily rx_exp_walk1d;
enum { RX_EXP_WALK_KEY = 0, RX_EXP_WALK_K, RX_EXP_WALK_X, RX_EXP_WALK_V, RX_EXP_WALK_TARGET,
       RX_EXP_WALK_ACT, RX_EXP_WALK_NOISE, RX_EXP_WALK_LIVE, RX_EXP_WALK_FIELDS };
#define RX_EXP_WALK_K_MAX 1023u
/* Thrust code for a thrust in -2..2. */
#define RX_EXP_WALK_THRUST(a) ((uint64_t)((a) + 3))
/* The stream key of a seed (24 bits, never 0). */
uint64_t rx_exp_walk_key(uint64_t seed);

/* ---- record payloads -------------------------------------------------------- */

/* CX_K_EXPERIENCE (CX_EVIDENCE, CX_PROT_VERIFY_EVIDENCE).
 * links {World commit record of the state, AEGIS evidence, promotion,
 * hypothesis or 0}. */
enum {
    RX_EXP_W_VERSION = 0, RX_EXP_W_FAMILY, RX_EXP_W_FAMILY_VERSION,
    RX_EXP_W_SEED_COMMIT0,                      /* SHA-256("omega.exp.seed.v1" | seed) */
    RX_EXP_W_EPISODE = RX_EXP_W_SEED_COMMIT0 + 4,
    RX_EXP_W_STEP,                              /* transitions accepted before it in the episode */
    RX_EXP_W_OBJECTIVE0,
    RX_EXP_W_PRIOR = RX_EXP_W_OBJECTIVE0 + 4,   /* packed prior state */
    RX_EXP_W_PRIOR_DIGEST0,
    RX_EXP_W_ACTION = RX_EXP_W_PRIOR_DIGEST0 + 4,
    RX_EXP_W_INPUT,                             /* goal input the World accepted */
    RX_EXP_W_PRED_STATUS,                       /* RX_EXP_PRED_* */
    RX_EXP_W_PREDICTED,
    RX_EXP_W_PREDICTOR,
    RX_EXP_W_NEXT,                              /* observed packed next state */
    RX_EXP_W_NEXT_DIGEST0,
    RX_EXP_W_VERIFY = RX_EXP_W_NEXT_DIGEST0 + 4,/* RX_EXP_V_* */
    RX_EXP_W_ERR_FIELDS,                        /* predicted fields != observed (unqualified) */
    RX_EXP_W_ERR_L1,                            /* sum |predicted - observed| (unqualified) */
    RX_EXP_W_COST,
    RX_EXP_W_COST_UNIT,                         /* RX_EXP_COST_* */
    RX_EXP_W_OWNER, RX_EXP_W_CONSENT, RX_EXP_W_LICENSE, RX_EXP_W_PRIVACY,
    RX_EXP_W_SPLIT,                             /* RX_EXP_SPLIT_* */
    RX_EXP_W_APPLY_OP, RX_EXP_W_APPLY_DOMAIN,
    RX_EXP_W_SESSION,
    RX_EXP_W_WORDS
};

/* CX_K_CANDIDATE hypothesis (CX_CLAIM, tag RX_EXP_TAG_HYPOTHESIS). */
enum { RX_EXP_H_VERSION = 0, RX_EXP_H_FAMILY, RX_EXP_H_PRIOR, RX_EXP_H_ACTION,
       RX_EXP_H_PREDICTED, RX_EXP_H_PREDICTOR, RX_EXP_H_WORDS };

/* CX_K_CONSENT_REVOKED (CX_EVIDENCE, CX_PROT_AUTHORITY, tag = owner).
 * links {experience, 0 = every experience of the owner recorded before it}. */
enum { RX_EXP_R_VERSION = 0, RX_EXP_R_OWNER, RX_EXP_R_TARGET, RX_EXP_R_SCOPE, RX_EXP_R_REASON,
       RX_EXP_R_WORDS };

/* Verification an experience passed (all of them, or it is not written). */
enum {
    RX_EXP_V_WORLD_COMMIT = 1u << 0,  /* the World recorder's record of a live commit crumb */
    RX_EXP_V_AEGIS        = 1u << 1,  /* compose.verify passed this result */
    RX_EXP_V_PROMOTED     = 1u << 2,  /* the winning claim was promoted on that evidence */
    RX_EXP_V_GOAL         = 1u << 3,  /* accepted goal input = prior + action */
    RX_EXP_V_REPLAY       = 1u << 4,  /* the family transition replays to it */
    RX_EXP_V_ALL          = 0x1fu
};
enum { RX_EXP_PRED_NONE = 0, RX_EXP_PRED_UNQUALIFIED = 1 };
enum { RX_EXP_COST_DECLARED_UNITS = 1 };   /* measured wall time stays in the World receipt */
enum { RX_EXP_CONSENT_RETRIEVE = 1u << 0, RX_EXP_CONSENT_TRAIN = 1u << 1,
       RX_EXP_CONSENT_SHARE = 1u << 2, RX_EXP_CONSENT_ALL = 0x7u };
enum { RX_EXP_PRIV_PRIVATE = 1, RX_EXP_PRIV_SYNTHETIC = 2 };
enum { RX_EXP_SPLIT_TRAIN = 1, RX_EXP_SPLIT_HOLDOUT = 2 };

enum {
    RX_EXP_OK = 0,
    RX_EXP_ERR_ARG = -1,
    RX_EXP_ERR_UNOBSERVED = -2,   /* no receipt, or the receipt is not a World commit */
    RX_EXP_ERR_FORGED = -3,       /* receipt not written by the World recorder for a live crumb */
    RX_EXP_ERR_MISMATCH = -4,     /* observed state differs from the receipts */
    RX_EXP_ERR_ORDER = -5,        /* not the pending transition of this environment */
    RX_EXP_ERR_DUPLICATE = -6,    /* receipt already used by an experience */
    RX_EXP_ERR_REPLAY = -7,       /* the family transition does not replay to it */
    RX_EXP_ERR_UNVERIFIED = -8,   /* no AEGIS pass / promotion / matching goal */
    RX_EXP_ERR_CONSENT = -9,      /* missing owner, consent, privacy or split */
    RX_EXP_ERR_OWNER = -10,       /* revocation by someone other than the owner */
    RX_EXP_ERR_HYPOTHESIS = -11,  /* hypothesis id is not one for this prior + action */
    RX_EXP_ERR_REFUSED = -12,     /* the environment refused the action (no winner) */
    RX_EXP_ERR_WORLD = -13,       /* composition / World / Cortex failure */
    RX_EXP_ERR_RETIRED = -14,     /* the state object (the subject) is retired in the World */
    RX_EXP_ERR_TENANT = -15,      /* owner is not the tenant whose capability took the action */
    RX_EXP_ERR_TAINTED = -16,     /* hypothesis basis imported, private into non-private, or unreadable */
    RX_EXP_ERR_CAPABILITY = -17,  /* action without a live capability of the tenant for the family */
    RX_EXP_ERR_TRACE = -18        /* episode already holds RX_EXP_MAX_STEPS transitions */
};
const char *rx_exp_strerror(int rc);

/* ---- the environment -------------------------------------------------------- */

typedef struct RxExpEnv {
    const RxExpFamily *fam;
    const AienosCapView *view; /* checks actors' capabilities */
    RxCompose c;               /* large: keep RxExpEnv static or on the heap */
    AienMachineId slots[8];
    AienMachineIndex ix;
    CqCatalog cat;
    AgSkillTable skills;
    SrRouter router;
    int open;
    uint64_t seed;
    uint8_t seed_commit[32];
    uint8_t objective[32];
    uint64_t episode;          /* resets so far in this process */
    uint64_t step;             /* experiences recorded in this episode */
    uint64_t taken;            /* transitions committed in this episode (RX_EXP_MAX_STEPS) */
    uint64_t dropped;          /* acted, never recorded (counted, never silent) */
    uint64_t now_us;
    struct {
        int live;
        uint64_t prior, action, input, next, cost, tenant;
        uint64_t goal_crumb, commit, evidence, promotion;
    } pend;
} RxExpEnv;

/* Open the environment's composition in `dir` (rx_compose_open: machine
 * identity checked, J-Space and Cortex journal reopened and verified, state
 * recovered OLD-or-NEW) with the family Skill as the only route.
 * `admin`/`view` is the AIENOS authority the caller runs. */
int rx_exp_open(RxExpEnv *e, const RxExpFamily *fam, const char *dir, const AienMachineId *self,
                uint64_t session, AienosCapAdmin *admin, AienosCapView *view);
void rx_exp_close(RxExpEnv *e);

/* Reset for `seed` through the accept chain (a committed reset goal); starts
 * a new episode. Drops (and counts) an unrecorded pending transition. */
int rx_exp_reset(RxExpEnv *e, uint64_t seed);

/* The committed environment state (the World state object). */
uint64_t rx_exp_state(RxExpEnv *e);

/* Record a hypothesis about applying `action` to the current state. It is a
 * claim only and never an observation. */
int rx_exp_hypothesis(RxExpEnv *e, uint64_t predictor, uint64_t action, uint64_t predicted,
                      uint64_t *out_id);

/* What the environment answered to an action: receipts and the observed state.
 * The recorder re-checks all of it; a caller cannot vouch for any field. */
typedef struct {
    uint64_t prior, action, next;
    uint64_t commit, evidence, promotion;   /* Cortex ids */
} RxExpObservation;

/* Run `action` through the accept chain for `tenant`, which presents `cap`.
 * RX_EXP_OK with `obs` filled when the World committed a verified next
 * state; RX_EXP_ERR_REFUSED when the environment refused it (the refusal
 * stays in Cortex as composition records). RX_EXP_ERR_CAPABILITY,
 * RX_EXP_ERR_RETIRED and RX_EXP_ERR_TRACE refuse before anything runs or is
 * written, and keep a pending transition recordable. */
int rx_exp_act(RxExpEnv *e, uint32_t tenant, AienosCapRef cap, uint64_t action,
               RxExpObservation *obs);

typedef struct {
    uint8_t objective[32];     /* goal digest (zero = the environment's objective) */
    uint64_t prior, action;
    uint64_t hypothesis;       /* hypothesis record id, 0 = none */
    RxExpObservation obs;      /* claimed observation (receipts + next state) */
    uint64_t owner, consent, license, privacy, split;
} RxExpDraft;

/* Verify a draft against the pending transition, the Cortex receipts, the
 * live World and a replay, then append CX_K_EXPERIENCE. Nothing is written on
 * refusal. */
int rx_exp_record(RxExpEnv *e, const RxExpDraft *d, uint64_t *out_id);

/* Withdraw consent `scope` (RX_EXP_CONSENT_*) for experience `exp` (0 = every
 * experience of `owner` recorded so far). RX_EXP_ERR_OWNER if `exp` belongs to
 * someone else. Appends CX_K_CONSENT_REVOKED; deletes nothing. */
int rx_exp_revoke(RxExpEnv *e, uint64_t owner, uint64_t exp, uint64_t scope, uint64_t reason,
                  uint64_t *out_id);

/* ---- reading ----------------------------------------------------------------- */

typedef struct {
    uint64_t id, t;
    uint64_t w[RX_EXP_W_WORDS];
    uint64_t commit, evidence, promotion, hypothesis;   /* links */
} RxExpRecord;

/* Decode one experience (digest checked): RX_EXP_OK or RX_EXP_ERR_ARG. */
int rx_exp_decode(const CxStore *s, uint64_t id, RxExpRecord *out);

typedef struct {
    uint64_t seen, admitted, revoked, no_consent, split, invalid;
} RxExpViewStats;

/* Dataset view over `s` (the Cortex store alone; works after restart without
 * a World): experiences of family `fam`, oldest first, that re-verify from
 * their receipts and replay, whose consent after revocations covers `need`,
 * and whose split is in `splits` (RX_EXP_SPLIT_* mask). Returns the number
 * written to `out` (at most `max`). */
uint32_t rx_exp_dataset(const CxStore *s, const RxExpFamily *fam, uint64_t need, uint64_t splits,
                        RxExpRecord *out, uint32_t max, RxExpViewStats *st);

#endif /* RX_EXPERIENCE_H */
