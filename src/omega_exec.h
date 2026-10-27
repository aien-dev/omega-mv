/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Drake Stapleton (AIEN / aien-dev)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OMEGA_EXEC_H
#define OMEGA_EXEC_H

#include "omega_realize.h"
#include <stdint.h>
#include <stdbool.h>

/* Native in-process execution on AArch64 host */
int omega_exec_native_f3(const RealizationObject *real, uint64_t a, uint64_t b, uint64_t c, uint64_t *out_res);

/* Bare-metal QEMU runner generation and execution */
int omega_build_qemu_runner(const RealizationObject *real, const char *out_bin_path);
int omega_exec_qemu_virt(const char *runner_bin_path, char *out_log, size_t out_log_len);

#endif /* OMEGA_EXEC_H */
