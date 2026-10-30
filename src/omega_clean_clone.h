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

#ifndef OMEGA_CLEAN_CLONE_H
#define OMEGA_CLEAN_CLONE_H

#include <stddef.h>

/* M18 gate 16 plans the clean-clone copy here so a host test can see the
 * path and the command without building the tree or touching a GPU.
 *
 * The log is created under the gate's own output directory
 * (<out_dir>/clean-clone/clean_clone_m18.log) when out_dir is set, otherwise
 * under tmpdir when that is set. It is never a fixed path on the host temp
 * directory (omega#526). The copy itself is placed beside that log when that
 * directory is outside the repo, and otherwise in a private directory under
 * tmpdir or temp_fallback, so the copy of the repo does not land inside the
 * repo.
 *
 * Return values:
 *   OMEGA_M18_CLEAN_CLONE_OK
 *   OMEGA_M18_CLEAN_CLONE_LOG_CREATE_FAILED  log file could not be created;
 *       err begins with "M18_CLEAN_CLONE_LOG_CREATE_FAILED:" and names the path
 *   OMEGA_M18_CLEAN_CLONE_CHECKOUT_DIR_FAILED  the copy directory could not
 *       be created; the log path is still filled when it was chosen
 *   OMEGA_M18_CLEAN_CLONE_PLAN_FAILED  the command was not built
 */
#define OMEGA_M18_CLEAN_CLONE_OK 0
#define OMEGA_M18_CLEAN_CLONE_LOG_CREATE_FAILED 1
#define OMEGA_M18_CLEAN_CLONE_PLAN_FAILED 2
#define OMEGA_M18_CLEAN_CLONE_CHECKOUT_DIR_FAILED 3

int omega_m18_clean_clone_plan(const char *out_dir, const char *tmpdir,
                               const char *repo_root, const char *temp_fallback,
                               const char *physics_dir,
                               char *checkout_dir, size_t checkout_cap,
                               char *log_path, size_t log_cap,
                               char *command, size_t command_cap,
                               char *err, size_t err_cap);

/* stderr line the gate prints once it has chosen a log path. */
void omega_m18_clean_clone_print_log_path(const char *log_path);

/* Remove <checkout_dir>/checkout and, when the log does not live there,
 * the private parent directory. The log file is left in place. */
int omega_m18_clean_clone_remove_checkout(const char *checkout_dir, const char *log_path);

#endif /* OMEGA_CLEAN_CLONE_H */
