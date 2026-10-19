/*
 * omega_submit_select.c -- backend choice and status names for the submit interface.
 */
#include "omega_gpu_submit_iface.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int omega_submit_select(OmegaSubmitBackend *out)
{
    const char *want = getenv("OMEGA_SUBMIT");
    if (!out) return OMEGA_SUB_E_ARG;
    if (want && *want) {
        if (strcmp(want, "nvrm") == 0) {
            out->ops = omega_submit_nvrm_ops();
            out->self = omega_submit_nvrm_self();
            return OMEGA_SUB_OK;
        }
        if (strcmp(want, "dxg") == 0) {
            out->ops = omega_submit_dxg_ops();
            out->self = omega_submit_dxg_self();
            return OMEGA_SUB_OK;
        }
        return OMEGA_SUB_E_ARG;
    }
    if (access("/dev/nvidiactl", F_OK) == 0) {
        out->ops = omega_submit_nvrm_ops();
        out->self = omega_submit_nvrm_self();
        return OMEGA_SUB_OK;
    }
    if (access("/dev/dxg", F_OK) == 0) {
        out->ops = omega_submit_dxg_ops();
        out->self = omega_submit_dxg_self();
        return OMEGA_SUB_OK;
    }
    return OMEGA_SUB_E_NO_DEVICE;
}

const char *omega_sub_status_name(int status)
{
    switch (status) {
    case OMEGA_SUB_OK: return "ok";
    case OMEGA_SUB_E_ARG: return "bad argument";
    case OMEGA_SUB_E_NO_DEVICE: return "no device node";
    case OMEGA_SUB_E_DRIVER: return "driver call failed";
    case OMEGA_SUB_E_TIMEOUT: return "timeout";
    case OMEGA_SUB_E_UNSUPPORTED: return "unsupported";
    case OMEGA_SUB_E_VENDOR_BLOB: return "needs NVIDIA private driver data";
    case OMEGA_SUB_E_STATE: return "wrong call order";
    default: return "unknown status";
    }
}
