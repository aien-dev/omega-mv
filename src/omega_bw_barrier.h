/*
 * omega_bw_barrier.h -- CPU memory barriers for the Blackwell host paths, per host ISA.
 *
 * The GB10 code writes `__asm__ volatile("dsb sy" ::: "memory")` inline (Grace is AArch64). The
 * RTX 5070 PC is x86_64, where that instruction does not exist. New code (the submit interface,
 * the target table tests) uses these macros instead; the existing GB10 files are NOT edited, so
 * their object code cannot change.
 *
 *   omega_bw_wmb()  before ringing a doorbell / publishing GPFIFO entries: all earlier CPU stores
 *                   (pushbuffer, QMD, GPPut) visible to the device. x86: sfence orders
 *                   write-combined BAR1 stores, which ordinary x86 ordering does not cover.
 *   omega_bw_mb()   full barrier, before reading device-written results.
 *
 * On AArch64 both are exactly `dsb sy` (the instruction every chip-proven GB10 path uses).
 * x86 choice: mfence is a superset of sfence and also orders loads; the Linux kernel's wmb() on
 * x86_64 is sfence and mb() is mfence (arch/x86/include/asm/barrier.h). CHIP-UNVERIFIED on x86.
 */
#ifndef OMEGA_BW_BARRIER_H
#define OMEGA_BW_BARRIER_H

#if defined(__aarch64__)
#define omega_bw_wmb() __asm__ volatile("dsb sy" ::: "memory")
#define omega_bw_mb() __asm__ volatile("dsb sy" ::: "memory")
#define OMEGA_BW_BARRIER_ISA "aarch64-dsb-sy"
#elif defined(__x86_64__)
#define omega_bw_wmb() __asm__ volatile("sfence" ::: "memory")
#define omega_bw_mb() __asm__ volatile("mfence" ::: "memory")
#define OMEGA_BW_BARRIER_ISA "x86_64-sfence-mfence"
#else
#error "omega_bw_barrier.h: no barrier defined for this host ISA"
#endif

#endif /* OMEGA_BW_BARRIER_H */
