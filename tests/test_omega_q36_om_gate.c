/* tests/test_omega_q36_om_gate.c -- host test of the Omegese decode gate's load and trap checks (omega-mv#886).
 *
 * om_gate_load (src/omega_gpu_q36_graph.c) must build nothing when the embedded gate code in omega_q36_om_gate.h does not
 * hash to OMEGA_Q36_OM_GATE_CODE_SHA256: it returns OMEGA_GPU_DGRAPH_CODEGEN_FAIL before any code allocation. The chip red
 * control rebuilds the header from a mutated .omegese, so its sha stays consistent and it never reaches this refusal; this
 * test does. It includes the graph source to reach the static functions and stands in for the session calls they make
 * (alloc_code, sync, free, lock), counting them. No device, no physics headers: the linker drops every graph function this
 * test does not reach (-ffunction-sections, --gc-sections; see mk/omega-q36-om-gate.mk).
 *
 * Cases: (1) the bytes as embedded load: OK, one allocation, the bytes copied, one sync, gpr as kern_build;
 * (2) for several byte positions, one bit flipped: CODEGEN_FAIL, no allocation, the Kern left empty; (3) the bytes restored
 * load again; (4) read_head with the gate on fails on a trap word the gate stored and reads with none; with the gate off a
 * stored word is not its business. The embedded array is const (.rodata): the test makes its pages writable with mprotect
 * to flip a byte. Copies of the graph source with the sha comparison, or read_head's trap check, deleted must fail
 * (mk/omega-q36-om-gate.mk). */
#ifndef OM_GATE_GRAPH_SRC
#define OM_GATE_GRAPH_SRC "omega_gpu_q36_graph.c" /* the make target's mutants pass an edited copy */
#endif
#include OM_GATE_GRAPH_SRC
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static unsigned n_alloc, n_sync, n_free;
static uint8_t code_buf[OMEGA_Q36_OM_GATE_CODE_SIZE + 64];

int omega_gpu_session_alloc_code(size_t code_size, OmegaGpuMem *out) {
    n_alloc++;
    if (code_size > sizeof code_buf) return -1;
    memset(out, 0, sizeof *out);
    out->cpu = code_buf; out->va = 0x100000u; out->size = code_size;
    return 0;
}
int omega_gpu_session_sync_to_gpu(const OmegaGpuMem *m) { (void)m; n_sync++; return 0; }
int omega_gpu_session_sync_to_cpu(const OmegaGpuMem *m) { (void)m; return 0; }
void omega_gpu_session_free(OmegaGpuMem *m) { n_free++; memset(m, 0, sizeof *m); }
void omega_gpu_session_lock(void) {}
void omega_gpu_session_unlock(void) {}
void omega_gpu_session_set_error(const char *msg) { printf("  (session error: %s)\n", msg); }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void expect_load(const char *what) {
    Kern k; memset(&k, 0, sizeof k);
    n_alloc = n_sync = n_free = 0; memset(code_buf, 0, sizeof code_buf);
    int rc = om_gate_load(&k);
    CHECK(rc == OMEGA_GPU_DGRAPH_OK, "%s: rc %d, want OK", what, rc);
    CHECK(n_alloc == 1 && n_sync == 1 && n_free == 0, "%s: alloc %u sync %u free %u, want 1 1 0", what, n_alloc, n_sync, n_free);
    CHECK(k.code.cpu == code_buf && memcmp(code_buf, OMEGA_Q36_OM_GATE_code, OMEGA_Q36_OM_GATE_CODE_SIZE) == 0, "%s: code not copied", what);
    CHECK(k.gpr == (OMEGA_Q36_OM_GATE_GPR < 64u ? 64u : OMEGA_Q36_OM_GATE_GPR), "%s: gpr %u", what, k.gpr);
    printf("%s: OK, 1 allocation, %u bytes copied\n", what, OMEGA_Q36_OM_GATE_CODE_SIZE);
}

/* read_head on a graph with only what it touches: one head, buf (host view) holding the gate's trap words */
static void read_head_cases(void) {
    static uint32_t buf[64] __attribute__((aligned(16)));
    static OmegaGpuQ36Graph g;
    memset(&g, 0, sizeof g); memset(buf, 0, sizeof buf);
    g.heads = 1; g.buf.cpu = buf; g.buf.size = sizeof buf; g.lo.omst = 16u * 4u; g.lo.split = sizeof buf;
    g.om_gate.code.cpu = code_buf;
    int rc = omega_gpu_q36_graph_read_head(&g, 0, NULL, NULL, NULL);
    CHECK(rc == OMEGA_GPU_DGRAPH_OK, "read_head, gate on, no trap word: rc %d, want OK", rc);
    unsigned caught = 0, tried = 0;
    for (uint32_t c = 0; c < OMEGA_Q36_OM_GATE_STATUS_WORDS; c += 5u) {
        buf[16u + c] = 1u; g.failed = 0; tried++;
        rc = omega_gpu_q36_graph_read_head(&g, 0, NULL, NULL, NULL);
        CHECK(rc == OMEGA_GPU_DGRAPH_CHIP_FAIL && g.failed, "read_head, gate on, trap word %u: rc %d, want CHIP_FAIL", c, rc);
        if (rc == OMEGA_GPU_DGRAPH_CHIP_FAIL) caught++;
        buf[16u + c] = 0u;
    }
    buf[16u + 3u] = 1u; g.om_gate.code.cpu = NULL;
    rc = omega_gpu_q36_graph_read_head(&g, 0, NULL, NULL, NULL);
    CHECK(rc == OMEGA_GPU_DGRAPH_OK, "read_head, gate off, word 3 set: rc %d, want OK", rc);
    printf("read_head: %u of %u trap words fail the read with the gate on; a stored word is ignored with it off\n", caught, tried);
}

int main(void) {
    const long pg = sysconf(_SC_PAGESIZE);
    uintptr_t lo = (uintptr_t)OMEGA_Q36_OM_GATE_code & ~(uintptr_t)(pg - 1);
    uintptr_t hi = ((uintptr_t)OMEGA_Q36_OM_GATE_code + OMEGA_Q36_OM_GATE_CODE_SIZE + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
    if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE) != 0) { perror("mprotect"); return 2; }
    volatile uint8_t *code = (volatile uint8_t *)(uintptr_t)OMEGA_Q36_OM_GATE_code;
    expect_load("embedded bytes");
    static const size_t pos[] = { 0, 1, 15, 16, 577, OMEGA_Q36_OM_GATE_CODE_SIZE - 17, OMEGA_Q36_OM_GATE_CODE_SIZE - 1 };
    unsigned refused = 0;
    for (size_t i = 0; i < sizeof pos / sizeof pos[0]; i++) {
        code[pos[i]] ^= 0x01u;
        Kern k; memset(&k, 0, sizeof k);
        n_alloc = n_sync = n_free = 0;
        int rc = om_gate_load(&k);
        int ok = rc == OMEGA_GPU_DGRAPH_CODEGEN_FAIL && n_alloc == 0 && n_sync == 0 && k.code.cpu == NULL && k.gpr == 0;
        CHECK(ok, "byte %zu flipped: rc %d alloc %u sync %u cpu %p, want CODEGEN_FAIL (%d), no allocation", pos[i], rc, n_alloc,
              n_sync, k.code.cpu, OMEGA_GPU_DGRAPH_CODEGEN_FAIL);
        if (ok) { refused++; printf("byte %zu flipped: refused, CODEGEN_FAIL, no allocation\n", pos[i]); }
        code[pos[i]] ^= 0x01u;
    }
    expect_load("bytes restored");
    read_head_cases();
    if (fails) { printf("test_omega_q36_om_gate: %d failing checks\n", fails); return 1; }
    printf("test_omega_q36_om_gate: PASS (%u of %zu flips refused, embedded bytes load, read_head trap check)\n", refused,
           sizeof pos / sizeof pos[0]);
    return 0;
}
