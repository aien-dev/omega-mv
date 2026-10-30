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
 * path without building the tree or touching a GPU.
 *
 * The log is created under the gate's own output directory
 * (<out_dir>/clean-clone/clean_clone_m18.log) when out_dir is set, otherwise
 * under tmpdir when that is set. It is never a fixed path on the host temp
 * directory (omega#526). The copy itself is placed beside that log when that
 * directory is outside the repo, and otherwise in a private directory under
 * tmpdir or temp_fallback, so the copy of the repo does not land inside the
 * repo.
 *
 * The copy, the build and the nested gate are started as programs. A path
 * from OUT_DIR, TMPDIR or the physics directory is an argument or an
 * environment value. It is not placed in a shell command (omega#935).
 *
 * Return values:
 *   OMEGA_M18_CLEAN_CLONE_OK
 *   OMEGA_M18_CLEAN_CLONE_LOG_CREATE_FAILED  log file could not be created;
 *       err begins with "M18_CLEAN_CLONE_LOG_CREATE_FAILED:" and names the path
 *   OMEGA_M18_CLEAN_CLONE_CHECKOUT_DIR_FAILED  the copy directory could not
 *       be created; the log path is still filled when it was chosen
 *   OMEGA_M18_CLEAN_CLONE_PLAN_FAILED  a path did not fit, or the physics
 *       path is empty
 */
#define OMEGA_M18_CLEAN_CLONE_OK 0
#define OMEGA_M18_CLEAN_CLONE_LOG_CREATE_FAILED 1
#define OMEGA_M18_CLEAN_CLONE_PLAN_FAILED 2
#define OMEGA_M18_CLEAN_CLONE_CHECKOUT_DIR_FAILED 3
/* The child never reached exec (chdir, setenv, dup2, or execvp failed).
 * This is not a waited-for exit status, so it is not make's 127. */
#define OMEGA_M18_CLEAN_CLONE_EXEC_FAILED (-2)

/* Nested tool, started with cwd set to the checkout. The name is fixed. */
#define OMEGA_M18_CLEAN_CLONE_TOOL "./build/omegatool"
#define OMEGA_M18_CLEAN_CLONE_TOOL_ARG "--run-m18-gates"
#define OMEGA_M18_CLEAN_CLONE_NESTED_ENV "OMEGA_IN_CLEAN_CLONE"

/* Working tree of this process, via git, with no shell. */
int omega_m18_git_toplevel(char *out, size_t cap);

int omega_m18_clean_clone_plan(const char *out_dir, const char *tmpdir,
                               const char *repo_root, const char *temp_fallback,
                               const char *physics_dir,
                               char *checkout_dir, size_t checkout_cap,
                               char *log_path, size_t log_cap,
                               char *err, size_t err_cap);

/* cp -r src dst. src and dst are arguments. Returns 0 when cp exits 0.
 * Returns OMEGA_M18_CLEAN_CLONE_EXEC_FAILED when cp cannot be started. */
int omega_m18_clean_clone_copy_tree(const char *src, const char *dst);

/* Run the clean clone: copy the git toplevel into <checkout_dir>/checkout,
 * make OUT_DIR=build clean, make -j OUT_DIR=build with PHYSICS_DIR set in
 * the environment, then the nested tool with its stdout and stderr in
 * log_path. Returns 0 when every step exits 0. A step that ran and failed
 * returns that process's exit status. A program that could not be started
 * returns OMEGA_M18_CLEAN_CLONE_EXEC_FAILED, which is not 127. */
int omega_m18_clean_clone_run(const char *checkout_dir, const char *log_path,
                              const char *physics_dir);

/* stderr line the gate prints once it has chosen a log path. */
void omega_m18_clean_clone_print_log_path(const char *log_path);

/* Remove <checkout_dir>/checkout and, when the log does not live there,
 * the private parent directory. The log file is left in place. */
int omega_m18_clean_clone_remove_checkout(const char *checkout_dir, const char *log_path);

#endif /* OMEGA_CLEAN_CLONE_H */
