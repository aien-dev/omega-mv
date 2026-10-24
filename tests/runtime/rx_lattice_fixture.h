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
 * Shared by rx_lattice_unit.c and rx_lattice_bench.c: the fixed synthetic
 * objective graph, the methods (Omega's real integer matvec arms) and the
 * runner that executes a dispatched coordinate and verifies its output
 * independently before sealing a receipt.
 *
 * Graph (17 nodes, scopes: 0 public, 1 tenant A, 2 tenant B):
 *
 *   0 SRC.p -> 1 MV.p w0 -+-> 2 MV.A w1 -+-> 6 JOIN.A -> 7 MV.A w1 -> 10 MV.A w2 -+
 *                         |-> 3 MV.A w2 -+                                        +-> 14 JOIN.A -> 15 EFFECT.A
 *                         |-> 12 MV.p w5 ------------------------------------------+
 *                         +-> 4 MV.B w3 -+-> 8 JOIN.B -> 9 MV.B w4 -> 11 MV.B w3 -+-> 16 JOIN.B
 *   0 --------------------------> 5 MV.B w3 -+                                     |
 *   0 --------------------------> 13 MV.p w5 --------------------------------------+
 *
 * Weights are shared: w1 by 2 and 7, w2 by 3 and 10, w3 by 4, 5 and 11, w5 by
 * 12 and 13. Several shapes have N % 4 != 0.
 *
 * Methods: arm 0 the compiled semantic reference (incumbent), arm 1 the
 * M12 scalar emitter, arm 2 quad4 (R10), arm 3 a planted wrong arm that
 * drops the last N % 4 columns, arm 4 a declared model-call method that
 * needs a model seat and has no executor on this host. Each emitted arm is
 * verified by differential test in a forked child before it may be admitted.
 *
 * Realizations: 0 Cortex-X925 lanes (2), 1 Cortex-A725 lanes (2), 2 a
 * simulated single-owner accelerator lane. It holds scopes 0 and 1 only (its
 * lease belongs to tenant A). It only exists as a simulation: the unit test
 * declares it verified and runs its work on the host; the benchmark leaves
 * it unverified, because no FORGE lease or GPU window exists for this run.
 */
#ifndef RX_LATTICE_FIXTURE_H
#define RX_LATTICE_FIXTURE_H

#include "runtime/rx_lattice.h"
#include "omega_machine.h"
#include "omega_matvec.h"
#include "omega_matvec_quad.h"
#include "sha256.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

enum { ARM_REF = 0, ARM_SCALAR, ARM_QUAD4, ARM_PLANTED, ARM_MODEL, N_ARMS };
static const char *const ARM_NAME[N_ARMS] = { "reference", "scalar", "quad4", "planted_wrong",
                                              "model_call" };
enum { M_REF = 0, M_SCALAR, M_QUAD4, M_PLANTED, M_MODEL, M_SOURCE, M_JOIN, M_EFFECT, N_METHODS };
enum { K_X925 = 0, K_A725, K_GPUSIM, N_REALS };
static const char *const REAL_NAME[N_REALS] = { "cpu.x925", "cpu.a725", "gpu.sim" };

#define FX_NODES   17u
#define FX_EFFECT  15u
#define FX_TXID    0x5EC0DE000698ull
#define FX_MAXLEN  512u
#define FX_GUARD   8u
#define FX_JOIN_CAP_PS   20000000ull   /* 20 us: declared cap for unmodelled JOIN/SOURCE/EFFECT */

typedef void (*MvFn)(const uint64_t *, const uint64_t *, uint64_t *, uint64_t, uint64_t);

static MvFn fx_fn[N_ARMS];
static int fx_verified[N_ARMS];

typedef struct { uint32_t M, N; } FxShape;
static const FxShape FX_W[6] = { { 256, 255 }, { 384, 256 }, { 128, 384 }, { 320, 320 }, { 192, 320 },
                                 { 512, 129 } };
static uint64_t *fx_A[6];

/* Per-node buffers: x (input), y (output + guard). Written only by the lane
 * running that node; read by dependents only after the node is DONE. */
static uint64_t fx_x[FX_NODES][FX_MAXLEN];
static uint64_t fx_y[FX_NODES][FX_MAXLEN + FX_GUARD];
static uint32_t fx_len[FX_NODES];
static const uint64_t FX_CANARY = 0xC0FFEE0DDBA11AD5ull;

static uint64_t fx_xs(uint64_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

static void fx_arm_reference(const uint64_t *A, const uint64_t *x, uint64_t *y, uint64_t M, uint64_t N) {
    omega_matvec_reference(A, x, y, (uint32_t)M, (uint32_t)N);
}

static void fx_arm_planted(const uint64_t *A, const uint64_t *x, uint64_t *y, uint64_t M, uint64_t N) {
    uint64_t n4 = N - (N % 4u);
    for (uint64_t i = 0; i < M; i++) {
        uint64_t s = 0;
        for (uint64_t j = 0; j < n4; j++) s += A[i * N + j] * x[j];
        y[i] = s;
    }
}

static int fx_map_code(const uint8_t *code, size_t len, MvFn *out) {
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED || len > 4096) return -1;
    memcpy(p, code, len);
    __builtin___clear_cache((char *)p, (char *)p + len);
    if (mprotect(p, 4096, PROT_READ | PROT_EXEC) != 0) return -1;
    union { void *v; MvFn f; } u = { p };
    *out = u.f;
    return 0;
}

/* Differential against the semantic reference, odd and even shapes, guard
 * words and input immutability. 0 pass, 1 wrong, 2 wrote outside y. */
static int fx_differential(MvFn fn) {
    static const uint32_t VM[] = { 1, 3, 4, 7, 16, 64 };
    static const uint32_t VN[] = { 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 33, 129, 255, 256 };
    static uint64_t A[64 * 256], A0[64 * 256], x[256], x0[256], y[64 + FX_GUARD], yr[64];
    int wrong = 0;
    for (unsigned mi = 0; mi < sizeof VM / sizeof VM[0]; mi++)
        for (unsigned ni = 0; ni < sizeof VN / sizeof VN[0]; ni++) {
            uint32_t M = VM[mi], N = VN[ni];
            uint64_t s = 0x9E3779B97F4A7C15ull + mi * 131u + ni;
            for (uint32_t i = 0; i < M * N; i++) A[i] = fx_xs(&s);
            for (uint32_t i = 0; i < N; i++) x[i] = fx_xs(&s);
            memcpy(A0, A, sizeof(uint64_t) * M * N);
            memcpy(x0, x, sizeof(uint64_t) * N);
            for (uint32_t i = 0; i < M + FX_GUARD; i++) y[i] = FX_CANARY;
            omega_matvec_reference(A, x, yr, M, N);
            fn(A, x, y, M, N);
            for (uint32_t i = M; i < M + FX_GUARD; i++)
                if (y[i] != FX_CANARY) return 2;
            if (memcmp(A, A0, sizeof(uint64_t) * M * N) || memcmp(x, x0, sizeof(uint64_t) * N)) return 2;
            if (memcmp(y, yr, sizeof(uint64_t) * M)) wrong = 1;
        }
    return wrong;
}

static int fx_verify_in_child(MvFn fn) {
    pid_t pid = fork();
    if (pid == 0) {
        alarm(20);
        _exit(fx_differential(fn));
    }
    int st = 0;
    if (pid < 0 || waitpid(pid, &st, 0) != pid || !WIFEXITED(st)) return 3;
    return WEXITSTATUS(st);
}

/* Synthesize the emitted arms and verify every executable arm. 0 on success. */
static int fx_setup_arms(void) {
    OmegaMachineGraph mg;
    MatVecSemanticSpec spec;
    MatVecLivingKernel kernel;
    MatVecRealization r;
    if (omega_machine_build_dgx_spark(&mg) != 0) return -1;
    if (omega_matvec_spec_init(&spec, "rx_lattice_matvec", 1024, 1024) != 0) return -1;
    memset(&kernel, 0, sizeof kernel);
    kernel.spec = spec;
    kernel.machine = &mg;
    fx_fn[ARM_REF] = fx_arm_reference;
    fx_fn[ARM_PLANTED] = fx_arm_planted;
    if (omega_matvec_synthesize(&kernel, (MatVecRealizationKind)0, &r) != 0 ||
        fx_map_code(r.realization.code_bytes, r.realization.code_len, &fx_fn[ARM_SCALAR]) != 0)
        return -1;
    if (omega_matvec_synthesize_quad4(&spec, &mg, &r) != 0 ||
        fx_map_code(r.realization.code_bytes, r.realization.code_len, &fx_fn[ARM_QUAD4]) != 0)
        return -1;
    for (uint32_t a = 0; a < ARM_MODEL; a++) fx_verified[a] = fx_verify_in_child(fx_fn[a]) == 0;
    fx_verified[ARM_MODEL] = 0;   /* no model-seat executor exists on this host */
    for (uint32_t w = 0; w < 6; w++) {
        size_t n = (size_t)FX_W[w].M * FX_W[w].N;
        fx_A[w] = (uint64_t *)malloc(n * sizeof(uint64_t));
        if (!fx_A[w]) return -1;
        uint64_t s = 0xA11CE000ull + w * 7919u;
        for (size_t i = 0; i < n; i++) fx_A[w][i] = fx_xs(&s);
    }
    return 0;
}

static void fx_digest(const uint8_t *tag, size_t tl, const uint8_t *p, size_t n, uint8_t out[32]) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, tag, tl);
    sha256_update(&c, p, n);
    sha256_final(&c, out);
}

static void fx_vec_digest(uint32_t node, uint8_t out[32]) {
    uint8_t tag[8];
    for (int i = 0; i < 4; i++) tag[i] = (uint8_t)(fx_len[node] >> (8 * i));
    tag[4] = 'v'; tag[5] = 'e'; tag[6] = 'c'; tag[7] = '1';
    fx_digest(tag, 8, (const uint8_t *)fx_y[node], (size_t)fx_len[node] * 8u, out);
}

static RxLatNode fx_node(uint32_t op, uint32_t scope, uint32_t weight, uint32_t n_deps, int d0, int d1) {
    RxLatNode a;
    memset(&a, 0, sizeof a);
    a.op = op;
    a.kind = op == RX_LAT_OP_MATVEC ? RX_LAT_KIND_PHYSICAL
           : op == RX_LAT_OP_SOURCE ? RX_LAT_KIND_CONST
           : op == RX_LAT_OP_JOIN   ? RX_LAT_KIND_JOIN : RX_LAT_KIND_EFFECT;
    a.scope = scope;
    a.rights = RX_LAT_RIGHT_COMPUTE;
    a.precision = RX_LAT_PREC_EXACT_U64;
    a.n_deps = n_deps;
    a.dep[0] = (uint16_t)(d0 < 0 ? 0 : d0);
    a.dep[1] = (uint16_t)(d1 < 0 ? 0 : d1);
    a.gen = 1;
    if (op == RX_LAT_OP_MATVEC) {
        /* The objective's approval allows a model call for these obligations. */
        a.rights |= RX_LAT_RIGHT_MODEL;
        a.weight = weight;
        a.M = FX_W[weight].M;
        a.N = FX_W[weight].N;
    } else {
        a.budget_ps = FX_JOIN_CAP_PS;
    }
    return a;
}

static void fx_build_graph(RxLattice *L) {
    const uint32_t MV = RX_LAT_OP_MATVEC, JN = RX_LAT_OP_JOIN;
    RxLatNode n[FX_NODES];
    n[0] = fx_node(RX_LAT_OP_SOURCE, 0, 0, 0, -1, -1);
    n[0].N = 256;
    n[1] = fx_node(MV, 0, 0, 1, 0, -1);
    n[2] = fx_node(MV, 1, 1, 1, 1, -1);
    n[3] = fx_node(MV, 1, 2, 1, 1, -1);
    n[4] = fx_node(MV, 2, 3, 1, 1, -1);
    n[5] = fx_node(MV, 2, 3, 1, 0, -1);
    n[6] = fx_node(JN, 1, 0, 2, 2, 3);
    n[6].N = 384;
    n[7] = fx_node(MV, 1, 1, 1, 6, -1);
    n[8] = fx_node(JN, 2, 0, 2, 4, 5);
    n[8].N = 320;
    n[9] = fx_node(MV, 2, 4, 1, 8, -1);
    n[10] = fx_node(MV, 1, 2, 1, 7, -1);
    n[11] = fx_node(MV, 2, 3, 1, 9, -1);
    n[12] = fx_node(MV, 0, 5, 1, 1, -1);
    n[13] = fx_node(MV, 0, 5, 1, 0, -1);
    n[14] = fx_node(JN, 1, 0, 2, 10, 12);
    n[14].N = 128;
    n[15] = fx_node(RX_LAT_OP_EFFECT, 1, 0, 1, 14, -1);
    n[15].rights = RX_LAT_RIGHT_COMPUTE | RX_LAT_RIGHT_EFFECT;
    n[15].txid = FX_TXID;
    n[16] = fx_node(JN, 2, 0, 2, 11, 13);
    n[16].N = 320;
    for (uint32_t i = 0; i < FX_NODES; i++) rx_lat_add_node(L, &n[i]);
}

static void fx_source_digest(const char *name, uint32_t rev, uint8_t out[32]) {
    uint8_t tag[4] = { (uint8_t)rev, (uint8_t)(rev >> 8), 'm', '1' };
    fx_digest(tag, 4, (const uint8_t *)name, strlen(name), out);
}

static void fx_build_methods(RxLattice *L) {
    static const struct { uint32_t op, arm, rights, seat, inc; const char *name; } D[N_METHODS] = {
        { RX_LAT_OP_MATVEC, ARM_REF, RX_LAT_RIGHT_COMPUTE, 0, 1, "matvec.reference" },
        { RX_LAT_OP_MATVEC, ARM_SCALAR, RX_LAT_RIGHT_COMPUTE, 0, 0, "matvec.m12.scalar" },
        { RX_LAT_OP_MATVEC, ARM_QUAD4, RX_LAT_RIGHT_COMPUTE, 0, 0, "matvec.r10.quad4" },
        { RX_LAT_OP_MATVEC, ARM_PLANTED, RX_LAT_RIGHT_COMPUTE, 0, 0, "matvec.planted_wrong" },
        { RX_LAT_OP_MATVEC, ARM_MODEL, RX_LAT_RIGHT_COMPUTE | RX_LAT_RIGHT_MODEL, 1, 0, "matvec.model_call" },
        { RX_LAT_OP_SOURCE, 0, RX_LAT_RIGHT_COMPUTE, 0, 1, "source.xorshift" },
        { RX_LAT_OP_JOIN, 0, RX_LAT_RIGHT_COMPUTE, 0, 1, "join.wrapping_sum" },
        { RX_LAT_OP_EFFECT, 0, RX_LAT_RIGHT_EFFECT, 0, 1, "effect.ledger_commit" },
    };
    for (uint32_t j = 0; j < N_METHODS; j++) {
        RxLatMethod m;
        memset(&m, 0, sizeof m);
        m.op = D[j].op;
        m.arm = D[j].arm;
        m.rights = D[j].rights;
        m.precision = RX_LAT_PREC_EXACT_U64;
        m.revision = m.evidence_rev = 1;
        m.needs_seat = D[j].seat;
        m.incumbent = D[j].inc;
        m.verified = D[j].op == RX_LAT_OP_MATVEC ? (uint32_t)fx_verified[D[j].arm] : 1u;
        fx_source_digest(D[j].name, m.revision, m.source);
        rx_lat_add_method(L, &m);
    }
}

static void fx_build_reals(RxLattice *L, int gpu_sim_verified) {
    static const struct { uint32_t core, lanes, scopes; uint64_t cache; const char *desc; } D[N_REALS] = {
        { RX_CM_CORE_X925, 2, 0x7, 2u << 20, "host.cpu.cortex-x925.lanes2" },
        { RX_CM_CORE_A725, 2, 0x7, 512u << 10, "host.cpu.cortex-a725.lanes2" },
        { RX_CM_CORE_OTHER, 1, 0x3, 1u << 20, "forge.gb10.sim.single-owner" },
    };
    for (uint32_t k = 0; k < N_REALS; k++) {
        RxLatReal r;
        memset(&r, 0, sizeof r);
        r.core = D[k].core;
        r.lanes = D[k].lanes;
        r.scopes = D[k].scopes;
        r.precisions = 1u << RX_LAT_PREC_EXACT_U64;
        r.forge_verified = k == K_GPUSIM ? (uint32_t)gpu_sim_verified : 1u;
        r.cache_bytes = D[k].cache;
        fx_digest((const uint8_t *)"desc", 4, (const uint8_t *)D[k].desc, strlen(D[k].desc), r.descriptor);
        memcpy(r.live, r.descriptor, 32);
        rx_lat_add_real(L, &r);
    }
    /* Bounded supported combinations. */
    rx_lat_support(L, M_REF, K_X925);
    rx_lat_support(L, M_REF, K_A725);
    rx_lat_support(L, M_SCALAR, K_X925);
    rx_lat_support(L, M_SCALAR, K_A725);
    rx_lat_support(L, M_QUAD4, K_X925);
    rx_lat_support(L, M_QUAD4, K_A725);
    rx_lat_support(L, M_QUAD4, K_GPUSIM);
    rx_lat_support(L, M_PLANTED, K_X925);
    rx_lat_support(L, M_MODEL, K_GPUSIM);
    rx_lat_support(L, M_SOURCE, K_X925);
    rx_lat_support(L, M_JOIN, K_X925);
    rx_lat_support(L, M_JOIN, K_A725);
    rx_lat_support(L, M_EFFECT, K_X925);
}

static void fx_build(RxLattice *L, int gpu_sim_verified) {
    rx_lat_init(L);
    fx_build_graph(L);
    fx_build_methods(L);
    fx_build_reals(L, gpu_sim_verified);
}

/* Independent output check, before any receipt: MATVEC recomputes four rows
 * with a plain loop and checks the guard words; SOURCE and JOIN recompute
 * fully. 0 pass. */
static uint32_t fx_check(const RxLattice *L, uint32_t i) {
    const RxLatNode *a = &L->node[i];
    for (uint32_t g = 0; g < FX_GUARD; g++)
        if (fx_y[i][fx_len[i] + g] != FX_CANARY) return 2;
    if (a->op == RX_LAT_OP_MATVEC) {
        const uint64_t *A = fx_A[a->weight];
        for (uint32_t t = 0; t < 4; t++) {
            uint32_t row = (uint32_t)((i * 2654435761u + t * 997u) % a->M);
            uint64_t s = 0;
            for (uint32_t j = 0; j < a->N; j++) s += A[(size_t)row * a->N + j] * fx_x[i][j];
            if (s != fx_y[i][row]) return 1;
        }
        return 0;
    }
    if (a->op == RX_LAT_OP_SOURCE) {
        uint64_t s = 0x0698ull;
        for (uint32_t n = 0; n < a->N; n++)
            if (fx_y[i][n] != fx_xs(&s)) return 1;
        return 0;
    }
    for (uint32_t n = 0; n < a->N; n++) {
        uint64_t v = 0;
        for (uint32_t d = 0; d < a->n_deps; d++) {
            uint32_t q = a->dep[d];
            v += fx_y[q][n % fx_len[q]];
        }
        if (v != fx_y[i][n]) return 1;
    }
    return 0;
}

/* Run one dispatched non-effect coordinate. Returns the independent verdict.
 * The model-call method has no executor here and always fails closed. */
static uint32_t fx_execute(const RxLattice *L, uint32_t i, uint32_t cand) {
    const RxLatNode *a = &L->node[i];
    const RxLatMethod *m = &L->method[L->cand[cand].method];
    uint32_t len = a->op == RX_LAT_OP_MATVEC ? a->M : a->N;
    fx_len[i] = len;
    for (uint32_t g = 0; g < FX_GUARD; g++) fx_y[i][len + g] = FX_CANARY;
    if (a->op == RX_LAT_OP_SOURCE) {
        uint64_t s = 0x0698ull;
        for (uint32_t n = 0; n < len; n++) fx_y[i][n] = fx_xs(&s);
    } else if (a->op == RX_LAT_OP_JOIN) {
        for (uint32_t n = 0; n < len; n++) {
            uint64_t v = 0;
            for (uint32_t d = 0; d < a->n_deps; d++) {
                uint32_t q = a->dep[d];
                v += fx_y[q][n % fx_len[q]];
            }
            fx_y[i][n] = v;
        }
    } else {
        uint32_t q = a->dep[0];
        for (uint32_t n = 0; n < a->N; n++) fx_x[i][n] = fx_y[q][n % fx_len[q]] ^ (n * 0x9E3779B97F4A7C15ull);
        if (m->arm >= N_ARMS || !fx_verified[m->arm]) return 3;   /* never run an unverified arm */
        fx_fn[m->arm](fx_A[a->weight], fx_x[i], fx_y[i], a->M, a->N);
    }
    return fx_check(L, i);
}

/* Independent oracle: the whole graph with the reference only, no lattice. */
static void fx_oracle(const RxLattice *L, uint8_t out[FX_NODES][32]) {
    static uint64_t y[FX_NODES][FX_MAXLEN], x[FX_MAXLEN];
    uint32_t len[FX_NODES];
    for (uint32_t t = 0; t < L->n_nodes; t++) {
        uint32_t i = L->topo[t];
        const RxLatNode *a = &L->node[i];
        if (a->op == RX_LAT_OP_EFFECT) continue;
        len[i] = a->op == RX_LAT_OP_MATVEC ? a->M : a->N;
        if (a->op == RX_LAT_OP_SOURCE) {
            uint64_t s = 0x0698ull;
            for (uint32_t n = 0; n < len[i]; n++) y[i][n] = fx_xs(&s);
        } else if (a->op == RX_LAT_OP_JOIN) {
            for (uint32_t n = 0; n < len[i]; n++) {
                uint64_t v = 0;
                for (uint32_t d = 0; d < a->n_deps; d++) v += y[a->dep[d]][n % len[a->dep[d]]];
                y[i][n] = v;
            }
        } else {
            uint32_t q = a->dep[0];
            for (uint32_t n = 0; n < a->N; n++) x[n] = y[q][n % len[q]] ^ (n * 0x9E3779B97F4A7C15ull);
            const uint64_t *A = fx_A[a->weight];
            for (uint32_t r = 0; r < a->M; r++) {
                uint64_t s = 0;
                for (uint32_t j = 0; j < a->N; j++) s += A[(size_t)r * a->N + j] * x[j];
                y[i][r] = s;
            }
        }
        uint8_t tag[8];
        for (int b = 0; b < 4; b++) tag[b] = (uint8_t)(len[i] >> (8 * b));
        tag[4] = 'v'; tag[5] = 'e'; tag[6] = 'c'; tag[7] = '1';
        fx_digest(tag, 8, (const uint8_t *)y[i], (size_t)len[i] * 8u, out[i]);
    }
}

#endif /* RX_LATTICE_FIXTURE_H */
