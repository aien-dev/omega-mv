/* TY-7 reducer (docs/turing/TURING_YIELD_TY7_PROTOCOL.md).
 *
 * usage: ty7_reduce OUTDIR SENSOR_CSV [ROUNDS] [PROMPTS]
 *
 * Reads OUTDIR/r<R>-p<I>.json (server replies), intervals.log, session.log and
 * health.log written by tools/ty7_drive.sh, and the sensor CSV slice. Checks
 * every reply (section 5), builds the symbol stream per round (section 4),
 * codes it with uniform, B (prequential order-1 PPM-C, exclusion at the
 * order-0 step, started empty per round) and B_warm (primed on the other
 * rounds, counts not charged), takes L(D|M) from reply_logprob, integrates
 * wall power over request intervals (section 6) and prints the per-round and
 * pooled table and both verdicts (section 9). Writes OUTDIR/symbols.tsv (one
 * row per symbol: round reply pos id B_bits B_warm_bits).
 *
 * C only, no JSON library: the reply fields are found by key. Any missing or
 * malformed field refuses the round.
 */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define V_VOCAB 248320u          /* config.json vocab_size */
#define K_ALPHA (V_VOCAB + 1u)   /* + end symbol (id V) */
#define START_CTX K_ALPHA        /* order-1 context at the first token of a reply */
#define MAX_ROUNDS 3
#define MAX_PROMPTS 8
#define L_M_BITS 243220274608.0  /* protocol section 4, calc:6f0cbc58 */
#define L_M_DIR_BITS (30890056546.0 * 8.0)
#define LN2 0.69314718055994530942

static void die(const char *m) { fprintf(stderr, "ty7_reduce: %s\n", m); exit(2); }

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	size_t cap = 1 << 16, n = 0;
	char *b = malloc(cap + 1);
	if (!b) die("oom");
	size_t k;
	while ((k = fread(b + n, 1, cap - n, f)) > 0) {
		n += k;
		if (n == cap) { cap *= 2; b = realloc(b, cap + 1); if (!b) die("oom"); }
	}
	fclose(f);
	b[n] = 0;
	if (len) *len = n;
	return b;
}

/* Pointer just after `"key":` (whitespace skipped), or NULL. */
static const char *field(const char *s, const char *key)
{
	char pat[96];
	snprintf(pat, sizeof pat, "\"%s\"", key);
	const char *p = strstr(s, pat);
	if (!p) return NULL;
	p += strlen(pat);
	while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
	if (*p != ':') return NULL;
	p++;
	while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
	return p;
}

static int field_num(const char *s, const char *key, double *out)
{
	const char *p = field(s, key);
	if (!p) return -1;
	char *e;
	errno = 0;
	double v = strtod(p, &e);
	if (e == p || errno) return -1;
	*out = v;
	return 0;
}

/* ---------------- symbol streams ---------------- */

typedef struct { uint32_t *sym; uint32_t *reply; uint32_t n, cap; } Stream;

static void push(Stream *s, uint32_t sym, uint32_t reply)
{
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 4096;
		s->sym = realloc(s->sym, s->cap * sizeof *s->sym);
		s->reply = realloc(s->reply, s->cap * sizeof *s->reply);
		if (!s->sym || !s->reply) die("oom");
	}
	s->sym[s->n] = sym;
	s->reply[s->n] = reply;
	s->n++;
}

/* ---------------- B: prequential order-1 PPM-C ---------------- */

typedef struct { uint32_t sym, cnt; } Ent;
typedef struct { Ent *e; uint32_t u, cap; uint64_t n; } Ctx;
typedef struct { Ctx *ctx; uint32_t *n0; uint64_t n; uint32_t u; } Ppm;

static void ppm_init(Ppm *m)
{
	m->ctx = calloc(K_ALPHA + 1u, sizeof *m->ctx);
	m->n0 = calloc(K_ALPHA, sizeof *m->n0);
	if (!m->ctx || !m->n0) die("oom");
	m->n = 0;
	m->u = 0;
}

static void ppm_free(Ppm *m)
{
	for (uint32_t c = 0; c <= K_ALPHA; c++) free(m->ctx[c].e);
	free(m->ctx);
	free(m->n0);
}

static Ent *ctx_find(Ctx *c, uint32_t x)
{
	for (uint32_t i = 0; i < c->u; i++)
		if (c->e[i].sym == x) return &c->e[i];
	return NULL;
}

/* Code length in bits of x in context c (protocol section 4); no update. */
static double ppm_cost(const Ppm *m, uint32_t c, uint32_t x)
{
	Ctx *cx = &m->ctx[c];
	double bits = 0.0;
	uint64_t excl_n = 0;
	uint32_t excl_u = 0;
	if (cx->n > 0) {
		Ent *e = ctx_find(cx, x);
		double den = (double)cx->n + (double)cx->u;
		if (e) return -log2((double)e->cnt / den);
		bits += -log2((double)cx->u / den);
		for (uint32_t i = 0; i < cx->u; i++) excl_n += m->n0[cx->e[i].sym];
		excl_u = cx->u;
	}
	uint64_t n1 = m->n - excl_n;
	uint32_t u1 = m->u - excl_u;
	if (n1 > 0) {
		double den = (double)n1 + (double)u1;
		if (m->n0[x] > 0) return bits - log2((double)m->n0[x] / den); /* x not in S: else found above */
		bits += -log2((double)u1 / den);
	}
	return bits + log2((double)(K_ALPHA - m->u));
}

static void ppm_update(Ppm *m, uint32_t c, uint32_t x)
{
	Ctx *cx = &m->ctx[c];
	Ent *e = ctx_find(cx, x);
	if (e) e->cnt++;
	else {
		if (cx->u == cx->cap) {
			cx->cap = cx->cap ? cx->cap * 2 : 4;
			cx->e = realloc(cx->e, cx->cap * sizeof *cx->e);
			if (!cx->e) die("oom");
		}
		cx->e[cx->u].sym = x;
		cx->e[cx->u].cnt = 1;
		cx->u++;
	}
	cx->n++;
	if (m->n0[x] == 0) m->u++;
	m->n0[x]++;
	m->n++;
}

/* Context of symbol i in a stream: previous symbol of the same reply, else START_CTX. */
static uint32_t ctx_of(const Stream *s, uint32_t i)
{
	return (i == 0 || s->reply[i - 1] != s->reply[i]) ? START_CTX : s->sym[i - 1];
}

static void ppm_prime(Ppm *m, const Stream *s)
{
	for (uint32_t i = 0; i < s->n; i++) ppm_update(m, ctx_of(s, i), s->sym[i]);
}

/* ---------------- sensors, intervals, health ---------------- */

typedef struct { long t; double w, gw, util, load, mem, locks; } Row;
static Row *rows; static size_t nrows;
typedef struct { int64_t t; int busy; } Hp;
static Hp *hp; static size_t nhp;

static void load_sensors(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) die("cannot open sensor csv");
	char line[512];
	size_t cap = 0;
	while (fgets(line, sizeof line, f)) {
		Row r;
		char iso[64];
		if (sscanf(line, "%ld,%63[^,],%lf,%lf,%lf,%lf,%lf,%lf", &r.t, iso, &r.w, &r.gw, &r.util, &r.load, &r.mem, &r.locks) != 8) continue;
		if (nrows == cap) { cap = cap ? cap * 2 : 4096; rows = realloc(rows, cap * sizeof *rows); if (!rows) die("oom"); }
		rows[nrows++] = r;
	}
	fclose(f);
}

static void load_health(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) die("cannot open health.log");
	char b[64];
	long long t;
	size_t cap = 0;
	while (fscanf(f, "%lld %63s", &t, b) == 2) {
		if (nhp == cap) { cap = cap ? cap * 2 : 4096; hp = realloc(hp, cap * sizeof *hp); if (!hp) die("oom"); }
		hp[nhp].t = t;
		hp[nhp].busy = b[0] == '0' ? 0 : b[0] == '1' ? 1 : -1;
		nhp++;
	}
	fclose(f);
}

/* Engine idle at epoch second t: nearest polls before and after both report busy=false. */
static int engine_idle_at(long t)
{
	int64_t tn = (int64_t)t * 1000000000LL;
	int before = -1, after = -1;
	for (size_t i = 0; i < nhp; i++) {
		if (hp[i].t <= tn) before = hp[i].busy;
		else { after = hp[i].busy; break; }
	}
	return before == 0 && after == 0;
}

/* Mean wall W over rows with t0 <= t <= t1 (epoch s, truncated, as spark-receipt);
 * if none, the row nearest the midpoint. Returns rows used. */
static int mean_w(int64_t t0n, int64_t t1n, double *w, double *gw)
{
	long t0 = (long)(t0n / 1000000000LL), t1 = (long)(t1n / 1000000000LL);
	double s = 0, g = 0;
	int k = 0;
	for (size_t i = 0; i < nrows; i++)
		if (rows[i].t >= t0 && rows[i].t <= t1) { s += rows[i].w; g += rows[i].gw; k++; }
	if (k) { *w = s / k; *gw = g / k; return k; }
	long mid = (t0 + t1) / 2, best = -1;
	for (size_t i = 0; i < nrows; i++)
		if (best < 0 || labs(rows[i].t - mid) < labs(rows[best].t - mid)) best = (long)i;
	if (best < 0) die("no sensor rows");
	*w = rows[best].w;
	*gw = rows[best].gw;
	return 0;
}

static const Row *row_near(int64_t tn)
{
	long t = (long)(tn / 1000000000LL);
	const Row *b = NULL;
	for (size_t i = 0; i < nrows; i++)
		if (!b || labs(rows[i].t - t) < labs(b->t - t)) b = &rows[i];
	return b;
}

/* Self-test: at several points of a pseudo-random reply stream, the B code
 * must satisfy Kraft with equality: sum over all K symbols of 2^-cost = 1. */
static int selftest(void)
{
	Ppm m;
	ppm_init(&m);
	uint64_t s = 0x7459370001ULL;
	uint32_t prev = START_CTX;
	int bad = 0;
	for (int k = 0; k < 3000; k++) {
		s = s * 6364136223846793005ULL + 1442695040888963407ULL;
		uint32_t x = (uint32_t)((s >> 33) % 400u);           /* small working set: repeats */
		if ((s >> 20) % 50 == 0) x = (uint32_t)((s >> 13) % K_ALPHA); /* rare new symbols */
		if (k % 500 == 0 || k == 2999) {
			for (int cc = 0; cc < 2; cc++) {
				uint32_t c = cc ? prev : START_CTX;
				double sum = 0;
				for (uint32_t y = 0; y < K_ALPHA; y++) sum += exp2(-ppm_cost(&m, c, y));
				if (fabs(sum - 1.0) > 1e-9) { printf("selftest: step %d ctx %u sum %.12f\n", k, c, sum); bad = 1; }
			}
		}
		ppm_update(&m, (k % 37 == 0) ? START_CTX : prev, x);
		prev = (k % 37 == 0) ? START_CTX : x;
	}
	ppm_free(&m);
	printf("selftest: %s\n", bad ? "FAIL" : "OK (Kraft sum = 1 within 1e-9 at 14 checks)");
	return bad;
}

/* ---------------- main ---------------- */

typedef struct {
	int ok; char why[160];
	uint32_t replies, tokens, symbols, ends;
	double lm_bits, lb_bits, lbw_bits, lu_bits;
	double req_s, e_gross, e_incr, gw_mean;
	int base_rows; double p_base;
	int64_t t_first, t_last;
	uint64_t tries;
} Round;

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest();
	if (argc < 3) die("usage: ty7_reduce OUTDIR SENSOR_CSV [ROUNDS] [PROMPTS]");
	const char *out = argv[1];
	int R = argc > 3 ? atoi(argv[3]) : MAX_ROUNDS;
	int P = argc > 4 ? atoi(argv[4]) : MAX_PROMPTS;
	if (R < 1 || R > MAX_ROUNDS || P < 1 || P > MAX_PROMPTS) die("bad ROUNDS/PROMPTS");
	char path[1024];
	load_sensors(argv[2]);
	snprintf(path, sizeof path, "%s/health.log", out);
	load_health(path);

	/* intervals and idle windows */
	int64_t iv0[MAX_ROUNDS + 1][MAX_PROMPTS + 1] = {{0}}, iv1[MAX_ROUNDS + 1][MAX_PROMPTS + 1] = {{0}};
	uint64_t ivtries[MAX_ROUNDS + 1][MAX_PROMPTS + 1] = {{0}};
	snprintf(path, sizeof path, "%s/intervals.log", out);
	FILE *f = fopen(path, "r");
	if (!f) die("cannot open intervals.log");
	int r, i;
	long long a, b;
	char rest[128];
	while (fscanf(f, "%d %d %lld %lld %127[^\n]", &r, &i, &a, &b, rest) == 5) {
		if (r < 1 || r > R || i < 1 || i > P) continue;
		iv0[r][i] = a;
		iv1[r][i] = b;
		const char *t = strstr(rest, "tries=");
		ivtries[r][i] = t ? strtoull(t + 6, NULL, 10) : 0;
	}
	fclose(f);
	int64_t idle0[MAX_ROUNDS + 1] = {0}, idle1[MAX_ROUNDS + 1] = {0};
	snprintf(path, sizeof path, "%s/session.log", out);
	f = fopen(path, "r");
	if (!f) die("cannot open session.log");
	char line[1024];
	while (fgets(line, sizeof line, f)) {
		int k;
		long long t;
		if (sscanf(line, "IDLE %d start %lld", &k, &t) == 2 && k >= 0 && k <= R) idle0[k] = t;
		if (sscanf(line, "IDLE %d end %lld", &k, &t) == 2 && k >= 0 && k <= R) idle1[k] = t;
	}
	fclose(f);

	Stream st[MAX_ROUNDS + 1];
	memset(st, 0, sizeof st);
	Round rd[MAX_ROUNDS + 1];
	memset(rd, 0, sizeof rd);

	for (r = 1; r <= R; r++) {
		Round *q = &rd[r];
		q->ok = 1;
		for (i = 1; i <= P && q->ok; i++) {
			snprintf(path, sizeof path, "%s/r%d-p%d.json", out, r, i);
			char *js = slurp(path, NULL);
			if (!js) { q->ok = 0; snprintf(q->why, sizeof q->why, "missing reply r%d-p%d", r, i); break; }
			double lp, sampled, greedy;
			const char *tk = field(js, "top_k_applied");
			const char *tp = field(js, "top_p");
			const char *fr = field(js, "finish_reason");
			const char *ids = field(js, "aien_token_ids");
			if (field_num(js, "reply_logprob", &lp) || field_num(js, "sampled_tokens", &sampled) ||
			    field_num(js, "greedy_tokens", &greedy) || !tk || !fr || !ids || *ids != '[') {
				q->ok = 0; snprintf(q->why, sizeof q->why, "r%d-p%d: missing field", r, i); free(js); break;
			}
			if (strncmp(tk, "false", 5) || (tp && strncmp(tp, "null", 4) && strtod(tp, NULL) != 1.0) || sampled <= 0 || greedy != 0) {
				q->ok = 0; snprintf(q->why, sizeof q->why, "r%d-p%d: sampling check failed (truncated or greedy)", r, i); free(js); break;
			}
			uint32_t reply = (uint32_t)(r * 100 + i), nid = 0;
			const char *p = ids + 1;
			while (*p && *p != ']') {
				char *e;
				unsigned long v = strtoul(p, &e, 10);
				if (e == p) { p++; continue; }
				if (v >= V_VOCAB) { q->ok = 0; snprintf(q->why, sizeof q->why, "r%d-p%d: token id out of range", r, i); break; }
				push(&st[r], (uint32_t)v, reply);
				nid++;
				p = e;
			}
			int stop = strncmp(fr, "\"stop\"", 6) == 0;
			if (q->ok) {
				if (nid == (uint32_t)sampled) { /* ids include every draw */ }
				else if (stop && nid + 1 == (uint32_t)sampled) { push(&st[r], V_VOCAB, reply); q->ends++; }
				else { q->ok = 0; snprintf(q->why, sizeof q->why, "r%d-p%d: %u ids vs %.0f draws", r, i, nid, sampled); }
			}
			q->replies++;
			q->tokens += nid;
			q->lm_bits += -lp / LN2;
			free(js);
			if (!iv0[r][i] || iv1[r][i] <= iv0[r][i]) { q->ok = 0; snprintf(q->why, sizeof q->why, "r%d-p%d: no interval", r, i); }
		}
		q->symbols = st[r].n;
	}

	/* code lengths */
	snprintf(path, sizeof path, "%s/symbols.tsv", out);
	FILE *sy = fopen(path, "w");
	if (!sy) die("cannot write symbols.tsv");
	fprintf(sy, "round\treply\tpos\tid\tB_bits\tB_warm_bits\n");
	for (r = 1; r <= R; r++) {
		if (!rd[r].ok) continue;
		Ppm b, w;
		ppm_init(&b);
		ppm_init(&w);
		for (int o = 1; o <= R; o++)
			if (o != r && rd[o].ok) ppm_prime(&w, &st[o]);
		uint32_t pos = 0;
		for (uint32_t k = 0; k < st[r].n; k++) {
			uint32_t c = ctx_of(&st[r], k), x = st[r].sym[k];
			pos = (c == START_CTX) ? 0 : pos + 1;
			double cb = ppm_cost(&b, c, x), cw = ppm_cost(&w, c, x);
			fprintf(sy, "%d\t%u\t%u\t%u\t%.6f\t%.6f\n", r, st[r].reply[k], pos, x, cb, cw);
			rd[r].lb_bits += cb;
			rd[r].lbw_bits += cw;
			ppm_update(&b, c, x);
			ppm_update(&w, c, x);
		}
		rd[r].lu_bits = (double)st[r].n * log2((double)K_ALPHA);
		ppm_free(&b);
		ppm_free(&w);
	}
	fclose(sy);

	/* energy */
	for (r = 1; r <= R; r++) {
		Round *q = &rd[r];
		if (!q->ok) continue;
		double sb = 0;
		int nb = 0;
		for (int k = r - 1; k <= r; k++) {
			if (!idle0[k] || !idle1[k]) continue;
			long t0 = (long)(idle0[k] / 1000000000LL), t1 = (long)(idle1[k] / 1000000000LL);
			for (size_t j = 0; j < nrows; j++)
				if (rows[j].t >= t0 && rows[j].t <= t1 && engine_idle_at(rows[j].t)) { sb += rows[j].w; nb++; }
		}
		q->base_rows = nb;
		q->p_base = nb ? sb / nb : NAN;
		double gws = 0;
		for (i = 1; i <= P; i++) {
			double w, gw, d = (double)(iv1[r][i] - iv0[r][i]) / 1e9;
			mean_w(iv0[r][i], iv1[r][i], &w, &gw);
			q->req_s += d;
			q->e_gross += w * d;
			q->e_incr += (w - q->p_base) * d;
			gws += gw * d;
			q->tries += ivtries[r][i];
		}
		q->gw_mean = gws / q->req_s;
		q->t_first = iv0[r][1];
		q->t_last = iv1[r][P];
	}

	/* report */
	printf("TY-7 reducer (protocol docs/turing/TURING_YIELD_TY7_PROTOCOL.md)\n");
	printf("K = %u symbols (vocab %u + end), L(M) = %.0f bits (whole dir %.0f bits)\n\n", K_ALPHA, V_VOCAB, L_M_BITS, L_M_DIR_BITS);
	double G[MAX_ROUNDS + 1], Gs[MAX_ROUNDS + 1], GE[MAX_ROUNDS + 1];
	int nok = 0, readable = 1;
	double sumG = 0, sumSym = 0, sumEg = 0, sumEi = 0, sumLM = 0, sumLB = 0, sumLBW = 0, sumLU = 0, sumTok = 0, sumS = 0;
	int incr_ok = 1;
	for (r = 1; r <= R; r++) {
		Round *q = &rd[r];
		if (!q->ok) { printf("round %d: REFUSED: %s\n", r, q->why); continue; }
		nok++;
		G[r] = q->lb_bits - q->lm_bits;
		Gs[r] = G[r] / q->symbols;
		GE[r] = G[r] / q->e_gross;
		int rr = q->req_s >= 30.0;
		readable &= rr;
		if (q->base_rows < 5) incr_ok = 0;
		const Row *s0 = row_near(q->t_first), *s1 = row_near(q->t_last);
		printf("round %d: replies %u tokens %u symbols %u (end symbols %u) request attempts %llu\n", r, q->replies, q->tokens, q->symbols, q->ends,
		       (unsigned long long)q->tries);
		printf("  L(D|uniform) %.3f  L(D|B) %.3f  L(D|B_warm) %.3f  L(D|M) %.3f bits\n", q->lu_bits, q->lb_bits, q->lbw_bits, q->lm_bits);
		printf("  per symbol: uniform %.4f  B %.4f  B_warm %.4f  M %.4f bits\n", q->lu_bits / q->symbols, q->lb_bits / q->symbols,
		       q->lbw_bits / q->symbols, q->lm_bits / q->symbols);
		printf("  G = %.3f bits (%.4f /symbol); T_V0 (full L(M)) = %.3f bits\n", G[r], Gs[r], G[r] - L_M_BITS);
		printf("  request time %.3f s (%s), %.3f tok/s; mean GPU W %.2f (context)\n", q->req_s, rr ? "readable" : "UNREADABLE <30 s",
		       q->tokens / q->req_s, q->gw_mean);
		printf("  E_gross %.1f J (%.2f J/token); P_base %.2f W from %d engine-idle IDLE rows; E_incr %s%.1f J (%.2f J/token)\n", q->e_gross,
		       q->e_gross / q->tokens, q->p_base, q->base_rows, q->base_rows < 5 ? "UNRESOLVED " : "", q->e_incr, q->e_incr / q->tokens);
		printf("  G/E_gross %.5f bit/J  G/E_incr %.5f bit/J  T_V0/E_gross %.4e bit/J\n", GE[r], G[r] / q->e_incr, (G[r] - L_M_BITS) / q->e_gross);
		printf("  box start: load1 %.2f MemAvail %.0f kB locks %.0f util %.0f | end: load1 %.2f MemAvail %.0f kB locks %.0f util %.0f\n\n",
		       s0->load, s0->mem, s0->locks, s0->util, s1->load, s1->mem, s1->locks, s1->util);
		sumG += G[r]; sumSym += q->symbols; sumEg += q->e_gross; sumEi += q->e_incr; sumLM += q->lm_bits; sumLB += q->lb_bits;
		sumLBW += q->lbw_bits; sumLU += q->lu_bits; sumTok += q->tokens; sumS += q->req_s;
	}
	if (nok != R) { printf("VERDICT: REFUSED (%d of %d rounds valid)\n", nok, R); return 1; }
	double mG = sumG / R, sd = 0;
	for (r = 1; r <= R; r++) sd += (G[r] - mG) * (G[r] - mG);
	sd = R > 1 ? sqrt(sd / (R - 1)) : 0;
	double eps_pool = 3e-6 * sumSym, margin = 3.0 * sqrt((double)R) * sd;
	double T_pool = sumG - L_M_BITS;
	printf("pooled: symbols %.0f tokens %.0f request s %.3f\n", sumSym, sumTok, sumS);
	printf("  L(D|uniform) %.3f L(D|B) %.3f L(D|B_warm) %.3f L(D|M) %.3f bits\n", sumLU, sumLB, sumLBW, sumLM);
	printf("  G %.3f bits (%.4f /symbol), sd_G %.3f bits, eps %.4f bits\n", sumG, sumG / sumSym, sd, eps_pool);
	printf("  T_V0 pooled (L(M) once) %.3f bits; margin 3 sqrt(%d) sd_G = %.3f bits\n", T_pool, R, margin);
	printf("  N* = L(M) / (G per symbol) = %.4e symbols\n", L_M_BITS / (sumG / sumSym));
	printf("  E_gross %.1f J (%.3f J/token)  E_incr %.1f J%s (%.3f J/token)\n", sumEg, sumEg / sumTok, sumEi, incr_ok ? "" : " UNRESOLVED", sumEi / sumTok);
	printf("  G/E_gross %.5f bit/J  G/E_incr %.5f bit/J  T_V0/E_gross %.4e bit/J\n\n", sumG / sumEg, sumG / sumEi, T_pool / sumEg);

	int v0 = T_pool - eps_pool > margin;
	for (r = 1; r <= R; r++) v0 &= (G[r] - L_M_BITS - 3e-6 * rd[r].symbols) > 0;
	printf("VERDICT TY-7 V0: %s (pooled T_V0 - eps > margin: %s; every round T_V0 > 0: %s)\n", v0 ? "PASS" : "FAIL",
	       T_pool - eps_pool > margin ? "yes" : "no", v0 ? "yes" : "no");
	int g1 = 1, g2 = 1, g3 = 1;
	double mGs = 0, mGE = 0;
	for (r = 1; r <= R; r++) { mGs += Gs[r] / R; mGE += GE[r] / R; }
	for (r = 1; r <= R; r++) {
		g1 &= (G[r] - 3e-6 * rd[r].symbols) > 3.0 * sd;
		g2 &= fabs(Gs[r] - mGs) <= 0.10 * mGs;
		g3 &= fabs(GE[r] - mGE) <= 0.20 * mGE;
	}
	printf("VERDICT G-rule (not V0): %s (G > 3 sd_G each round: %s; G/symbol within 10%%: %s; G/E_gross within 20%%: %s; readable: %s)\n",
	       g1 && g2 && g3 && readable ? "PASS" : "FAIL", g1 ? "yes" : "no", g2 ? "yes" : "no", g3 ? "yes" : "no", readable ? "yes" : "no");
	return 0;
}
