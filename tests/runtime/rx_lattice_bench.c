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
 * rx_lattice host benchmark (omega-mv #698). AArch64 (GB10 host CPUs); no GPU.
 *
 * Same objective graph, same admitted candidate set, same lanes for every
 * policy: serial, lcf (lowest predicted cost first, dependency-driven) and
 * q3d. Lanes are real threads pinned to two Cortex-X925 and two Cortex-A725
 * cores, chosen as the quietest of their class at start. The simulated
 * accelerator realization is declared but not FORGE-verified here, so the
 * lattice refuses it; no GPU work is done.
 *
 * The cost model is rx_costmodel, fitted from this program's own timed runs
 * of every verified arm on each core class over a training grid of shapes
 * that differs from the graph's shapes. The benchmark runs are separate from
 * the calibration runs.
 *
 * Reported per policy, over rounds in rotating policy order after one
 * discarded warm-up round: objective wall time (clock_gettime MONOTONIC,
 * includes every pick, dispatch, verification and receipt), selection
 * overhead (time inside rx_lat_pick / rx_lat_finish), summed lane busy time,
 * realized critical path (longest dependency chain of measured node times),
 * per-lane-class utilization, and output equality with serial. Nothing
 * here measures energy, memory traffic or GPU utilization; none is claimed.
 */
#include "rx_lattice_fixture.h"

#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ---- cores ---- */

static int g_cpus[RX_CM_CORES][64], g_ncpu[RX_CM_CORES];

static void discover(void) {
    long n = sysconf(_SC_NPROCESSORS_CONF);
    for (long c = 0; c < n && c < 64; c++) {
        char path[128];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%ld/regs/identification/midr_el1", c);
        FILE *fp = fopen(path, "r");
        unsigned long long midr = 0;
        int ok = fp && fscanf(fp, "%llx", &midr) == 1;
        if (fp) fclose(fp);
        uint32_t part = ok ? (uint32_t)((midr >> 4) & 0xfffu) : 0;
        uint32_t k = part == 0xd85u ? RX_CM_CORE_X925 : part == 0xd87u ? RX_CM_CORE_A725 : RX_CM_CORE_OTHER;
        g_cpus[k][g_ncpu[k]++] = (int)c;
    }
}

static void cpu_busy(double *busy) {
    unsigned long long a[2][64][2];
    memset(a, 0, sizeof a);
    for (int s = 0; s < 2; s++) {
        FILE *fp = fopen("/proc/stat", "r");
        char line[512];
        while (fp && fgets(line, sizeof line, fp)) {
            int c;
            unsigned long long u, ni, sy, id, io, irq, sirq, st;
            if (sscanf(line, "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu", &c, &u, &ni, &sy, &id, &io, &irq,
                       &sirq, &st) == 9 && c >= 0 && c < 64) {
                a[s][c][0] = u + ni + sy + irq + sirq + st;
                a[s][c][1] = a[s][c][0] + id + io;
            }
        }
        if (fp) fclose(fp);
        if (s == 0) nanosleep(&(struct timespec){ 0, 200000000 }, NULL);
    }
    for (int c = 0; c < 64; c++) {
        unsigned long long tot = a[1][c][1] - a[0][c][1];
        busy[c] = tot ? (double)(a[1][c][0] - a[0][c][0]) / (double)tot : 1.0;
    }
}

/* The two quietest cpus of class k. */
static void quietest2(uint32_t k, const double *busy, int out[2]) {
    out[0] = out[1] = -1;
    for (int i = 0; i < g_ncpu[k]; i++) {
        int c = g_cpus[k][i];
        if (out[0] < 0 || busy[c] < busy[out[0]]) {
            out[1] = out[0];
            out[0] = c;
        } else if (out[1] < 0 || busy[c] < busy[out[1]]) {
            out[1] = c;
        }
    }
}

static int pin_self(int cpu) {
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    return pthread_setaffinity_np(pthread_self(), sizeof s, &s);
}

static void loadavg(char *out, size_t n) {
    FILE *fp = fopen("/proc/loadavg", "r");
    out[0] = 0;
    if (fp) {
        if (!fgets(out, (int)n, fp)) out[0] = 0;
        fclose(fp);
    }
    size_t l = strlen(out);
    if (l && out[l - 1] == '\n') out[l - 1] = 0;
}

/* ---- calibration: rx_costmodel from our own timed runs ---- */

static RxCostModel g_cm;
static uint32_t g_obs;

static uint64_t time_ps_per_call(uint32_t arm, const uint64_t *A, const uint64_t *x, uint64_t *y, uint32_t M,
                                 uint32_t N) {
    uint64_t calls = 400000ull / ((uint64_t)M * N + 64u);
    if (calls < 3) calls = 3;
    uint64_t best = UINT64_MAX;
    for (int chunk = 0; chunk < 3; chunk++) {
        uint64_t t0 = now_ns();
        for (uint64_t c = 0; c < calls; c++) fx_fn[arm](A, x, y, M, N);
        uint64_t ps = (now_ns() - t0) * 1000u / calls;
        if (ps < best) best = ps;
    }
    return best ? best : 1;
}

static int calibrate(const int cpu[RX_CM_CORES][2]) {
    static const uint32_t TM[] = { 16, 64, 128, 256, 512 };
    static const uint32_t TN[] = { 16, 33, 64, 100, 200, 257, 300, 400 };
    static const uint64_t CACHE[RX_CM_CORES] = { 2u << 20, 512u << 10, 1u << 20 };
    static uint64_t A[512 * 400], x[400], y[512], yr[512];
    uint64_t s = 0xCA1Bu;
    for (size_t i = 0; i < sizeof A / sizeof A[0]; i++) A[i] = fx_xs(&s);
    for (size_t i = 0; i < 400; i++) x[i] = fx_xs(&s);
    rx_cm_init(&g_cm, N_ARMS);
    for (uint32_t core = RX_CM_CORE_X925; core <= RX_CM_CORE_A725; core++) {
        if (pin_self(cpu[core][0]) != 0) return -1;
        for (unsigned a = 0; a < sizeof TM / sizeof TM[0]; a++)
            for (unsigned b = 0; b < sizeof TN / sizeof TN[0]; b++)
                for (uint32_t r = 0; r < 3; r++) {   /* rotate arm order per shape */
                    uint32_t arm = (r + a + b) % 3u;  /* ARM_REF, ARM_SCALAR, ARM_QUAD4 */
                    if (!fx_verified[arm]) continue;
                    RxCmFeatures f;
                    memset(&f, 0, sizeof f);
                    f.op = RX_CM_OP_MATVEC;
                    f.M = TM[a];
                    f.N = TN[b];
                    f.state_bytes = ((uint64_t)TM[a] * TN[b] + TM[a] + TN[b]) * 8u;
                    f.cache_bytes = CACHE[core];
                    f.core = core;
                    f.eligible = 0x7;
                    f.need_evidence = 1;
                    uint64_t ps = time_ps_per_call(arm, A, x, y, TM[a], TN[b]);
                    omega_matvec_reference(A, x, yr, TM[a], TN[b]);
                    RxCmObservation o = { ps, memcmp(y, yr, (size_t)TM[a] * 8u) != 0 };
                    rx_cm_observe(&g_cm, &f, arm, &o);
                    g_obs++;
                }
    }
    return 0;
}

/* ---- lanes ---- */

#define N_LANES 4u
typedef struct {
    pthread_t th;
    uint32_t real, lane;
    int cpu;
    int has_job, quit;
    uint32_t node, cand;
} Lane;

static Lane g_lane[N_LANES];
static RxLattice g_L;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_job = PTHREAD_COND_INITIALIZER, g_cv_done = PTHREAD_COND_INITIALIZER;

typedef struct {
    RxLatReceipt rc;
    uint64_t t_start, t_end;
} Done;
static Done g_doneq[RX_LAT_MAX_NODES];
static uint32_t g_done_head, g_done_tail;

static void *lane_main(void *arg) {
    Lane *ln = (Lane *)arg;
    pin_self(ln->cpu);
    pthread_mutex_lock(&g_mu);
    for (;;) {
        while (!ln->has_job && !ln->quit) pthread_cond_wait(&g_cv_job, &g_mu);
        if (ln->quit) break;
        uint32_t i = ln->node, c = ln->cand;
        pthread_mutex_unlock(&g_mu);
        Done d;
        memset(&d, 0, sizeof d);
        d.t_start = now_ns();
        d.rc.node = (uint16_t)i;
        d.rc.cand = (uint16_t)c;
        d.rc.verdict = fx_execute(&g_L, i, c);
        fx_vec_digest(i, d.rc.output);
        d.t_end = now_ns();
        d.rc.elapsed_ps = (d.t_end - d.t_start) * 1000u;
        rx_lat_receipt_seal(&g_L, &d.rc);
        pthread_mutex_lock(&g_mu);
        ln->has_job = 0;
        g_doneq[g_done_tail++ % RX_LAT_MAX_NODES] = d;
        pthread_cond_broadcast(&g_cv_done);
    }
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

static Lane *lane_for(uint32_t real, uint32_t lane) {
    for (uint32_t t = 0; t < N_LANES; t++)
        if (g_lane[t].real == real && g_lane[t].lane == lane) return &g_lane[t];
    return NULL;
}

/* ---- one measured run ---- */

typedef struct {
    uint64_t wall_ns, select_ns, busy_ns, busy_real_ns[N_REALS], crit_ns;
    uint32_t done, failed, refused, switches, arm_runs[N_ARMS], dispatched[N_REALS];
    uint8_t out[FX_NODES][32];
    uint8_t effect[32];
    int effect_rc;
} Meas;

static int run_measured(uint32_t policy, Meas *m) {
    memset(m, 0, sizeof *m);
    RxLatRun r;
    RxLatLedger g;
    memset(&r, 0, sizeof r);
    memset(&g, 0, sizeof g);
    uint64_t node_ns[FX_NODES];
    memset(node_ns, 0, sizeof node_ns);
    g_done_head = g_done_tail = 0;
    uint64_t t0 = now_ns();
    r.now_ps = 0;
    if (rx_lat_run_init(&g_L, &r, policy) != RX_LAT_OK) return -1;
    pthread_mutex_lock(&g_mu);
    for (;;) {
        RxLatPick p;
        r.now_ps = (now_ns() - t0) * 1000u;
        for (;;) {
            uint64_t ts = now_ns();
            int got = rx_lat_pick(&g_L, &r, &p);
            m->select_ns += now_ns() - ts;
            if (!got) break;
            if (g_L.node[p.node].op == RX_LAT_OP_EFFECT) {
                /* The single writer commits the effect itself, after verified inputs. */
                Done d;
                memset(&d, 0, sizeof d);
                d.t_start = now_ns();
                d.rc.node = (uint16_t)p.node;
                d.rc.cand = (uint16_t)p.cand;
                m->effect_rc = rx_lat_effect_commit(&g, &g_L, &r, p.node, d.rc.output);
                d.rc.verdict = m->effect_rc == RX_LAT_OK ? 0u : 1u;
                d.t_end = now_ns();
                d.rc.elapsed_ps = (d.t_end - d.t_start) * 1000u;
                rx_lat_receipt_seal(&g_L, &d.rc);
                g_doneq[g_done_tail++ % RX_LAT_MAX_NODES] = d;
                continue;
            }
            Lane *ln = lane_for(p.real, p.lane);
            if (!ln || ln->has_job) {
                pthread_mutex_unlock(&g_mu);
                return -2;   /* the lattice handed out a busy or unknown lane */
            }
            if (g_L.node[p.node].op == RX_LAT_OP_MATVEC) m->arm_runs[g_L.method[g_L.cand[p.cand].method].arm]++;
            ln->node = p.node;
            ln->cand = p.cand;
            ln->has_job = 1;
            pthread_cond_broadcast(&g_cv_job);
        }
        if (g_done_head == g_done_tail) {
            if (r.in_flight == 0) break;
            pthread_cond_wait(&g_cv_done, &g_mu);
            continue;
        }
        Done d = g_doneq[g_done_head++ % RX_LAT_MAX_NODES];
        r.now_ps = (now_ns() - t0) * 1000u;
        uint32_t k = g_L.cand[d.rc.cand].real;
        node_ns[d.rc.node] = d.t_end - d.t_start;
        m->busy_ns += node_ns[d.rc.node];
        m->busy_real_ns[k] += node_ns[d.rc.node];
        uint64_t ts = now_ns();
        rx_lat_finish(&g_L, &r, &d.rc);
        m->select_ns += now_ns() - ts;
    }
    pthread_mutex_unlock(&g_mu);
    m->wall_ns = now_ns() - t0;
    /* Realized critical path: longest dependency chain of measured node times. */
    uint64_t fin[FX_NODES];
    for (uint32_t t = 0; t < g_L.n_nodes; t++) {
        uint32_t i = g_L.topo[t];
        uint64_t st = 0;
        for (uint32_t q = 0; q < g_L.node[i].n_deps; q++)
            if (fin[g_L.node[i].dep[q]] > st) st = fin[g_L.node[i].dep[q]];
        fin[i] = st + node_ns[i];
        if (fin[i] > m->crit_ns) m->crit_ns = fin[i];
    }
    m->done = r.done;
    m->failed = r.failed;
    m->refused = r.refused;
    m->switches = r.weight_switches;
    for (uint32_t k = 0; k < N_REALS; k++) m->dispatched[k] = r.dispatched[k];
    memcpy(m->out, r.out, sizeof m->out);
    if (g.n == 1) memcpy(m->effect, g.value[0], 32);
    return rx_lat_settled(&g_L, &r) ? 0 : -3;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static uint64_t median(uint64_t *v, uint32_t n) {
    qsort(v, n, sizeof *v, cmp_u64);
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2u;
}

#define ROUNDS 15u

int main(int argc, char **argv) {
    uint32_t rounds = ROUNDS;
    if (argc > 1) rounds = (uint32_t)strtoul(argv[1], NULL, 10);
    if (rounds < 5) rounds = 5;
    printf("rx_lattice host benchmark (omega-mv #698). Host CPUs only, no GPU.\n");
    discover();
    if (g_ncpu[RX_CM_CORE_X925] < 2 || g_ncpu[RX_CM_CORE_A725] < 2) {
        printf("NOT_RUN: needs two Cortex-X925 and two Cortex-A725 cpus (found %d, %d)\n",
               g_ncpu[RX_CM_CORE_X925], g_ncpu[RX_CM_CORE_A725]);
        return 2;
    }
    if (fx_setup_arms() != 0) {
        printf("FAIL: arm setup\n");
        return 1;
    }
    for (uint32_t a = 0; a < N_ARMS; a++) printf("  arm %-13s %s\n", ARM_NAME[a], fx_verified[a] ? "verified" : "refused");
    double busy[64];
    cpu_busy(busy);
    int cpu[RX_CM_CORES][2];
    quietest2(RX_CM_CORE_X925, busy, cpu[RX_CM_CORE_X925]);
    quietest2(RX_CM_CORE_A725, busy, cpu[RX_CM_CORE_A725]);
    char la[128];
    loadavg(la, sizeof la);
    printf("  lanes: x925 cpu%d (busy %.2f) cpu%d (busy %.2f); a725 cpu%d (busy %.2f) cpu%d (busy %.2f); loadavg %s\n",
           cpu[0][0], busy[cpu[0][0]], cpu[0][1], busy[cpu[0][1]], cpu[1][0], busy[cpu[1][0]], cpu[1][1],
           busy[cpu[1][1]], la);

    uint64_t tc = now_ns();
    if (calibrate((const int (*)[2])cpu) != 0) {
        printf("FAIL: calibration pinning\n");
        return 1;
    }
    printf("  calibration: %u timed observations in %.2f s (training grid, not the graph's shapes)\n", g_obs,
           (double)(now_ns() - tc) / 1e9);

    fx_build(&g_L, 0);
    if (rx_lat_build(&g_L) < 0) {
        printf("FAIL: build\n");
        return 1;
    }
    rx_lat_pin(&g_L, &g_cm);
    RxCmPolicy pol;
    rx_cm_default_policy(&pol);
    int empty = rx_lat_admit(&g_L, &g_cm, &pol);
    printf("  lattice: %u candidates, %d nodes without a candidate;", g_L.n_cands, empty);
    for (uint32_t q = 0; q < RX_LAT_R_COUNT; q++)
        if (g_L.reasons[q]) printf(" %s %u", rx_lat_reason_name(q), g_L.reasons[q]);
    printf("\n  predicted critical path (best admitted estimate per node; caps for unmodelled ops): %.1f us\n",
           (double)rx_lat_critical_path_ps(&g_L) / 1e6);
    if (empty) {
        printf("FAIL: objective not admissible\n");
        return 1;
    }

    for (uint32_t k = 0, t = 0; k < 2; k++)
        for (uint32_t l = 0; l < 2; l++, t++) {
            g_lane[t].real = k;
            g_lane[t].lane = l;
            g_lane[t].cpu = cpu[k][l];
            pthread_create(&g_lane[t].th, NULL, lane_main, &g_lane[t]);
        }
    /* The dispatcher (single writer) runs on the quietest cpu that is not a lane. */
    int disp = -1;
    for (int c = 0; c < 64; c++) {
        int lane_cpu = c == cpu[0][0] || c == cpu[0][1] || c == cpu[1][0] || c == cpu[1][1];
        int known = 0;
        for (uint32_t k = 0; k < RX_CM_CORES; k++)
            for (int i = 0; i < g_ncpu[k]; i++) known |= g_cpus[k][i] == c;
        if (known && !lane_cpu && (disp < 0 || busy[c] < busy[disp])) disp = c;
    }
    if (disp >= 0) pin_self(disp);
    printf("  dispatcher cpu%d\n", disp);

    static uint64_t wall[RX_LAT_POLICY_COUNT][64], sel[RX_LAT_POLICY_COUNT][64], bsy[RX_LAT_POLICY_COUNT][64],
        crit[RX_LAT_POLICY_COUNT][64], util[RX_LAT_POLICY_COUNT][2][64];
    Meas last[RX_LAT_POLICY_COUNT], base;
    int equal_all = 1, fails = 0;
    memset(&base, 0, sizeof base);
    if (run_measured(RX_LAT_POLICY_SERIAL, &base) != 0 || base.done != FX_NODES) fails++;
    {
        uint8_t oracle[FX_NODES][32];
        int ok = 1;
        fx_oracle(&g_L, oracle);
        for (uint32_t i = 0; i < FX_NODES; i++)
            if (i != FX_EFFECT && memcmp(oracle[i], base.out[i], 32)) ok = 0;
        printf("  serial outputs equal the independent reference-only oracle: %s\n", ok ? "yes" : "NO");
        if (!ok) fails++;
    }
    for (uint32_t rd = 0; rd <= rounds; rd++)   /* round 0 is a discarded warm-up */
        for (uint32_t q = 0; q < RX_LAT_POLICY_COUNT; q++) {
            uint32_t p = (q + rd) % RX_LAT_POLICY_COUNT;
            Meas m;
            if (run_measured(p, &m) != 0 || m.done != FX_NODES || m.effect_rc != RX_LAT_OK) fails++;
            if (memcmp(m.out, base.out, sizeof m.out) || memcmp(m.effect, base.effect, 32)) equal_all = 0;
            if (rd == 0) continue;
            uint32_t i = rd - 1;
            wall[p][i] = m.wall_ns;
            sel[p][i] = m.select_ns;
            bsy[p][i] = m.busy_ns;
            crit[p][i] = m.crit_ns;
            for (uint32_t k = 0; k < 2; k++)
                util[p][k][i] = m.busy_real_ns[k] * 1000u / (m.wall_ns * g_L.real[k].lanes);   /* per mille */
            last[p] = m;
        }
    pthread_mutex_lock(&g_mu);
    for (uint32_t t = 0; t < N_LANES; t++) g_lane[t].quit = 1;
    pthread_cond_broadcast(&g_cv_job);
    pthread_mutex_unlock(&g_mu);
    for (uint32_t t = 0; t < N_LANES; t++) pthread_join(g_lane[t].th, NULL);

    loadavg(la, sizeof la);
    printf("  MEASURED, %u rounds after 1 warm-up, rotating policy order; loadavg after: %s\n", rounds, la);
    printf("  policy  wall_med_us  wall_min  wall_max  select_us  busy_sum_us  busy/wall  crit_path_us  util_x925  util_a725  arms(ref/scalar/quad4)  dispatched(x925/a725/gpu.sim)  weight_switches\n");
    for (uint32_t p = 0; p < RX_LAT_POLICY_COUNT; p++) {
        uint64_t w[64], s[64], b[64], c[64], u0[64], u1[64];
        memcpy(w, wall[p], sizeof w);
        memcpy(s, sel[p], sizeof s);
        memcpy(b, bsy[p], sizeof b);
        memcpy(c, crit[p], sizeof c);
        memcpy(u0, util[p][0], sizeof u0);
        memcpy(u1, util[p][1], sizeof u1);
        uint64_t wmin = UINT64_MAX, wmax = 0;
        for (uint32_t i = 0; i < rounds; i++) {
            if (w[i] < wmin) wmin = w[i];
            if (w[i] > wmax) wmax = w[i];
        }
        uint64_t wm = median(w, rounds), bm = median(b, rounds);
        printf("  %-6s  %11.1f  %8.1f  %8.1f  %9.1f  %11.1f  %9.2f  %12.1f  %9.3f  %9.3f  %u/%u/%u  %u/%u/%u  %u\n",
               rx_lat_policy_name(p), (double)wm / 1e3, (double)wmin / 1e3, (double)wmax / 1e3,
               (double)median(s, rounds) / 1e3, (double)bm / 1e3, (double)bm / (double)wm,
               (double)median(c, rounds) / 1e3, (double)median(u0, rounds) / 1e3, (double)median(u1, rounds) / 1e3,
               last[p].arm_runs[ARM_REF], last[p].arm_runs[ARM_SCALAR], last[p].arm_runs[ARM_QUAD4],
               last[p].dispatched[0], last[p].dispatched[1], last[p].dispatched[2], last[p].switches);
    }
    /* Paired per-round ratios against serial, so drift between rounds cancels. */
    for (uint32_t p = 1; p < RX_LAT_POLICY_COUNT; p++) {
        uint64_t ratio[64];
        uint32_t wins = 0;
        for (uint32_t i = 0; i < rounds; i++) {
            ratio[i] = wall[p][i] * 1000u / wall[RX_LAT_POLICY_SERIAL][i];
            wins += wall[p][i] < wall[RX_LAT_POLICY_SERIAL][i];
        }
        printf("  %s/serial wall ratio: median %.3f; faster than serial in %u of %u paired rounds\n",
               rx_lat_policy_name(p), (double)median(ratio, rounds) / 1e3, wins, rounds);
    }
    {
        uint64_t ratio[64];
        uint32_t wins = 0;
        for (uint32_t i = 0; i < rounds; i++) {
            ratio[i] = wall[RX_LAT_POLICY_Q3D][i] * 1000u / wall[RX_LAT_POLICY_LCF][i];
            wins += wall[RX_LAT_POLICY_Q3D][i] < wall[RX_LAT_POLICY_LCF][i];
        }
        printf("  q3d/lcf wall ratio: median %.3f; q3d faster in %u of %u paired rounds\n",
               (double)median(ratio, rounds) / 1e3, wins, rounds);
    }
    printf("  outputs and effect value identical to serial in every run: %s; failed runs: %d\n",
           equal_all ? "yes" : "NO", fails);
    printf("  not measured: energy, memory bytes, GPU (gpu.sim refused: no FORGE lease).\n");
    return equal_all && !fails ? 0 : 1;
}
