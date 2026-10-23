/* SPDX-License-Identifier: GPL-2.0-only */
/* Probe: per-thread, inherited counters opened on both GB10 CPU PMUs (legacy
 * no-argument mode), plus verified 10-event A725/X925 attribution and branch
 * diagnostic contract mode (omega #453). Read-only; changes no setting.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pmu_contract.h"

#ifndef PMU_BUILD_CFLAGS
#define PMU_BUILD_CFLAGS "-std=c11 -O2 -Wall -Wextra -Werror -pthread"
#endif
#ifndef PMU_GIT_HEAD_SHA
#define PMU_GIT_HEAD_SHA "265ce82a1c220caa5f027abe068ca3aa813c633c"
#endif
#ifndef PMU_SIBLING_SYNAPSE_SHA
#define PMU_SIBLING_SYNAPSE_SHA "91733a3c39848358476eaf0bff9cf7a140880a23"
#endif
#ifndef PMU_SIBLING_ASC_SHA
#define PMU_SIBLING_ASC_SHA "d01201b3015575a9868ff5a733c9d8ae7c561ec6"
#endif
#ifndef PMU_SIBLING_ARCH_SHA
#define PMU_SIBLING_ARCH_SHA "d0f0b95d344b4c6628c3d868346932fd247ce168"
#endif

static const uint64_t ev[PMU_N_LEGACY_EVENTS] = {
    0x11, 0x08, 0x19, 0x37, 0x17, 0x13
};
static const char *nm[PMU_N_LEGACY_EVENTS] = {
    "cpu_cycles", "inst_retired", "bus_access", "ll_cache_miss_rd",
    "l2d_cache_refill", "mem_access"
};
static volatile uint64_t sink;

static void pin(int cpu)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    sched_setaffinity(0, sizeof s, &s);
}

static int pin_checked(int cpu, int *observed_cpu)
{
    cpu_set_t s;
    int cur;
    if (cpu < 0 || cpu >= CPU_SETSIZE) return EINVAL;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    if (sched_setaffinity(0, sizeof s, &s) != 0)
        return errno ? errno : EPERM;
    cur = sched_getcpu();
    if (observed_cpu) *observed_cpu = cur;
    if (cur != cpu) return EPERM;
    return 0;
}

static void work(void)
{
    uint64_t x = 1;
    for (uint64_t i = 0; i < 200000000ull; i++)
        x = x * 6364136223846793005ull + i;
    sink = x;
}

static void *worker(void *a)
{
    (void)a;
    pin(1);
    work();
    int c1 = sched_getcpu();
    pin(6);
    work();
    int c2 = sched_getcpu();
    printf("worker ran on cpu %d then cpu %d\n", c1, c2);
    return NULL;
}

static int run_legacy_probe(void)
{
    int types[2] = { 10, 11 }, fd[2][6];
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 6; i++) {
            struct perf_event_attr a;
            memset(&a, 0, sizeof a);
            a.size = sizeof a;
            a.type = types[t];
            a.config = ev[i];
            a.disabled = 1;
            a.inherit = 1;
            a.exclude_hv = 1;
            a.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                            PERF_FORMAT_TOTAL_TIME_RUNNING;
            fd[t][i] = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
            if (fd[t][i] < 0) {
                perror("perf_event_open");
                return 1;
            }
        }
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 6; i++)
            ioctl(fd[t][i], PERF_EVENT_IOC_ENABLE, 0);
    pin(0);
    pthread_t th;
    pthread_create(&th, NULL, worker, NULL);
    pthread_join(th, NULL);
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 6; i++)
            ioctl(fd[t][i], PERF_EVENT_IOC_DISABLE, 0);
    for (int i = 0; i < 6; i++) {
        uint64_t v[2][3];
        for (int t = 0; t < 2; t++) {
            if (read(fd[t][i], v[t], sizeof v[t]) != sizeof v[t]) {
                perror("read");
                return 1;
            }
        }
        printf("%-17s A725=%-12llu X925=%-12llu sum=%-12llu run/en A=%.4f X=%.4f\n",
               nm[i],
               (unsigned long long)v[0][0],
               (unsigned long long)v[1][0],
               (unsigned long long)(v[0][0] + v[1][0]),
               v[0][1] ? (double)v[0][2] / v[0][1] : 0,
               v[1][1] ? (double)v[1][2] / v[1][1] : 0);
    }
    return 0;
}

/* Minimal self-contained SHA-256 for binary and source receipt sealing */
struct sha256_ctx {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t buf[64];
};

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfc, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffa, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotr32(uint32_t x, unsigned int n)
{
    return (x >> n) | (x << (32u - n));
}

static void sha256_block(struct sha256_ctx *ctx, const uint8_t data[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static int sha256_file_hex(const char *path, char out_hex[65])
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        snprintf(out_hex, 65, "unavailable");
        return -1;
    }
    struct sha256_ctx ctx;
    ctx.state[0] = 0x6a09e667u; ctx.state[1] = 0xbb67ae85u;
    ctx.state[2] = 0x3c6ef372u; ctx.state[3] = 0xa54ff53au;
    ctx.state[4] = 0x510e527fu; ctx.state[5] = 0x9b05688cu;
    ctx.state[6] = 0x1f83d9abu; ctx.state[7] = 0x5be0cd19u;
    ctx.bitcount = 0;
    size_t buf_used = 0;
    uint8_t chunk[4096];
    ssize_t n;
    while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            ctx.buf[buf_used++] = chunk[i];
            ctx.bitcount += 8;
            if (buf_used == 64) {
                sha256_block(&ctx, ctx.buf);
                buf_used = 0;
            }
        }
    }
    close(fd);
    if (n < 0) {
        snprintf(out_hex, 65, "unavailable");
        return -1;
    }
    ctx.buf[buf_used++] = 0x80u;
    if (buf_used > 56) {
        while (buf_used < 64) ctx.buf[buf_used++] = 0;
        sha256_block(&ctx, ctx.buf);
        buf_used = 0;
    }
    while (buf_used < 56) ctx.buf[buf_used++] = 0;
    for (int i = 0; i < 8; i++)
        ctx.buf[56 + i] = (uint8_t)(ctx.bitcount >> (56 - i * 8));
    sha256_block(&ctx, ctx.buf);
    for (int i = 0; i < 8; i++)
        snprintf(out_hex + i * 8, 9, "%08x", ctx.state[i]);
    out_hex[64] = '\0';
    return 0;
}

static int read_small_file(const char *path, char *buf, size_t cap)
{
    if (!buf || cap == 0) return -1;
    buf[0] = '\0';
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, cap - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return (int)n;
}

static void trim_newline(char *s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void json_escape_print(FILE *out, const char *s)
{
    if (!s) {
        fputs("\"\"", out);
        return;
    }
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            fputc('\\', out);
            fputc((int)c, out);
        } else if (c == '\n') {
            fputs("\\n", out);
        } else if (c == '\r') {
            fputs("\\r", out);
        } else if (c == '\t') {
            fputs("\\t", out);
        } else if (c >= 0x20 && c < 0x7f) {
            fputc((int)c, out);
        }
    }
    fputc('"', out);
}

struct discovered_event {
    char raw_text[128];
    int verified;
    unsigned long long code;
};

struct discovered_pmu {
    int type_verified;
    int type_val;
    char type_raw[128];
    char cpus_raw[128];
    int selected_cpu;
    int cpu_member_ok;
    char midr_raw[128];
    unsigned long long midr_val;
    int midr_ok;
    int pmu_ok;
    int all_events_ok;
    struct discovered_event events[PMU_N_EVENTS];
};

static void discover_pmu(const char *sysfs_root, int idx, int override_cpu,
                         struct discovered_pmu *out)
{
    const struct pmu_core_spec *spec = &pmu_core_specs[idx];
    char path[512];
    char raw[128];

    memset(out, 0, sizeof(*out));
    out->selected_cpu = (override_cpu >= 0) ? override_cpu : spec->default_cpu;

    snprintf(path, sizeof(path), "%s/bus/event_source/devices/%s/type",
             sysfs_root, spec->pmu_name);
    if (read_small_file(path, raw, sizeof(raw)) > 0) {
        snprintf(out->type_raw, sizeof(out->type_raw), "%s", raw);
        trim_newline(out->type_raw);
        out->type_verified = pmu_parse_type(raw, spec->expected_type, &out->type_val);
    }

    snprintf(path, sizeof(path), "%s/bus/event_source/devices/%s/cpus",
             sysfs_root, spec->pmu_name);
    if (read_small_file(path, raw, sizeof(raw)) > 0) {
        snprintf(out->cpus_raw, sizeof(out->cpus_raw), "%s", raw);
        trim_newline(out->cpus_raw);
        out->cpu_member_ok = (pmu_cpu_member(raw, out->selected_cpu) == 1);
    }

    snprintf(path, sizeof(path),
             "%s/devices/system/cpu/cpu%d/regs/identification/midr_el1",
             sysfs_root, out->selected_cpu);
    if (read_small_file(path, raw, sizeof(raw)) > 0) {
        snprintf(out->midr_raw, sizeof(out->midr_raw), "%s", raw);
        trim_newline(out->midr_raw);
        if (pmu_parse_midr(raw, &out->midr_val) &&
            pmu_core(out->midr_val) == spec->core_id) {
            out->midr_ok = 1;
        }
    }

    out->all_events_ok = 1;
    for (int e = 0; e < PMU_N_EVENTS; e++) {
        snprintf(path, sizeof(path), "%s/bus/event_source/devices/%s/events/%s",
                 sysfs_root, spec->pmu_name, pmu_events[e].name);
        if (read_small_file(path, raw, sizeof(raw)) > 0) {
            snprintf(out->events[e].raw_text, sizeof(out->events[e].raw_text), "%s", raw);
            trim_newline(out->events[e].raw_text);
            out->events[e].verified = pmu_event_code(
                raw, pmu_events[e].code, &out->events[e].code);
        }
        if (!out->events[e].verified)
            out->all_events_ok = 0;
    }

    out->pmu_ok = (out->type_verified && out->cpu_member_ok && out->midr_ok);
}

/* Deterministic built-in workloads (fixed seed, conditional branches preserved). */
__attribute__((noinline))
uint64_t pmu_workload_fixed(uint64_t iters, uint64_t seed)
{
    uint64_t x = seed ? seed : 1ULL;
    for (uint64_t i = 0; i < iters; i++) {
        x = x * 6364136223846793005ULL + i;
        __asm__ volatile("" : "+r"(x));
    }
    return x;
}

__attribute__((noinline))
uint64_t pmu_workload_predictable(uint64_t iters, uint64_t seed)
{
    uint64_t x = seed ? seed : PMU_DEFAULT_SEED;
    uint64_t acc = x;
    for (uint64_t i = 0; i < iters; i++) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        uint64_t r = x * 2685821657736338717ULL;
        if ((i & 1023ULL) != 1023ULL) {
            acc = acc * 2862933555777941757ULL + (r & 0xffULL);
            __asm__ volatile("" : "+r"(acc));
        } else {
            acc = (acc ^ (i + r)) + PMU_DEFAULT_SEED;
            __asm__ volatile("" : "+r"(acc));
        }
    }
    return acc;
}

__attribute__((noinline))
uint64_t pmu_workload_difficult(uint64_t iters, uint64_t seed)
{
    uint64_t x = seed ? seed : PMU_DEFAULT_SEED;
    uint64_t acc = x;
    for (uint64_t i = 0; i < iters; i++) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        uint64_t r = x * 2685821657736338717ULL;
        if (r & 1ULL) {
            acc = acc * 2862933555777941757ULL + (r & 0xffULL);
            __asm__ volatile("" : "+r"(acc));
        } else {
            acc = (acc ^ (i + r)) + PMU_DEFAULT_SEED;
            __asm__ volatile("" : "+r"(acc));
        }
    }
    return acc;
}

static uint64_t execute_builtin_workload(const char *wname, uint64_t iters, uint64_t seed)
{
    if (strcmp(wname, "fixed") == 0)
        return pmu_workload_fixed(iters, seed);
    if (strcmp(wname, "predictable") == 0)
        return pmu_workload_predictable(iters, seed);
    return pmu_workload_difficult(iters, seed);
}

static void run_single_builtin_trial(int pmu_type, uint64_t event_code, int target_cpu,
                                     const char *wname, uint64_t iters, uint64_t seed,
                                     int *observed_cpu, int *open_err_out,
                                     struct pmu_trial_eval_out *eval_out)
{
    int obs_cpu = -1;
    int pin_err = pin_checked(target_cpu, &obs_cpu);
    if (observed_cpu) *observed_cpu = obs_cpu;
    if (open_err_out) *open_err_out = 0;
    if (pin_err != 0) {
        pmu_eval_trial(pin_err, 0, 0, 0, 0, 0, 0, 0, 0, eval_out);
        return;
    }

    struct perf_event_attr a;
    memset(&a, 0, sizeof(a));
    a.size = sizeof(a);
    a.type = (uint32_t)pmu_type;
    a.config = event_code;
    a.disabled = 1;
    a.inherit = 0;
    a.exclude_hv = 1;
    a.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

    errno = 0;
    int fd = (int)syscall(SYS_perf_event_open, &a, 0, target_cpu, -1, 0);
    if (fd < 0) {
        int oerr = errno ? errno : EIO;
        if (open_err_out) *open_err_out = oerr;
        pmu_eval_trial(0, oerr, 0, 0, 0, 0, 0, 0, 0, eval_out);
        return;
    }

    int ioctl_err = 0;
    if (ioctl(fd, PERF_EVENT_IOC_RESET, 0) != 0 ||
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
        ioctl_err = errno ? errno : EIO;
    }

    sink = execute_builtin_workload(wname, iters, seed);

    if (ioctl(fd, PERF_EVENT_IOC_DISABLE, 0) != 0 && ioctl_err == 0) {
        ioctl_err = errno ? errno : EIO;
    }

    int end_cpu = sched_getcpu();
    if (end_cpu != target_cpu && pin_err == 0)
        pin_err = EPERM;

    uint64_t v[3] = { 0, 0, 0 };
    errno = 0;
    ssize_t rbytes = read(fd, v, sizeof(v));
    int rerr = (rbytes < 0) ? (errno ? errno : EIO) : 0;
    close(fd);

    pmu_eval_trial(pin_err, 0, ioctl_err, rerr, rbytes, v[0], v[1], v[2], 0, eval_out);
}

static void run_external_trial(int pmu_type, uint64_t event_code, int target_cpu,
                               char **ext_argv, int *observed_cpu, int *open_err_out,
                               int *child_exit_code,
                               struct pmu_trial_eval_out *eval_out)
{
    int pipe_ready[2] = { -1, -1 };
    int pipe_go[2] = { -1, -1 };
    if (observed_cpu) *observed_cpu = target_cpu;
    if (open_err_out) *open_err_out = 0;
    if (child_exit_code) *child_exit_code = -1;

    if (pipe(pipe_ready) != 0 || pipe(pipe_go) != 0) {
        if (pipe_ready[0] >= 0) { close(pipe_ready[0]); close(pipe_ready[1]); }
        pmu_eval_trial(EIO, 0, 0, 0, 0, 0, 0, 0, 1, eval_out);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_ready[0]); close(pipe_ready[1]);
        close(pipe_go[0]); close(pipe_go[1]);
        pmu_eval_trial(EIO, 0, 0, 0, 0, 0, 0, 0, 1, eval_out);
        return;
    }

    if (pid == 0) {
        close(pipe_ready[0]);
        close(pipe_go[1]);
        int obs = -1;
        int perr = pin_checked(target_cpu, &obs);
        unsigned char status_byte = (perr == 0) ? 0 : 1;
        if (write(pipe_ready[1], &status_byte, 1) != 1)
            _exit(126);
        close(pipe_ready[1]);
        if (perr != 0)
            _exit(126);
        unsigned char go_byte = 0;
        ssize_t gn = read(pipe_go[0], &go_byte, 1);
        close(pipe_go[0]);
        if (gn != 1 || go_byte != 1)
            _exit(125);
        execvp(ext_argv[0], ext_argv);
        _exit(127);
    }

    close(pipe_ready[1]);
    close(pipe_go[0]);
    unsigned char child_pin_status = 1;
    if (read(pipe_ready[0], &child_pin_status, 1) != 1 || child_pin_status != 0) {
        close(pipe_ready[0]);
        close(pipe_go[1]);
        int st = 0;
        waitpid(pid, &st, 0);
        pmu_eval_trial(EPERM, 0, 0, 0, 0, 0, 0, 0, 1, eval_out);
        return;
    }
    close(pipe_ready[0]);

    struct perf_event_attr a;
    memset(&a, 0, sizeof(a));
    a.size = sizeof(a);
    a.type = (uint32_t)pmu_type;
    a.config = event_code;
    a.disabled = 1;
    a.enable_on_exec = 1;
    a.inherit = 0;
    a.exclude_hv = 1;
    a.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

    errno = 0;
    int fd = (int)syscall(SYS_perf_event_open, &a, pid, target_cpu, -1, 0);
    if (fd < 0) {
        int oerr = errno ? errno : EIO;
        if (open_err_out) *open_err_out = oerr;
        unsigned char abort_byte = 0;
        if (write(pipe_go[1], &abort_byte, 1) < 0) {}
        close(pipe_go[1]);
        int st = 0;
        waitpid(pid, &st, 0);
        pmu_eval_trial(0, oerr, 0, 0, 0, 0, 0, 0, 1, eval_out);
        return;
    }

    unsigned char go_byte = 1;
    if (write(pipe_go[1], &go_byte, 1) != 1) {
        close(pipe_go[1]);
        close(fd);
        int st = 0;
        waitpid(pid, &st, 0);
        pmu_eval_trial(EIO, 0, 0, 0, 0, 0, 0, 0, 1, eval_out);
        return;
    }
    close(pipe_go[1]);

    int st = 0;
    waitpid(pid, &st, 0);
    int ioctl_err = 0;
    if (ioctl(fd, PERF_EVENT_IOC_DISABLE, 0) != 0)
        ioctl_err = errno ? errno : EIO;

    uint64_t v[3] = { 0, 0, 0 };
    errno = 0;
    ssize_t rbytes = read(fd, v, sizeof(v));
    int rerr = (rbytes < 0) ? (errno ? errno : EIO) : 0;
    close(fd);

    int c_ok = (WIFEXITED(st) && WEXITSTATUS(st) == 0);
    if (child_exit_code)
        *child_exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    if (!c_ok && rerr == 0)
        rerr = ECHILD;

    pmu_eval_trial(0, 0, ioctl_err, rerr, rbytes, v[0], v[1], v[2], 1, eval_out);
}

struct probe_attempt_record {
    const char *pmu_name;
    int pmu_type;
    const char *core_class;
    int cpu;
    const char *event_id;
    unsigned long long event_code;
    int attempted;
    int open_errno;
    struct pmu_trial_eval_out eval;
};

struct trial_record {
    int trial_index;
    const char *workload;
    int repetition;
    uint64_t iterations;
    int has_seed;
    uint64_t seed;
    const char *pmu_name;
    int pmu_type;
    const char *core_class;
    int affinity_cpu;
    int has_observed_cpu;
    int observed_cpu;
    const char *event_id;
    unsigned long long event_code;
    const char *unit;
    const char *refusal_reason;
    struct pmu_trial_eval_out eval;
};

static void print_usage(FILE *out)
{
    fprintf(out,
        "usage:\n"
        "  pmu_probe                        # legacy 6-event cross-core probe\n"
        "  pmu_probe --json [options] [-- <argv...>]\n"
        "options:\n"
        "  --json                           emit gb10-pmu-contract/v1 JSON\n"
        "  --discover-only                  verify sysfs PMUs/MIDRs/events without perf_event_open\n"
        "  --trials                         run preregistered 3x(fixed,predictable,difficult) matrix\n"
        "  --workload <fixed|predictable|difficult>\n"
        "  --event <name|all|branch|legacy> filter events (default: all)\n"
        "  --core <725|925|both>            select core class (default: both)\n"
        "  --cpu-a725 <cpu>                 override A725 pinned CPU (default: 1)\n"
        "  --cpu-x925 <cpu>                 override X925 pinned CPU (default: 6)\n"
        "  --cpu <cpu>                      pin external or single-core run to <cpu>\n"
        "  --iters <N>                      loop iterations (default: 1000000)\n"
        "  --reps <N>                       repetitions per workload (default: 3 for --trials, 1 otherwise)\n"
        "  --sysfs-root <dir>               sysfs root (default: /sys)\n"
        "  --lock-file <path>               nonblocking lock file (default: /tmp/aien-gb10.lock)\n");
}

int main(int argc, char **argv)
{
    if (argc == 1)
        return run_legacy_probe();

    int json_mode = 0;
    int discover_only = 0;
    int trials_mode = 0;
    const char *workload_opt = NULL;
    const char *event_opt = "all";
    const char *core_opt = "both";
    int cpu_a725 = -1;
    int cpu_x925 = -1;
    int single_cpu = -1;
    uint64_t iters = PMU_DEFAULT_ITERS;
    int reps = -1;
    const char *sysfs_root = "/sys";
    const char *lock_path = "/tmp/aien-gb10.lock";
    char **ext_argv = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "pmu_probe: empty external command after --\n");
                return 2;
            }
            ext_argv = &argv[i + 1];
            break;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_mode = 1;
        } else if (strcmp(argv[i], "--discover-only") == 0) {
            discover_only = 1;
            json_mode = 1;
        } else if (strcmp(argv[i], "--trials") == 0) {
            trials_mode = 1;
            json_mode = 1;
        } else if (strcmp(argv[i], "--workload") == 0 && i + 1 < argc) {
            workload_opt = argv[++i];
            if (strcmp(workload_opt, "fixed") != 0 &&
                strcmp(workload_opt, "predictable") != 0 &&
                strcmp(workload_opt, "difficult") != 0) {
                fprintf(stderr, "pmu_probe: invalid --workload %s\n", workload_opt);
                return 2;
            }
        } else if (strcmp(argv[i], "--event") == 0 && i + 1 < argc) {
            event_opt = argv[++i];
        } else if (strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
            core_opt = argv[++i];
        } else if (strcmp(argv[i], "--cpu-a725") == 0 && i + 1 < argc) {
            cpu_a725 = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cpu-x925") == 0 && i + 1 < argc) {
            cpu_x925 = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            single_cpu = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            iters = strtoull(argv[++i], NULL, 10);
            if (iters == 0) iters = PMU_DEFAULT_ITERS;
        } else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) {
            reps = atoi(argv[++i]);
            if (reps <= 0) reps = 1;
        } else if (strcmp(argv[i], "--sysfs-root") == 0 && i + 1 < argc) {
            sysfs_root = argv[++i];
        } else if (strcmp(argv[i], "--lock-file") == 0 && i + 1 < argc) {
            lock_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "pmu_probe: unknown option %s\n", argv[i]);
            print_usage(stderr);
            return 2;
        }
    }

    if (!json_mode)
        json_mode = 1;
    if (!discover_only && !ext_argv && !workload_opt)
        trials_mode = 1;
    if (reps < 0)
        reps = trials_mode ? PMU_DEFAULT_REPS : 1;

    /* Select events */
    int event_enabled[PMU_N_EVENTS];
    memset(event_enabled, 0, sizeof(event_enabled));
    if (strcmp(event_opt, "all") == 0) {
        for (int e = 0; e < PMU_N_EVENTS; e++) event_enabled[e] = 1;
    } else if (strcmp(event_opt, "legacy") == 0) {
        for (int e = 0; e < PMU_N_LEGACY_EVENTS; e++) event_enabled[e] = 1;
    } else if (strcmp(event_opt, "branch") == 0) {
        for (int e = PMU_N_LEGACY_EVENTS; e < PMU_N_EVENTS; e++) event_enabled[e] = 1;
    } else {
        int idx = pmu_find_event(event_opt);
        if (idx < 0) {
            fprintf(stderr, "pmu_probe: unknown event %s\n", event_opt);
            return 2;
        }
        event_enabled[idx] = 1;
    }

    /* Select cores */
    int core_enabled[PMU_N_CORE_CLASSES] = { 1, 1 };
    if (strcmp(core_opt, "725") == 0 || strcmp(core_opt, "A725") == 0) {
        core_enabled[0] = 1; core_enabled[1] = 0;
    } else if (strcmp(core_opt, "925") == 0 || strcmp(core_opt, "X925") == 0) {
        core_enabled[0] = 0; core_enabled[1] = 1;
    } else if (strcmp(core_opt, "both") != 0) {
        fprintf(stderr, "pmu_probe: invalid --core %s\n", core_opt);
        return 2;
    }

    /* Discover sysfs PMUs */
    struct discovered_pmu pmus[PMU_N_CORE_CLASSES];
    discover_pmu(sysfs_root, 0, cpu_a725, &pmus[0]);
    discover_pmu(sysfs_root, 1, cpu_x925, &pmus[1]);

    if (single_cpu >= 0) {
        /* Route single_cpu to matching PMU core class */
        int m0 = (pmu_cpu_member(pmus[0].cpus_raw, single_cpu) == 1);
        int m1 = (pmu_cpu_member(pmus[1].cpus_raw, single_cpu) == 1);
        if (m0 && !m1) {
            core_enabled[0] = 1; core_enabled[1] = 0;
            discover_pmu(sysfs_root, 0, single_cpu, &pmus[0]);
        } else if (m1 && !m0) {
            core_enabled[0] = 0; core_enabled[1] = 1;
            discover_pmu(sysfs_root, 1, single_cpu, &pmus[1]);
        } else {
            core_enabled[0] = 1; core_enabled[1] = 0;
            discover_pmu(sysfs_root, 0, single_cpu, &pmus[0]);
        }
    } else if (ext_argv && core_enabled[0] && core_enabled[1]) {
        /* External attachment counts initial task on selected CPU (default CPU 1) */
        core_enabled[0] = 1;
        core_enabled[1] = 0;
    }

    /* Host metadata */
    struct utsname uts;
    memset(&uts, 0, sizeof(uts));
    uname(&uts);

    char loadavg_buf[128] = "unavailable";
    if (read_small_file("/proc/loadavg", loadavg_buf, sizeof(loadavg_buf)) > 0)
        trim_newline(loadavg_buf);

    char paranoid_buf[64] = "";
    int paranoid_val = -999;
    if (read_small_file("/proc/sys/kernel/perf_event_paranoid",
                        paranoid_buf, sizeof(paranoid_buf)) > 0) {
        trim_newline(paranoid_buf);
        paranoid_val = atoi(paranoid_buf);
    }

    char bin_sha[65], src_sha[65], hdr_sha[65];
    sha256_file_hex("/proc/self/exe", bin_sha);
    if (sha256_file_hex("pmu_probe.c", src_sha) < 0)
        sha256_file_hex("research/m15/pmu/pmu_probe.c", src_sha);
    if (sha256_file_hex("pmu_contract.h", hdr_sha) < 0)
        sha256_file_hex("research/m15/pmu/pmu_contract.h", hdr_sha);

    /* Check discovery validity for enabled core classes and events */
    int discovery_ok = 1;
    const char *discovery_fail_status = NULL;
    const char *discovery_fail_reason = NULL;
    for (int c = 0; c < PMU_N_CORE_CLASSES; c++) {
        if (!core_enabled[c]) continue;
        if (!pmus[c].pmu_ok) {
            discovery_ok = 0;
            discovery_fail_status = "COUNTER_UNAVAILABLE";
            discovery_fail_reason = "PMU sysfs type, CPU list membership, or MIDR verification failed";
            break;
        }
        for (int e = 0; e < PMU_N_EVENTS; e++) {
            if (!event_enabled[e]) continue;
            if (!pmus[c].events[e].verified) {
                discovery_ok = 0;
                discovery_fail_status = "UNSUPPORTED_EVENT";
                discovery_fail_reason = "sysfs event encoding absent, malformed, or mismatched";
                break;
            }
        }
        if (!discovery_ok) break;
    }

    /* Nonblocking lock acquisition for hardware runs */
    int lock_fd = -1;
    const char *lock_status = "NOT_ACQUIRED";
    if (!discover_only && discovery_ok) {
        lock_fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (lock_fd < 0) {
            lock_status = "LOCK_ERROR";
        } else if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
            lock_status = "LOCK_BUSY";
            close(lock_fd);
            lock_fd = -1;
        } else {
            lock_status = "ACQUIRED";
        }
    }

    struct probe_attempt_record probes[PMU_N_CORE_CLASSES];
    memset(probes, 0, sizeof(probes));
    for (int c = 0; c < PMU_N_CORE_CLASSES; c++) {
        probes[c].pmu_name = pmu_core_specs[c].pmu_name;
        probes[c].pmu_type = pmus[c].type_val;
        probes[c].core_class = pmu_core_specs[c].core_class;
        probes[c].cpu = pmus[c].selected_cpu;
        int first_ev = 0;
        for (int e = 0; e < PMU_N_EVENTS; e++) {
            if (event_enabled[e]) { first_ev = e; break; }
        }
        probes[c].event_id = pmu_events[first_ev].name;
        probes[c].event_code = pmu_events[first_ev].code;
        probes[c].eval.status = "NOT_RUN";
    }

    const char *overall_status = "NOT_RUN";
    const char *host_measurement = "NOT_RUN";
    const char *overall_refusal = NULL;

    /* Allocate trial array (up to 3 workloads * reps * 2 cores * 10 events) */
    int n_workloads = trials_mode ? 3 : 1;
    const char *workload_names[3] = { "fixed", "predictable", "difficult" };
    if (!trials_mode) {
        workload_names[0] = ext_argv ? "external" : (workload_opt ? workload_opt : "fixed");
    }
    int max_trials = n_workloads * reps * PMU_N_CORE_CLASSES * PMU_N_EVENTS;
    struct trial_record *trials = calloc((size_t)max_trials, sizeof(*trials));
    if (!trials) {
        if (lock_fd >= 0) close(lock_fd);
        return 1;
    }
    int n_trials = 0;

    if (!discovery_ok) {
        overall_status = discovery_fail_status;
        overall_refusal = discovery_fail_reason;
        host_measurement = "NOT_RUN";
    } else if (discover_only) {
        overall_status = "NOT_RUN";
        overall_refusal = "discover-only mode requested";
        host_measurement = "NOT_RUN";
    } else if (strcmp(lock_status, "LOCK_BUSY") == 0) {
        overall_status = "NOT_RUN";
        overall_refusal = "LOCK_BUSY: /tmp/aien-gb10.lock held by another process";
        host_measurement = "NOT_RUN";
    } else if (strcmp(lock_status, "ACQUIRED") != 0) {
        overall_status = "COUNTER_UNAVAILABLE";
        overall_refusal = "unable to open or acquire lock file";
        host_measurement = "NOT_RUN";
    } else {
        /* Perform one initial permission/availability probe per enabled core class */
        int any_denied = 0;
        int any_probe_failed = 0;
        for (int c = 0; c < PMU_N_CORE_CLASSES; c++) {
            if (!core_enabled[c]) continue;
            probes[c].attempted = 1;
            int obs_cpu = -1;
            run_single_builtin_trial(
                pmus[c].type_val, probes[c].event_code, pmus[c].selected_cpu,
                "fixed", 1000ULL, PMU_DEFAULT_SEED,
                &obs_cpu, &probes[c].open_errno, &probes[c].eval);
            if (strcmp(probes[c].eval.status, "PERMISSION_DENIED") == 0) {
                any_denied = 1;
            } else if (strcmp(probes[c].eval.status, "OK") != 0) {
                any_probe_failed = 1;
            }
        }

        if (any_denied) {
            /* Stop hardware trials after one attempt per core class; mark trial matrix NOT_RUN */
            overall_status = "PERMISSION_DENIED";
            host_measurement = "NOT_RUN";
            overall_refusal = "perf_event_open denied (EACCES/EPERM); stopped after one probe per core class; trial matrix marked NOT_RUN";
            for (int c = 0; c < PMU_N_CORE_CLASSES; c++) {
                if (!core_enabled[c]) continue;
                for (int w = 0; w < n_workloads; w++) {
                    for (int r = 1; r <= reps; r++) {
                        for (int e = 0; e < PMU_N_EVENTS; e++) {
                            if (!event_enabled[e]) continue;
                            struct trial_record *tr = &trials[n_trials];
                            tr->trial_index = n_trials + 1;
                            tr->workload = workload_names[w];
                            tr->repetition = r;
                            tr->iterations = ext_argv ? 0 : iters;
                            tr->has_seed = ext_argv ? 0 : 1;
                            tr->seed = PMU_DEFAULT_SEED;
                            tr->pmu_name = pmu_core_specs[c].pmu_name;
                            tr->pmu_type = pmus[c].type_val;
                            tr->core_class = pmu_core_specs[c].core_class;
                            tr->affinity_cpu = pmus[c].selected_cpu;
                            tr->has_observed_cpu = 0;
                            tr->event_id = pmu_events[e].name;
                            tr->event_code = pmu_events[e].code;
                            tr->unit = pmu_events[e].unit;
                            tr->refusal_reason = "PERMISSION_DENIED: stopped after initial core-class denial probe";
                            tr->eval.status = "NOT_RUN";
                            tr->eval.has_raw_count = 0;
                            tr->eval.has_times = 0;
                            n_trials++;
                        }
                    }
                }
            }
        } else if (any_probe_failed) {
            overall_status = "COUNTER_UNAVAILABLE";
            host_measurement = "NOT_RUN";
            overall_refusal = "initial core-class probe failed before trial matrix";
        } else {
            /* Execute full trial matrix: one event per trial */
            int all_ok = 1;
            const char *first_non_ok = NULL;
            for (int c = 0; c < PMU_N_CORE_CLASSES; c++) {
                if (!core_enabled[c]) continue;
                for (int w = 0; w < n_workloads; w++) {
                    for (int r = 1; r <= reps; r++) {
                        for (int e = 0; e < PMU_N_EVENTS; e++) {
                            if (!event_enabled[e]) continue;
                            struct trial_record *tr = &trials[n_trials];
                            tr->trial_index = n_trials + 1;
                            tr->workload = workload_names[w];
                            tr->repetition = r;
                            tr->iterations = ext_argv ? 0 : iters;
                            tr->has_seed = ext_argv ? 0 : 1;
                            tr->seed = PMU_DEFAULT_SEED;
                            tr->pmu_name = pmu_core_specs[c].pmu_name;
                            tr->pmu_type = pmus[c].type_val;
                            tr->core_class = pmu_core_specs[c].core_class;
                            tr->affinity_cpu = pmus[c].selected_cpu;
                            tr->event_id = pmu_events[e].name;
                            tr->event_code = pmu_events[e].code;
                            tr->unit = pmu_events[e].unit;

                            int obs_cpu = -1;
                            int oerr = 0;
                            if (ext_argv) {
                                int cexit = 0;
                                run_external_trial(
                                    pmus[c].type_val, pmu_events[e].code,
                                    pmus[c].selected_cpu, ext_argv,
                                    &obs_cpu, &oerr, &cexit, &tr->eval);
                            } else {
                                run_single_builtin_trial(
                                    pmus[c].type_val, pmu_events[e].code,
                                    pmus[c].selected_cpu, workload_names[w],
                                    iters, PMU_DEFAULT_SEED,
                                    &obs_cpu, &oerr, &tr->eval);
                            }
                            if (obs_cpu >= 0) {
                                tr->has_observed_cpu = 1;
                                tr->observed_cpu = obs_cpu;
                            }
                            if (strcmp(tr->eval.status, "OK") != 0) {
                                all_ok = 0;
                                if (!first_non_ok) first_non_ok = tr->eval.status;
                                tr->refusal_reason = tr->eval.status;
                            }
                            n_trials++;
                        }
                    }
                }
            }
            if (all_ok && n_trials > 0) {
                overall_status = "OK";
                host_measurement = "OK";
            } else {
                overall_status = first_non_ok ? first_non_ok : "COUNTER_UNAVAILABLE";
                host_measurement = "NOT_RUN";
                overall_refusal = "one or more trials did not complete with status OK";
            }
        }
    }

    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }

    /* Emit JSON receipt */
    FILE *out = stdout;
    fprintf(out, "{\n");
    fprintf(out, "  \"schema_version\": \"%s\",\n", PMU_CONTRACT_SCHEMA);
    fprintf(out, "  \"tool\": \"research/m15/pmu/pmu_probe\",\n");
    fprintf(out, "  \"status\": \"%s\",\n", overall_status);
    fprintf(out, "  \"host_measurement\": \"%s\",\n", host_measurement);
    fprintf(out, "  \"refusal_reason\": ");
    if (overall_refusal) json_escape_print(out, overall_refusal);
    else fputs("null", out);
    fprintf(out, ",\n");

    fprintf(out, "  \"provenance\": {\n");
    fprintf(out, "    \"baseline_commit\": \"%s\",\n", PMU_GIT_HEAD_SHA);
    fprintf(out, "    \"source_sha256\": \"%s\",\n", src_sha);
    fprintf(out, "    \"contract_header_sha256\": \"%s\",\n", hdr_sha);
    fprintf(out, "    \"binary_sha256\": \"%s\",\n", bin_sha);
    fprintf(out, "    \"sibling_shas\": {\n");
    fprintf(out, "      \"synapse\": \"%s\",\n", PMU_SIBLING_SYNAPSE_SHA);
    fprintf(out, "      \"aien_sovereign_core\": \"%s\",\n", PMU_SIBLING_ASC_SHA);
    fprintf(out, "      \"aien_architecture\": \"%s\"\n", PMU_SIBLING_ARCH_SHA);
    fprintf(out, "    }\n");
    fprintf(out, "  },\n");

    fprintf(out, "  \"host\": {\n");
    fprintf(out, "    \"hostname\": "); json_escape_print(out, uts.nodename); fprintf(out, ",\n");
    fprintf(out, "    \"arch\": "); json_escape_print(out, uts.machine); fprintf(out, ",\n");
    fprintf(out, "    \"kernel\": "); json_escape_print(out, uts.release); fprintf(out, ",\n");
    fprintf(out, "    \"compiler\": "); json_escape_print(out, __VERSION__); fprintf(out, ",\n");
    fprintf(out, "    \"cflags\": "); json_escape_print(out, PMU_BUILD_CFLAGS); fprintf(out, ",\n");
    if (paranoid_val != -999)
        fprintf(out, "    \"perf_event_paranoid\": %d,\n", paranoid_val);
    else
        fprintf(out, "    \"perf_event_paranoid\": null,\n");
    fprintf(out, "    \"loadavg\": "); json_escape_print(out, loadavg_buf); fprintf(out, ",\n");
    fprintf(out, "    \"lock_path\": "); json_escape_print(out, lock_path); fprintf(out, ",\n");
    fprintf(out, "    \"lock_status\": \"%s\"\n", lock_status);
    fprintf(out, "  },\n");

    fprintf(out, "  \"sampling_policy\": {\n");
    fprintf(out, "    \"mode\": \"one_event_per_trial\",\n");
    fprintf(out, "    \"inherit\": false,\n");
    fprintf(out, "    \"exclude_hv\": true,\n");
    fprintf(out, "    \"scaled_counts\": false,\n");
    fprintf(out, "    \"inferred_branch_miss_ratio\": false,\n");
    fprintf(out, "    \"external_attachment\": %s,\n", ext_argv ? "true" : "false");
    fprintf(out, "    \"scope_note\": \"%s\"\n",
            ext_argv
            ? "external argv executed directly without shell; counts initial task on selected CPU only; descendants and other CPUs excluded; incomplete running time reported as RUNNING_TIME_INCOMPLETE"
            : "pinned single-thread trial on verified core class; one event per trial; raw counts only when time_running == time_enabled > 0");
    fprintf(out, "  },\n");

    fprintf(out, "  \"pmus\": [\n");
    for (int c = 0; c < PMU_N_CORE_CLASSES; c++) {
        const struct pmu_core_spec *spec = &pmu_core_specs[c];
        const struct discovered_pmu *dp = &pmus[c];
        fprintf(out, "    {\n");
        fprintf(out, "      \"pmu_name\": \"%s\",\n", spec->pmu_name);
        fprintf(out, "      \"expected_type\": %d,\n", spec->expected_type);
        fprintf(out, "      \"type\": %s,\n", dp->type_verified ? dp->type_raw : "null");
        fprintf(out, "      \"type_verified\": %s,\n", dp->type_verified ? "true" : "false");
        fprintf(out, "      \"core_class\": \"%s\",\n", spec->core_class);
        fprintf(out, "      \"core_id\": %d,\n", spec->core_id);
        fprintf(out, "      \"cpus_raw\": "); json_escape_print(out, dp->cpus_raw); fprintf(out, ",\n");
        fprintf(out, "      \"selected_cpu\": %d,\n", dp->selected_cpu);
        fprintf(out, "      \"cpu_member_verified\": %s,\n", dp->cpu_member_ok ? "true" : "false");
        fprintf(out, "      \"midr_raw\": "); json_escape_print(out, dp->midr_raw); fprintf(out, ",\n");
        fprintf(out, "      \"midr_verified\": %s,\n", dp->midr_ok ? "true" : "false");
        fprintf(out, "      \"pmu_verified\": %s,\n", dp->pmu_ok ? "true" : "false");
        fprintf(out, "      \"events\": [\n");
        for (int e = 0; e < PMU_N_EVENTS; e++) {
            fprintf(out, "        {\n");
            fprintf(out, "          \"event_id\": \"%s\",\n", pmu_events[e].name);
            fprintf(out, "          \"expected_code\": \"0x%04llx\",\n", pmu_events[e].code);
            fprintf(out, "          \"sysfs_raw\": ");
            json_escape_print(out, dp->events[e].raw_text);
            fprintf(out, ",\n");
            if (dp->events[e].verified)
                fprintf(out, "          \"verified_code\": \"0x%04llx\",\n", dp->events[e].code);
            else
                fprintf(out, "          \"verified_code\": null,\n");
            fprintf(out, "          \"unit\": \"%s\",\n", pmu_events[e].unit);
            fprintf(out, "          \"legacy\": %s,\n", pmu_events[e].legacy ? "true" : "false");
            fprintf(out, "          \"discovery_status\": \"%s\"\n",
                    dp->events[e].verified ? "VERIFIED" : "UNSUPPORTED_EVENT");
            fprintf(out, "        }%s\n", (e + 1 < PMU_N_EVENTS) ? "," : "");
        }
        fprintf(out, "      ]\n");
        fprintf(out, "    }%s\n", (c + 1 < PMU_N_CORE_CLASSES) ? "," : "");
    }
    fprintf(out, "  ],\n");

    fprintf(out, "  \"probe_attempts\": [\n");
    for (int c = 0; c < PMU_N_CORE_CLASSES; c++) {
        char count_frag[256];
        pmu_format_trial_counts(count_frag, sizeof(count_frag), &probes[c].eval);
        fprintf(out, "    {\n");
        fprintf(out, "      \"pmu_name\": \"%s\",\n", probes[c].pmu_name);
        fprintf(out, "      \"pmu_type\": %d,\n", probes[c].pmu_type);
        fprintf(out, "      \"core_class\": \"%s\",\n", probes[c].core_class);
        fprintf(out, "      \"affinity_cpu\": %d,\n", probes[c].cpu);
        fprintf(out, "      \"event_id\": \"%s\",\n", probes[c].event_id);
        fprintf(out, "      \"event_code\": \"0x%04llx\",\n", probes[c].event_code);
        fprintf(out, "      \"attempted\": %s,\n", probes[c].attempted ? "true" : "false");
        fprintf(out, "      \"open_errno\": %d,\n", probes[c].open_errno);
        fprintf(out, "      %s\n", count_frag);
        fprintf(out, "    }%s\n", (c + 1 < PMU_N_CORE_CLASSES) ? "," : "");
    }
    fprintf(out, "  ],\n");

    fprintf(out, "  \"trials\": [\n");
    for (int i = 0; i < n_trials; i++) {
        const struct trial_record *tr = &trials[i];
        char count_frag[256];
        pmu_format_trial_counts(count_frag, sizeof(count_frag), &tr->eval);
        fprintf(out, "    {\n");
        fprintf(out, "      \"trial_index\": %d,\n", tr->trial_index);
        fprintf(out, "      \"workload\": \"%s\",\n", tr->workload);
        fprintf(out, "      \"repetition\": %d,\n", tr->repetition);
        if (tr->iterations > 0)
            fprintf(out, "      \"iterations\": %" PRIu64 ",\n", tr->iterations);
        else
            fprintf(out, "      \"iterations\": null,\n");
        if (tr->has_seed)
            fprintf(out, "      \"seed\": \"0x%016" PRIx64 "\",\n", tr->seed);
        else
            fprintf(out, "      \"seed\": null,\n");
        fprintf(out, "      \"pmu_name\": \"%s\",\n", tr->pmu_name);
        fprintf(out, "      \"pmu_type\": %d,\n", tr->pmu_type);
        fprintf(out, "      \"core_class\": \"%s\",\n", tr->core_class);
        fprintf(out, "      \"affinity_cpu\": %d,\n", tr->affinity_cpu);
        if (tr->has_observed_cpu)
            fprintf(out, "      \"observed_cpu\": %d,\n", tr->observed_cpu);
        else
            fprintf(out, "      \"observed_cpu\": null,\n");
        fprintf(out, "      \"event_id\": \"%s\",\n", tr->event_id);
        fprintf(out, "      \"event_code\": \"0x%04llx\",\n", tr->event_code);
        fprintf(out, "      \"unit\": \"%s\",\n", tr->unit);
        fprintf(out, "      \"refusal_reason\": ");
        if (tr->refusal_reason) json_escape_print(out, tr->refusal_reason);
        else fputs("null", out);
        fprintf(out, ",\n");
        fprintf(out, "      %s\n", count_frag);
        fprintf(out, "    }%s\n", (i + 1 < n_trials) ? "," : "");
    }
    fprintf(out, "  ]\n");
    fprintf(out, "}\n");

    free(trials);
    return 0;
}
