/*
 * omega_dxg_abi.h -- the one way Omega includes the WSL2 dxgkrnl uapi header.
 *
 * third_party/wsl2-dxgkrnl/d3dkmthk.h is a kernel uapi file. It ends a struct with `__packed`,
 * which only the kernel's compiler.h defines; without this define a user-space compile silently
 * creates a global variable named __packed instead of a packed struct.
 */
#ifndef OMEGA_DXG_ABI_H
#define OMEGA_DXG_ABI_H

#include <linux/types.h>
#include <sys/ioctl.h>

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#include "d3dkmthk.h"

#endif
