/* Host test of the resident seat (src/runtime/rx_resident_gpu.c) over the
 * simulated driver (tests/fake_m16_native.c). L202, cut A: the seat's threads,
 * waits, heap and configuration move onto the platform services, and this test
 * is the evidence that the seat drives the driver the same way before and
 * after. It needs no GPU.
 *
 * A stand-in thread plays the chip's part the fake does not model: once the
 * world image is bound to device memory it writes the first heartbeat word and a constant LIVE count, so the
 * seat's pass count is 0 and the lease budget is the fixed minimum. The fake
 * executes the launch stream's release packets on submit, so the launch marker
 * is written at the doorbell.
 *
 * stdout carries only deterministic lines (driver events, allocation sizes, the
 * submitted words' digest, seat return codes and heartbeat words). Run it on the
 * old and the new seat and compare stdout byte for byte. stderr carries the
 * seat's own diagnostics.
 *
 * nvrm_channel and nvrm_channel_destroy are not in the fake (only kill and
 * relaunch use them); this file supplies counting versions.
 */
#include "runtime/aienos_cap.h"
#include "runtime/rx_world.h"
#include "runtime/rx_resident_gpu.h"
#include "fake_m16_native.h"
#include "m16_native.h"

#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RES_A 0x10u
#define RES_B 0x20u
enum { SUBJ_SEAT = 3, SUBJ_EXTERNAL = 100, ISSUER = 3 };

static int g_checks, g_fail;
#define CHECK(cond, ...) do {                                                 \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            fprintf(stderr, "  FAIL %s:%d ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                     \
            fputc('\n', stderr);                                              \
        }                                                                     \
    } while (0)

static int g_chan_calls, g_chan_destroy_calls;
int nvrm_channel(Nvrm *rm) { (void)rm; g_chan_calls++; fk.channel_up = 1; return 0; }
int nvrm_channel_destroy(Nvrm *rm) { (void)rm; g_chan_destroy_calls++; fk.channel_up = 0; return 0; }

static AienosCapAdmin *g_admin;

static RxCapRef mint(uint32_t subject, uint64_t resource, uint32_t rights) {
    AienosCapRef office;
    aienos_cap_office(g_admin, &office);
    AienosCapMint m = { ISSUER, subject, resource, rights, 0, { UINT32_MAX, 0 }, office };
    AienosCapRef r = { UINT32_MAX, 0 };
    if (aienos_cap_mint(g_admin, &m, &r) != 0) r = (AienosCapRef){ UINT32_MAX, 0 };
    return (RxCapRef){ r.cap_id, r.generation };
}

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static volatile uint32_t *hb(RxWorld *w, uint32_t off) {
    return (volatile uint32_t *)(w->coherent + rx_world_off_heartbeat() + off);
}

/* ---- the chip stand-in ---------------------------------------------------- */

typedef struct {
    RxWorld *w;
    uint8_t *host_image;  /* the world's own memory before the seat binds device memory */
    atomic_int run;
} StandIn;

static void *stand_in_main(void *arg) {
    StandIn *si = arg;
    while (atomic_load(&si->run)) {
        uint8_t *img = si->w->coherent;
        if (img != si->host_image) {
            volatile uint32_t *first = (volatile uint32_t *)(img + rx_world_off_heartbeat());
            volatile uint32_t *live = (volatile uint32_t *)(img + rx_world_off_heartbeat() + RX_SEAT_HB_LIVE);
            if (*first == 0) *first = 1u; /* the word wait_heartbeat looks at */
            if (*live == 0) *live = 1000u; /* constant: the seat measures 0 passes */
        }
        __asm__ volatile("dsb sy" ::: "memory");
        sleep_ms(1);
    }
    return NULL;
}

/* ---- deterministic trace -------------------------------------------------- */

static const char *ev_name(int k) {
    static const char *n[] = { "?", "OPEN", "CHANNEL", "ALLOC", "FREE", "SUBMIT", "WAIT", "CLOSE", "PB_CBANK",
                               "PB_ARGS", "PB_SEM_INIT", "PB_QMD", "PB_RELEASE", "PB_FLUSH", "KERNEL" };
    return k >= 0 && k <= FK_EV_KERNEL ? n[k] : "?";
}

/* Addresses are host pointers in the fake (they change run to run), so events print
 * kind and the size-like argument only; release and QMD events print their order. */
static void dump_trace(const char *tag) {
    printf("%s events %d overflow %d\n", tag, fk.nev, fk.ev_overflow);
    for (int i = 0; i < fk.nev; i++) {
        const FakeEvent *e = &fk.ev[i];
        if (e->kind == FK_EV_ALLOC || e->kind == FK_EV_SUBMIT || e->kind == FK_EV_CLOSE || e->kind == FK_EV_KERNEL)
            printf("%s ev %d %s a=%llu b=%llu\n", tag, i, ev_name(e->kind), (unsigned long long)e->a,
                   (unsigned long long)e->b);
        else if (e->kind == FK_EV_PB_RELEASE)
            printf("%s ev %d %s payload=0x%llx flags=0x%llx\n", tag, i, ev_name(e->kind), (unsigned long long)e->b,
                   (unsigned long long)e->c);
        else
            printf("%s ev %d %s\n", tag, i, ev_name(e->kind));
    }
    printf("%s calls open %d channel %d alloc %d uncached %d free %d close %d submit %d chan_rebuild %d chan_destroy %d\n",
           tag, fk.open_calls, fk.channel_calls, fk.alloc_calls, fk.uncached_calls, fk.free_calls, fk.close_calls,
           fk.submit_calls, g_chan_calls, g_chan_destroy_calls);
    printf("%s alloc sizes", tag);
    for (int i = 0; i < fk.alloc_size_n && i < FK_SIZE_LOG; i++) printf(" %llu", (unsigned long long)fk.alloc_size_log[i]);
    printf("\n%s live %d live_at_close %d double_free %d pb_parse_error %d\n", tag, fk.live, fk.live_at_close,
           fk.double_free, fk.pb_parse_error);
}

/* The submitted words hold device addresses (host pointers in the fake, different run to run). Walk the
 * packets and print only the headers: opcode h >> 29, 4 = immediate (no data words), every other opcode is
 * followed by count = (h >> 16) & 0x1fff data words, which are skipped. */
static void dump_pb(const char *tag) {
    printf("%s pb words %zu headers", tag, fk.pb_len);
    for (size_t i = 0; i < fk.pb_len;) {
        uint32_t h = fk.pb[i];
        printf(" %08x", h);
        i += (h >> 29) == 4u ? 1u : 1u + ((h >> 16) & 0x1fffu);
    }
    printf("\n");
}

/* ---- the lease thread is named for counting ------------------------------- */

static int count_threads_named(const char *want) {
    DIR *d = opendir("/proc/self/task");
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[300], name[64] = { 0 };
        snprintf(path, sizeof path, "/proc/self/task/%s/comm", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(name, sizeof name, f)) {
            name[strcspn(name, "\n")] = 0;
            if (strcmp(name, want) == 0) n++;
        }
        fclose(f);
    }
    closedir(d);
    return n;
}

/* ---- world set-up ----------------------------------------------------------- */

static int world_up(RxWorld *w, AienosCapView **view) {
    if (aienos_cap_start(&g_admin, view) != 0) return -1;
    if (rx_world_init_native(w, *view, 2, 1u << 16) != RX_OK) return -1;
    w->external_subject = SUBJ_EXTERNAL;
    uint64_t zero[RX_MAX_FIELDS] = { 0 };
    RxObjRef A = { UINT32_MAX, 0 }, B = { UINT32_MAX, 0 };
    if (rx_world_create(w, 1, RX_PERSIST_RESIDENT, RES_A, zero, &A) != RX_OK ||
        rx_world_create(w, 1, RX_PERSIST_RESIDENT, RES_B, zero, &B) != RX_OK ||
        rx_world_attach_physical(w, A) != RX_OK || rx_world_attach_physical(w, B) != RX_OK)
        return -1;
    RxCapRef in = mint(SUBJ_SEAT, RES_A, RX_RIGHT_READ);
    RxCapRef out = mint(SUBJ_SEAT, RES_B, RX_RIGHT_READ | RX_RIGHT_WRITE);
    if (in.cap_id == UINT32_MAX || out.cap_id == UINT32_MAX) return -1;
    if (rx_world_bind_capability(w, A, in) != RX_OK || rx_world_bind_capability(w, B, out) != RX_OK) return -1;
    return rx_world_enable_resident(w) == RX_OK ? 0 : -1;
}

static void world_down(RxWorld *w, AienosCapView *view) {
    rx_world_destroy(w);
    aienos_cap_stop(g_admin, view);
}

/* ---- cases ----------------------------------------------------------------- */

/* The device does not open: begin refuses, frees what it took, leaves nothing live. */
static void case_open_refused(void) {
    printf("[*] case open_refused\n");
    RxWorld w;
    memset(&w, 0, sizeof w);
    AienosCapView *view = NULL;
    CHECK(world_up(&w, &view) == 0, "world did not start");
    fake_reset();
    fk.open_fail = 1;
    RxGpuSeat *seat = (RxGpuSeat *)1;
    int rc = rx_gpu_seat_begin(&w, &seat);
    printf("open_refused begin %d seat_null %d\n", rc, seat == NULL);
    CHECK(rc == -1 && seat == NULL, "begin did not refuse a closed device");
    dump_trace("open_refused");
    CHECK(fk.live == 0, "allocations left live: %d", fk.live);
    fake_release_all();
    world_down(&w, view);
}

/* Full life: begin, heartbeat, lease, kill, relaunch, finish. pin = RX_SEAT_LEASE_CPU or NULL. */
static void case_life(const char *tag, const char *pin) {
    printf("[*] case %s\n", tag);
    if (pin) setenv("RX_SEAT_LEASE_CPU", pin, 1); else unsetenv("RX_SEAT_LEASE_CPU");
    RxWorld w;
    memset(&w, 0, sizeof w);
    AienosCapView *view = NULL;
    CHECK(world_up(&w, &view) == 0, "world did not start");
    fake_reset();
    fk.kernel_mode = FK_KERNEL_SKIP;
    StandIn si = { &w, w.coherent, 1 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, stand_in_main, &si) == 0, "stand-in did not start");

    RxGpuSeat *seat = NULL;
    int rc = rx_gpu_seat_begin(&w, &seat);
    printf("%s begin %d seat %d\n", tag, rc, seat != NULL);
    CHECK(rc == 0 && seat != NULL, "begin %d", rc);
    printf("%s budget %u\n", tag, *hb(&w, RX_SEAT_HB_BUDGET));
    CHECK(*hb(&w, RX_SEAT_HB_BUDGET) == 20000u, "budget is not the minimum");
    uint32_t l0 = *hb(&w, RX_SEAT_HB_LEASE);
    sleep_ms(30);
    uint32_t l1 = *hb(&w, RX_SEAT_HB_LEASE);
    printf("%s lease moving %d\n", tag, l1 != l0);
    CHECK(l1 != l0, "lease word did not move (%u)", l1);
    int named = count_threads_named("rx-seat-lease");
    printf("%s lease threads named %d\n", tag, named);
    CHECK(named == 1, "lease thread name count %d", named);
    dump_pb(tag);

    if (seat) {
        int k = rx_gpu_seat_kill(seat);
        printf("%s kill %d lease_word 0x%08x\n", tag, k, *hb(&w, RX_SEAT_HB_LEASE));
        CHECK(k == 0, "kill %d", k);
        CHECK(*hb(&w, RX_SEAT_HB_LEASE) == RX_SEAT_LEASE_QUIT, "kill did not post quit");
        /* pthread_join returns when the kernel clears the thread id at exit; the task can stay listed in
         * /proc/self/task a moment longer (seen on the old and the new seat alike, 6 of 12 runs each), so
         * allow it up to 500 ms to leave. A lease thread that is still running stays listed and fails. */
        int left = count_threads_named("rx-seat-lease");
        for (int i = 0; i < 500 && left != 0; i++) { sleep_ms(1); left = count_threads_named("rx-seat-lease"); }
        CHECK(left == 0, "lease thread still running after kill (%d)", left);
        int r = rx_gpu_seat_relaunch(seat);
        printf("%s relaunch %d\n", tag, r);
        CHECK(r == 0, "relaunch %d", r);
        atomic_store(&si.run, 0);
        pthread_join(th, NULL);
        RxGpuSeatLeave leave;
        memset(&leave, 0, sizeof leave);
        int f = rx_gpu_seat_finish_ex(seat, &leave);
        printf("%s finish %d marker_ok %d sem_ok %d marker 0x%08x\n", tag, f, leave.marker_ok, leave.sem_ok,
               leave.marker_value);
    } else {
        atomic_store(&si.run, 0);
        pthread_join(th, NULL);
    }
    dump_trace(tag);
    fake_release_all();
    world_down(&w, view);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    case_open_refused();
    case_life("life_unpinned", NULL);
    case_life("life_pinned", "0");
    printf("rx_resident_gpu_fake_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
