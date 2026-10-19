/*
 * omega_bw_copy.c -- copy-engine pushbuffer encoder. See omega_bw_copy.h.
 */
#include "omega_bw_copy.h"

#define VA_LIMIT (1ull << 57) /* Blackwell GMMU virtual address width (clca6f/clcab5 UPPER fields 24:0 -> 57 bits) */

size_t omega_bw_copy_bind(uint32_t *pb, size_t cap, uint32_t copy_class)
{
    if (!pb || cap < 2) return 0;
    pb[0] = omega_bw_mthd(OMEGA_BW_COPY_SUBCHANNEL, OMEGA_BW_CE_SET_OBJECT, 1);
    pb[1] = copy_class;
    return 2;
}

size_t omega_bw_copy_linear_words(uint64_t bytes, int with_semaphore)
{
    if (bytes == 0) return 0;
    uint64_t chunks = (bytes + OMEGA_BW_COPY_CHUNK_MAX - 1) / OMEGA_BW_COPY_CHUNK_MAX;
    /* per chunk: 1 + 4 offsets, 1 + 1 length, 1 + 1 launch = 9 words */
    return (size_t)(chunks * 9u + (with_semaphore ? 6u : 0u));
}

size_t omega_bw_copy_linear(uint32_t *pb, size_t cap, uint64_t src_va, uint64_t dst_va, uint64_t bytes,
                            uint64_t sem_va, uint32_t payload)
{
    if (!pb || bytes == 0) return 0;
    if (src_va >= VA_LIMIT || dst_va >= VA_LIMIT || sem_va >= VA_LIMIT) return 0;
    if (bytes > VA_LIMIT - src_va || bytes > VA_LIMIT - dst_va) return 0;
    if (sem_va & 3u) return 0;
    size_t need = omega_bw_copy_linear_words(bytes, sem_va != 0);
    if (cap < need) return 0;

    size_t n = 0;
    const uint32_t s = OMEGA_BW_COPY_SUBCHANNEL;
    for (uint64_t off = 0; off < bytes; off += OMEGA_BW_COPY_CHUNK_MAX) {
        uint64_t len = bytes - off < OMEGA_BW_COPY_CHUNK_MAX ? bytes - off : OMEGA_BW_COPY_CHUNK_MAX;
        uint64_t a = src_va + off, b = dst_va + off;
        pb[n++] = omega_bw_mthd(s, OMEGA_BW_CE_OFFSET_IN_UPPER, 4);
        pb[n++] = (uint32_t)(a >> 32) & 0x1ffffffu;
        pb[n++] = (uint32_t)a;
        pb[n++] = (uint32_t)(b >> 32) & 0x1ffffffu;
        pb[n++] = (uint32_t)b;
        pb[n++] = omega_bw_mthd(s, OMEGA_BW_CE_LINE_LENGTH_IN, 1);
        pb[n++] = (uint32_t)len; /* 2^31 fits the 32-bit VALUE field */
        pb[n++] = omega_bw_mthd(s, OMEGA_BW_CE_LAUNCH_DMA, 1);
        pb[n++] = OMEGA_BW_CE_LAUNCH_NON_PIPELINED | OMEGA_BW_CE_LAUNCH_SRC_PITCH | OMEGA_BW_CE_LAUNCH_DST_PITCH;
    }
    if (sem_va) {
        pb[n++] = omega_bw_mthd(s, OMEGA_BW_CE_SET_SEMAPHORE_A, 3);
        pb[n++] = (uint32_t)(sem_va >> 32) & 0x1ffffffu;
        pb[n++] = (uint32_t)sem_va;
        pb[n++] = payload;
        pb[n++] = omega_bw_mthd(s, OMEGA_BW_CE_LAUNCH_DMA, 1);
        pb[n++] = OMEGA_BW_CE_LAUNCH_FLUSH | OMEGA_BW_CE_LAUNCH_SEM_ONE_WORD;
    }
    return n;
}
