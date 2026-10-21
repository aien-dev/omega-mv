/* omega_osc_launch.h -- the launcher for Omegese kernels built by omega_osc_gpu (L386, C1 of docs/OMEGA.md).
 *
 * docs/omegese/GPU.md section 11 states what it checks. A launch goes through the one shared device
 * (omega_gpu_session_launch): the caller holds omega_gpu_session_lock, has the session open, and owns
 * the code buffer (omega_kernel_load) and the data buffers (omega_gpu_session_alloc). Everything the
 * kernel can touch is named by the arguments, and the launcher checks each one before the chip sees it:
 *
 *  - the code in the code buffer is the code the receipt names (SHA-256 of the bytes);
 *  - argument count and kinds match the kernel line, a bool is 0 or 1;
 *  - a buffer is a range inside one session allocation, its length comes from that allocation (the
 *    caller names a start and an element count, the launcher refuses a range past the end);
 *  - no two buffers overlap when either is an edit buffer;
 *  - more than one block along x (or y) is refused when the kernel never reads block (or batch);
 *  - the status words are zeroed before the launch and read after it.
 *
 * A refusal launches nothing. A trap is not a launch failure: the run is reported with the trap codes
 * the kernel stored (GPU.md section 10). */
#ifndef OMEGA_OSC_LAUNCH_H
#define OMEGA_OSC_LAUNCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omega_gpu_session.h"
#include "omega_osc_gpu.h"

typedef struct {
    uint8_t kind;              /* OscGpuParamKind the caller means; must match the kernel line */
    const OmegaGpuMem *mem;    /* buffers: the session allocation the range lives in */
    uint64_t byte_off;         /* buffers: start of the range inside mem, a multiple of 4 */
    uint32_t len;              /* buffers: elements (4 bytes each); the kernel sees this as len */
    uint32_t word;             /* scalars: the 32 bits (u32, f32 bits, bool 0 or 1) */
} OscGpuArg;

typedef struct {
    uint32_t status[OSC_GPU_STATUS_WORDS]; /* word N = 1 when some thread trapped with code N */
    uint32_t trap;             /* lowest trap code stored, 0 when the run was clean */
    uint64_t elapsed_ns;       /* submit to second marker */
} OscGpuRun;

#define OSC_LAUNCH_OK 0
#define OSC_LAUNCH_REFUSED (-1) /* nothing was launched; err says why */
#define OSC_LAUNCH_CHIP (-2)    /* the session failed (err names its stage); see omega_gpu_session_last_error */

/* Caller holds the lock, session open. Allocates a code buffer, copies k->code into it and syncs it to
 * the device. 0, or OSC_LAUNCH_CHIP. */
int omega_kernel_load(const OscGpuKernel *k, OmegaGpuMem *code);

/* Caller holds the lock, session open. Launches k (resident in code) over blocks x batches blocks of
 * k->threads threads. timeout_ms bounds the completion poll (past it the session latches). */
int omega_kernel_launch(const OscGpuKernel *k, const OmegaGpuMem *code, const OscGpuArg *args, unsigned nargs,
                        uint32_t blocks, uint32_t batches, uint32_t timeout_ms, OscGpuRun *run,
                        char *err, size_t errn);

/* Every check of omega_kernel_launch and the argument words it writes, without the device: the host
 * tests prove each refusal through this. status_va is the address the status words live at. */
int omega_kernel_launch_words(const OscGpuKernel *k, const OmegaGpuMem *code, const OscGpuArg *args, unsigned nargs,
                              uint32_t blocks, uint32_t batches, uint64_t status_va,
                              uint32_t words[OSC_GPU_MAX_ARG_WORDS], char *err, size_t errn);

/* Releases the launcher's status buffer (also done by the session's close hook). */
void omega_kernel_launch_release(void);

#endif /* OMEGA_OSC_LAUNCH_H */
