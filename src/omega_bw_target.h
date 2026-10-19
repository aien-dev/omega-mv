/*
 * omega_bw_target.h -- Blackwell target table: one row per chip Omega's Blackwell backend may drive.
 *
 * Rows today:
 *   gb10          NVIDIA DGX Spark GB10 (sm_121, integrated, 128 GiB unified LPDDR5x). Chip-proven.
 *   gb205-rtx5070 GeForce RTX 5070 (GB205-300, sm_120, discrete, 12 GiB GDDR7 over PCIe 5.0 x16).
 *                 THEORETICAL: built from public sources only, never run on the chip.
 *   gb203-rtx5070 the second RTX 5070 board id (0x2C09, GB203 die per the 0x2Cxx range). Same
 *                 caveats as the GB205 row; kept so the identifier does not refuse that board.
 *
 * Every field carries an evidence grade and a source string (OmegaBwFieldNote). Nothing in this
 * file changes the GB10 launch path: src/omega_blackwell_{qmd,submit,engine}.c do not include it.
 * omega_bw_target_build_qmd1() wraps the unchanged GB10 builder and only rewrites fields whose
 * per-target value differs from GB10's; tests/test_omega_bw_target.c proves the GB10 output is
 * the same bytes as omega_blackwell_build_qmd1() for a sweep of configurations.
 *
 * Sources (pinned in docs/rig/GB20X_TARGET.md):
 *   [OGKM]  NVIDIA open-gpu-kernel-modules 615.78.08 (ca2d03e): nv_arch.h, g_nv_name_released.h,
 *           class headers; physics third_party 580.173.02 ctrl2080gr.h / ctrl2080mc.h
 *   [DOC]   NVIDIA open-gpu-doc class headers as vendored by Mesa (77781ae): clcec0.h, clcec0qmd.h,
 *           clcab5.h, clca6f.h (copies in third_party/nvidia-open-gpu-doc/)
 *   [TG]    tinygrad ops_nv.py (45fc5ae), which drives RTX 50 cards on the same open kernel module
 *   [NAK]   Mesa NAK sm70_encode.rs / nouveau_device.c (77781ae): chipset >= 0x1b0 is SM 120
 *   [SPEC]  public board specifications (NVIDIA, Hot Chips 2025 GB10 talk, launch reviews)
 *   [GB10]  observed on the Spark by tools/rig/omega_bw_probe.c or earlier chip receipts
 */
#ifndef OMEGA_BW_TARGET_H
#define OMEGA_BW_TARGET_H

#include <stddef.h>
#include <stdint.h>

#include "omega_blackwell_qmd.h"

/* Class ids. Values from [OGKM] class headers and [TG] autogen (nv_570.py). */
#define OMEGA_BW_CLASS_BLACKWELL_COMPUTE_A        0xCDC0u /* GB100 */
#define OMEGA_BW_CLASS_BLACKWELL_COMPUTE_B        0xCEC0u /* GB20x and GB10 */
#define OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_A 0xC96Fu
#define OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_B 0xCA6Fu
#define OMEGA_BW_CLASS_HOPPER_USERMODE_A          0xC661u
#define OMEGA_BW_CLASS_BLACKWELL_USERMODE_A       0xC761u
#define OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_A       0xC9B5u
#define OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_B       0xCAB5u

/* NV2080_CTRL_MC_ARCH_INFO_ARCHITECTURE_* and GPU_IMPLEMENTATION_* ([OGKM] ctrl2080mc.h, nv_arch.h). */
#define OMEGA_BW_ARCH_GB100 0x1A0u
#define OMEGA_BW_ARCH_GB200 0x1B0u
#define OMEGA_BW_IMPL_GB202 0x02u
#define OMEGA_BW_IMPL_GB203 0x03u
#define OMEGA_BW_IMPL_GB205 0x05u
#define OMEGA_BW_IMPL_GB206 0x06u
#define OMEGA_BW_IMPL_GB207 0x07u
#define OMEGA_BW_IMPL_GB20B 0x0Bu
#define OMEGA_BW_IMPL_GB20C 0x0Cu

/* RM SM version (NV2080_CTRL_GR_INFO_INDEX_SM_VERSION). 0xA04 is the legacy code the 580 RM still
 * returns for the 12.x family (ctrl2080gr.h TODO bug 4333440); GB10 returns it ([GB10]) and tinygrad
 * maps it to sm_120 on RTX 50 cards ([TG] "sm_120" if sm_version==0xa04). 0xC00/0xC01 are the new
 * codes a later RM may return instead. */
#define OMEGA_BW_RM_SM_VERSION_10_04 0x0A04u
#define OMEGA_BW_RM_SM_VERSION_12_00 0x0C00u
#define OMEGA_BW_RM_SM_VERSION_12_01 0x0C01u

typedef enum {
    OMEGA_BW_EV_OBSERVED = 1,   /* read on this chip by our own code */
    OMEGA_BW_EV_DOCUMENTED = 2, /* written in a public NVIDIA header or spec sheet */
    OMEGA_BW_EV_THIRD_PARTY = 3,/* used successfully by another open driver on this chip family (tinygrad, Mesa) */
    OMEGA_BW_EV_INFERRED = 4,   /* our reasoning from the above; plausible, unproven */
    OMEGA_BW_EV_UNKNOWN = 5     /* nobody public says; must be measured on the chip */
} OmegaBwEvidence;

typedef enum {
    OMEGA_BW_MEM_UNIFIED_SYSMEM = 1, /* GB10: no VRAM, everything is coherent system memory (LOCATION_PCI) */
    OMEGA_BW_MEM_DISCRETE_VRAM = 2   /* GB20x: device-local GDDR7 behind PCIe; host memory is reached over PCIe */
} OmegaBwMemModel;

typedef enum {
    OMEGA_BW_HOST_AARCH64 = 1, /* Grace: barrier is dsb sy */
    OMEGA_BW_HOST_X86_64 = 2   /* desktop PC: barrier is mfence (sfence for write-combined BAR1 stores) */
} OmegaBwHostIsa;

typedef struct {
    const char *field;
    OmegaBwEvidence evidence;
    const char *source;
} OmegaBwFieldNote;

typedef struct {
    const char *id;
    const char *name;
    int chip_proven;                 /* 1 only when Omega's own gates passed on this chip */
    uint16_t pci_vendor;
    uint16_t pci_device[2];          /* 0 terminated */
    uint32_t arch;                   /* NV2080_CTRL_MC_ARCH_INFO_ARCHITECTURE_* */
    uint32_t impl;                   /* GPU_IMPLEMENTATION_* */
    uint32_t sm_major, sm_minor;     /* CUDA-style compute capability */
    uint32_t rm_sm_version[2];       /* accepted RM SM_VERSION values, 0 terminated */
    /* channel and object classes */
    uint32_t compute_class;
    uint32_t gpfifo_class;           /* preferred */
    uint32_t gpfifo_class_alt;       /* accepted when the preferred one is absent, 0 = none */
    uint32_t usermode_class;
    uint32_t usermode_class_alt;
    uint32_t copy_class;
    /* launch descriptor */
    uint32_t qmd_major;              /* NVCEC0_QMDV05_00_QMD_MAJOR_VERSION */
    uint32_t qmd_words;              /* 96 for QMD V05 ([TG] sz 0x60) */
    uint32_t qmd_sass_version;       /* NVCEC0_QMDV05_00_SASS_VERSION (455:448) */
    uint32_t qmd_smem_min_cfg;       /* MIN_SM_CONFIG_SHARED_MEM_SIZE (1168:1163) */
    uint32_t qmd_smem_max_cfg;       /* MAX_SM_CONFIG_SHARED_MEM_SIZE (1174:1169) */
    uint32_t qmd_smem_target_cfg;    /* TARGET_SM_CONFIG_SHARED_MEM_SIZE (1180:1175) */
    uint32_t qmd_free_cta_slots;     /* FREE_CTA_SLOTS_EMPTY_SM (635:628) */
    uint32_t cbank0_driver_words;    /* 224 on Blackwell ([TG] min_cbuf0_entries) */
    /* machine shape */
    uint32_t sm_count;
    uint32_t gpc_count;
    uint32_t max_smem_per_block;     /* bytes */
    uint64_t l2_bytes;
    uint64_t vram_bytes;             /* 0 = no device-local memory */
    uint32_t vram_bus_bits;
    uint32_t mem_bandwidth_gbs;      /* public peak, GB/s */
    OmegaBwMemModel mem_model;
    OmegaBwHostIsa host_isa;
    uint32_t pcie_gen, pcie_lanes;   /* 0 for the integrated GB10 (C2C) */
    const OmegaBwFieldNote *notes;
    size_t note_count;
} OmegaBwTarget;

/* Classes found in an RM class list, chosen with the same preference order as physics/nvrm
 * (compute B; GPFIFO A then B; Hopper usermode then Blackwell usermode; copy B then A). */
typedef struct {
    uint32_t compute, gpfifo, usermode, copy;
} OmegaBwClassSet;

size_t omega_bw_target_count(void);
const OmegaBwTarget *omega_bw_target_at(size_t i);
const OmegaBwTarget *omega_bw_target_by_id(const char *id);
const OmegaBwTarget *omega_bw_target_gb10(void);
/* Identify by chip id (arch + impl) first; when those are 0 (unknown), fall back to the PCI
 * device id; rm_sm_version must then be one the row accepts (0 skips that check). NULL if no row. */
const OmegaBwTarget *omega_bw_target_identify(uint32_t arch, uint32_t impl, uint32_t rm_sm_version,
                                              uint16_t pci_device);
void omega_bw_classes_from_list(const uint32_t *classes, size_t n, OmegaBwClassSet *out);
/* 1 if the class set satisfies the row (compute exact, gpfifo/usermode preferred or alt, copy any). */
int omega_bw_target_classes_ok(const OmegaBwTarget *t, const OmegaBwClassSet *cs);
/* "sm_120" style name. */
const char *omega_bw_target_arch_name(const OmegaBwTarget *t);
const char *omega_bw_evidence_name(OmegaBwEvidence e);

/* QMD1 for a target. Calls the unchanged GB10 builder (omega_blackwell_build_qmd1), then rewrites
 * SASS_VERSION, the three SM_CONFIG shared memory sizes and FREE_CTA_SLOTS when the target's value
 * differs from GB10's. Returns the builder's code. */
int omega_bw_target_build_qmd1(const OmegaBwTarget *t, uint32_t *qmd1_words, const OmegaBlackwellQmdConfig *cfg);

/* Generic field access on a QMD (bit numbers as in clcec0qmd.h MW(hi:lo)). */
uint32_t omega_bw_qmd_get(const uint32_t *qmd, unsigned hi, unsigned lo);
void omega_bw_qmd_set(uint32_t *qmd, unsigned hi, unsigned lo, uint32_t v);

#endif /* OMEGA_BW_TARGET_H */
