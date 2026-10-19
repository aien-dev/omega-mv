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
 * undisturbed within the attempt budget the check fails and says why; a
 * busy machine never turns into a pass. Where schedstat is missing every
 * repetition counts (the old behaviour) and the output says so.
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

#endif
