/* SPDX-License-Identifier: GPL-2.0-only */
/* GB10 CPU PMU attribution & branch/stall diagnostic contract (omega #453).
 * Read-only qualification contract for Cortex-A725 (armv8_pmuv3_0, type 10)
 * and Cortex-X925 (armv8_pmuv3_1, type 11). Changes no machine setting.
 */
#ifndef AIEN_GB10_PMU_CONTRACT_H
#define AIEN_GB10_PMU_CONTRACT_H

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PMU_CONTRACT_SCHEMA "gb10-pmu-contract/v1"
#define PMU_DEFAULT_SEED 0x9e3779b97f4a7c15ULL
#define PMU_DEFAULT_ITERS 1000000ULL
#define PMU_DEFAULT_REPS 3

#define PMU_MIDR_A725 0x410fd871ULL
#define PMU_MIDR_X925 0x410fd851ULL

#define PMU_N_LEGACY_EVENTS 6
#define PMU_N_EVENTS 10
#define PMU_N_CORE_CLASSES 2

struct pmu_event_spec {
    const char *name;
    unsigned long long code;
    const char *unit;
    int legacy;
};

static const struct pmu_event_spec pmu_events[PMU_N_EVENTS] = {
    { "cpu_cycles",          0x11ULL, "cycles",       1 },
    { "inst_retired",        0x08ULL, "instructions", 1 },
    { "bus_access",          0x19ULL, "accesses",     1 },
    { "ll_cache_miss_rd",    0x37ULL, "misses",       1 },
    { "l2d_cache_refill",    0x17ULL, "refills",      1 },
    { "mem_access",          0x13ULL, "accesses",     1 },
    { "br_retired",          0x21ULL, "branches",     0 },
    { "br_mis_pred_retired", 0x22ULL, "branches",     0 },
    { "stall_frontend",      0x23ULL, "cycles",       0 },
    { "stall_backend",       0x24ULL, "cycles",       0 },
};

struct pmu_core_spec {
    const char *pmu_name;
    int expected_type;
    const char *core_class;
    int core_id;
    unsigned long long expected_midr;
    int default_cpu;
};

static const struct pmu_core_spec pmu_core_specs[PMU_N_CORE_CLASSES] = {
    { "armv8_pmuv3_0", 10, "Cortex-A725", 725, PMU_MIDR_A725, 1 },
    { "armv8_pmuv3_1", 11, "Cortex-X925", 925, PMU_MIDR_X925, 6 },
};

/* Verify sysfs event file content ("event=0xXXXX\n") against expected code.
 * Never substitutes speculative br_mis_pred (0x10) for br_mis_pred_retired (0x22).
 */
static inline int pmu_event_code(const char *sysfs_text,
                                 unsigned long long expected,
                                 unsigned long long *out)
{
    const char *p;
    char *end = NULL;
    unsigned long long val;
    if (!sysfs_text || !out) return 0; /* GUARD:event-null */
    if (strncmp(sysfs_text, "event=0x", 8) != 0 &&
        strncmp(sysfs_text, "event=0X", 8) != 0)
        return 0; /* GUARD:event-prefix */
    p = sysfs_text + 8;
    if (!isxdigit((unsigned char)*p)) return 0; /* GUARD:event-xdigit */
    errno = 0;
    val = strtoull(p, &end, 16);
    if (errno != 0 || !end || end == p) return 0; /* GUARD:event-strtoull */
    if (*end == '\n') end++;
    if (*end != '\0') return 0; /* GUARD:event-trailing */
    if (val != expected) return 0; /* GUARD:event-expected */
    *out = val;
    return 1;
}

/* Validate entire Linux sysfs cpulist ("0-4,10-14\n") and test membership of cpu.
 * Returns 1 if cpu is in the list, 0 if valid list and cpu not in list, -1 if malformed.
 */
static inline int pmu_cpu_member(const char *cpulist_text, int cpu)
{
    const char *p = cpulist_text;
    int found = 0;
    if (!p || *p == '\0' || *p == '\n' || cpu < 0) return -1; /* GUARD:cpu-init */
    while (*p != '\0') {
        char *end = NULL;
        unsigned long a, b;
        if (!isdigit((unsigned char)*p)) return -1; /* GUARD:cpu-digit-a */
        errno = 0;
        a = strtoul(p, &end, 10);
        if (errno != 0 || end == p || a > (unsigned long)INT_MAX) return -1; /* GUARD:cpu-range-a */
        p = end;
        b = a;
        if (*p == '-') {
            p++;
            if (!isdigit((unsigned char)*p)) return -1; /* GUARD:cpu-digit-b */
            errno = 0;
            b = strtoul(p, &end, 10);
            if (errno != 0 || end == p || b > (unsigned long)INT_MAX || b < a) return -1; /* GUARD:cpu-range-b */
            p = end;
        }
        if ((unsigned long)cpu >= a && (unsigned long)cpu <= b) found = 1;
        if (*p == ',') {
            p++;
            if (*p == '\0' || *p == '\n') return -1; /* GUARD:cpu-trailing-comma */
        } else if (*p == '\n') {
            p++;
            if (*p != '\0') return -1; /* GUARD:cpu-trailing-newline */
        } else if (*p != '\0') {
            return -1; /* GUARD:cpu-junk */
        }
    }
    return found;
}

/* Identify verified GB10 ARMv9 core class from MIDR_EL1.
 * Returns 725 for Cortex-A725, 925 for Cortex-X925, 0 for unknown.
 */
static inline int pmu_core(unsigned long long midr)
{
    if (midr == PMU_MIDR_A725) return 725; /* GUARD:midr-a725 */
    if (midr == PMU_MIDR_X925) return 925; /* GUARD:midr-x925 */
    return 0;
}

/* Classify a counter open/read observation into the contract status vocabulary. */
static inline const char *pmu_status(int open_errno, int read_errno,
                                     ssize_t read_bytes,
                                     uint64_t time_enabled,
                                     uint64_t time_running,
                                     int cross_core)
{
    if (open_errno == EACCES || open_errno == EPERM)
        return "PERMISSION_DENIED"; /* GUARD:status-perm */
    if (open_errno == EINVAL || open_errno == ENOENT ||
        open_errno == ENODEV || open_errno == EOPNOTSUPP)
        return "UNSUPPORTED_EVENT"; /* GUARD:status-unsup */
    if (open_errno != 0)
        return "COUNTER_UNAVAILABLE"; /* GUARD:status-open-err */
    if (read_errno != 0 || read_bytes != 24)
        return "COUNTER_UNAVAILABLE"; /* GUARD:status-read-err */
    if (time_enabled == 0 || time_running == 0 || time_running > time_enabled)
        return "COUNTER_UNAVAILABLE"; /* GUARD:status-time-invalid */
    if (time_running < time_enabled)
        return cross_core ? "RUNNING_TIME_INCOMPLETE" : "MULTIPLEXED"; /* GUARD:status-mux */
    return "OK";
}

struct pmu_trial_eval_out {
    const char *status;
    int has_raw_count;
    uint64_t raw_count;
    int has_times;
    uint64_t time_enabled_ns;
    uint64_t time_running_ns;
};

/* Evaluate full trial outcome including pin and ioctl errors.
 * Guarantees that any non-OK status has has_raw_count == 0 (serialized as null, never 0).
 */
static inline void pmu_eval_trial(int pin_err, int open_errno, int ioctl_errno,
                                  int read_errno, ssize_t read_bytes,
                                  uint64_t raw_value, uint64_t time_enabled,
                                  uint64_t time_running, int cross_core,
                                  struct pmu_trial_eval_out *out)
{
    memset(out, 0, sizeof(*out));
    if (pin_err != 0) {
        out->status = "COUNTER_UNAVAILABLE"; /* GUARD:eval-pin */
        return;
    }
    if (open_errno != 0) {
        out->status = pmu_status(open_errno, 0, 0, 0, 0, cross_core);
        return;
    }
    if (ioctl_errno != 0) {
        out->status = "COUNTER_UNAVAILABLE"; /* GUARD:eval-ioctl */
        return;
    }
    out->status = pmu_status(0, read_errno, read_bytes, time_enabled, time_running, cross_core);
    if (strcmp(out->status, "OK") == 0) {
        out->has_raw_count = 1; /* GUARD:eval-ok-raw */
        out->raw_count = raw_value;
        out->has_times = 1;
        out->time_enabled_ns = time_enabled;
        out->time_running_ns = time_running;
    } else if (strcmp(out->status, "MULTIPLEXED") == 0 ||
               strcmp(out->status, "RUNNING_TIME_INCOMPLETE") == 0) {
        out->has_raw_count = 0; /* GUARD:eval-mux-null */
        out->has_times = 1;
        out->time_enabled_ns = time_enabled;
        out->time_running_ns = time_running;
    }
}

/* Format evaluated trial counts/status fragment as JSON.
 * Non-OK trials always emit "raw_count":null.
 */
static inline int pmu_format_trial_counts(char *buf, size_t cap,
                                          const struct pmu_trial_eval_out *ev)
{
    if (!buf || cap == 0 || !ev || !ev->status) return -1;
    if (ev->has_raw_count && strcmp(ev->status, "OK") == 0) {
        return snprintf(buf, cap,
                        "\"status\":\"%s\",\"raw_count\":%" PRIu64
                        ",\"time_enabled_ns\":%" PRIu64
                        ",\"time_running_ns\":%" PRIu64,
                        ev->status, ev->raw_count,
                        ev->time_enabled_ns, ev->time_running_ns);
    }
    if (ev->has_times) {
        return snprintf(buf, cap,
                        "\"status\":\"%s\",\"raw_count\":null"
                        ",\"time_enabled_ns\":%" PRIu64
                        ",\"time_running_ns\":%" PRIu64,
                        ev->status,
                        ev->time_enabled_ns, ev->time_running_ns);
    }
    return snprintf(buf, cap,
                    "\"status\":\"%s\",\"raw_count\":null"
                    ",\"time_enabled_ns\":null,\"time_running_ns\":null",
                    ev->status);
}

/* Lookup an event spec by name. Returns index 0..PMU_N_EVENTS-1 or -1. */
static inline int pmu_find_event(const char *name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < PMU_N_EVENTS; i++) {
        if (strcmp(pmu_events[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Parse hex MIDR text ("0x00000000410fd871\n") strictly. */
static inline int pmu_parse_midr(const char *text, unsigned long long *out)
{
    const char *p;
    char *end = NULL;
    unsigned long long val;
    if (!text || !out) return 0; /* GUARD:midr-null */
    if (strncmp(text, "0x", 2) != 0 && strncmp(text, "0X", 2) != 0)
        return 0; /* GUARD:midr-prefix */
    p = text + 2;
    if (!isxdigit((unsigned char)*p)) return 0; /* GUARD:midr-xdigit */
    errno = 0;
    val = strtoull(p, &end, 16);
    if (errno != 0 || !end || end == p) return 0;
    if (*end == '\n') end++;
    if (*end != '\0') return 0; /* GUARD:midr-trailing */
    *out = val;
    return 1;
}

/* Parse decimal PMU type ("10\n") strictly. */
static inline int pmu_parse_type(const char *text, int expected_type, int *out)
{
    char *end = NULL;
    unsigned long val;
    if (!text || !out || !isdigit((unsigned char)*text)) return 0; /* GUARD:type-init */
    errno = 0;
    val = strtoul(text, &end, 10);
    if (errno != 0 || !end || end == text || val > (unsigned long)INT_MAX) return 0;
    if (*end == '\n') end++;
    if (*end != '\0') return 0; /* GUARD:type-trailing */
    if ((int)val != expected_type) return 0; /* GUARD:type-expected */
    *out = (int)val;
    return 1;
}

#endif /* AIEN_GB10_PMU_CONTRACT_H */
