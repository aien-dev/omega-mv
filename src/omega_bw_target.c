/*
 * omega_bw_target.c -- Blackwell target table. See omega_bw_target.h and docs/rig/GB20X_TARGET.md.
 */
#include "omega_bw_target.h"

#include <string.h>

/* ---- field notes ------------------------------------------------------- */

static const OmegaBwFieldNote GB10_NOTES[] = {
    { "pci_device", OMEGA_BW_EV_OBSERVED, "nvidia-smi pci.device_id 0x2E1210DE on the Spark; [OGKM] g_nv_name_released.h 0x2E12 \"NVIDIA GB10\"" },
    { "arch/impl", OMEGA_BW_EV_OBSERVED, "tools/rig/omega_bw_probe.c NV2080_CTRL_CMD_MC_GET_ARCH_INFO on the Spark (docs/rig/evidence/gb10_probe.json)" },
    { "rm_sm_version", OMEGA_BW_EV_OBSERVED, "Nvrm.sm_version 0xa04 in chip receipts (omega_evidence rm_sm_version) and the probe" },
    { "classes", OMEGA_BW_EV_OBSERVED, "physics/nvrm/nvrm.c refuses to open without CEC0/C96F/C661; probe class list" },
    { "qmd", OMEGA_BW_EV_OBSERVED, "src/omega_blackwell_qmd.c words, chip-proven by the M17 gates and every later GB10 receipt" },
    { "sm_count", OMEGA_BW_EV_DOCUMENTED, "48 SMs, 6144 CUDA cores (NVIDIA DGX Spark hardware guide; deviceQuery)" },
    { "l2_bytes", OMEGA_BW_EV_DOCUMENTED, "24 MB graphics L2 (Hot Chips 2025 GB10 talk); deviceQuery 25165824" },
    { "mem_model", OMEGA_BW_EV_OBSERVED, "physics/nvrm/nvrm.c: no local video memory, LOCATION_PCI for every buffer" },
};

static const OmegaBwFieldNote GB205_NOTES[] = {
    { "pci_device", OMEGA_BW_EV_DOCUMENTED, "[OGKM] 615.78.08 g_nv_name_released.h: 0x2F04 \"NVIDIA GeForce RTX 5070\"" },
    { "arch/impl", OMEGA_BW_EV_DOCUMENTED, "[OGKM] nv_arch.h GPU_IMPLEMENTATION_GB205 0x05; ctrl2080mc.h ARCHITECTURE_GB200 0x1B0" },
    { "sm_major/minor", OMEGA_BW_EV_DOCUMENTED, "CUDA compute capability 12.0 for GeForce RTX 50; [NAK] chipset >= 0x1b0 is SM 120" },
    { "rm_sm_version", OMEGA_BW_EV_THIRD_PARTY, "[TG] ops_nv.py maps 0xa04 to sm_120 on RTX 50; 0xC00 accepted for a later RM (ctrl2080gr.h)" },
    { "compute_class", OMEGA_BW_EV_THIRD_PARTY, "[TG] picks BLACKWELL_COMPUTE_B 0xCEC0 on RTX 50; [DOC] clcec0.h" },
    { "gpfifo_class", OMEGA_BW_EV_THIRD_PARTY, "[TG] picks BLACKWELL_CHANNEL_GPFIFO_A 0xC96F; [DOC] clca6f.h is the gb202 header for GPFIFO_B 0xCA6F, accepted as fallback" },
    { "usermode_class", OMEGA_BW_EV_THIRD_PARTY, "[TG] picks HOPPER_USERMODE_A 0xC661 first; BLACKWELL_USERMODE_A 0xC761 accepted as fallback (UNVERIFIED doorbell offset)" },
    { "copy_class", OMEGA_BW_EV_THIRD_PARTY, "[TG] picks BLACKWELL_DMA_COPY_B 0xCAB5; [DOC] clcab5.h" },
    { "qmd_major/words", OMEGA_BW_EV_THIRD_PARTY, "[TG] QMD v5, 0x60 words whenever compute_class >= BLACKWELL_COMPUTE_A; [DOC] clcec0qmd.h" },
    { "qmd_sass_version", OMEGA_BW_EV_THIRD_PARTY, "[TG] sass_version = ((sm>>4)&0xf0)|(sm&0xf) = 0xa4 for 0xa04" },
    { "qmd_smem_cfg", OMEGA_BW_EV_THIRD_PARTY, "[TG] min/target = KB/4+1 from {32,64,100} KB, max 0x1a, same for every Blackwell; [NAK] SM 120 has 6 smem sizes up to 100 KB" },
    { "qmd_free_cta_slots", OMEGA_BW_EV_INFERRED, "GB10 value 24 kept; both chips are SM 12.x with the same per-SM CTA limit. tinygrad leaves it 0" },
    { "cbank0_driver_words", OMEGA_BW_EV_THIRD_PARTY, "[TG] min_cbuf0_entries 224 on Blackwell; the GB10 template contents are a GB10 capture (UNVERIFIED on GB205)" },
    { "isa", OMEGA_BW_EV_INFERRED, "[NAK] uses one encoder for every chipset >= 0x1b0 (GB10 and GB20x); no sm_120 vs sm_121 encoding split is public" },
    { "sm_count/gpc_count", OMEGA_BW_EV_DOCUMENTED, "48 of 50 SMs enabled, 5 GPCs, 24 TPCs (RTX 5070 launch reviews, NVIDIA spec)" },
    { "l2_bytes", OMEGA_BW_EV_DOCUMENTED, "48 MB L2 (49152 KB) per NVIDIA spec as quoted by reviews" },
    { "vram", OMEGA_BW_EV_DOCUMENTED, "12 GB GDDR7, 192-bit, 28 Gbps, 672 GB/s, PCIe 5.0 x16" },
    { "mem_model", OMEGA_BW_EV_DOCUMENTED, "discrete card: device-local VRAM; host memory only over PCIe" },
};

/* ---- rows -------------------------------------------------------------- */

static const OmegaBwTarget TARGETS[] = {
    {
        .id = "gb10",
        .name = "NVIDIA DGX Spark (Grace Blackwell GB10)",
        .chip_proven = 1,
        .pci_vendor = 0x10DE,
        .pci_device = { 0x2E12, 0 },
        .arch = OMEGA_BW_ARCH_GB200,
        .impl = OMEGA_BW_IMPL_GB20B,
        .sm_major = 12, .sm_minor = 1,
        .rm_sm_version = { OMEGA_BW_RM_SM_VERSION_10_04, OMEGA_BW_RM_SM_VERSION_12_01 },
        .compute_class = OMEGA_BW_CLASS_BLACKWELL_COMPUTE_B,
        .gpfifo_class = OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_A,
        .gpfifo_class_alt = 0,
        .usermode_class = OMEGA_BW_CLASS_HOPPER_USERMODE_A,
        .usermode_class_alt = 0,
        .copy_class = OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_B,
        .qmd_major = 5, .qmd_words = 96,
        .qmd_sass_version = 0xa4,
        .qmd_smem_min_cfg = 9, .qmd_smem_max_cfg = 0x1a, .qmd_smem_target_cfg = 9,
        .qmd_free_cta_slots = 24,
        .cbank0_driver_words = 224,
        .sm_count = 48, .gpc_count = 0,
        .max_smem_per_block = 99u * 1024u,
        .l2_bytes = 24ull << 20,
        .vram_bytes = 0, .vram_bus_bits = 256, .mem_bandwidth_gbs = 273,
        .mem_model = OMEGA_BW_MEM_UNIFIED_SYSMEM,
        .host_isa = OMEGA_BW_HOST_AARCH64,
        .pcie_gen = 0, .pcie_lanes = 0,
        .notes = GB10_NOTES, .note_count = sizeof GB10_NOTES / sizeof GB10_NOTES[0],
    },
    {
        .id = "gb205-rtx5070",
        .name = "NVIDIA GeForce RTX 5070 (GB205-300)",
        .chip_proven = 0,
        .pci_vendor = 0x10DE,
        .pci_device = { 0x2F04, 0 },
        .arch = OMEGA_BW_ARCH_GB200,
        .impl = OMEGA_BW_IMPL_GB205,
        .sm_major = 12, .sm_minor = 0,
        .rm_sm_version = { OMEGA_BW_RM_SM_VERSION_10_04, OMEGA_BW_RM_SM_VERSION_12_00 },
        .compute_class = OMEGA_BW_CLASS_BLACKWELL_COMPUTE_B,
        .gpfifo_class = OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_A,
        .gpfifo_class_alt = OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_B,
        .usermode_class = OMEGA_BW_CLASS_HOPPER_USERMODE_A,
        .usermode_class_alt = OMEGA_BW_CLASS_BLACKWELL_USERMODE_A,
        .copy_class = OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_B,
        .qmd_major = 5, .qmd_words = 96,
        .qmd_sass_version = 0xa4,
        .qmd_smem_min_cfg = 9, .qmd_smem_max_cfg = 0x1a, .qmd_smem_target_cfg = 9,
        .qmd_free_cta_slots = 24,
        .cbank0_driver_words = 224,
        .sm_count = 48, .gpc_count = 5,
        .max_smem_per_block = 99u * 1024u,
        .l2_bytes = 48ull << 20,
        .vram_bytes = 12ull << 30, .vram_bus_bits = 192, .mem_bandwidth_gbs = 672,
        .mem_model = OMEGA_BW_MEM_DISCRETE_VRAM,
        .host_isa = OMEGA_BW_HOST_X86_64,
        .pcie_gen = 5, .pcie_lanes = 16,
        .notes = GB205_NOTES, .note_count = sizeof GB205_NOTES / sizeof GB205_NOTES[0],
    },
    {
        .id = "gb203-rtx5070",
        .name = "NVIDIA GeForce RTX 5070 (board id 0x2C09, GB203 die range)",
        .chip_proven = 0,
        .pci_vendor = 0x10DE,
        .pci_device = { 0x2C09, 0 },
        .arch = OMEGA_BW_ARCH_GB200,
        .impl = OMEGA_BW_IMPL_GB203,
        .sm_major = 12, .sm_minor = 0,
        .rm_sm_version = { OMEGA_BW_RM_SM_VERSION_10_04, OMEGA_BW_RM_SM_VERSION_12_00 },
        .compute_class = OMEGA_BW_CLASS_BLACKWELL_COMPUTE_B,
        .gpfifo_class = OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_A,
        .gpfifo_class_alt = OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_B,
        .usermode_class = OMEGA_BW_CLASS_HOPPER_USERMODE_A,
        .usermode_class_alt = OMEGA_BW_CLASS_BLACKWELL_USERMODE_A,
        .copy_class = OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_B,
        .qmd_major = 5, .qmd_words = 96,
        .qmd_sass_version = 0xa4,
        .qmd_smem_min_cfg = 9, .qmd_smem_max_cfg = 0x1a, .qmd_smem_target_cfg = 9,
        .qmd_free_cta_slots = 24,
        .cbank0_driver_words = 224,
        .sm_count = 48, .gpc_count = 0,
        .max_smem_per_block = 99u * 1024u,
        .l2_bytes = 0, /* UNKNOWN for this board: a cut GB203 may keep more L2 than GB205 */
        .vram_bytes = 12ull << 30, .vram_bus_bits = 192, .mem_bandwidth_gbs = 672,
        .mem_model = OMEGA_BW_MEM_DISCRETE_VRAM,
        .host_isa = OMEGA_BW_HOST_X86_64,
        .pcie_gen = 5, .pcie_lanes = 16,
        .notes = GB205_NOTES, .note_count = sizeof GB205_NOTES / sizeof GB205_NOTES[0],
    },
};

#define NTARGETS (sizeof TARGETS / sizeof TARGETS[0])

size_t omega_bw_target_count(void) { return NTARGETS; }

const OmegaBwTarget *omega_bw_target_at(size_t i) { return i < NTARGETS ? &TARGETS[i] : NULL; }

const OmegaBwTarget *omega_bw_target_by_id(const char *id)
{
    if (!id) return NULL;
    for (size_t i = 0; i < NTARGETS; i++)
        if (strcmp(TARGETS[i].id, id) == 0) return &TARGETS[i];
    return NULL;
}

const OmegaBwTarget *omega_bw_target_gb10(void) { return &TARGETS[0]; }

static int sm_version_ok(const OmegaBwTarget *t, uint32_t v)
{
    if (v == 0) return 1;
    for (int i = 0; i < 2; i++)
        if (t->rm_sm_version[i] && t->rm_sm_version[i] == v) return 1;
    return 0;
}

const OmegaBwTarget *omega_bw_target_identify(uint32_t arch, uint32_t impl, uint32_t rm_sm_version,
                                              uint16_t pci_device)
{
    for (size_t i = 0; i < NTARGETS; i++) {
        const OmegaBwTarget *t = &TARGETS[i];
        if (arch != 0 && (t->arch != arch || t->impl != impl)) continue;
        if (arch == 0) {
            int hit = 0;
            for (int k = 0; k < 2 && t->pci_device[k]; k++)
                if (t->pci_device[k] == pci_device) hit = 1;
            if (!hit) continue;
        }
        if (!sm_version_ok(t, rm_sm_version)) continue;
        return t;
    }
    return NULL;
}

static int has(const uint32_t *c, size_t n, uint32_t v)
{
    for (size_t i = 0; i < n; i++)
        if (c[i] == v) return 1;
    return 0;
}

void omega_bw_classes_from_list(const uint32_t *classes, size_t n, OmegaBwClassSet *out)
{
    memset(out, 0, sizeof *out);
    if (!classes) n = 0;
    if (has(classes, n, OMEGA_BW_CLASS_BLACKWELL_COMPUTE_B)) out->compute = OMEGA_BW_CLASS_BLACKWELL_COMPUTE_B;
    if (has(classes, n, OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_A)) out->gpfifo = OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_A;
    else if (has(classes, n, OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_B)) out->gpfifo = OMEGA_BW_CLASS_BLACKWELL_CHANNEL_GPFIFO_B;
    if (has(classes, n, OMEGA_BW_CLASS_HOPPER_USERMODE_A)) out->usermode = OMEGA_BW_CLASS_HOPPER_USERMODE_A;
    else if (has(classes, n, OMEGA_BW_CLASS_BLACKWELL_USERMODE_A)) out->usermode = OMEGA_BW_CLASS_BLACKWELL_USERMODE_A;
    if (has(classes, n, OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_B)) out->copy = OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_B;
    else if (has(classes, n, OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_A)) out->copy = OMEGA_BW_CLASS_BLACKWELL_DMA_COPY_A;
}

int omega_bw_target_classes_ok(const OmegaBwTarget *t, const OmegaBwClassSet *cs)
{
    if (!t || !cs) return 0;
    if (cs->compute != t->compute_class) return 0;
    if (cs->gpfifo != t->gpfifo_class && !(t->gpfifo_class_alt && cs->gpfifo == t->gpfifo_class_alt)) return 0;
    if (cs->usermode != t->usermode_class && !(t->usermode_class_alt && cs->usermode == t->usermode_class_alt)) return 0;
    return 1;
}

const char *omega_bw_target_arch_name(const OmegaBwTarget *t)
{
    if (!t) return "unknown";
    if (t->sm_major == 12 && t->sm_minor == 1) return "sm_121";
    if (t->sm_major == 12 && t->sm_minor == 0) return "sm_120";
    return "unknown";
}

const char *omega_bw_evidence_name(OmegaBwEvidence e)
{
    switch (e) {
    case OMEGA_BW_EV_OBSERVED: return "OBSERVED";
    case OMEGA_BW_EV_DOCUMENTED: return "DOCUMENTED";
    case OMEGA_BW_EV_THIRD_PARTY: return "THIRD_PARTY";
    case OMEGA_BW_EV_INFERRED: return "INFERRED";
    case OMEGA_BW_EV_UNKNOWN: return "UNKNOWN";
    }
    return "?";
}

/* ---- QMD field access -------------------------------------------------- */

uint32_t omega_bw_qmd_get(const uint32_t *qmd, unsigned hi, unsigned lo)
{
    uint32_t v = 0;
    for (unsigned b = lo; b <= hi; b++)
        if (qmd[b / 32] & (1u << (b % 32))) v |= 1u << (b - lo);
    return v;
}

void omega_bw_qmd_set(uint32_t *qmd, unsigned hi, unsigned lo, uint32_t v)
{
    for (unsigned b = lo; b <= hi; b++) {
        uint32_t bit = 1u << (b % 32);
        if (v & (1u << (b - lo))) qmd[b / 32] |= bit;
        else qmd[b / 32] &= ~bit;
    }
}

/* Bit ranges from clcec0qmd.h (third_party/nvidia-open-gpu-doc), checked against the header by
 * tests/test_omega_bw_target.c. */
#define QMD_SASS_VERSION_HI 455u
#define QMD_SASS_VERSION_LO 448u
#define QMD_MIN_SM_CFG_HI 1168u
#define QMD_MIN_SM_CFG_LO 1163u
#define QMD_MAX_SM_CFG_HI 1174u
#define QMD_MAX_SM_CFG_LO 1169u
#define QMD_TARGET_SM_CFG_HI 1180u
#define QMD_TARGET_SM_CFG_LO 1175u
#define QMD_FREE_CTA_HI 635u
#define QMD_FREE_CTA_LO 628u

int omega_bw_target_build_qmd1(const OmegaBwTarget *t, uint32_t *qmd1_words, const OmegaBlackwellQmdConfig *cfg)
{
    const OmegaBwTarget *g = omega_bw_target_gb10();
    if (!t || !qmd1_words || !cfg) return -1;
    if (t->qmd_major != 5 || t->qmd_words != OMEGA_BW_QMD_WORDS) return -1;
    int rc = omega_blackwell_build_qmd1(qmd1_words, cfg);
    if (rc != 0 || t == g) return rc;
    if (t->qmd_sass_version != g->qmd_sass_version)
        omega_bw_qmd_set(qmd1_words, QMD_SASS_VERSION_HI, QMD_SASS_VERSION_LO, t->qmd_sass_version);
    if (t->qmd_smem_min_cfg != g->qmd_smem_min_cfg)
        omega_bw_qmd_set(qmd1_words, QMD_MIN_SM_CFG_HI, QMD_MIN_SM_CFG_LO, t->qmd_smem_min_cfg);
    if (t->qmd_smem_max_cfg != g->qmd_smem_max_cfg)
        omega_bw_qmd_set(qmd1_words, QMD_MAX_SM_CFG_HI, QMD_MAX_SM_CFG_LO, t->qmd_smem_max_cfg);
    if (t->qmd_smem_target_cfg != g->qmd_smem_target_cfg)
        omega_bw_qmd_set(qmd1_words, QMD_TARGET_SM_CFG_HI, QMD_TARGET_SM_CFG_LO, t->qmd_smem_target_cfg);
    if (t->qmd_free_cta_slots != g->qmd_free_cta_slots)
        omega_bw_qmd_set(qmd1_words, QMD_FREE_CTA_HI, QMD_FREE_CTA_LO, t->qmd_free_cta_slots);
    return 0;
}
