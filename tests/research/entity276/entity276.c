/*
 * entity276.c -- #276 entity pilot (ledger L058): one persistent entity whose screen is a
 * projection of its committed state, on existing omega mechanisms only.
 *
 * Contract read: aien-dev/aien-architecture-mv#276 operator clarification 2026-10-19,
 * aien-dev/omega-mv#584 "Unified entity pilot and bounded optimization policy",
 * aien-dev/aien-architecture-mv#246 "Minimum-work policy integration" (bodies read
 * 2026-10-19). Code read at omega a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a.
 * Mapping: docs/research/276-entity-mapping.md. Frozen run: evidence/ENTITY-276/FREEZE.md.
 *
 *   s      RxWorld objects (units, goal, facts, generations, decision, index) + the Cortex
 *          journal the World writes through rx_cortex_attach + template records.
 *   e      an outside publication of PL_OT_GOAL fields (rx_world_publish_external).
 *   F      the rx_plan path of tests/runtime/rx_plan_reuse.c solve(): view, goal read,
 *          retrieve, bind, applicability, search when nothing applies, execute, record.
 *   theta  the PlCache template table; its version is the cognitive generation object.
 *   R      visor_world_snapshot + visor_world_format_text/json, plus a value-only
 *          history formatter over cx_recall (VisorCortexView does not exist).
 *
 * This file calls runtime, visor and plan code and edits none of it.
 *
 * UNVERIFIED (choices the contract did not pin):
 *   - the correction is an advance of the cognitive generation plus re-admission of the
 *     objective (theta's version changes; rx_plan refuses on PL_AX_COG_GEN);
 *   - templates rebuilt after restart re-enter as CANDIDATE through rx_plan_cache_store,
 *     so their first use runs the sequential reference again;
 *   - the action-graph body is stored as AgGraph struct bytes (no canonical encoding
 *     exists); a record is accepted only when rx_plan_identify reproduces its id;
 *   - the World is rebuilt after restart by booting the frozen specification and
 *     publishing the journal's newest committed values (no runtime World restore exists);
 *   - identity across restart = lineage (digest of record 1) + replayed record count +
 *     chain + an entity-state digest over the role objects (object-digest rule of
 *     rx_world.c), because the whole-World digest covers session objects;
 *   - the "screen" is the visor text/JSON projection plus the history text, in files.
 *
 * Horizon mode (ledger L166, evidence/ENTITY-276/h40/FREEZE.md), read at omega 21913232
 * (the run of record above) and the #246 E03 net(H) model (293b49a7). UNVERIFIED:
 *   - arms share one process and the one capability library; each has its own World,
 *     journal copy and plan cache, and process CPU includes the other arms' idle threads;
 *   - the forget arm (never stores a template) is the comparator the r = 1 model needs;
 *   - --release removes a finished run's reactions and retires its cells and run token
 *     from the harness, because rx_plan_execute keeps them and a World holds 256 objects.
 */
#include "runtime/aienos_cap.h"
#include "runtime/rx_cortex.h"
#include "runtime/rx_cortex_record.h"
#include "runtime/rx_plan.h"
#include "runtime/rx_plan_arrange.h"
#include "runtime/rx_world.h"
#include "visor/visor_world.h"
#include "sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- frozen constants (evidence/ENTITY-276/FREEZE.md) ---- */
enum { SUBJ_EXTERNAL = 100, ISSUER = 3, SUBJ_PLAN = 71 };
#define RES_CELL     0xB000001ull
#define RES_RUN      0xB000002ull
#define RES_DECISION 0xB000003ull
#define RES_INDEX    0xB000004ull
#define RES_ENV      0xB000005ull
#define RES_GOAL     0xB000006ull
#define RES_COG      0xB000007ull
#define RES_UNIT     0xB000100ull
#define SEED         276u
#define N_GOAL_UNITS 6u
#define N_UNITS      (N_GOAL_UNITS + 4u)
#define WORLD_GEN    7u
#define COG_GEN      3u
#define MAX_STEPS    32u
#define ENERGY       1000u
#define MAX_EXPAND   2000000u
#define IDLE_MS      3000u
#define N_AFTER      3u
#define REC_TAG      0x276A0001ull   /* template candidate record */
#define EV_TAG       0x276A0002ull   /* evidence for a verified reuse */
#define REC_VERSION  1u
#define SESSION_1    1u
#define SESSION_2    2u
#define MAX_RUNS     64u   /* one lowered graph per solve: the horizon mode runs up to 40 objectives per arm */

/* ---- counters (every call the pilot makes through the plan and view paths) ---- */
typedef struct {
    uint64_t lookups, hits, misses, arrange, snapshots, plan_views, admits, refusals, bad;
} Cnt;
static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; C.bad++;                  \
        fprintf(stderr, "  BAD %s:%d ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__);    \
        fputc('\n', stderr); } } while (0)

static uint64_t clk(clockid_t id) {
    struct timespec ts;
    clock_gettime(id, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static uint64_t wall_ns(void) { return clk(CLOCK_MONOTONIC); }
static uint64_t cpu_ns(void) { return clk(CLOCK_THREAD_CPUTIME_ID); }
static uint64_t pcpu_ns(void) { return clk(CLOCK_PROCESS_CPUTIME_ID); }

static void hex(const uint8_t *b, size_t n, char *out) {
    static const char *h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2 * i] = h[b[i] >> 4]; out[2 * i + 1] = h[b[i] & 15]; }
    out[2 * n] = 0;
}

/* ---- the entity ---- */
typedef struct {
    AienosCapAdmin *admin;
    AienosCapView *view;
    RxWorld w;
    PlEnv env;
    RxObjRef decision, index;
    RxObjRef unit[N_UNITS];
    RxCapRef ext_unit[N_UNITS];
    RxCapRef ext_run, ext_decision, ext_index, ext_goal, ext_goal_ro, ext_cog, ext_env;
    AgCapTable caps;
    PlExecCtx x;
    uint64_t seq, run;
    PlRunStore *rs[MAX_RUNS];
    uint32_t n_rs;
} Ent;

#define N_ROLES (N_UNITS + PL_ENV_COUNT + 2u)
#define MAX_ROWS 128u
#define MAX_ARMS 4u

typedef struct { uint8_t id[32]; uint64_t wg, cg, rec; } ThetaRow;

typedef struct {
    VisorWorldView v;
    uint64_t support[N_UNITS];
    uint64_t records;
    uint8_t last_digest[32];
} Screen;

typedef struct {
    char name[40];
    uint64_t cpu, pcpu, wall, jbytes, records, cog, order;
    uint32_t objects_live, reactions_active;   /* World footprint after a repeated objective */
    Cnt c;
    long rss_kib;
} Row;

/* ---- one arm = one entity with its own World, journal, plan cache and counters ----
 * The horizon mode (evidence/ENTITY-276/h40/FREEZE.md) keeps several arms alive in one
 * process and switches A between them; every other mode uses arm 0 only. The names below
 * (E, C, g_cache, g_cx, ...) always mean the current arm's copy. */
typedef struct {
    Ent e;
    PlCache cache;
    CxStore cx;
    AgSkillTable skills;
    Cnt c;
    uint64_t cog_cpu, rebuild_cpu;  /* thread CPU in cognition (view, goal, lookup, search) and theta rebuild */
    ThetaRow theta_rec[PL_MAX_TEMPLATES];  /* newest candidate record per semantic id */
    uint32_t n_theta_rec;
    uint64_t view_last_id;
    uint8_t view_last_digest[32], lineage[32];
    Screen screen;
    uint32_t rebuilt, rebuild_refused, promotions_seen;
    Row rows[MAX_ROWS];
    uint32_t n_rows;
    uint32_t after_hits, after_arrange, after_ok, wrong;
    int restore_refused;
    uint64_t n_at_open;
    uint8_t final_ed[32];   /* entity digest taken before the World shuts down */
    const char *jpath, *out;
    char mode[48], obuf[512], jbuf[600];
    int cold;               /* template records ignored at restart (cold and forget arms) */
    int lazy, lazy_pending; /* theta rebuilt by the first lookup, not at boot */
    int forget;             /* never stores a template: every objective searches */
    int horizon;            /* horizon-mode checks on the search count */
    int release;            /* release each finished run (release_run) */
    int hmutant;            /* horizon-mode mutant (ENTITY276_HMUTANT), 0 = none */
} Arm;

/* Horizon-mode mutants (env ENTITY276_HMUTANT): each must turn its arm's check red. */
enum { HM_NONE, HM_FORGETSTORE, HM_LAZYSKIP, HM_COLDKEEP, HM_DOUBLERELEASE };

static Arm g_arm[MAX_ARMS];
static Arm *A = &g_arm[0];
#define E                  (A->e)
#define C                  (A->c)
#define g_cache            (A->cache)
#define g_cx               (A->cx)
#define g_skills           (A->skills)
#define g_cog_cpu          (A->cog_cpu)
#define g_rebuild_cpu      (A->rebuild_cpu)
#define g_theta_rec        (A->theta_rec)
#define g_n_theta_rec      (A->n_theta_rec)
#define g_view_last_id     (A->view_last_id)
#define g_view_last_digest (A->view_last_digest)
#define g_lineage          (A->lineage)
#define g_screen           (A->screen)
#define g_rebuilt          (A->rebuilt)
#define g_rebuild_refused  (A->rebuild_refused)
#define g_promotions_seen  (A->promotions_seen)
#define g_rows             (A->rows)
#define g_n_rows           (A->n_rows)
#define g_after_hits       (A->after_hits)
#define g_after_arrange    (A->after_arrange)
#define g_after_ok         (A->after_ok)
#define g_wrong            (A->wrong)
#define g_restore_refused  (A->restore_refused)
#define g_n_at_open        (A->n_at_open)
#define g_final_ed         (A->final_ed)
#define g_jpath            (A->jpath)
#define g_out              (A->out)
#define g_mode             (A->mode)

static RxCapRef mint(uint32_t subject, uint64_t resource, uint32_t rights) {
    AienosCapRef office;
    aienos_cap_office(E.admin, &office);
    AienosCapMint m = { ISSUER, subject, resource, rights, 0, { UINT32_MAX, 0 }, office };
    AienosCapRef r = { UINT32_MAX, 0 };
    if (aienos_cap_mint(E.admin, &m, &r) != 0) r = (AienosCapRef){ UINT32_MAX, 0 };
    return (RxCapRef){ r.cap_id, r.generation };
}

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

static int put_unit(uint32_t k, const RxObjRef *on) {
    RxMutation m[2] = { { E.unit[k], PLA_F_SUPPORT, on ? pl_ref_pack(*on) : 0 },
                        { E.unit[k], PLA_F_SELF, pl_ref_pack(E.unit[k]) } };
    return rx_world_publish_external(&E.w, E.ext_unit[k], m, 2) > 0 ? 0 : -1;
}

/* Boot: the frozen specification (tests/runtime/rx_plan_reuse.c env_build, reverse shape,
 * no perturbation), with goal/facts and cognition on their own resources so the human's
 * capabilities are narrow. Deterministic: same slots and generations every boot. */
static int boot(void) {
    memset(&E, 0, sizeof E);
    if (aienos_cap_start(&E.admin, &E.view) != 0) return -1;
    if (rx_world_init_native(&E.w, E.view, 4, 1u << 16) != RX_OK) return -1;
    E.w.external_subject = SUBJ_EXTERNAL;
    const uint32_t R_ = RX_RIGHT_READ, W = RX_RIGHT_WRITE, RW = R_ | W;
    uint32_t seed = SEED * 2654435761u + 17u;
    uint32_t nf = 1 + lcg(&seed) % 9;
    RxObjRef fill[16];
    for (uint32_t i = 0; i < nf; i++)
        if (rx_world_create(&E.w, 0x5B0F, RX_PERSIST_EPHEMERAL, RES_ENV, NULL, &fill[i]) != RX_OK) return -1;
    for (uint32_t i = 0; i < nf; i += 2) rx_world_retire(&E.w, fill[i]);
    uint32_t order[N_UNITS];
    for (uint32_t k = 0; k < N_UNITS; k++) order[k] = k;
    for (uint32_t k = N_UNITS; k > 1; k--) {
        uint32_t j = lcg(&seed) % k, t = order[k - 1];
        order[k - 1] = order[j];
        order[j] = t;
    }
    for (uint32_t i = 0; i < N_UNITS; i++) {
        uint32_t k = order[i];
        uint64_t f[RX_MAX_FIELDS] = { 0, 0x100u + k };
        if (rx_world_create(&E.w, PL_OT_UNIT, RX_PERSIST_RESIDENT, RES_UNIT + k, f, &E.unit[k]) != RX_OK)
            return -1;
    }
    for (uint32_t k = 0; k < N_UNITS; k++) E.ext_unit[k] = mint(SUBJ_EXTERNAL, RES_UNIT + k, W);
    const RxObjRef *U = E.unit;
    const uint32_t g = N_GOAL_UNITS;
    put_unit(0, NULL);
    for (uint32_t k = 1; k < g; k++) put_unit(k, &U[k - 1]);
    put_unit(g, NULL); put_unit(g + 1, &U[g]); put_unit(g + 2, NULL); put_unit(g + 3, NULL);

    uint64_t z[RX_MAX_FIELDS] = { 0 };
    uint64_t wg[RX_MAX_FIELDS] = { WORLD_GEN };
    uint64_t cg[RX_MAX_FIELDS] = { COG_GEN, 0xA1E0 };
    uint64_t st[RX_MAX_FIELDS] = { 1u };
    if (rx_world_create(&E.w, PL_OT_WORLDGEN, RX_PERSIST_RESIDENT, RES_ENV, wg, &E.env.obj[PL_ENV_WORLDGEN]) != RX_OK ||
        rx_world_create(&E.w, PL_OT_COGNITION, RX_PERSIST_RESIDENT, RES_COG, cg, &E.env.obj[PL_ENV_COGNITION]) != RX_OK ||
        rx_world_create(&E.w, PL_OT_STATION, RX_PERSIST_RESIDENT, RES_ENV, st, &E.env.obj[PL_ENV_STATION]) != RX_OK ||
        rx_world_create(&E.w, PL_OT_GOALFACTS, RX_PERSIST_RESIDENT, RES_GOAL, z, &E.env.obj[PL_ENV_FACTS]) != RX_OK ||
        rx_world_create(&E.w, PL_OT_GOAL, RX_PERSIST_RESIDENT, RES_GOAL, z, &E.env.obj[PL_ENV_GOAL]) != RX_OK ||
        rx_world_create(&E.w, PL_OT_DECISION, RX_PERSIST_RESIDENT, RES_DECISION, z, &E.decision) != RX_OK ||
        rx_world_create(&E.w, PL_OT_INDEX, RX_PERSIST_RESIDENT, RES_INDEX, z, &E.index) != RX_OK)
        return -1;
    E.caps.subject = SUBJ_PLAN;
    for (uint32_t k = 0; k < N_UNITS; k++) {
        E.caps.cap[E.caps.n].ref = mint(SUBJ_PLAN, RES_UNIT + k, RW);
        E.caps.cap[E.caps.n].resource = RES_UNIT + k;
        E.caps.cap[E.caps.n].rights = RW;
        E.caps.n++;
    }
    E.ext_run = mint(SUBJ_EXTERNAL, RES_RUN, W);
    E.ext_decision = mint(SUBJ_EXTERNAL, RES_DECISION, W);
    E.ext_index = mint(SUBJ_EXTERNAL, RES_INDEX, W);
    E.ext_goal = mint(SUBJ_EXTERNAL, RES_GOAL, W);      /* the human's admission right */
    E.ext_goal_ro = mint(SUBJ_EXTERNAL, RES_GOAL, R_);  /* a right that must not admit */
    E.ext_cog = mint(SUBJ_EXTERNAL, RES_COG, W);        /* the human's correction right */
    E.ext_env = mint(SUBJ_EXTERNAL, RES_ENV, W);
    E.x.cell_cap = mint(SUBJ_PLAN, RES_CELL, RW);
    E.x.run_cap = mint(SUBJ_PLAN, RES_RUN, R_);
    E.x.ext_run_cap = E.ext_run;
    E.x.cell_res = RES_CELL;
    E.x.run_res = RES_RUN;
    E.x.skills = &g_skills;
    E.x.core_class = 0;
    rx_world_wait_quiescent(&E.w, 30000);
    return 0;
}

static void shutdown_entity(void) {
    rx_world_wait_quiescent(&E.w, 30000);
    rx_world_destroy(&E.w);
    aienos_cap_stop(E.admin, E.view);
    for (uint32_t i = 0; i < E.n_rs; i++) free(E.rs[i]);
    E.n_rs = 0;
}

/* Role objects whose committed values make up the entity state, in a fixed order. */
#define N_ROLES_W (N_ROLES * 10u)
#define OBS_KIND 0x2760u              /* writer kind (below CX_K_BASE): entity state observation */
#define OBS_TAG  0x276A0003ull
static RxObjRef role(uint32_t r) {
    if (r < N_UNITS) return E.unit[r];
    r -= N_UNITS;
    if (r < PL_ENV_COUNT) return E.env.obj[r];
    return r == PL_ENV_COUNT ? E.decision : E.index;
}

/* Entity-state digest: object-digest rule of rx_world.c (type + fields) per role. */
static void entity_digest_world(uint8_t out[32]) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)"ENTITY276_STATE_V1", 18);
    for (uint32_t r = 0; r < N_ROLES; r++) {
        RxObject o;
        uint32_t ty = 0;
        uint64_t f[RX_MAX_FIELDS] = { 0 };
        if (rx_world_read(&E.w, role(r), &o) == RX_OK) { ty = o.type; memcpy(f, o.field, sizeof f); }
        sha256_update(&c, (const uint8_t *)&r, 4);
        sha256_update(&c, (const uint8_t *)&ty, 4);
        sha256_update(&c, (const uint8_t *)f, sizeof f);
    }
    sha256_final(&c, out);
}

/* The journal records one object per crumb (the first output, rx_cortex_record.c), and a
 * move commit names its graph cell first, so unit values never reach the journal as World
 * records (MISSING PREREQUISITE E). The entity therefore appends a state observation
 * (class OBSERVATION, writer kind OBS_KIND below CX_K_BASE) through the World's attached
 * writer after each commit: per role object its slot, generation and field values. */
static uint64_t obs_latest(CxStore *s, const uint64_t **payload) {
    for (uint64_t id = s->n; id >= 1; id--) {
        const CxObject *o = cx_get(s, id);
        if (o->cls != CX_OBSERVATION || o->kind != OBS_KIND || o->tag != OBS_TAG) continue;
        if (cx_verify(s, id) != CX_OK || o->n != N_ROLES_W) return 0;
        *payload = cx_payload(s, o);
        return id;
    }
    return 0;
}

static int journal_fields(CxStore *s, RxObjRef ref, uint64_t f[RX_MAX_FIELDS]) {
    const uint64_t *p;
    if (!obs_latest(s, &p)) return 0;
    for (uint32_t r = 0; r < N_ROLES; r++, p += 10)
        if (p[0] == ref.id && p[1] == ref.generation) { memcpy(f, p + 2, 8 * RX_MAX_FIELDS); return 1; }
    return 0;
}

static int observe(uint64_t cause_crumb) {
    static uint64_t p[N_ROLES_W];
    for (uint32_t r = 0; r < N_ROLES; r++) {
        RxObject o;
        RxObjRef ref = role(r);
        memset(&p[10 * r], 0, 80);
        p[10 * r] = ref.id;
        p[10 * r + 1] = ref.generation;
        if (rx_world_read(&E.w, ref, &o) == RX_OK) memcpy(&p[10 * r + 2], o.field, 8 * RX_MAX_FIELDS);
    }
    CxHeader h = { CX_OBSERVATION, OBS_KIND, (uint64_t)E.env.obj[PL_ENV_GOAL].id + 1u, rx_cortex_next_t(&E.w),
                   0, 0, 0, OBS_TAG, { 0 } };
    h.links[0] = cause_crumb ? rx_cortex_record_of(&E.w, cause_crumb) : 0;
    uint64_t id;
    return rx_cortex_append(&E.w, &h, p, N_ROLES_W, &id) == RX_OK ? 0 : -1;
}

static void entity_digest_journal(CxStore *s, uint8_t out[32], uint32_t *missing) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)"ENTITY276_STATE_V1", 18);
    *missing = 0;
    for (uint32_t r = 0; r < N_ROLES; r++) {
        RxObject o;
        uint32_t ty = 0;
        uint64_t f[RX_MAX_FIELDS] = { 0 };
        if (rx_world_read(&E.w, role(r), &o) == RX_OK) ty = o.type;  /* type is boot spec */
        if (!journal_fields(s, role(r), f)) (*missing)++;
        sha256_update(&c, (const uint8_t *)&r, 4);
        sha256_update(&c, (const uint8_t *)&ty, 4);
        sha256_update(&c, (const uint8_t *)f, sizeof f);
    }
    sha256_final(&c, out);
}

/* ---- the view R(s, h) ---- */


/* Value-only history formatter over cx_recall (stand-in for the missing VisorCortexView).
 * Outcome: for each unit, the label of what it rests on, from the newest committed record. */
static int history_format(CxStore *s, char *out, size_t n, uint64_t support[N_UNITS]) {
    size_t len = 0;
    int w = snprintf(out, n, "history: records %llu\n", (unsigned long long)s->n);
    if (w < 0 || (size_t)w >= n) return -1;
    len = (size_t)w;
    for (uint32_t k = 0; k < N_UNITS; k++) {
        uint64_t f[RX_MAX_FIELDS] = { 0 };
        int ok = journal_fields(s, E.unit[k], f);
        support[k] = ok ? f[PLA_F_SUPPORT] : UINT64_MAX;
        const char *on = "floor";
        char lab[16];
        if (!ok) on = "(no record)";
        else if (f[PLA_F_SUPPORT]) {
            RxObjRef r = pl_ref_unpack(f[PLA_F_SUPPORT]);
            snprintf(lab, sizeof lab, "slot %u", r.id);
            for (uint32_t j = 0; j < N_UNITS; j++)
                if (E.unit[j].id == r.id && E.unit[j].generation == r.generation) snprintf(lab, sizeof lab, "U%u", j);
            on = lab;
        }
        w = snprintf(out + len, n - len, "  U%u rests on %s\n", k, on);
        if (w < 0 || (size_t)w >= n - len) return -1;
        len += (size_t)w;
    }
    w = snprintf(out + len, n - len, "  note: value-only history view; holds no authority\n");
    if (w < 0 || (size_t)w >= n - len) return -1;
    return (int)(len + (size_t)w);
}

/* One projection: visor snapshot + history, written to <out>/screen-<tag>.{txt,json}. */
static int project(const char *tag, Screen *sc) {
    C.snapshots++;
    if (visor_world_snapshot(&E.w, &sc->v) != 0) return -1;
    static char t1[16384], t2[4096], js[16384];
    if (visor_world_format_text(&sc->v, t1, sizeof t1) < 0) return -1;
    if (visor_world_format_json(&sc->v, js, sizeof js) < 0) return -1;
    if (history_format(&g_cx, t2, sizeof t2, sc->support) < 0) return -1;
    sc->records = g_cx.n;
    const CxObject *last = g_cx.n ? cx_get(&g_cx, g_cx.n) : NULL;
    if (last) memcpy(sc->last_digest, last->digest, 32);
    char p[512];
    snprintf(p, sizeof p, "%s/screen-%s.txt", g_out, tag);
    FILE *fp = fopen(p, "w");
    if (fp) { fputs(t1, fp); fputs(t2, fp); fclose(fp); }
    snprintf(p, sizeof p, "%s/screen-%s.json", g_out, tag);
    fp = fopen(p, "w");
    if (fp) { fputs(js, fp); fputc('\n', fp); fclose(fp); }
    return 0;
}

/* ---- theta persisted through the attached Cortex writer ---- */


static ThetaRow *theta_row(const uint8_t id[32], int add) {
    for (uint32_t i = 0; i < g_n_theta_rec; i++)
        if (memcmp(g_theta_rec[i].id, id, 32) == 0) return &g_theta_rec[i];
    if (!add || g_n_theta_rec >= PL_MAX_TEMPLATES) return NULL;
    ThetaRow *r = &g_theta_rec[g_n_theta_rec++];
    memset(r, 0, sizeof *r);
    memcpy(r->id, id, 32);
    return r;
}

static uint64_t g_payload[(1u << 16) / 8 + sizeof(AgGraph) / 8 + 64];

static int theta_append(const PlanTemplate *t, uint64_t cause_crumb) {
    static uint8_t sem[1u << 16];
    size_t ns = rx_plan_encode(t, sem, sizeof sem);
    if (!ns) return -1;
    uint32_t sw = (uint32_t)((ns + 7) / 8), gw = (uint32_t)((sizeof(AgGraph) + 7) / 8);
    uint32_t n = 9 + sw + gw;
    memset(g_payload, 0, (size_t)n * 8);
    g_payload[0] = REC_VERSION;
    g_payload[1] = ns;
    g_payload[2] = sizeof(AgGraph);
    g_payload[3] = t->verified_world_gen;
    g_payload[4] = t->verified_cog_gen;
    for (uint32_t k = 0; k < 4; k++) g_payload[5 + k] = pl_word(t->semantic_id, k);
    memcpy(&g_payload[9], sem, ns);
    memcpy(&g_payload[9 + sw], &t->graph, sizeof(AgGraph));
    CxHeader h = { CX_CLAIM, CX_K_CANDIDATE, (uint64_t)E.index.id + 1u, rx_cortex_next_t(&E.w),
                   t->verified_cog_gen, 0, 0, REC_TAG, { 0 } };
    h.links[0] = cause_crumb ? rx_cortex_record_of(&E.w, cause_crumb) : 0;
    uint64_t id = 0;
    if (rx_cortex_append(&E.w, &h, g_payload, n, &id) != RX_OK) return -1;
    ThetaRow *r = theta_row(t->semantic_id, 1);
    if (!r) return -1;
    r->wg = t->verified_world_gen;
    r->cg = t->verified_cog_gen;
    r->rec = id;
    return 0;
}

static int theta_promote(const PlanTemplate *t, const uint8_t realization[32]) {
    ThetaRow *r = theta_row(t->semantic_id, 0);
    if (!r || !r->rec) return -1;
    uint64_t pl[8];
    for (uint32_t k = 0; k < 4; k++) { pl[k] = pl_word(realization, k); pl[4 + k] = pl_word(t->semantic_id, k); }
    CxHeader h = { CX_EVIDENCE, CX_K_EVIDENCE_REF, (uint64_t)E.index.id + 1u, rx_cortex_next_t(&E.w),
                   t->verified_cog_gen, 0, CX_PROT_VERIFY_EVIDENCE, EV_TAG, { r->rec, 0, 0, 0 } };
    uint64_t ev = 0, pid = 0;
    if (rx_cortex_append(&E.w, &h, pl, 8, &ev) != RX_OK) return -1;
    return rx_cortex_promote(&E.w, r->rec, ev, &pid) == RX_OK ? 0 : -1;
}

/* theta in memory vs theta as the journal records it: one bad row per mismatch. */
static uint32_t theta_check(const char *where) {
    uint32_t bad = 0;
    for (uint32_t i = 0; i < g_cache.n; i++) {
        const PlanTemplate *t = g_cache.t[i];
        ThetaRow *r = theta_row(t->semantic_id, 0);
        if (!r || !r->rec || r->wg != t->verified_world_gen || r->cg != t->verified_cog_gen) {
            bad++;
            fprintf(stderr, "  theta %s: template %016llx not as journaled\n", where,
                    (unsigned long long)pl_word(t->semantic_id, 0));
        }
    }
    for (uint32_t i = 0; i < g_n_theta_rec; i++)
        if (!rx_plan_cache_find(&g_cache, g_theta_rec[i].id)) bad++;
    return bad;
}

/* Rebuild theta from the replayed journal: newest verifying candidate per semantic id,
 * accepted only when rx_plan_identify reproduces the recorded id. */

static void theta_rebuild(CxStore *s) {
    static PlanTemplate t;
    g_n_theta_rec = 0;
    for (uint64_t id = 1; id <= s->n; id++) {
        const CxObject *o = cx_get(s, id);
        if (o->kind == CX_K_PROMOTION) { g_promotions_seen++; continue; }
        if (o->cls != CX_CLAIM || o->kind != CX_K_CANDIDATE || o->tag != REC_TAG) continue;
        const uint64_t *p = cx_payload(s, o);
        if (cx_verify(s, id) != CX_OK || o->n < 9 || p[0] != REC_VERSION || p[2] != sizeof(AgGraph)) {
            g_rebuild_refused++;
            continue;
        }
        uint32_t sw = (uint32_t)((p[1] + 7) / 8), gw = (uint32_t)((p[2] + 7) / 8);
        if (o->n != 9 + sw + gw) { g_rebuild_refused++; continue; }
        memset(&t, 0, sizeof t);
        if (rx_plan_decode((const uint8_t *)&p[9], (size_t)p[1], &t) != 0) { g_rebuild_refused++; continue; }
        memcpy(&t.graph, &p[9 + sw], sizeof(AgGraph));
        rx_plan_identify(&t);
        uint8_t want[32];
        for (uint32_t k = 0; k < 4; k++) {
            uint64_t v = p[5 + k];
            memcpy(want + 8 * k, &v, 8);
        }
        if (memcmp(want, t.semantic_id, 32) != 0) { g_rebuild_refused++; continue; }
        ThetaRow *r = theta_row(t.semantic_id, 1);
        if (!r) { g_rebuild_refused++; continue; }
        r->wg = p[3];
        r->cg = p[4];
        r->rec = id;
        PlanTemplate *have = rx_plan_cache_find(&g_cache, t.semantic_id);
        if (have) { have->verified_world_gen = p[3]; have->verified_cog_gen = p[4]; continue; }
        if (rx_plan_cache_store(&g_cache, &t, p[3], p[4])) g_rebuilt++;
    }
}

/* ---- F_theta(s, e): the rx_plan path (rx_plan_reuse.c solve, counted) ---- */

static PlView g_v, g_v2;
static PlanTemplate g_tmp;

typedef struct {
    int path;                   /* 1 first/search, 2 reuse */
    PlAxis refused_axis;        /* furthest refusal of a retrieved candidate, PL_AX_OK if none */
    uint32_t refused;
    int success, goal;
    uint32_t status_before;
    uint64_t decision_crumb;
    PlanTemplate *t;
} Out;

static void plan_view(PlView *v) { C.plan_views++; rx_plan_view(&E.w, v); }

/* --release (horizon mode): a finished run's lowered reactions, cells and run token leave
 * the World through rx_world_remove_reaction and rx_world_retire. rx_plan_execute keeps
 * them, and a World holds RX_MAX_OBJECTS objects, so without this one session runs at most
 * six objectives (35 objects and 34 reactions stay per objective). Returns failures. */
static uint32_t release_run(PlRunStore *rs) {
    const AgLowered *L = &rs->L;
    uint32_t bad = 0;
    if (!L->w || !L->g) return 0;
    for (uint32_t i = 0; i < L->g->n_nodes; i++) {
        bad += rx_world_remove_reaction(&E.w, L->reaction[i]) != RX_OK;
        bad += rx_world_retire(&E.w, L->cell[i]) != RX_OK;
    }
    for (uint32_t i = 0; i < L->n_ask; i++) bad += rx_world_remove_reaction(&E.w, L->ask_reaction[i]) != RX_OK;
    bad += rx_world_retire(&E.w, L->run) != RX_OK;
    return bad;
}

static PlanTemplate *decide(const PlaGoal *g, RxObjRef *slots, PlCost *cost, Out *o) {
    PlanTemplate *cand[PL_MAX_TEMPLATES];
    if (A->lazy_pending) {
        /* Lazy arm: theta is rebuilt from the journal by the first lookup that needs it,
         * inside the planning CPU of that objective. */
        uint64_t b0 = cpu_ns();
        theta_rebuild(&g_cx);
        g_rebuild_cpu = cpu_ns() - b0;
        A->lazy_pending = 0;
    }
    C.lookups++;
    uint32_t n = rx_plan_cache_retrieve(&g_cache, (uint32_t)g->kind, g->shape, cand, PL_MAX_TEMPLATES, cost);
    RxObjRef s[PL_MAX_SLOTS];
    o->refused_axis = PL_AX_OK;
    for (uint32_t i = 0; i < n; i++) {
        PlAxis best = PL_AX_SHAPE;
        for (uint32_t attempt = 0;; attempt++) {
            int rc = pla_bind(cand[i], &g_v, g, attempt, s, cost);
            if (rc == 0) break;
            PlAxis ax = PL_AX_BINDING;
            if (rc > 0) ax = rx_plan_applicable(cand[i], &E.w, &g_v, &E.env, s, &E.caps, cost, NULL);
            if (ax == PL_AX_OK) { memcpy(slots, s, sizeof s); C.hits++; return cand[i]; }
            if (ax > best) best = ax;
        }
        cand[i]->refusals[best]++;
        o->refused++;
        if (best > o->refused_axis) o->refused_axis = best;
    }
    C.misses++;
    return NULL;
}

static int goal_holds(void) {
    PlaGoal g;
    plan_view(&g_v2);
    if (pla_goal_read(&g_v2, &E.env, &g, NULL) != 0) return 0;
    for (uint32_t i = 0; i < g.n_facts; i++) {
        uint64_t f = g_v2.o[g.x[i].id].field[PLA_F_SUPPORT];
        uint64_t want = g.floor[i] ? 0 : pl_ref_pack((RxObjRef){ g.t[i].id, g_v2.o[g.t[i].id].generation });
        if (f != want) return 0;
    }
    return pla_legal(&g_v2);
}

/* probe_only: decide and report, execute nothing (counterfactual before a correction). */
static int solve(Out *o, int probe_only) {
    uint64_t k0 = cpu_ns();
    memset(o, 0, sizeof *o);
    PlCost cost;
    memset(&cost, 0, sizeof cost);
    plan_view(&g_v);
    PlaGoal g;
    if (pla_goal_read(&g_v, &E.env, &g, &cost) != 0) return -1;
    RxObjRef slots[PL_MAX_SLOTS];
    PlanTemplate *use = decide(&g, slots, &cost, o);
    if (probe_only) { o->path = use ? 2 : 1; g_cog_cpu += cpu_ns() - k0; return 0; }
    const PlanTemplate *run = use;
    if (use) {
        o->path = 2;
        o->status_before = use->status;
    } else {
        PlaTarget tg;
        PlaPlan plan;
        pla_target_from_goal(&g, &tg);
        C.arrange++;
        if (pla_plan(&g_v, &g, &tg, MAX_EXPAND, &plan, &cost) != 0) return -2;
        const uint64_t *cf = g_v.o[E.env.obj[PL_ENV_COGNITION].id].field;
        if (pla_generalize(&g_v, &g, &plan, PL_ORIGIN_SEARCH, NULL, cf[0], &g_tmp, slots) != 0) return -3;
        o->path = 1;
        run = &g_tmp;
    }
    g_cog_cpu += cpu_ns() - k0;
    int64_t crumb = rx_plan_decide(&E.w, E.ext_decision, E.decision, g.seq, use ? PL_AX_OK : o->refused ? o->refused_axis : PL_AX_SHAPE,
                                   use, -1, ++E.seq, NULL);
    o->decision_crumb = crumb > 0 ? (uint64_t)crumb : 0;
    if (E.n_rs >= MAX_RUNS) return -4;
    PlRunStore *rs = calloc(1, sizeof *rs);   /* lowered graphs must outlive the World */
    if (!rs) return -4;
    E.rs[E.n_rs++] = rs;
    E.x.reference_first = use && use->status == PL_STATUS_CANDIDATE;
    PlExecResult ex;
    rx_plan_execute(run, &E.w, &E.env, slots, &E.caps, &E.x, ++E.run, rs, pla_legal, &ex);
    rx_world_wait_quiescent(&E.w, 30000);
    o->success = ex.success;
    o->goal = goal_holds();
    if (use) {
        uint32_t before = use->status;
        rx_plan_record_use(use, ex.success && o->goal);
        o->t = use;
        if (before == PL_STATUS_CANDIDATE && use->status == PL_STATUS_VERIFIED)
            if (theta_promote(use, ex.realization_id) != 0) return -5;
    } else if (ex.success && o->goal && (!A->forget || A->hmutant == HM_FORGETSTORE)) {
        plan_view(&g_v2);
        uint64_t wg = g_v2.o[E.env.obj[PL_ENV_WORLDGEN].id].field[0];
        uint64_t cg = g_v2.o[E.env.obj[PL_ENV_COGNITION].id].field[0];
        o->t = rx_plan_cache_store(&g_cache, &g_tmp, wg, cg);
        if (!o->t || theta_append(o->t, o->decision_crumb) != 0) return -6;
    }
    if (o->t) rx_plan_publish_index(&E.w, E.ext_index, E.index, &g_cache, o->t);
    rx_world_wait_quiescent(&E.w, 30000);
    if (A->release) {
        uint32_t rb = release_run(rs);
        if (A->hmutant == HM_DOUBLERELEASE) rb += release_run(rs);
        CHECK(rb == 0, "release: %u reactions or objects not released", rb);
        rx_world_wait_quiescent(&E.w, 30000);
    }
    if (observe(o->decision_crumb) != 0) return -7;
    return 0;
}

/* ---- e: admission through the outside-publication path ---- */

/* Objective k (frozen): k even = tower U5..U0 bottom to top (reverse of boot), k odd =
 * U0..U5 bottom to top. Facts listed in a fixed order. */
static uint32_t objective_mutations(uint32_t k, RxCapRef cap, int64_t *rc) {
    RxMutation m[RX_MAX_MUTATIONS];
    uint32_t n = 0;
    RxObjRef L[N_GOAL_UNITS];
    for (uint32_t i = 0; i < N_GOAL_UNITS; i++) L[i] = E.unit[k % 2 ? i : N_GOAL_UNITS - 1 - i];
    RxObjRef facts = E.env.obj[PL_ENV_FACTS], goal = E.env.obj[PL_ENV_GOAL];
    for (uint32_t i = 0; i < PLA_MAX_FACTS; i++)
        m[n++] = (RxMutation){ facts, i, i < N_GOAL_UNITS ? pla_fact(L[i], i ? &L[i - 1] : NULL) : 0 };
    m[n++] = (RxMutation){ goal, 0, 1000u + k };
    m[n++] = (RxMutation){ goal, 1, PLA_GOAL_ARRANGE };
    m[n++] = (RxMutation){ goal, 2, MAX_STEPS };
    m[n++] = (RxMutation){ goal, 3, ENERGY };
    m[n++] = (RxMutation){ goal, 4, pl_ref_pack(facts) };
    *rc = rx_world_publish_external(&E.w, cap, m, n);
    return n;
}

static int admit(uint32_t k) {
    int64_t rc;
    objective_mutations(k, E.ext_goal, &rc);
    rx_world_wait_quiescent(&E.w, 30000);
    if (rc > 0) C.admits++;
    if (rc > 0 && observe(0) != 0) return -1;
    return rc > 0 ? 0 : -1;
}

/* ---- per-step accounting ---- */

static uint64_t r_cpu, r_pcpu, r_wall, r_jb, r_rec, r_cog;
static Cnt r_c;
static uint64_t g_order;   /* step order in the process, across arms */

static uint64_t journal_size(void) {
    struct stat st;
    return g_jpath && stat(g_jpath, &st) == 0 ? (uint64_t)st.st_size : 0;
}
static void step_begin(void) {
    r_c = C;
    r_jb = journal_size();
    r_rec = g_cx.n;
    r_cog = g_cog_cpu;
    r_wall = wall_ns();
    r_pcpu = pcpu_ns();
    r_cpu = cpu_ns();
}
static Row *step_end(const char *name) {
    uint64_t c1 = cpu_ns(), p1 = pcpu_ns(), w1 = wall_ns();
    static Row spare;
    Row *r = g_n_rows < MAX_ROWS ? &g_rows[g_n_rows++] : &spare;
    CHECK(r != &spare, "step table full at %s", name);
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name);
    r->order = ++g_order;
    r->cpu = c1 - r_cpu;
    r->pcpu = p1 - r_pcpu;
    r->wall = w1 - r_wall;
    r->jbytes = journal_size() - r_jb;
    r->records = g_cx.n - r_rec;
    r->cog = g_cog_cpu - r_cog;
    r->c.lookups = C.lookups - r_c.lookups;
    r->c.hits = C.hits - r_c.hits;
    r->c.misses = C.misses - r_c.misses;
    r->c.arrange = C.arrange - r_c.arrange;
    r->c.snapshots = C.snapshots - r_c.snapshots;
    r->c.plan_views = C.plan_views - r_c.plan_views;
    r->c.admits = C.admits - r_c.admits;
    r->c.refusals = C.refusals - r_c.refusals;
    r->c.bad = C.bad - r_c.bad;
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    r->rss_kib = ru.ru_maxrss;
    return r;
}

/* ---- checks at attach / reattach ---- */

static void view_attach(const char *tag) {
    CHECK(project(tag, &g_screen) == 0, "snapshot %s", tag);
    g_view_last_id = g_cx.n;
    memcpy(g_view_last_digest, g_screen.last_digest, 32);
}

/* Reattach: the new projection must equal the runtime and the journal. */
static void view_reattach(const char *tag, int expect_goal) {
    uint64_t prev_id = g_view_last_id;
    uint8_t prev_digest[32];
    memcpy(prev_digest, g_view_last_digest, 32);
    CHECK(project(tag, &g_screen) == 0, "snapshot %s", tag);
    uint8_t d[32];
    char hx[72] = "sha256:";
    rx_world_digest(&E.w, d);
    hex(d, 32, hx + 7);
    CHECK(strcmp(hx, g_screen.v.digest) == 0, "%s: view digest is the runtime digest", tag);
    CHECK(cx_verify_chain(&g_cx) == CX_OK, "%s: journal chain verifies", tag);
    const CxObject *p = prev_id ? cx_get(&g_cx, prev_id) : NULL;
    CHECK(!prev_id || (p && memcmp(p->digest, prev_digest, 32) == 0), "%s: earlier history unchanged", tag);
    const CxObject *one = cx_get(&g_cx, 1);
    CHECK(one && memcmp(one->digest, g_lineage, 32) == 0, "%s: lineage unchanged", tag);
    if (expect_goal) {
        /* Outcome visible: the history view shows every unit where the World has it. */
        plan_view(&g_v2);
        for (uint32_t k = 0; k < N_UNITS; k++)
            CHECK(g_screen.support[k] == g_v2.o[E.unit[k].id].field[PLA_F_SUPPORT],
                  "%s: U%u shown where it rests", tag, k);
        CHECK(goal_holds(), "%s: committed goal holds", tag);
    }
    /* The history must not lag the World: its observation is newer than every commit. */
    const uint64_t *op;
    uint64_t obs = obs_latest(&g_cx, &op), last_commit = 0;
    for (uint64_t id = g_cx.n; id >= 1 && !last_commit; id--)
        if (cx_get(&g_cx, id)->kind == CX_K_EXEC_COMMIT) last_commit = id;
    CHECK(obs > last_commit, "%s: history observation (%llu) newer than the last commit (%llu)", tag,
          (unsigned long long)obs, (unsigned long long)last_commit);
    uint32_t tb = theta_check(tag);
    C.bad += tb;
    g_view_last_id = g_cx.n;
    memcpy(g_view_last_digest, g_screen.last_digest, 32);
}

/* ---- output ---- */

static void meminfo(double *load1, long *avail_kib) {
    *load1 = -1;
    *avail_kib = -1;
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%lf", load1) != 1) *load1 = -1; fclose(f); }
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "MemAvailable: %ld kB", avail_kib) == 1) break;
        fclose(f);
    }
}

static double g_load0;
static long g_mem0;

typedef struct {
    uint64_t idle_pcpu, idle_vcsw, idle_ivcsw, idle_rchar, idle_wchar, idle_ms_measured;
    int idle_run;
} Idle;
static Idle g_idle;
static int g_cog_refused, g_probe_accepted, g_admit_refused_ok;
static char g_expect_path[512];

static void write_outputs(void) {
    double l1;
    long m1;
    meminfo(&l1, &m1);
    char p[512];
    snprintf(p, sizeof p, "%s/steps-%s.csv", g_out, g_mode);
    FILE *f = fopen(p, "w");
    if (f) {
        fprintf(f, "# load1_start=%.2f memavail_kib_start=%ld load1_end=%.2f memavail_kib_end=%ld\n",
                g_load0, g_mem0, l1, m1);
        fprintf(f, "step,cpu_ns,proc_cpu_ns,cog_cpu_ns,wall_ns,journal_bytes,records,lookups,hits,misses,arrange_calls,"
                   "snapshot_calls,plan_views,admits,refusals,bad_rows,peak_rss_kib,order,objects_live,reactions_active\n");
        for (uint32_t i = 0; i < g_n_rows; i++) {
            Row *r = &g_rows[i];
            fprintf(f, "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%ld,%llu,%u,%u\n", r->name,
                    (unsigned long long)r->cpu, (unsigned long long)r->pcpu, (unsigned long long)r->cog, (unsigned long long)r->wall,
                    (unsigned long long)r->jbytes, (unsigned long long)r->records,
                    (unsigned long long)r->c.lookups, (unsigned long long)r->c.hits,
                    (unsigned long long)r->c.misses, (unsigned long long)r->c.arrange,
                    (unsigned long long)r->c.snapshots, (unsigned long long)r->c.plan_views,
                    (unsigned long long)r->c.admits, (unsigned long long)r->c.refusals,
                    (unsigned long long)r->c.bad, r->rss_kib, (unsigned long long)r->order,
                    r->objects_live, r->reactions_active);
        }
        fclose(f);
    }
    /* Deterministic summary: counts and state digests only (no time). */
    char edh[65], lin[65], ch[65];
    hex(g_final_ed, 32, edh);
    hex(g_lineage, 32, lin);
    hex(g_cx.chain, 32, ch);
    snprintf(p, sizeof p, "%s/summary-%s.json", g_out, g_mode);
    f = fopen(p, "w");
    if (f) {
        fprintf(f, "{\"mode\":\"%s\",\"checks\":%d,\"failed\":%d,\"bad_rows\":%llu,\"records\":%llu,"
                   "\"records_at_open\":%llu,\"entity_digest\":\"%s\",\"templates\":%u,\"rebuilt\":%u,"
                   "\"rebuild_refused\":%u,\"promotions_seen\":%u,\"restore_refused\":%d,"
                   "\"lookups\":%llu,\"hits\":%llu,\"misses\":%llu,\"arrange_calls\":%llu,"
                   "\"snapshot_calls\":%llu,\"admits\":%llu,\"refusals\":%llu,\"cog_gen_refused\":%d,"
                   "\"probe_accepted_before_correction\":%d,\"admission_refused_ok\":%d,"
                   "\"after_restart_hits\":%u,\"after_restart_arrange\":%u,\"after_restart_ok\":%u,"
                   "\"wrong_answers\":%u,\"idle_ms\":%u,\"idle_run\":%d}\n",
                g_mode, g_checks, g_fail, (unsigned long long)C.bad, (unsigned long long)g_cx.n,
                (unsigned long long)g_n_at_open, edh, g_cache.n, g_rebuilt, g_rebuild_refused,
                g_promotions_seen, g_restore_refused, (unsigned long long)C.lookups,
                (unsigned long long)C.hits, (unsigned long long)C.misses, (unsigned long long)C.arrange,
                (unsigned long long)C.snapshots, (unsigned long long)C.admits,
                (unsigned long long)C.refusals, g_cog_refused, g_probe_accepted, g_admit_refused_ok,
                g_after_hits, g_after_arrange, g_after_ok, g_wrong, IDLE_MS, g_idle.idle_run);
        fclose(f);
    }
    /* Session identity values (journal digests carry World timestamps: per run). */
    snprintf(p, sizeof p, "%s/identity-%s.json", g_out, g_mode);
    f = fopen(p, "w");
    if (f) {
        fprintf(f, "{\"lineage\":\"%s\",\"chain\":\"%s\",\"records\":%llu,\"entity_digest\":\"%s\","
                   "\"idle_proc_cpu_ns\":%llu,\"idle_vol_ctxsw\":%llu,\"idle_invol_ctxsw\":%llu,"
                   "\"idle_rchar\":%llu,\"idle_wchar\":%llu,\"idle_ms_measured\":%llu,\"rebuild_cpu_ns\":%llu}\n",
                lin, ch, (unsigned long long)g_cx.n, edh, (unsigned long long)g_idle.idle_pcpu,
                (unsigned long long)g_idle.idle_vcsw, (unsigned long long)g_idle.idle_ivcsw,
                (unsigned long long)g_idle.idle_rchar, (unsigned long long)g_idle.idle_wchar,
                (unsigned long long)g_idle.idle_ms_measured, (unsigned long long)g_rebuild_cpu);
        fclose(f);
    }
}

/* ---- the unchanged-state control ---- */

static void io_chars(uint64_t *r, uint64_t *w) {
    *r = *w = 0;
    FILE *f = fopen("/proc/self/io", "r");
    if (!f) return;
    char line[128];
    unsigned long long v;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "rchar: %llu", &v) == 1) *r = v;
        if (sscanf(line, "wchar: %llu", &v) == 1) *w = v;
    }
    fclose(f);
}

/* No admitted events for IDLE_MS. Counters must stay 0 and the World must commit
 * nothing (journal records unchanged). poll_ms > 0 is the IDLEPOLL mutant. */
static void idle_interval(uint32_t poll_ms) {
    step_begin();
    struct rusage a, b;
    uint64_t r0, w0, r1, w1;
    getrusage(RUSAGE_SELF, &a);
    io_chars(&r0, &w0);
    uint64_t t0 = wall_ns(), deadline = t0 + (uint64_t)IDLE_MS * 1000000ull;
    for (;;) {
        uint64_t now = wall_ns();
        if (now >= deadline) break;
        uint64_t left = deadline - now, nap = poll_ms ? (uint64_t)poll_ms * 1000000ull : left;
        if (nap > left) nap = left;
        struct timespec ts = { (time_t)(nap / 1000000000ull), (long)(nap % 1000000000ull) };
        while (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts) == EINTR) {}
        if (poll_ms) { Screen s; project("poll", &s); }
    }
    uint64_t t1 = wall_ns();
    getrusage(RUSAGE_SELF, &b);
    io_chars(&r1, &w1);
    Row *r = step_end("6_idle_control");
    g_idle.idle_run = 1;
    g_idle.idle_pcpu = r->pcpu;
    g_idle.idle_vcsw = (uint64_t)(b.ru_nvcsw - a.ru_nvcsw);
    g_idle.idle_ivcsw = (uint64_t)(b.ru_nivcsw - a.ru_nivcsw);
    g_idle.idle_rchar = r1 - r0;
    g_idle.idle_wchar = w1 - w0;
    g_idle.idle_ms_measured = (t1 - t0) / 1000000ull;
    uint32_t bad0 = (uint32_t)C.bad;
    CHECK(r->c.lookups == 0 && r->c.arrange == 0 && r->c.plan_views == 0, "idle: no plan work");
    CHECK(r->c.snapshots == 0, "idle: no projection calls (%llu)", (unsigned long long)r->c.snapshots);
    CHECK(r->c.admits == 0 && r->records == 0, "idle: World committed nothing (%llu records)",
          (unsigned long long)r->records);
    r->c.bad += C.bad - bad0;
}

/* ---- modes ---- */

static int open_journal(int must_be_new) {
    if (must_be_new) unlink(g_jpath);
    int rc = cx_open(&g_cx, g_jpath, RX_CORTEX_SUBJECTS, CX_OPEN_SYNC);
    if (rc != CX_OK) return rc;
    g_n_at_open = g_cx.n;
    return CX_OK;
}

static int read_expect(const char *key, char *out, size_t n) {
    FILE *f = fopen(g_expect_path, "r");
    if (!f) return -1;
    static char buf[4096];
    size_t k = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[k] = 0;
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    char *p = strstr(buf, pat);
    if (!p) return -1;
    p += strlen(pat);
    if (*p == '"') p++;
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' && i + 1 < n) out[i++] = *p++;
    out[i] = 0;
    return 0;
}

/* Process 1: steps 1 to 4 and 6. shadow / viewwrite / idlepoll select a mutant. */
static int run_pilot(int shadow, int viewwrite, uint32_t idlepoll) {
    if (open_journal(1) != CX_OK) { fprintf(stderr, "journal open failed\n"); return 2; }
    if (boot() != 0) { fprintf(stderr, "boot failed\n"); return 2; }
    if (rx_cortex_attach(&E.w, &g_cx, SESSION_1) != RX_OK) { fprintf(stderr, "attach failed\n"); return 2; }
    rx_world_wait_quiescent(&E.w, 30000);
    memcpy(g_lineage, cx_get(&g_cx, 1)->digest, 32);
    rx_plan_cache_init(&g_cache);
    CHECK(observe(0) == 0, "boot state observed");

    /* Step 1: admit one objective, view attached. An unauthorized attempt is refused first. */
    step_begin();
    uint64_t n0 = g_cx.n;
    int64_t bad_rc;
    objective_mutations(0, E.ext_goal_ro, &bad_rc);
    rx_world_wait_quiescent(&E.w, 30000);
    g_admit_refused_ok = bad_rc < 0 && g_cx.n == n0;
    if (bad_rc < 0) C.refusals++;
    CHECK(g_admit_refused_ok, "unauthorized admission refused and not recorded (rc %lld)", (long long)bad_rc);
    CHECK(admit(0) == 0, "objective 0 admitted");
    view_attach("1_attached");
    step_end("1_admit_attached");

    /* Step 2: view detached; the entity completes the authorized work. */
    step_begin();
    uint64_t snaps = C.snapshots;
    Out o;
    int rc = solve(&o, 0);
    CHECK(rc == 0 && o.path == 1 && o.success && o.goal, "objective 0 solved by search (rc %d)", rc);
    CHECK(C.snapshots == snaps, "no projection while detached");
    step_end("2_detached_execute");

    /* Step 3: reattach. */
    step_begin();
    view_reattach("3_reattached", 1);
    if (viewwrite) {
        /* Mutant: the view edits its copy. The runtime digest must not move; the edited
         * view must then fail the projection check. */
        uint8_t d0[32], d1[32];
        rx_world_digest(&E.w, d0);
        g_screen.v.digest[10] ^= 1;
        snprintf(g_screen.v.objects[0].id, sizeof g_screen.v.objects[0].id, "rx:0@999");
        g_screen.support[0] ^= 1;
        rx_world_digest(&E.w, d1);
        CHECK(memcmp(d0, d1, 32) == 0, "viewwrite: runtime digest unchanged by a view edit");
        char hx[72] = "sha256:";
        hex(d1, 32, hx + 7);
        CHECK(strcmp(hx, g_screen.v.digest) == 0, "viewwrite: edited view still equals the runtime");
        plan_view(&g_v2);
        CHECK(g_screen.support[0] == g_v2.o[E.unit[0].id].field[PLA_F_SUPPORT], "viewwrite: edited outcome");
    }
    step_end("3_reattach_check");

    /* Step 4: correction. Counterfactual probe first: without it the shortcut would apply. */
    step_begin();
    CHECK(admit(1) == 0, "objective 1 admitted");
    Out pr;
    solve(&pr, 1);
    g_probe_accepted = pr.path == 2;
    CHECK(g_probe_accepted, "before the correction the retained template applies");
    RxMutation cm = { E.env.obj[PL_ENV_COGNITION], 0, COG_GEN + 1 };
    int64_t crc = rx_world_publish_external(&E.w, E.ext_cog, &cm, 1);
    rx_world_wait_quiescent(&E.w, 30000);
    CHECK(crc > 0, "correction admitted");
    if (crc > 0) C.admits++;
    CHECK(observe(0) == 0, "state observed after the correction");
    if (shadow) {
        /* Mutant: retained knowledge written outside the journal/commit path. */
        g_cache.t[0]->verified_cog_gen = COG_GEN + 1;
    }
    view_attach("4_correction_admitted");
    rc = solve(&o, 0);
    g_cog_refused = o.refused_axis == PL_AX_COG_GEN;
    if (!shadow) {
        CHECK(g_cog_refused, "retained template refused on the cognitive generation (axis %s)",
              rx_plan_axis_name(o.refused_axis));
        CHECK(rc == 0 && o.path == 1 && o.success && o.goal, "corrected objective re-derived and committed");
        CHECK(o.t && o.t->verified_cog_gen == COG_GEN + 1 && o.t->status == PL_STATUS_CANDIDATE,
              "template renewed under the corrected generation");
    } else {
        CHECK(rc == 0 && o.goal, "shadow: objective still committed");
    }
    view_reattach("4_corrected", 1);
    step_end("4_correction");

    /* Step 6: unchanged-state control. */
    idle_interval(idlepoll);

    /* Close: journal flushed (CX_OPEN_SYNC), process state dropped by exiting. */
    step_begin();
    rx_world_wait_quiescent(&E.w, 30000);
    entity_digest_world(g_final_ed);
    shutdown_entity();
    step_end("close");
    return 0;
}

/* Process 2, steps 5a and 5b: replay the journal, rebuild theta (unless cold or lazy), boot,
 * restore, check identity against process 1. cold = ignore
 * the template records (the no-retained-knowledge control). */
static int restart_open(int cold) {
    step_begin();
    int orc = cx_open(&g_cx, g_jpath, RX_CORTEX_SUBJECTS, CX_OPEN_SYNC);
    if (orc != CX_OK) {
        /* Refused journal: start from empty retained knowledge in a fresh journal. */
        g_restore_refused = 1;
        C.refusals++;
        fprintf(stderr, "  journal refused by cx_open (rc %d); starting empty\n", orc);
        char alt[600];
        snprintf(alt, sizeof alt, "%s.fallback", g_jpath);
        unlink(alt);
        if (cx_open(&g_cx, alt, RX_CORTEX_SUBJECTS, CX_OPEN_SYNC) != CX_OK) return 2;
    }
    g_n_at_open = g_cx.n;
    uint8_t chain_at_open[32];
    memcpy(chain_at_open, g_cx.chain, 32);
    if (boot() != 0) return 2;
    rx_plan_cache_init(&g_cache);
    uint32_t missing = 0;
    uint8_t ej[32];
    static uint64_t restore_f[N_ROLES][RX_MAX_FIELDS];
    static int restore_ok[N_ROLES];
    if (!g_restore_refused) {
        uint64_t b0 = cpu_ns();
        if (!cold && !A->lazy) theta_rebuild(&g_cx);
        A->lazy_pending = A->lazy;
        g_rebuild_cpu = cpu_ns() - b0;
        entity_digest_journal(&g_cx, ej, &missing);
        for (uint32_t r = 0; r < N_ROLES; r++) restore_ok[r] = journal_fields(&g_cx, role(r), restore_f[r]);
    }
    step_end("5a_replay_rebuild");

    step_begin();
    if (rx_cortex_attach(&E.w, &g_cx, SESSION_2) != RX_OK) return 2;
    rx_world_wait_quiescent(&E.w, 30000);
    /* Restore: publish each role's committed values through the outside path. */
    for (uint32_t r = 0; r < N_ROLES && !g_restore_refused; r++) {
        if (!restore_ok[r]) continue;
        RxObject ob;
        if (rx_world_read(&E.w, role(r), &ob) != RX_OK) continue;
        RxMutation m[RX_MAX_FIELDS];
        uint32_t n = 0;
        for (uint32_t f = 0; f < RX_MAX_FIELDS; f++)
            if (ob.field[f] != restore_f[r][f]) m[n++] = (RxMutation){ role(r), f, restore_f[r][f] };
        if (!n) continue;
        RxCapRef cap = r < N_UNITS ? E.ext_unit[r]
                     : role(r).id == E.decision.id ? E.ext_decision
                     : role(r).id == E.index.id ? E.ext_index
                     : role(r).id == E.env.obj[PL_ENV_COGNITION].id ? E.ext_cog
                     : (role(r).id == E.env.obj[PL_ENV_GOAL].id || role(r).id == E.env.obj[PL_ENV_FACTS].id) ? E.ext_goal
                     : E.ext_env;
        CHECK(rx_world_publish_external(&E.w, cap, m, n) > 0, "restore role %u", r);
    }
    rx_world_wait_quiescent(&E.w, 30000);
    CHECK(observe(0) == 0, "restored state observed");
    memcpy(g_lineage, cx_get(&g_cx, 1)->digest, 32);
    view_attach("5_restart_attached");
    /* Identity against process 1 (the expectations file is the checker's oracle; the
     * restore above did not read it). */
    uint8_t ew[32];
    char ewh[65], ejh[65], lin[65], ch[65], want[80];
    entity_digest_world(ew);
    hex(ew, 32, ewh);
    hex(ej, 32, ejh);
    hex(g_lineage, 32, lin);
    hex(chain_at_open, 32, ch);
    CHECK(!g_restore_refused, "journal accepted by cx_open");
    CHECK(missing == 0, "every role has a committed record (%u missing)", missing);
    CHECK(read_expect("entity_digest", want, sizeof want) == 0 && strcmp(want, ewh) == 0,
          "entity digest after restore equals process 1 (%s)", ewh);
    CHECK(strcmp(ewh, ejh) == 0, "restored World equals the journal's committed state");
    CHECK(read_expect("lineage", want, sizeof want) == 0 && strcmp(want, lin) == 0, "lineage equals process 1");
    CHECK(read_expect("chain", want, sizeof want) == 0 && strcmp(want, ch) == 0, "chain at open equals process 1");
    CHECK(read_expect("records", want, sizeof want) == 0 && strtoull(want, NULL, 10) == g_n_at_open,
          "history count equals process 1 (%llu)", (unsigned long long)g_n_at_open);
    {
        RxObject cg;
        CHECK(rx_world_read(&E.w, E.env.obj[PL_ENV_COGNITION], &cg) == RX_OK && cg.field[0] == COG_GEN + 1,
              "the correction survived the restart");
    }
    if (!cold && !A->lazy && !g_restore_refused)
        CHECK(g_rebuilt >= 1 && g_rebuild_refused == 0 && theta_check("restart") == 0,
              "retained knowledge rebuilt from the journal (%u)", g_rebuilt);
    step_end("5b_restore_identity");
    return 0;
}

/* Steps 1 to 3 of the pilot for one repeated objective k after the restart. */
static void restart_objective(uint32_t k) {
    char tag[48];
    step_begin();
    CHECK(admit(k) == 0, "objective %u admitted", k);
    snprintf(tag, sizeof tag, "5_obj%u_attached", k);
    view_attach(tag);
    uint64_t snaps = C.snapshots;
    Out o;
    int rc = solve(&o, 0);
    CHECK(C.snapshots == snaps, "no projection while detached (objective %u)", k);
    int ok = rc == 0 && o.success && o.goal;
    g_after_ok += ok;
    g_wrong += rc == 0 && !o.goal;
    if (o.path == 2) g_after_hits++;
    snprintf(tag, sizeof tag, "5_obj%u_reattached", k);
    view_reattach(tag, 1);
    snprintf(tag, sizeof tag, "5_obj%u", k);
    Row *r = step_end(tag);
    RxFootprint fp;
    rx_world_footprint(&E.w, &fp);
    r->objects_live = fp.objects_live;
    r->reactions_active = fp.reactions_active;
}

/* After n repeated objectives: outcome checks, then the World shuts down. */
static void restart_finish(int cold, uint32_t n) {
    entity_digest_world(g_final_ed);
    g_after_arrange = (uint32_t)C.arrange;
    CHECK(g_after_ok == n && g_wrong == 0, "every repeated objective committed correctly");
    if (!cold && !g_restore_refused)
        CHECK(g_after_hits == n && g_after_arrange == 0, "retained knowledge used: %u hits, %u searches",
              g_after_hits, g_after_arrange);
    if (A->lazy && !g_restore_refused)
        CHECK(!A->lazy_pending && g_rebuilt >= 1 && g_rebuild_refused == 0 && theta_check("lazy") == 0,
              "lazy arm: retained knowledge rebuilt by the first lookup (%u)", g_rebuilt);
    if (A->horizon && cold && !A->forget && !g_restore_refused)
        CHECK(g_after_arrange == 1 && g_after_hits == n - 1, "cold arm learns once in its session: %u hits, %u searches",
              g_after_hits, g_after_arrange);
    if (A->forget)
        CHECK(g_after_arrange == n && g_after_hits == 0 && g_cache.n == 0, "forget arm keeps nothing: %u hits, %u searches",
              g_after_hits, g_after_arrange);
    step_begin();
    shutdown_entity();
    step_end("close");
}

/* Process 2: restart on the same journal; steps 5 and repeated objectives. cold = ignore
 * the template records (the no-retained-knowledge control). */
static int run_restart(int cold) {
    int rc = restart_open(cold);
    if (rc) return rc;
    for (uint32_t i = 0; i < N_AFTER; i++) restart_objective(2 + i);
    restart_finish(cold, N_AFTER);
    return 0;
}

/* Flip one payload word of the newest template record in the journal file. */
static int tamper_file(void) {
    CxStore s;
    if (cx_open(&s, g_jpath, RX_CORTEX_SUBJECTS, CX_OPEN_READONLY) != CX_OK) return -1;
    uint64_t off = 32, target = 0, at = 0;
    for (uint64_t id = 1; id <= s.n; id++) {
        const CxObject *o = cx_get(&s, id);
        if (o->tag == REC_TAG && o->kind == CX_K_CANDIDATE) { target = id; at = off + 13 * 8 + 9 * 8; }
        off += (13u + o->n + 4u) * 8u;
    }
    /* In-memory tamper: cx_verify_chain must notice (cx_tamper never reaches the file). */
    if (target) cx_tamper(&s, target, 9, cx_payload(&s, cx_get(&s, target))[9] ^ 1u);
    int chain_bad = cx_verify_chain(&s) != CX_OK;
    cx_close(&s);
    CHECK(chain_bad, "tamper: cx_verify_chain notices an in-memory edit");
    if (!target) return -1;
    int fd = open(g_jpath, O_RDWR);
    if (fd < 0) return -1;
    uint64_t v;
    if (pread(fd, &v, 8, (off_t)at) != 8) { close(fd); return -1; }
    v ^= 1u;
    int ok = pwrite(fd, &v, 8, (off_t)at) == 8;
    close(fd);
    fprintf(stderr, "  tamper: flipped bit 0 of record %llu payload word 9 at byte %llu\n",
            (unsigned long long)target, (unsigned long long)at);
    return ok ? 0 : -1;
}

/* ---- horizon mode (evidence/ENTITY-276/h40/FREEZE.md) ----
 * Several arms restart from copies of one pilot journal inside this process, each with its
 * own World, journal and plan cache: warm (theta rebuilt at boot), cold (template records
 * ignored, may learn in its own session, the earlier control), lazy (theta rebuilt by the
 * first lookup) and forget (never stores a template). Then H repeated objectives per arm. */

static int copy_file(const char *from, const char *to) {
    FILE *a = fopen(from, "rb"), *b = a ? fopen(to, "wb") : NULL;
    static char buf[1u << 16];
    size_t n;
    int ok = a && b;
    while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0) ok = fwrite(buf, 1, n, b) == n;
    if (a) fclose(a);
    if (b && fclose(b) != 0) ok = 0;
    return ok ? 0 : -1;
}

/* Arm at position j of objective i: a balanced (Williams) order, so for every pair of arms
 * each goes first equally often over n objectives (2n for odd n). Row 0 is 0, 1, n-1, 2, n-2, ... */
static uint32_t arm_at(uint32_t n, uint32_t i, uint32_t j) {
    if (n % 2 && (i / n) % 2) j = n - 1 - j;
    uint32_t f = j == 0 ? 0 : j % 2 ? (j + 1) / 2 : n - j / 2;
    return (f + i) % n;
}

static int run_horizon(uint32_t h, int interleave, int lazy, int forget, int release, const char *base,
                       const char *out) {
    static const char *names[MAX_ARMS];
    const char *hm = getenv("ENTITY276_HMUTANT");
    int hmutant = !hm ? HM_NONE : !strcmp(hm, "FORGETSTORE") ? HM_FORGETSTORE : !strcmp(hm, "LAZYSKIP") ? HM_LAZYSKIP
                : !strcmp(hm, "COLDKEEP") ? HM_COLDKEEP : !strcmp(hm, "DOUBLERELEASE") ? HM_DOUBLERELEASE : -1;
    if (hmutant < 0) return 2;
    uint32_t n = 0;
    names[n++] = "warm";
    names[n++] = "cold";
    if (lazy) names[n++] = "lazy";
    if (forget) names[n++] = "forget";
    for (uint32_t a = 0; a < n; a++) {
        A = &g_arm[a];
        snprintf(A->obuf, sizeof A->obuf, "%s/%s", out, names[a]);
        mkdir(A->obuf, 0755);
        snprintf(A->jbuf, sizeof A->jbuf, "%s/journal.cx", A->obuf);
        if (copy_file(base, A->jbuf) != 0) { fprintf(stderr, "copy %s failed\n", base); return 2; }
        A->out = A->obuf;
        A->jpath = A->jbuf;
        snprintf(A->mode, sizeof A->mode, "horizon-%s", names[a]);
        A->cold = !strcmp(names[a], "cold") || !strcmp(names[a], "forget");
        A->lazy = !strcmp(names[a], "lazy");
        A->forget = !strcmp(names[a], "forget");
        A->horizon = 1;
        A->release = release;
        A->hmutant = hmutant;
    }
    for (uint32_t a = 0; a < n; a++) {
        A = &g_arm[a];
        int rc = restart_open(A->cold);
        if (A->hmutant == HM_LAZYSKIP && A->lazy) A->lazy_pending = 0;
        if (A->hmutant == HM_COLDKEEP && A->cold && !A->forget) theta_rebuild(&g_cx);
        if (rc) { fprintf(stderr, "%s: restart failed (%d)\n", A->mode, rc); return rc; }
    }
    if (interleave)
        for (uint32_t i = 0; i < h; i++)
            for (uint32_t j = 0; j < n; j++) { A = &g_arm[arm_at(n, i, j)]; restart_objective(2 + i); }
    else
        for (uint32_t a = 0; a < n; a++) {
            A = &g_arm[a];
            for (uint32_t i = 0; i < h; i++) restart_objective(2 + i);
        }
    for (uint32_t a = 0; a < n; a++) { A = &g_arm[a]; restart_finish(A->cold, h); }
    int bad = 0;
    for (uint32_t a = 0; a < n; a++) {
        A = &g_arm[a];
        write_outputs();
        printf("entity276 %s H %u: bad_rows %llu records %llu searches %u hits %u rebuild_cpu_ns %llu\n", g_mode, h,
               (unsigned long long)C.bad, (unsigned long long)g_cx.n, g_after_arrange, g_after_hits,
               (unsigned long long)g_rebuild_cpu);
        bad |= C.bad != 0;
        cx_close(&g_cx);
    }
    printf("entity276 horizon H %u arms %u interleave %d release %d: checks %d failed %d\n", h, n, interleave, release,
           g_checks, g_fail);
    return bad || g_fail ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: entity276 pilot|restart|restart-cold|control-idle|tamper-file JOURNAL "
                        "[EXPECT_IDENTITY_JSON]\n"
                        "       entity276 mutant SHADOW|VIEWWRITE|IDLEPOLL JOURNAL\n"
                        "       entity276 horizon H PILOT_JOURNAL EXPECT_IDENTITY_JSON [--interleave] "
                        "[--lazy-rebuild] [--forget] [--release]\n"
                        "env PILOT_OUT = output directory\n");
        return 2;
    }
    g_out = getenv("PILOT_OUT") ? getenv("PILOT_OUT") : ".";
    meminfo(&g_load0, &g_mem0);
    const char *mode = argv[1];
    int rc = 0;
    if (!strcmp(mode, "horizon")) {
        if (argc < 5) return 2;
        uint32_t h = (uint32_t)strtoul(argv[2], NULL, 10);
        if (h < 1 || h + 3 > MAX_ROWS || h > MAX_RUNS) return 2;
        int il = 0, lz = 0, fg = 0, rl = 0;
        for (int i = 5; i < argc; i++) {
            if (!strcmp(argv[i], "--interleave")) il = 1;
            else if (!strcmp(argv[i], "--lazy-rebuild")) lz = 1;
            else if (!strcmp(argv[i], "--forget")) fg = 1;
            else if (!strcmp(argv[i], "--release")) rl = 1;
            else return 2;
        }
        snprintf(g_expect_path, sizeof g_expect_path, "%s", argv[4]);
        return run_horizon(h, il, lz, fg, rl, argv[3], g_out);
    }
    if (!strcmp(mode, "mutant")) {
        if (argc < 4) return 2;
        snprintf(g_mode, sizeof g_mode, "mutant-%s", argv[2]);
        g_jpath = argv[3];
        if (!strcmp(argv[2], "SHADOW")) rc = run_pilot(1, 0, 0);
        else if (!strcmp(argv[2], "VIEWWRITE")) rc = run_pilot(0, 1, 0);
        else if (!strcmp(argv[2], "IDLEPOLL")) rc = run_pilot(0, 0, 100);
        else return 2;
    } else {
        snprintf(g_mode, sizeof g_mode, "%s", mode);
        g_jpath = argv[2];
        if (argc > 3) snprintf(g_expect_path, sizeof g_expect_path, "%s", argv[3]);
        if (!strcmp(mode, "pilot")) rc = run_pilot(0, 0, 0);
        else if (!strcmp(mode, "restart")) rc = run_restart(0);
        else if (!strcmp(mode, "restart-cold")) rc = run_restart(1);
        else if (!strcmp(mode, "control-idle")) {
            if (open_journal(1) != CX_OK || boot() != 0 || rx_cortex_attach(&E.w, &g_cx, SESSION_1) != RX_OK) return 2;
            rx_world_wait_quiescent(&E.w, 30000);
            memcpy(g_lineage, cx_get(&g_cx, 1)->digest, 32);
            idle_interval(0);
            entity_digest_world(g_final_ed);
            shutdown_entity();
        } else if (!strcmp(mode, "dump")) {
            /* Read-only listing of the journal: id, class, kind, subject, object, outputs. */
            CxStore s;
            if (cx_open(&s, g_jpath, RX_CORTEX_SUBJECTS, CX_OPEN_READONLY) != CX_OK) return 2;
            for (uint64_t id = 1; id <= s.n; id++) {
                CxRecord r;
                CxWorldRecord wr;
                cx_recall_id(&s, id, &r);
                int isw = cx_world_decode(&r, &wr) == CX_OK;
                printf("%llu cls %u kind %#x subj %llu", (unsigned long long)id, r.hdr.cls, r.hdr.kind,
                       (unsigned long long)r.hdr.subject);
                if (isw) printf(" crumb_kind %u obj %llu outs %u ins %u f0 %#llx", wr.crumb_kind,
                                (unsigned long long)wr.obj, wr.n_outputs, wr.n_inputs,
                                (unsigned long long)wr.field[0]);
                printf("\n");
            }
            cx_close(&s);
            return 0;
        } else if (!strcmp(mode, "tamper-file")) {
            rc = tamper_file() == 0 ? 0 : 2;
            printf("tamper-file %s checks %d bad %llu\n", rc ? "FAILED" : "done", g_checks,
                   (unsigned long long)C.bad);
            return rc ? rc : (C.bad ? 1 : 0);
        } else return 2;
    }
    write_outputs();
    printf("entity276 %s: checks %d failed %d bad_rows %llu records %llu\n", g_mode, g_checks, g_fail,
           (unsigned long long)C.bad, (unsigned long long)g_cx.n);
    cx_close(&g_cx);
    if (rc) return rc;
    return C.bad ? 1 : 0;
}
