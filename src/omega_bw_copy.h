/*
 * omega_bw_copy.h -- copy-engine (BLACKWELL_DMA_COPY_B, 0xCAB5) pushbuffer encoder for discrete
 * Blackwell cards: upload host -> VRAM and download VRAM -> host over PCIe.
 *
 * Host side only. Nothing here touches a device. The words follow third_party/nvidia-open-gpu-doc/
 * clcab5.h (MIT, vendored from Mesa 77781ae) and the method order tinygrad uses on RTX 50 cards
 * (ops_nv.py NVCopyQueue.copy / .semaphore, 45fc5ae):
 *   per chunk of at most 2^31 bytes:
 *     OFFSET_IN_UPPER, OFFSET_IN_LOWER, OFFSET_OUT_UPPER, OFFSET_OUT_LOWER  (one incrementing header)
 *     LINE_LENGTH_IN = chunk bytes
 *     LAUNCH_DMA = NON_PIPELINED | SRC pitch | DST pitch            (virtual addresses, one line)
 *   then the completion:
 *     SET_SEMAPHORE_A (upper), SET_SEMAPHORE_B (lower), SET_SEMAPHORE_PAYLOAD
 *     LAUNCH_DMA = FLUSH_ENABLE | SEMAPHORE_TYPE RELEASE_ONE_WORD
 *
 * Subchannel: tinygrad binds the copy object on subchannel 4 of its own copy channel (SET_OBJECT
 * 0xCAB5 first). omega_bw_copy_bind() emits that SET_OBJECT. GB10 never runs this: the Spark has no
 * VRAM, its buffers are CPU-mapped system memory (memcpy is the upload).
 *
 * CHIP-UNVERIFIED: no copy engine method has been executed by Omega on any chip.
 */
#ifndef OMEGA_BW_COPY_H
#define OMEGA_BW_COPY_H

#include <stddef.h>
#include <stdint.h>

#define OMEGA_BW_COPY_SUBCHANNEL 4u
#define OMEGA_BW_COPY_CHUNK_MAX (1ull << 31)

/* Method offsets, clcab5.h. */
#define OMEGA_BW_CE_SET_OBJECT 0x0000u
#define OMEGA_BW_CE_SET_SEMAPHORE_A 0x0240u
#define OMEGA_BW_CE_SET_SEMAPHORE_B 0x0244u
#define OMEGA_BW_CE_SET_SEMAPHORE_PAYLOAD 0x0248u
#define OMEGA_BW_CE_LAUNCH_DMA 0x0300u
#define OMEGA_BW_CE_OFFSET_IN_UPPER 0x0400u
#define OMEGA_BW_CE_LINE_LENGTH_IN 0x0418u

/* LAUNCH_DMA fields, clcab5.h. */
#define OMEGA_BW_CE_LAUNCH_NON_PIPELINED (2u << 0)   /* DATA_TRANSFER_TYPE 1:0 = NON_PIPELINED */
#define OMEGA_BW_CE_LAUNCH_FLUSH (1u << 2)           /* FLUSH_ENABLE 2:2 = TRUE */
#define OMEGA_BW_CE_LAUNCH_SEM_ONE_WORD (1u << 3)    /* SEMAPHORE_TYPE 4:3 = RELEASE_ONE_WORD_SEMAPHORE */
#define OMEGA_BW_CE_LAUNCH_SRC_PITCH (1u << 7)       /* SRC_MEMORY_LAYOUT 7:7 = PITCH */
#define OMEGA_BW_CE_LAUNCH_DST_PITCH (1u << 8)       /* DST_MEMORY_LAYOUT 8:8 = PITCH */

/* Pushbuffer method header, the same shape as physics nvrm_mthd(): SEC_OP 2 = incrementing. */
static inline uint32_t omega_bw_mthd(uint32_t subc, uint32_t mthd, uint32_t count)
{
    return (2u << 28) | (count << 16) | (subc << 13) | (mthd >> 2);
}

/* Words for SET_OBJECT on the copy subchannel. Returns words written, 0 if cap is too small. */
size_t omega_bw_copy_bind(uint32_t *pb, size_t cap, uint32_t copy_class);

/* Words for one linear copy of `bytes` from src_va to dst_va (both GPU virtual addresses), then a
 * one-word semaphore release of `payload` at sem_va (skipped when sem_va is 0). Returns words
 * written, or 0 if bytes is 0, an address range wraps, a VA is above 2^57 or cap is too small. */
size_t omega_bw_copy_linear(uint32_t *pb, size_t cap, uint64_t src_va, uint64_t dst_va, uint64_t bytes,
                            uint64_t sem_va, uint32_t payload);

/* Exact word count omega_bw_copy_linear() needs. */
size_t omega_bw_copy_linear_words(uint64_t bytes, int with_semaphore);

#endif /* OMEGA_BW_COPY_H */
