/*
 * tools/rig/omega_bw_probe.c -- read-only chip identity probe for the Blackwell target table.
 *
 * Opens the NVIDIA open kernel module through physics/nvrm (the same nvrm_open every Omega
 * launch uses), then asks RM a few read-only questions:
 *   - the device class list (which compute / GPFIFO / usermode / copy classes exist)
 *   - NV2080_CTRL_CMD_MC_GET_ARCH_INFO (architecture and implementation = chip id)
 *   - NV2080_CTRL_CMD_GR_GET_INFO for the SM version and the litter (max) GPC/TPC/SM counts
 * and prints one JSON object. It allocates no GPU memory beyond what nvrm_open itself does,
 * creates no channel and launches no work. It never edits the chip state.
 *
 * On the Spark this fills the OBSERVED column for GB10. On a Linux PC with an RTX 5070 and the
 * NVIDIA open kernel module this is the FIRST test to run (docs/rig/GB20X_TARGET.md section 6):
 * its output decides whether src/omega_bw_target.c picked the right row.
 */
#include "nvrm.h"
#include "omega_bw_target.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include "nvos.h"
#include "nv-ioctl.h"
#include "nv-ioctl-numbers.h"
#include "nv_escape.h"
#include "ctrl/ctrl0080/ctrl0080gpu.h"
#include "ctrl/ctrl2080/ctrl2080gr.h"
#include "ctrl/ctrl2080/ctrl2080mc.h"

#define NV_IOWR(nr, sz) _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, (nr), (sz))

static int ctl(Nvrm *rm, uint32_t obj, uint32_t cmd, void *params, uint32_t psz, uint32_t *status)
{
    NVOS54_PARAMETERS p;
    memset(&p, 0, sizeof p);
    p.hClient = rm->root;
    p.hObject = obj;
    p.cmd = cmd;
    p.params = NV_PTR_TO_NvP64(params);
    p.paramsSize = psz;
    if (ioctl(rm->fd_ctl, NV_IOWR(NV_ESC_RM_CONTROL, sizeof p), &p) != 0) return -errno;
    *status = p.status;
    return p.status == 0 ? 0 : -1;
}

int main(void)
{
    static Nvrm rm; /* about 225 KiB, keep it off the stack */
    uint32_t st = 0;
    if (nvrm_open(&rm) != 0) {
        printf("{\"ok\": false, \"stage\": \"nvrm_open\", \"err\": \"%s\"}\n", rm.err);
        nvrm_close(&rm);
        return 2;
    }

    printf("{\n  \"ok\": true,\n");
    printf("  \"nvrm_picked\": {\"compute_class\": \"0x%04x\", \"gpfifo_class\": \"0x%04x\", "
           "\"usermode_class\": \"0x%04x\", \"rm_sm_version\": \"0x%04x\"},\n",
           rm.compute_class, rm.gpfifo_class, rm.usermode_class, rm.sm_version);

    NV2080_CTRL_MC_GET_ARCH_INFO_PARAMS ai;
    memset(&ai, 0, sizeof ai);
    int rc = ctl(&rm, rm.subdevice, NV2080_CTRL_CMD_MC_GET_ARCH_INFO, &ai, sizeof ai, &st);
    printf("  \"arch_info\": {\"rc\": %d, \"status\": \"0x%x\", \"architecture\": \"0x%x\", "
           "\"implementation\": \"0x%x\", \"revision\": \"0x%x\"},\n",
           rc, st, ai.architecture, ai.implementation, ai.revision);

    struct { uint32_t idx; const char *name; } gr[] = {
        { NV2080_CTRL_GR_INFO_INDEX_SM_VERSION, "sm_version" },
        { NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_GPCS, "litter_num_gpcs" },
        { NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_TPC_PER_GPC, "litter_num_tpc_per_gpc" },
        { NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_SM_PER_TPC, "litter_num_sm_per_tpc" },
        { NV2080_CTRL_GR_INFO_INDEX_MAX_WARPS_PER_SM, "max_warps_per_sm" },
        { NV2080_CTRL_GR_INFO_INDEX_SHADER_PIPE_COUNT, "shader_pipe_count" },
        { NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_LTCS, "litter_num_ltcs" },
        { NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_SLICES_PER_LTC, "litter_num_slices_per_ltc" },
        { NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_FBPAS, "litter_num_fbpas" },
    };
    enum { NGR = sizeof gr / sizeof gr[0] };
    NV2080_CTRL_GR_INFO gi[NGR];
    for (unsigned i = 0; i < NGR; i++) { gi[i].index = gr[i].idx; gi[i].data = 0; }
    NV2080_CTRL_GR_GET_INFO_PARAMS gp;
    memset(&gp, 0, sizeof gp);
    gp.grInfoListSize = NGR;
    gp.grInfoList = NV_PTR_TO_NvP64(gi);
    rc = ctl(&rm, rm.subdevice, NV2080_CTRL_CMD_GR_GET_INFO, &gp, sizeof gp, &st);
    printf("  \"gr_info\": {\"rc\": %d", rc);
    for (unsigned i = 0; i < NGR; i++) printf(", \"%s\": %u", gr[i].name, gi[i].data);
    printf("},\n");

    NV0080_CTRL_GPU_GET_CLASSLIST_PARAMS cl;
    memset(&cl, 0, sizeof cl);
    uint32_t classes[512];
    unsigned n = 0;
    if (ctl(&rm, rm.device, NV0080_CTRL_CMD_GPU_GET_CLASSLIST, &cl, sizeof cl, &st) == 0 && cl.numClasses <= 512) {
        cl.classList = NV_PTR_TO_NvP64(classes);
        if (ctl(&rm, rm.device, NV0080_CTRL_CMD_GPU_GET_CLASSLIST, &cl, sizeof cl, &st) == 0) n = cl.numClasses;
    }
    printf("  \"classes\": [");
    for (unsigned i = 0; i < n; i++) printf("%s\"0x%04x\"", i ? ", " : "", classes[i]);
    printf("],\n");

    OmegaBwClassSet cs;
    omega_bw_classes_from_list(classes, n, &cs);
    const OmegaBwTarget *t = omega_bw_target_identify(ai.architecture, ai.implementation, rm.sm_version, 0);
    printf("  \"target_table\": {\"picked\": \"%s\", \"classes_ok\": %d, \"compute\": \"0x%04x\", "
           "\"gpfifo\": \"0x%04x\", \"usermode\": \"0x%04x\", \"copy\": \"0x%04x\"}\n}\n",
           t ? t->id : "NONE", omega_bw_target_classes_ok(t, &cs), cs.compute, cs.gpfifo, cs.usermode, cs.copy);

    int crc = nvrm_close(&rm);
    return (crc == 0 && t) ? 0 : 1;
}
