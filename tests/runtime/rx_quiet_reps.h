/* Speed checks on a shared machine: keep only the repetitions the machine did
 * not disturb.
 *
 * A stopwatch check compares wall times. On the Spark other jobs share the
 * cores, and a run whose threads sat runnable without a core (the kernel's
 * run-queue delay) is longer by time the code under test never asked for.
 * That delay is measured, not guessed: /proc/self/task/<tid>/schedstat field 2
 * is the time each thread of this process has spent runnable but waiting for
 * a CPU (Documentation/scheduler/sched-stats.rst, "time spent waiting on a
 * runqueue"). A repetition whose threads waited more than the allowance below
 * is set aside and run again; the check then takes the same statistic (the
 * median) with the same threshold over undisturbed repetitions only.
 *
 * Nothing is loosened: the work, the statistic and the threshold are the
 * ones the check always had. A real slowdown is in every repetition,
 * undisturbed ones included, so it still fails. When too few repetitions are
 * undisturbed within the attempt budget the check is refused and says why
 * (rx_busy_refuse below: NOT_RUN cpu_busy, never a pass). Where schedstat is missing every
 * repetition counts (the old behaviour) and the output says so.
 *
 * The delay is summed over the whole process, so it cannot tell the
 * machine's load from contention the code under test makes itself. A leg
 * that intermittently makes its own threads wait (one run in three, say)
 * would have exactly those slow runs set aside, and its slowdown would pass
 * unseen. So each leg's disturbed repetitions are counted, and the check is
 * refused (NOT_RUN leg_skew, never a pass) when the leg the check claims is
 * the faster one was disturbed significantly more often than the other leg
 * (rx_pairs_skew_p below) and the claim holds on the kept runs. A claim
 * that fails on them is judged as it would be without the test: the skew
 * only set aside more of the claimed-faster leg's runs, which favours the
 * claim. Set-aside runs of the other leg only make that leg look faster,
 * which works against the claim, so the test is one-sided.
 *
 * Known limit: the test assumes both legs offer the machine's load the same
 * number of threads to delay. Where one leg keeps several workers on cores
 * and the other one (a parallel graph against its serialized fusion), load
 * alone disturbs the wider leg more often: on the loaded Spark the 4-worker
 * leg was refused for skew in runs where nothing but load was at work, and
 * dividing each repetition's delay by its mean number of threads on a core
 * still left 7 of 25 such runs refused. Checks whose legs differ in
 * parallelism therefore skip the skew test and say so in their output and
 * receipt; for them, contention a leg makes itself only intermittently can
 * still hide in its set-aside runs, as before this test existed.
 */
#ifndef RX_QUIET_REPS_H
#define RX_QUIET_REPS_H

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>

/* Allowance: run-queue delay summed over all threads of the process during
 * one repetition, at most RX_QUIET_FLOOR_NS plus RX_QUIET_PERMILLE of the
 * repetition's wall time. The floor covers waking an idle worker (a few
 * microseconds per hand-off on an idle core). */
#define RX_QUIET_FLOOR_NS 50000ull
#define RX_QUIET_PERMILLE 20ull

/* Sum of run-queue delay (ns) over every thread of this process; -1 when the
 * kernel does not report it. */
static inline int64_t rx_run_delay_ns(void) {
    DIR *d = opendir("/proc/self/task");
    if (!d) return -1;
    int64_t sum = 0;
    int seen = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
        char path[288];
        snprintf(path, sizeof path, "/proc/self/task/%s/schedstat", de->d_name);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        unsigned long long on_cpu = 0, waited = 0;
        if (fscanf(fp, "%llu %llu", &on_cpu, &waited) == 2) {
            sum += (int64_t)waited;
            seen = 1;
        }
        fclose(fp);
    }
    closedir(d);
    return seen ? sum : -1;
}

/* Whether a repetition of `wall_ns` whose threads waited `delay_ns` for a
 * core counts. delay < 0 (not reported) always counts. */
static inline int rx_quiet_rep(int64_t delay_ns, uint64_t wall_ns) {
    if (delay_ns < 0) return 1;
    return (uint64_t)delay_ns <= RX_QUIET_FLOOR_NS + wall_ns * RX_QUIET_PERMILLE / 1000ull;
}

/* Leg skew. Under "the machine, not the leg, disturbs runs" a leg's
 * disturbed repetitions are no more frequent than the other leg's. Legs are
 * paired (each attempt times both legs back to back, as in
 * rx_workflow_fusion.c), so the test is McNemar's exact test, one-sided at
 * RX_SKEW_ALPHA: among the attempts where exactly one leg was disturbed, the
 * count where it was the claimed-faster leg is Binomial(n, 1/2) under the
 * null; p = P(X >= that count).
 *
 * A false refusal is a NOT_RUN, never a false FAIL. At RX_SKEW_ALPHA = 1/1000
 * the paired test cannot refuse with fewer than 10 one-leg-disturbed
 * attempts, so a quiet machine (a handful of disturbed attempts) is never
 * refused. Thresholds (WolframLanguageEvaluator, 2026-10-31, with
 * up[n_, k_] := 1 - CDF[BinomialDistribution[n, 1/2], k - 1]):
 *   Min[Select[Range[1, 40], up[#, #] <= 1/1000 &]] -> 10
 *   up[10, 10] -> 1/1024 (0.000977), up[10, 9] -> 0.0107
 *   smallest refused count for n = 20, 50, 100, 300, 600, 2460:
 *   18, 37, 66, 178, 339, 1308
 * rx_sign_upper is checked against these in rx_workflow_fusion.c
 * (t_skew_math). */
#define RX_SKEW_ALPHA 0.001

/* The tail sums pmf weights scaled to 1 at the mode, built by the pmf ratio
 * of neighbours, so nothing overflows and no libm is needed; weights that
 * underflow are below 1e-300 of the total. */

/* P(X >= k) for X ~ Binomial(n, 1/2). */
static inline double rx_sign_upper(uint32_t n, uint32_t k) {
    if (k == 0) return 1.0;
    if (k > n) return 0.0;
    uint32_t m = n / 2;
    double w = 1.0, total = 1.0, tail = m >= k ? 1.0 : 0.0;
    for (uint32_t i = m + 1; i <= n; i++) {        /* w(i) = w(i-1) (n-i+1) / i */
        w *= (double)(n - i + 1) / (double)i;
        total += w;
        if (i >= k) tail += w;
    }
    w = 1.0;
    for (uint32_t i = m; i-- > 0;) {               /* w(i) = w(i+1) (i+1) / (n-i) */
        w *= (double)(i + 1) / (double)(n - i);
        total += w;
        if (i >= k) tail += w;
    }
    return tail / total;
}

/* Paired legs: attempts where exactly one leg was disturbed, by leg. */
typedef struct {
    uint32_t only[2];
} RxPairs;

static inline void rx_pairs_note(RxPairs *s, int quiet0, int quiet1) {
    if (quiet0 != quiet1) s->only[quiet0 ? 1 : 0]++;
}

/* One-sided p that leg `fast` (the one the check claims is faster) was
 * disturbed more often than the other leg. */
static inline double rx_pairs_skew_p(const RxPairs *s, int fast) {
    return rx_sign_upper(s->only[0] + s->only[1], s->only[fast ? 1 : 0]);
}

/* A check that found the machine too busy to time is a refusal, not a
 * failure: nothing was measured, so nothing is judged, and the checks that
 * need that measurement are not judged either. The program then ends with
 * the local judge's "could not run" contract (aien-test SELF_NOT_RUN_EXIT):
 * stdout line `AIEN-OBS not_run_reason "<reason>"` and exit 77, a retryable
 * NOT_RUN, never a pass. Any real failure anywhere in the run wins: the exit
 * is then 1, whatever was refused. The reason is "cpu_busy", or "leg_skew"
 * when a leg-skew refusal (rx_skew_refuse) happened: then the timing could
 * not be trusted because of the code under test, not the machine. */
#define RX_NOT_RUN_EXIT 77

static inline unsigned *rx_busy_refusals(void) {
    static unsigned n;
    return &n;
}

static inline unsigned *rx_skew_refusals(void) {
    static unsigned n;
    return &n;
}

/* Record one refusal; `what` names the measurement that was not taken. */
static inline void rx_busy_refuse(const char *what) {
    (*rx_busy_refusals())++;
    fprintf(stderr, "  NOT RUN %s (cpu_busy): checks that need this measurement are not judged\n", what);
}

/* Record a leg-skew refusal: the leg the check claims is faster was disturbed
 * in `fast_disturbed` attempts, the other leg in `other_disturbed`, one-sided
 * p = `p` <= RX_SKEW_ALPHA. */
static inline void rx_skew_refuse(const char *what, uint32_t fast_disturbed, uint32_t other_disturbed, double p) {
    (*rx_busy_refusals())++;
    (*rx_skew_refusals())++;
    fprintf(stderr,
            "  NOT RUN %s (leg_skew): the leg claimed faster was disturbed in %u attempts, the other leg in %u "
            "(one-sided p %.3g <= %g): its set-aside runs may hide a slowdown it causes itself; checks that need "
            "this measurement are not judged\n",
            what, fast_disturbed, other_disturbed, p, RX_SKEW_ALPHA);
}

/* The not_run_reason of this run's refusals. */
static inline const char *rx_busy_reason(void) {
    return *rx_skew_refusals() ? "leg_skew" : "cpu_busy";
}

/* The program's exit status from its failure count and `pass`, everything
 * else its receipt judges: a refusal never covers a check judged false. */
static inline int rx_busy_exit(int failures, int pass) {
    if (failures) return 1;
    if (*rx_busy_refusals()) {
        if (!pass) return 1;
        printf("AIEN-OBS not_run_reason \"%s\"\n", rx_busy_reason());
        fflush(stdout);
        return RX_NOT_RUN_EXIT;
    }
    return 0;
}

/* A receipt's gate word: FAIL when `pass` (everything judged) is false,
 * NOT_RUN when a measurement was refused, else PASS. */
static inline const char *rx_busy_verdict(int pass) {
    if (!pass) return "FAIL";
    return *rx_busy_refusals() ? "NOT_RUN" : "PASS";
}

#endif
