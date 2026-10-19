/* TY-4/TY-5 wall amendment reducer (docs/turing/TURING_YIELD_ENERGY_PROTOCOL_V0.md,
 * amendment 2).
 *
 * usage: ty_energy_wall OUTDIR SENSOR_CSV
 *
 * Reads OUTDIR/offsets.txt (window file, epoch ns, /proc/uptime s, written by
 * tools/ty_energy_wall_run.sh right before each window) and OUTDIR/raw/s2_*.jsonl
 * (ty_energy_window records), places each window [GO, GO + planned] on the
 * sensor log, and computes E_w(wall) = mean UPS W over the rows inside x planned.
 * Per configuration and round: I = E_AB - E_A - E_B + E_0, slowdowns from call
 * counts; mean I with a 95% Student-t interval; check (a) wall form.
 * Package energy is not read (no meter): printed UNAVAILABLE.
 */
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXR 16
static void die(const char *m) { fprintf(stderr, "ty_energy_wall: %s\n", m); exit(2); }

typedef struct { long t; double w; double load; } Row;
static Row *rows; static size_t nrows;

static const char *field(const char *s, const char *key)
{
	char pat[64];
	snprintf(pat, sizeof pat, "\"%s\":", key);
	const char *p = strstr(s, pat);
	return p ? p + strlen(pat) : NULL;
}

static double num(const char *s, const char *key)
{
	const char *p = field(s, key);
	return p ? strtod(p, NULL) : NAN;
}

static int str(const char *s, const char *key, char *out, size_t n)
{
	const char *p = field(s, key);
	if (!p || *p != '"') return -1;
	p++;
	size_t k = 0;
	while (*p && *p != '"' && k + 1 < n) out[k++] = *p++;
	out[k] = 0;
	return 0;
}

typedef struct { int ok; double e, calls_a, calls_b, load; int nrow; } Win;
/* [config 0=S6,1=S2][round][cond 0=IDLE,1=A,2=B,3=AB] */
static Win W[2][MAXR + 1][4];
static const char *CFG[2] = {"S6", "S2"};
static const char *COND[4] = {"IDLE", "A", "B", "AB"};

static double tq(int df)
{
	static const double t[] = {0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228};
	return df >= 1 && df <= 10 ? t[df] : 1.96;
}

static void window(const char *dir, const char *name, long long ep_ns, double up_s)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/raw/%s", dir, name);
	FILE *f = fopen(path, "r");
	if (!f) return;
	size_t cap = 1 << 22;
	char *b = malloc(cap + 1);
	if (!b) die("oom");
	size_t n = fread(b, 1, cap, f);
	b[n] = 0;
	fclose(f);
	char cfg[16], cond[16];
	if (str(b, "config", cfg, sizeof cfg) || str(b, "level", cond, sizeof cond)) { free(b); return; }
	int c = !strcmp(cfg, "S6") ? 0 : !strcmp(cfg, "S2") ? 1 : -1;
	int k = -1;
	for (int i = 0; i < 4; i++) if (!strcmp(cond, COND[i])) k = i;
	int r = (int)num(b, "round");
	if (c < 0 || k < 0 || r < 1 || r > MAXR) { free(b); return; }
	Win *w = &W[c][r][k];
	double go = num(b, "go_ns"), planned = num(b, "planned_ns");
	w->ok = isfinite(go) && isfinite(planned) && planned > 0 && num(b, "spawn_ok") == 1.0;
	/* participants */
	const char *p = field(b, "participants");
	int nparts = 0;
	while (p && (p = strstr(p, "{\"ok\":")) != NULL) {
		int pok = atoi(p + 6) == 1;
		const char *res = strstr(p, "\"result\":");
		char tag[8] = "";
		if (!res || str(res, "tag", tag, sizeof tag) || !pok || num(res, "oracle_ok") != 1.0 || num(res, "pinned") != 1.0) w->ok = 0;
		if (!strcmp(tag, "A")) w->calls_a = num(res, "calls");
		if (!strcmp(tag, "B")) w->calls_b = num(res, "calls");
		nparts++;
		p += 6;
	}
	int want = k == 0 ? 0 : k == 3 ? 2 : 1;
	if (nparts != want) w->ok = 0;
	/* epoch placement: go (CLOCK_MONOTONIC ns) + (epoch - boottime) */
	double off_s = (double)ep_ns / 1e9 - up_s;
	double t0 = go / 1e9 + off_s, t1 = t0 + planned / 1e9;
	double s = 0, l = 0;
	int m = 0;
	for (size_t i = 0; i < nrows; i++)
		if (rows[i].t >= t0 && rows[i].t <= t1) { s += rows[i].w; l += rows[i].load; m++; }
	w->nrow = m;
	if (m < 10) w->ok = 0;
	w->e = m ? s / m * planned / 1e9 : NAN;
	w->load = m ? l / m : NAN;
	free(b);
}

int main(int argc, char **argv)
{
	if (argc != 3) die("usage: ty_energy_wall OUTDIR SENSOR_CSV");
	FILE *f = fopen(argv[2], "r");
	if (!f) die("sensor csv");
	char line[512];
	size_t cap = 0;
	while (fgets(line, sizeof line, f)) {
		Row r;
		char iso[64];
		double gw, ut;
		if (sscanf(line, "%ld,%63[^,],%lf,%lf,%lf,%lf", &r.t, iso, &r.w, &gw, &ut, &r.load) != 6) continue;
		if (nrows == cap) { cap = cap ? cap * 2 : 4096; rows = realloc(rows, cap * sizeof *rows); if (!rows) die("oom"); }
		rows[nrows++] = r;
	}
	fclose(f);
	char path[1024];
	snprintf(path, sizeof path, "%s/offsets.txt", argv[1]);
	f = fopen(path, "r");
	if (!f) die("offsets.txt");
	char name[256];
	long long ep;
	double up;
	while (fscanf(f, "%255s %lld %lf", name, &ep, &up) == 3) window(argv[1], name, ep, up);
	fclose(f);
	struct timespec m, bt;
	clock_gettime(CLOCK_MONOTONIC, &m);
	clock_gettime(CLOCK_BOOTTIME, &bt);
	printf("TYW (TY-4/TY-5 wall amendment). BOOTTIME - MONOTONIC now = %.3f s (0 = no suspend; offsets valid)\n",
	       (double)(bt.tv_sec - m.tv_sec) + (double)(bt.tv_nsec - m.tv_nsec) / 1e9);
	printf("package energy: UNAVAILABLE (no aien_spbm hwmon). wall = UPS output, 10 W steps, ~2 s rows\n\n");
	for (int c = 0; c < 2; c++) {
		double I[MAXR + 1], EAB[MAXR + 1];
		int n = 0;
		printf("%s\n", CFG[c]);
		for (int r = 1; r <= MAXR; r++) {
			Win *w = W[c][r];
			if (!w[0].nrow && !w[1].nrow && !w[2].nrow && !w[3].nrow) continue;
			int ok = w[0].ok && w[1].ok && w[2].ok && w[3].ok;
			printf("  round %d: E0 %.1f  EA %.1f  EB %.1f  EAB %.1f J (rows %d/%d/%d/%d, load %.1f/%.1f/%.1f/%.1f)", r, w[0].e, w[1].e,
			       w[2].e, w[3].e, w[0].nrow, w[1].nrow, w[2].nrow, w[3].nrow, w[0].load, w[1].load, w[2].load, w[3].load);
			if (!ok) { printf("  REFUSED\n"); continue; }
			double i_r = w[3].e - w[1].e - w[2].e + w[0].e;
			double sa = 1 - w[3].calls_a / w[1].calls_a, sb = 1 - w[3].calls_b / w[2].calls_b;
			printf("  I %.1f J  slowdown A %.4f B %.4f\n", i_r, sa, sb);
			I[n] = i_r;
			EAB[n] = w[3].e;
			n++;
		}
		if (n < 2) { printf("  fewer than 2 valid rounds: no interval\n\n"); continue; }
		double mi = 0, me = 0, sd = 0;
		for (int k = 0; k < n; k++) { mi += I[k] / n; me += EAB[k] / n; }
		for (int k = 0; k < n; k++) sd += (I[k] - mi) * (I[k] - mi);
		sd = sqrt(sd / (n - 1));
		double hw = tq(n - 1) * sd / sqrt((double)n);
		printf("  mean I %.1f J, sd %.1f J, 95%% t-interval [%.1f, %.1f] J (half-width %.1f J = %.2f%% of mean E_AB %.1f J)\n", mi, sd,
		       mi - hw, mi + hw, hw, 100 * hw / me, me);
		printf("  I distinguishable from zero: %s\n", (mi - hw > 0 || mi + hw < 0) ? "yes" : "no");
		printf("  CHECK (a) wall form: %s (valid rounds %d of 3; half-width <= 2%% of E_AB: %s)\n\n",
		       n >= 3 && hw <= 0.02 * me ? "PASS" : "FAIL", n, hw <= 0.02 * me ? "yes" : "no");
	}
	printf("CHECK (b): NOT RUN (amendment 2: waits for the package meter)\n");
	return 0;
}
