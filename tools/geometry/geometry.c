/* geometry.c: exact solver for the shape of the motor (L323).
 *
 * The machine is a graph of places that hold bytes (DRAM, CPU caches, GPU L2, a second machine) joined by roads
 * (the DRAM pipe, the CPU-GPU link, 10 GbE). The 35B is a set of parts with bytes and reuse per word. A shape is a
 * placement (which part lives where) plus routes (which road carries it) plus timing (how many staggered groups the
 * conversations are split into, so one group's waits hide under another group's reads). The solver finds, for every
 * format and number of conversations N, the shape with the shortest round (time for every conversation to get one word).
 *
 * Time model (every term is printed with its receipt by `geometry edges`):
 *   per group stroke (n = N/G conversations read the weights together):
 *     P     = pipe reads / BWr + pipe writes / BWw (+ 1.17 x CPU DRAM bytes: the GPU loses 1.17 B per B others take)
 *     chain = P + stalls; a stall is one crossing to the CPU and back that the pipe cannot hide:
 *             max(0, round trip + CPU work for n conversations - prefetch reservoir / BWr)
 *   round T = max(G x P, chain, CPU work for all N, second-road bytes / second-road rate, other machine's pipe)
 *   words/s = N / T.
 * T is the maximum of functions that are linear in m (layers whose state lives in CPU caches), so its minimum over
 * m sits at an end or at a crossing of two lines: `solve` checks only those points, `brute` checks every m, and the
 * self test demands they agree on every case (exactness), plus a planted wrong solver that must be caught.
 *
 * Usage: geometry parts.tsv rank|edges|sweep|config|selftest [options]
 * No outside libraries. C11, libm. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GB 1e9
#define MB 1e6
#define US 1e-6

/* ---- the machine: every edge with its receipt or UNKNOWN + the experiment that measures it ---- */
typedef struct {
	const char *name;
	double value;
	const char *unit;
	const char *receipt;
} Edge;

enum {
	E_BWR, E_BWR_BEST, E_BWW, E_PENALTY, E_CPU_DRAM, E_CAP_LO, E_CAP_HI, E_CPU_L2BW, E_CPU_L3BW, E_STEP_WORK,
	E_STEP_CORES, E_X_C2C, E_X_MEM, E_PREFETCH, E_ROAD2, E_GPU_L2, E_LAUNCH, E_NVME, E_GBE_BW, E_GBE_RTT,
	E_MAC_BW, E_MAC_CAP, E_MAC_RTT, E_MAC_LINK, E_ROUTER_STEP, E_CPU_DRAM_SHARED, E_L2_HOLD, E_COUNT
};

static Edge edge[E_COUNT] = {
	[E_BWR] = {"DRAM pipe, GPU reading, held 30 s", 228.0e9, "B/s",
		"omega doc/machine-ceiling docs/MACHINE_CEILING.md:57 raw/gpu-stream-cached-30s.txt"},
	[E_BWR_BEST] = {"DRAM pipe, GPU reading, best burst", 245.1e9, "B/s",
		"docs/MACHINE_CEILING.md:57 raw/gpu-tc-peak.txt"},
	[E_BWW] = {"DRAM pipe, GPU writing", 175.1e9, "B/s", "docs/MACHINE_CEILING.md:58 raw/gpu-ceiling.txt"},
	[E_PENALTY] = {"GPU bytes lost per byte the CPU or drive takes from DRAM", 1.17, "B/B",
		"docs/MACHINE_CEILING.md:105-107: (228.0-189.1)/(22.7+10.545); CPU-only split UNKNOWN (L321 E2)"},
	[E_CPU_DRAM] = {"DRAM pipe, 20 CPU cores reading", 71.3e9, "B/s", "docs/MACHINE_CEILING.md:59 raw/cpu-dram-read-all20.txt"},
	[E_CAP_LO] = {"CPU caches that can hold state, low (L2 25 MB + L3 24 MB, lscpu)", 49.0 * MB, "B",
		"docs/MACHINE_CEILING.md:69-70 raw/cpu-env.txt; real usable size UNKNOWN until L321 E1"},
	[E_CAP_HI] = {"CPU caches that can hold state, high (L2 25 + L3 32 + SLC 16 MB, NVIDIA HC2025 deck)", 73.0 * MB, "B",
		"~/handoffs/2026-10-21-cpu-piston.md:8-11 (~/docs/gb10 deck txt 193-195, 240-243); UNKNOWN until L321 E1"},
	[E_CPU_L2BW] = {"CPU L2, all 20 cores", 467.5e9, "B/s", "docs/MACHINE_CEILING.md:69 raw/cpu-bw.txt"},
	[E_CPU_L3BW] = {"CPU L3, all 20 cores", 253.0e9, "B/s", "docs/MACHINE_CEILING.md:70 raw/cpu-bw.txt"},
	[E_STEP_WORK] = {"CPU DeltaNet state step, one layer one conversation", 118.0 * US, "core-s",
		"~/handoffs/2026-10-21-cpu-piston.md:48-49 smoke (2 cores 59 us/step at load 45, NOT evidence); L321 E3a measures"},
	[E_STEP_CORES] = {"CPU cores given to the state step (X925)", 10, "cores",
		"lscpu 10 X925 + 10 A725 (docs/MACHINE_CEILING.md:61); perfect split over 32 value heads ASSUMED, L321 E3a"},
	[E_X_C2C] = {"crossing round trip GPU->CPU->GPU over NVLink-C2C, cache to cache", 2.0 * US, "s",
		"UNKNOWN: asked of L321 (ledger note, 16-49 KB message round trip); sweep 0..20 us"},
	[E_X_MEM] = {"extra round trip when the cylinder sits in DRAM and the flag is polled (three pistons)", 1.0 * US, "s",
		"UNKNOWN: L312 chip piston live mode (host fill flag -> chip pickup gap); ASSUMED 1 us over C2C"},
	[E_PREFETCH] = {"weights the GPU can read ahead while a crossing is out (reservoir)", 0.0, "B",
		"UNKNOWN: resident kernel reading next layer into L2/SMEM during the wait; L2 24 MB but reuse across launches 0.84-1.00 (MACHINE_CEILING.md:65)"},
	[E_ROAD2] = {"second road: GPU reading CPU-cache-held bytes over C2C beside the pipe", 0.0, "B/s",
		"UNKNOWN: L321 E1b (paper: 600 GB/s bidirectional, The Register, MACHINE_CEILING.md:71)"},
	[E_GPU_L2] = {"GPU L2 size", 24.0 * MB, "B", "docs/MACHINE_CEILING.md:64 (ServeTheHome)"},
	[E_LAUNCH] = {"GPU dependent launch cost", 24.5 * US, "s",
		"docs/MACHINE_CEILING.md:78 raw/gpu-launch-cost.txt (23-26 us); every shape pays it alike, left out of the ranking"},
	[E_NVME] = {"NVMe read into DRAM", 10.54e9, "B/s", "docs/MACHINE_CEILING.md:72 raw/nvme-read-30s.txt"},
	[E_GBE_BW] = {"10 GbE to a second machine, per direction", 1.25e9, "B/s",
		"docs/MACHINE_CEILING.md:74 (paper; no 10 Gb/s partner exists, UNMEASURED); L258 measures"},
	[E_GBE_RTT] = {"10 GbE round trip, small message", 30.0 * US, "s",
		"UNKNOWN: no partner machine (MACHINE_CEILING.md:74); L258 link receipt; sweep 10..100 us"},
	[E_MAC_BW] = {"Drake's Mac M2 memory read", 100.0e9, "B/s", "SPEC, not measured (~/handoffs/briefs/L258-two-pipelines.md:15)"},
	[E_MAC_CAP] = {"Drake's Mac room for model bytes", 5.0 * GB, "B", "~/handoffs/briefs/L258-two-pipelines.md:15,24-25"},
	[E_MAC_RTT] = {"Mac round trip over Wi-Fi", 5.93e-3, "s", "~/handoffs/briefs/L258-two-pipelines.md:16 (ping, avg of 5)"},
	[E_MAC_LINK] = {"Mac Wi-Fi bulk rate", 0.05e9, "B/s", "UNKNOWN: ASSUMED 50 MB/s; only 4 KB per hop crosses, so it barely matters"},
	[E_ROUTER_STEP] = {"CPU router step (2048x256 bf16 from CPU caches), one conversation", 4.0 * US, "s",
		"INFERRED: 1 MB at 253 GB/s L3 (MACHINE_CEILING.md:70); UNKNOWN until measured"},
	[E_CPU_DRAM_SHARED] = {"DRAM pipe, 20 CPU cores reading while the GPU streams", 22.7e9, "B/s",
		"docs/MACHINE_CEILING.md:63,106 raw/cc-cpu.txt (calc:e7991b26)"},
	[E_L2_HOLD] = {"GPU L2 that can be pinned to hold state across words", 0.0, "B",
		"UNKNOWN: L2 is 24 MB (MACHINE_CEILING.md:64) but whether lines stay pinned while 3.5 GB streams through is unmeasured; experiment: hold 2-22 MB, stream 1.5 GB, re-read, count DRAM bytes"},
};

/* ---- the model: parts with bytes and reuse per word (from the safetensors headers) ---- */
typedef struct {
	double linear_attn, full_attn, router, shared, norms, expert_total, lm_head;
	double experts; /* count of routed experts over all layers */
	int layers, lin_layers, full_layers, n_expert, top_k, hidden, vocab;
	double expert_params, head_params;
	double state_f32;   /* per linear layer per conversation: 32 value heads x 128 x 128 x 4 B */
	double conv_f32;    /* per linear layer per conversation: 8192 channels x (kernel 4 - 1) x 4 B */
	double kv_per_tok;  /* all full layers: 10 x 2 kv heads x 256 x (K + V) x 2 B */
	double cyl_out, cyl_back; /* figure-eight crossing per layer per conversation */
} Model;

static Model model_35b(const char *parts)
{
	Model m = {0};
	FILE *f = fopen(parts, "r");
	if (!f) {
		perror(parts);
		exit(2);
	}
	char cls[64];
	long count;
	double bytes;
	while (fscanf(f, "%63s %ld %lf", cls, &count, &bytes) == 3) {
		if (!strcmp(cls, "linear_attn")) m.linear_attn = bytes;
		else if (!strcmp(cls, "full_attn")) m.full_attn = bytes;
		else if (!strcmp(cls, "router")) m.router = bytes;
		else if (!strcmp(cls, "shared")) m.shared = bytes;
		else if (!strcmp(cls, "norms")) m.norms = bytes;
		else if (!strcmp(cls, "expert")) { m.expert_total = bytes; m.experts = count / 6.0; } /* 3 weights + 3 scales */
		else if (!strcmp(cls, "lm_head")) m.lm_head = bytes;
	}
	fclose(f);
	/* config.json of ~/models/qwen3.6-35b-a3b-fp8-95a723d0 */
	m.layers = 40, m.lin_layers = 30, m.full_layers = 10, m.n_expert = 256, m.top_k = 8, m.hidden = 2048, m.vocab = 248320;
	m.expert_params = 3.0 * 512 * 2048;
	m.head_params = (double)m.vocab * m.hidden;
	m.state_f32 = 32.0 * 128 * 128 * 4;
	m.conv_f32 = 8192.0 * 3 * 4;
	m.kv_per_tok = 10.0 * 2 * 256 * 2 * 2;
	m.cyl_out = (2048.0 + 2048 + 4096 + 32 + 32) * 4; /* q, k, v, a, b as f32 */
	m.cyl_back = 4096.0 * 4;                          /* 32 x 128 output as f32 */
	if (m.experts != m.n_expert * m.layers) {
		fprintf(stderr, "parts: %g experts, want %d\n", m.experts, m.n_expert * m.layers);
		exit(2);
	}
	return m;
}

/* ---- formats: the HUM ladder ---- */
typedef struct {
	const char *name;
	double expert_bpp; /* bytes per routed expert parameter; 0 = as on disk (FP8 + scales) */
	double head_bpp;   /* bytes per head parameter; 0 = as on disk (bf16) */
	double state_bpe;  /* bytes per state element: 4 f32, 2 bf16 */
} Format;

static const Format formats[] = {
	{"FP8", 0, 0, 4},
	{"FP8+bf16-state", 0, 0, 2},
	{"W4", 0.5 + 2.0 / 32, 0.5 + 2.0 / 32, 4}, /* 4 bit + one bf16 scale per 32 weights, ASSUMED (L310/L315 own it) */
	{"W4+bf16-state", 0.5 + 2.0 / 32, 0.5 + 2.0 / 32, 2},
};
#define N_FORMATS (int)(sizeof formats / sizeof formats[0])

/* ---- a shape ---- */
enum { FAM_STRAIGHT, FAM_PISTONS, FAM_EIGHT, FAM_TWO_SPARK, FAM_TWO_MAC, FAM_EXPERT_SPLIT, FAM_FREE, FAM_COUNT };
static const char *fam_name[FAM_COUNT] = {"straight line", "three pistons", "figure eight", "two machines (2nd Spark, 10 GbE)",
	"two machines (Mac, Wi-Fi)", "two machines, experts split", "solver free"};

typedef struct {
	int fam;
	int N, G;         /* conversations, staggered groups */
	int m;            /* linear layers per conversation whose state lives in CPU caches, the CPU runs the step (figure eight) */
	int m2;           /* linear layers per conversation whose state is pinned in GPU L2 (no crossing) */
	int m3;           /* linear layers per conversation whose state lives in CPU caches, the GPU reads it over the second road */
	int router_cpu;   /* router held and run on the CPU (40 more crossings) */
	double head_cpu;  /* share of the lm_head the CPU computes from DRAM */
	double road2;     /* static bytes per stroke moved onto the second road */
	int split;        /* two machines: first layer on machine B (B also holds the head) */
	int cyl_in_dram;  /* crossing cylinder goes through DRAM (three pistons) */
} Shape;

#define N_LINES 5
typedef struct {
	double T, wps, P, chain, cpu, road, remote;
	double line[N_LINES];
	const char *bound;
	int feasible;
} Result;

typedef struct {
	const Model *md;
	const Format *fm;
	double ctx;     /* context length in words, for the KV read */
	double cap;     /* CPU cache bytes for held state */
	double x_c2c, x_mem, prefetch, l2_hold, road2_bw, cores, step_work, router_step, bwr, bww;
	double gbe_rtt;
	int coord;      /* coordinator's first-pass assumptions: one rate 237 GB/s, no CPU step time, 3.537 GB weights */
} Env;

static double expert_bytes(const Env *e)
{
	return e->fm->expert_bpp > 0 ? e->md->expert_params * e->fm->expert_bpp : e->md->expert_total / e->md->experts;
}
static double head_bytes(const Env *e) { return e->fm->head_bpp > 0 ? e->md->head_params * e->fm->head_bpp : e->md->lm_head; }
static double state_layer(const Env *e) { return e->md->state_f32 * e->fm->state_bpe / 4; }
static double conv_layer(const Env *e) { return e->md->conv_f32 * e->fm->state_bpe / 4; }
static double union_experts(const Model *md, double n)
{
	return md->n_expert * (1 - pow(1 - (double)md->top_k / md->n_expert, n));
}
static double trunk_bytes(const Env *e)
{
	const Model *md = e->md;
	return md->linear_attn + md->full_attn + md->router + md->shared + md->norms;
}

/* bytes one machine reads and writes for one group stroke of n conversations over layers [a, b) (+ head if with_head) */
static void stroke_bytes(const Env *e, const Shape *s, double n, int a, int b, int with_head, double *rd, double *wr)
{
	const Model *md = e->md;
	double lin = 0, full = 0;
	for (int l = a; l < b; l++) (l % 4 == 3) ? full++ : lin++;
	double per_lin = md->linear_attn / md->lin_layers, per_full = md->full_attn / md->full_layers;
	double per_common = (md->router + md->shared + md->norms) / md->layers;
	double r = lin * per_lin + full * per_full + (lin + full) * (per_common + union_experts(md, n) * expert_bytes(e));
	if (s->router_cpu) r -= (lin + full) * md->router / md->layers;
	r -= s->road2;
	if (with_head) r += head_bytes(e) * (1 - s->head_cpu);
	double held = lin - (s->m + s->m2 + s->m3); /* linear layers whose state stays in DRAM */
	if (held < 0) held = 0;
	double st = n * (held * state_layer(e) + lin * conv_layer(e));
	double kv = n * full / md->full_layers * md->kv_per_tok;
	*rd = r + st + kv * e->ctx;
	*wr = st + kv;
	if (s->cyl_in_dram) {
		double c = n * s->m * (md->cyl_out + md->cyl_back);
		*rd += c, *wr += c;
	}
}

static double pipe_time(const Env *e, double rd, double wr, double cpu_dram)
{
	if (e->coord) return (rd + wr) / 237e9;
	return (rd + edge[E_PENALTY].value * cpu_dram) / e->bwr + wr / e->bww;
}

static Result eval(const Env *e, const Shape *s)
{
	const Model *md = e->md;
	Result R = {0};
	R.feasible = 1;
	double n = (double)s->N / s->G;
	double hide = e->prefetch / e->bwr;
	double step = e->coord ? 0 : e->step_work * n / e->cores; /* one crossing's CPU work for the group */
	double x = e->x_c2c + (s->cyl_in_dram ? e->x_mem : 0);
	double rd, wr;

	/* capacity of the CPU caches: held state for every conversation + router + second-road bytes */
	double sl = state_layer(e) + conv_layer(e);
	double held = s->N * (s->m + s->m3) * sl + (s->router_cpu ? md->router : 0) + s->road2;
	if (held > e->cap) R.feasible = 0;
	if (s->N * s->m2 * sl > e->l2_hold) R.feasible = 0;
	if (s->m + s->m2 + s->m3 > md->lin_layers) R.feasible = 0;
	if (s->m3 > 0 && e->road2_bw <= 0) R.feasible = 0;
	if (s->road2 > 0 && e->road2_bw <= 0) R.feasible = 0;

	if (s->fam == FAM_TWO_SPARK || s->fam == FAM_TWO_MAC) {
		int remote_mac = s->fam == FAM_TWO_MAC;
		double rdA, wrA, rdB, wrB;
		stroke_bytes(e, s, n, 0, s->split, 0, &rdA, &wrA);
		stroke_bytes(e, s, n, s->split, md->layers, 1, &rdB, &wrB);
		double PA = pipe_time(e, rdA, wrA, 0);
		double PB = remote_mac ? (rdB + wrB) / edge[E_MAC_BW].value : pipe_time(e, rdB, wrB, 0);
		double rtt = remote_mac ? edge[E_MAC_RTT].value : e->gbe_rtt;
		double bw = remote_mac ? edge[E_MAC_LINK].value : edge[E_GBE_BW].value;
		double hop = rtt + n * md->hidden * 2 / bw; /* hidden out, token id back: one round trip per word */
		if (remote_mac) {
			/* model bytes held on the Mac: layers [split, 40) all experts + trunk share + head */
			double lB = md->layers - s->split;
			double hold = lB * (md->n_expert * expert_bytes(e) + trunk_bytes(e) / md->layers) + head_bytes(e);
			if (hold > edge[E_MAC_CAP].value) R.feasible = 0;
		}
		R.P = PA, R.remote = PB;
		R.chain = PA + PB + hop;
		R.T = fmax(fmax(s->G * PA, s->G * PB), R.chain);
		R.bound = R.T == R.chain ? "chain (link round trip)" : (s->G * PA >= s->G * PB ? "pipe A" : "pipe B");
		R.wps = s->N / R.T;
		return R;
	}
	if (s->fam == FAM_EXPERT_SPLIT) {
		/* B (second Spark) reads half of each layer's experts; one round trip per layer per word */
		Shape t = *s;
		stroke_bytes(e, &t, n, 0, md->layers, 1, &rd, &wr);
		double half = md->layers * union_experts(md, n) * expert_bytes(e) / 2;
		double PA = pipe_time(e, rd - half, wr, 0), PB = half / e->bwr;
		double hop = e->gbe_rtt + n * md->hidden * 2 * 2 / edge[E_GBE_BW].value;
		R.P = PA, R.remote = PB;
		R.chain = fmax(PA, PB) + md->layers * fmax(0, hop - hide);
		R.T = fmax(s->G * fmax(PA, PB), R.chain);
		R.bound = R.T == R.chain ? "chain (link round trip per layer)" : "pipe";
		R.wps = s->N / R.T;
		return R;
	}

	stroke_bytes(e, s, n, 0, md->layers, 1, &rd, &wr);
	double cpu_dram = s->head_cpu * head_bytes(e); /* the head is read once per group stroke, whatever n */
	R.P = pipe_time(e, rd, wr, cpu_dram);
	double stalls = s->m * fmax(0, x + step - hide);
	if (s->router_cpu) stalls += md->layers * fmax(0, x + (e->coord ? 0 : e->router_step * n) - hide);
	/* the CPU's head share arrives at the CPU's all-at-once DRAM rate; if it is later than the pipe, the word waits */
	double head_late = cpu_dram / edge[E_CPU_DRAM_SHARED].value;
	R.chain = fmax(R.P, head_late) + stalls;
	R.cpu = e->coord ? 0 : s->N * (s->m * e->step_work / e->cores + (s->router_cpu ? md->layers * e->router_step : 0));
	/* state over the second road is the last word's state: read ahead, written behind, so only its rate counts */
	R.road = e->road2_bw > 0 ? s->G * (s->road2 + 2 * n * s->m3 * sl) / e->road2_bw : 0;
	/* T is the largest of these lines; each is linear in m, which is what makes m_points exact */
	R.line[0] = s->G * R.P, R.line[1] = R.P + stalls, R.line[2] = head_late + stalls, R.line[3] = R.cpu, R.line[4] = R.road;
	R.T = 0;
	for (int i = 0; i < N_LINES; i++) R.T = fmax(R.T, R.line[i]);
	R.bound = R.T == R.line[0] && s->G > 1 ? "pipe (all groups)" : R.T == R.line[0] ? "pipe" : R.T == R.chain ? "chain (crossings)" : R.T == R.cpu ? "CPU work" : "second road";
	R.wps = s->N / R.T;
	return R;
}

/* ---- solvers ---- */
static const double head_cpu_opts[] = {0, 0.1, 0.2, 0.3};
#define N_HEAD (int)(sizeof head_cpu_opts / sizeof head_cpu_opts[0])

typedef struct {
	Shape s;
	Result r;
} Best;

static void consider(const Env *e, Shape *s, Best *b)
{
	Result r = eval(e, s);
	if (r.feasible && (b->r.T == 0 || r.T < b->r.T - 1e-15)) b->s = *s, b->r = r;
}

/* the options each family may change; everything else stays at the straight-line value */
static void fam_bounds(int fam, int *m_max, int *router, int *head, int *road, int *dram)
{
	*m_max = 0, *router = 0, *head = 0, *road = 0, *dram = 0;
	if (fam == FAM_PISTONS) *m_max = 30, *dram = 1;
	if (fam == FAM_EIGHT) *m_max = 30;
	if (fam == FAM_FREE) *m_max = 30, *router = 1, *head = N_HEAD - 1, *road = 1;
}

static double road2_amount(const Env *e, const Shape *s)
{
	/* fill whatever CPU cache the held state and router leave free with static trunk bytes */
	double used = s->N * (s->m + s->m3) * (state_layer(e) + conv_layer(e)) + (s->router_cpu ? e->md->router : 0);
	return e->road2_bw > 0 && e->cap > used ? e->cap - used : 0;
}

/* m candidates where T(m) can have its minimum: ends, the capacity edge, and where two of its lines cross */
static int m_points(const Env *e, Shape s, int m_max, int rd2, int *out)
{
	int k = 0;
	out[k++] = 0;
	if (m_max == 0) return k;
	out[k++] = m_max;
	double per = s.N * (state_layer(e) + conv_layer(e));
	double room = e->cap - (s.router_cpu ? e->md->router : 0) - s.N * s.m3 * (state_layer(e) + conv_layer(e));
	if (m_max > e->md->lin_layers - s.m2 - s.m3) m_max = e->md->lin_layers - s.m2 - s.m3;
	out[1] = m_max;
	int cap_m = per > 0 ? (int)floor(room / per) : m_max;
	if (cap_m >= 0 && cap_m < m_max) out[k++] = cap_m;
	/* every line of T is linear in m: read slope and intercept at m = 0 and 1, add both integers around each crossing */
	Shape a = s, b = s;
	a.m = 0, b.m = 1;
	a.road2 = rd2 ? road2_amount(e, &a) : 0, b.road2 = rd2 ? road2_amount(e, &b) : 0;
	Result ra = eval(e, &a), rb = eval(e, &b);
	for (int i = 0; i < N_LINES; i++)
		for (int j = i + 1; j < N_LINES; j++) {
			double si = rb.line[i] - ra.line[i], sj = rb.line[j] - ra.line[j];
			if (fabs(si - sj) < 1e-18) continue;
			double mx = (ra.line[j] - ra.line[i]) / (si - sj);
			if (mx > 0 && mx < m_max) out[k++] = (int)floor(mx), out[k++] = (int)ceil(mx);
		}
	return k;
}

static Best solve_family(const Env *e, int fam, int N, int brute, int plant_bug)
{
	Best b = {0};
	int m_max, router, head, road, dram;
	fam_bounds(fam, &m_max, &router, &head, &road, &dram);
	for (int G = 1; G <= N; G++) {
		if (N % G) continue;
		Shape s = {.fam = fam, .N = N, .G = G, .cyl_in_dram = dram};
		if (fam == FAM_TWO_SPARK || fam == FAM_TWO_MAC) {
			for (s.split = 1; s.split < e->md->layers; s.split++) consider(e, &s, &b);
			continue;
		}
		double sl = state_layer(e) + conv_layer(e);
		int m2_max = fam == FAM_FREE ? (int)fmin(30, floor(e->l2_hold / (N * sl))) : 0;
		int m3_max = fam == FAM_FREE && e->road2_bw > 0 ? (int)fmin(30, floor(e->cap / (N * sl))) : 0;
		for (s.m2 = 0; s.m2 <= m2_max; s.m2++)
		for (s.m3 = 0; s.m3 <= m3_max && s.m2 + s.m3 <= 30; s.m3++)
		for (s.router_cpu = 0; s.router_cpu <= router; s.router_cpu++)
			for (int h = 0; h <= head; h++)
				for (int rd2 = 0; rd2 <= road; rd2++) {
					s.head_cpu = head_cpu_opts[h];
					int pts[2 * N_LINES * N_LINES + 3], np = 0;
					if (brute)
						for (int m = 0; m <= m_max - s.m2 - s.m3; m++) pts[np++] = m;
					else {
						np = m_points(e, s, m_max, rd2, pts);
						if (plant_bug && np > 2) np = 2; /* the wrong solver: ends only, misses the crossings */
					}
					for (int i = 0; i < np; i++) {
						s.m = pts[i];
						s.road2 = rd2 ? road2_amount(e, &s) : 0;
						consider(e, &s, &b);
					}
				}
	}
	return b;
}

/* ---- output ---- */
static void env_default(Env *e, const Model *md, const Format *fm)
{
	memset(e, 0, sizeof *e);
	e->md = md, e->fm = fm, e->ctx = 4096, e->cap = edge[E_CAP_HI].value;
	e->x_c2c = edge[E_X_C2C].value, e->x_mem = edge[E_X_MEM].value, e->prefetch = edge[E_PREFETCH].value;
	e->l2_hold = edge[E_L2_HOLD].value;
	e->road2_bw = edge[E_ROAD2].value, e->cores = edge[E_STEP_CORES].value, e->step_work = edge[E_STEP_WORK].value;
	e->router_step = edge[E_ROUTER_STEP].value, e->bwr = edge[E_BWR].value, e->bww = edge[E_BWW].value;
	e->gbe_rtt = edge[E_GBE_RTT].value;
}

static void shape_text(const Shape *s, char *buf, size_t len)
{
	snprintf(buf, len, "G=%d m=%d m2=%d m3=%d router_cpu=%d head_cpu=%.1f road2=%.0fMB split=%d", s->G, s->m, s->m2, s->m3, s->router_cpu, s->head_cpu,
		s->road2 / MB, s->split);
}

static const int Ns[] = {1, 2, 4, 8, 16, 32};
#define N_NS (int)(sizeof Ns / sizeof Ns[0])

static void rank(const Env *base, int brute)
{
	printf("format\tN\tshape\twords_per_s_total\twords_per_s_each\tms_per_round\tbound\tconfig\tvs_straight\n");
	for (int f = 0; f < N_FORMATS; f++) {
		Env e = *base;
		e.fm = &formats[f];
		for (int i = 0; i < N_NS; i++) {
			Best st = solve_family(&e, FAM_STRAIGHT, Ns[i], brute, 0);
			for (int fam = 0; fam < FAM_COUNT; fam++) {
				Best b = solve_family(&e, fam, Ns[i], brute, 0);
				char cfg[160];
				if (b.r.T == 0) {
					printf("%s\t%d\t%s\t-\t-\t-\tinfeasible\t-\t-\n", formats[f].name, Ns[i], fam_name[fam]);
					continue;
				}
				shape_text(&b.s, cfg, sizeof cfg);
				printf("%s\t%d\t%s\t%.2f\t%.2f\t%.3f\t%s\t%s\t%+.2f%%\n", formats[f].name, Ns[i], fam_name[fam], b.r.wps,
					b.r.wps / Ns[i], b.r.T * 1e3, b.r.bound, cfg, (b.r.wps / st.r.wps - 1) * 100);
			}
		}
	}
}

static void edges(void)
{
	printf("edge\tvalue\tunit\treceipt\n");
	for (int i = 0; i < E_COUNT; i++) printf("%s\t%g\t%s\t%s\n", edge[i].name, edge[i].value, edge[i].unit, edge[i].receipt);
}

static void parts(const Env *e)
{
	const Model *md = e->md;
	double eb = expert_bytes(e);
	printf("part\tbytes_on_disk\treuse\tbytes_per_word_N1\n");
	printf("routed experts (8 of 256 per layer)\t%.0f\tper word, union over N\t%.0f\n", md->expert_total,
		md->layers * md->top_k * eb);
	printf("one routed expert\t%.0f\t-\t-\n", eb);
	printf("linear attention (30 layers)\t%.0f\tper stroke\t%.0f\n", md->linear_attn, md->linear_attn);
	printf("full attention (10 layers)\t%.0f\tper stroke\t%.0f\n", md->full_attn, md->full_attn);
	printf("router (40 x 256x2048 bf16)\t%.0f\tper stroke\t%.0f\n", md->router, md->router);
	printf("shared experts\t%.0f\tper stroke\t%.0f\n", md->shared, md->shared);
	printf("norms\t%.0f\tper stroke\t%.0f\n", md->norms, md->norms);
	printf("lm_head (bf16)\t%.0f\tper stroke\t%.0f\n", md->lm_head, head_bytes(e));
	printf("DeltaNet state f32 (per conversation, read+write)\t%.0f\tper conversation per word\t%.0f\n",
		md->lin_layers * md->state_f32, 2 * md->lin_layers * md->state_f32);
	printf("conv state f32 (per conversation, read+write)\t%.0f\tper conversation per word\t%.0f\n",
		md->lin_layers * md->conv_f32, 2 * md->lin_layers * md->conv_f32);
	printf("KV per word of context (per conversation)\t%.0f\tread per word x context\t%.0f\n", md->kv_per_tok,
		md->kv_per_tok * e->ctx);
	printf("figure-eight crossing per layer (out + back)\t%.0f\tper conversation per word x 30\t%.0f\n",
		md->cyl_out + md->cyl_back, md->lin_layers * (md->cyl_out + md->cyl_back));
	double w = trunk_bytes(e) + md->layers * md->top_k * eb + head_bytes(e);
	printf("weights per word N=1\t-\t-\t%.0f\n", w);
}

static int selftest(const Model *md)
{
	int fail = 0;
	Env e;
	env_default(&e, md, &formats[0]);
	/* 1. the coordinator's first pass: 3.747 GB at 237 GB/s; figure eight at 0, 2, 10 us; break-even 17.7 us */
	{
		Env c = e;
		c.coord = 1;
		c.ctx = 4096;
		/* the coordinator used 3.537 GB of weights; put the difference to the headers into the norms line */
		Model mc = *md;
		double w = trunk_bytes(&c) + md->layers * md->top_k * md->expert_total / md->experts + md->lm_head;
		mc.norms += 3.537e9 - w - 2 * md->lin_layers * md->conv_f32; /* their 3.747 had no conv state */
		c.md = &mc;
		Shape st = {.fam = FAM_STRAIGHT, .N = 1, .G = 1};
		Result r = eval(&c, &st);
		printf("coord straight: %.2f words/s (want 63.25)\n", r.wps);
		fail |= fabs(r.wps - 63.25) > 0.02;
		double want[] = {65.45, 65.19, 64.19}, xs[] = {0, 2e-6, 10e-6};
		for (int i = 0; i < 3; i++) {
			c.x_c2c = xs[i];
			Shape f8 = {.fam = FAM_EIGHT, .N = 1, .G = 1, .m = 30};
			Result q = eval(&c, &f8);
			printf("coord figure eight at %.0f us: %.2f words/s (want %.2f)\n", xs[i] * 1e6, q.wps, want[i]);
			fail |= fabs(q.wps - want[i]) > 0.02;
		}
		c.x_c2c = 0;
		Shape f8 = {.fam = FAM_EIGHT, .N = 1, .G = 1, .m = 30};
		double be = (eval(&c, &st).T - eval(&c, &f8).T) / 30;
		printf("coord break-even round trip: %.1f us (want 17.7)\n", be * 1e6);
		fail |= fabs(be * 1e6 - 17.7) > 0.1;
	}
	/* 2. exactness: the line-crossing solver equals brute force on every family, format, N and a grid of unknowns */
	int cases = 0, agree = 0, caught = 0;
	double xs[] = {0, 2e-6, 10e-6, 30e-6}, pf[] = {0, 8e6, 24e6}, r2[] = {0, 100e9}, caps[] = {49e6, 73e6, 400e6};
	double works[] = {0, 118e-6, 30e-6}, l2s[] = {0, 24e6};
	for (int f = 0; f < N_FORMATS; f++)
		for (int a = 0; a < 4; a++)
			for (int p = 0; p < 3; p++)
				for (int q = 0; q < 2; q++)
					for (int c = 0; c < 3; c++)
						for (int w = 0; w < 3; w++)
						for (int l2 = 0; l2 < 2; l2++)
							for (int i = 0; i < N_NS; i++)
								for (int fam = 0; fam < FAM_COUNT; fam++) {
									Env t = e;
									t.fm = &formats[f], t.x_c2c = xs[a], t.prefetch = pf[p], t.road2_bw = r2[q];
									t.cap = caps[c], t.step_work = works[w], t.l2_hold = l2s[l2];
									Best s = solve_family(&t, fam, Ns[i], 0, 0), b = solve_family(&t, fam, Ns[i], 1, 0);
									Best bad = solve_family(&t, fam, Ns[i], 0, 1);
									cases++;
									if (fabs(s.r.T - b.r.T) <= 1e-12 * (b.r.T + 1e-30)) agree++;
									else if (agree + 5 > cases)
										fprintf(stderr, "DISAGREE fam %d N %d: solve %.9g brute %.9g\n", fam, Ns[i],
											s.r.T, b.r.T);
									if (fabs(bad.r.T - b.r.T) > 1e-12 * b.r.T) caught++;
								}
	printf("exactness: %d/%d cases solve == brute force\n", agree, cases);
	printf("negative control: planted ends-only solver wrong in %d cases (must be > 0)\n", caught);
	fail |= agree != cases;
	fail |= caught == 0;
	printf("%s\n", fail ? "SELFTEST FAIL" : "SELFTEST PASS");
	return fail;
}

static void config(const Env *e, int N)
{
	/* the winner and the runner-up across families for one format and N, as a schedule file */
	Best all[FAM_COUNT];
	int order[FAM_COUNT];
	/* only shapes this machine can build today: no second Spark exists (MACHINE_CEILING.md:74-75) */
	for (int f = 0; f < FAM_COUNT; f++) {
		all[f] = solve_family(e, f, N, 0, 0), order[f] = f;
		if (f == FAM_TWO_SPARK || f == FAM_EXPERT_SPLIT || f == FAM_TWO_MAC) all[f].r.T = 0; /* Mac: Drake's daily machine, no serving on it (L258 brief) */
	}
	for (int i = 0; i < FAM_COUNT; i++)
		for (int j = i + 1; j < FAM_COUNT; j++) {
			double ti = all[order[i]].r.T ? all[order[i]].r.T : 1e9, tj = all[order[j]].r.T ? all[order[j]].r.T : 1e9;
			/* ties go to the simpler shape (lower family number): solver free equal to figure eight reports figure eight */
			if (tj < ti - 1e-12) { int t = order[i]; order[i] = order[j]; order[j] = t; }
		}
	char first[160] = "";
	for (int k = 0, shown = 0; k < FAM_COUNT && shown < 2; k++) {
		const Best *b = &all[order[k]];
		char cfg[160];
		shape_text(&b->s, cfg, sizeof cfg);
		if (b->r.T == 0 || !strcmp(cfg, first)) continue; /* the runner-up must be a different shape */
		if (!shown) strcpy(first, cfg);
		int is_runner = shown++;
		printf("# %s for %s, N=%d: %.2f words/s total predicted (%s bound)\n", is_runner ? "runner-up" : "winner",
			e->fm->name, N, b->r.wps, b->r.bound);
		printf("[%s]\nshape = %s\nformat = %s\nconversations = %d\ngroups = %d\nper_group = %d\n", is_runner ? "runner_up" : "winner",
			fam_name[b->s.fam], e->fm->name, N, b->s.G, N / b->s.G);
		printf("state_on_cpu_layers = %d\nstate_in_gpu_l2_layers = %d\nstate_over_road2_layers = %d\nrouter_on_cpu = %d\nhead_cpu_share = %.2f\nroad2_bytes = %.0f\nsplit_layer = %d\n",
			b->s.m, b->s.m2, b->s.m3, b->s.router_cpu, b->s.head_cpu, b->s.road2, b->s.split);
		printf("cylinder = %s\npredicted_ms_per_round = %.4f\npredicted_words_per_s = %.3f\n\n",
			b->s.m ? (b->s.cyl_in_dram ? "dram" : "c2c") : "none", b->r.T * 1e3, b->r.wps);
	}
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: geometry parts.tsv rank|brute|edges|parts|selftest|config|sweep [key=value ...]\n");
		return 2;
	}
	Model md = model_35b(argv[1]);
	Env e;
	env_default(&e, &md, &formats[0]);
	int N = 1;
	for (int i = 3; i < argc; i++) {
		char k[32];
		double v;
		if (sscanf(argv[i], "%31[^=]=%lf", k, &v) != 2) continue;
		if (!strcmp(k, "x_us")) e.x_c2c = v * US;
		else if (!strcmp(k, "prefetch_mb")) e.prefetch = v * MB;
		else if (!strcmp(k, "l2_hold_mb")) e.l2_hold = v * MB;
		else if (!strcmp(k, "road2_gbs")) e.road2_bw = v * GB;
		else if (!strcmp(k, "cap_mb")) e.cap = v * MB;
		else if (!strcmp(k, "step_us")) e.step_work = v * US;
		else if (!strcmp(k, "cores")) e.cores = v;
		else if (!strcmp(k, "ctx")) e.ctx = v;
		else if (!strcmp(k, "bwr_gbs")) e.bwr = v * GB;
		else if (!strcmp(k, "gbe_rtt_us")) e.gbe_rtt = v * US;
		else if (!strcmp(k, "format")) e.fm = &formats[(int)v];
		else if (!strcmp(k, "N")) N = (int)v;
		else { fprintf(stderr, "unknown key %s\n", k); return 2; }
	}
	if (!strcmp(argv[2], "rank")) rank(&e, 0);
	else if (!strcmp(argv[2], "brute")) rank(&e, 1);
	else if (!strcmp(argv[2], "edges")) edges();
	else if (!strcmp(argv[2], "parts")) parts(&e);
	else if (!strcmp(argv[2], "selftest")) return selftest(&md);
	else if (!strcmp(argv[2], "config")) config(&e, N);
	else { fprintf(stderr, "unknown command %s\n", argv[2]); return 2; }
	return 0;
}
