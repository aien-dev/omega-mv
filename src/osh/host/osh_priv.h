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

/* osh_priv.h -- internal to the Linux host adapter; not part of the adapter interface. */
#ifndef OSH_PRIV_H
#define OSH_PRIV_H
#include "osh_host.h"

/* Write "osh: <fmt>\n" to fd (best effort, full-write loop). */
void osh_diag(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Write all n bytes; 0 ok, -1 with errno set. */
int osh_write_all(int fd, const char *buf, size_t n);
/* Run builtin c->builtin_id with io[0..2] as its stdin/out/err. in_parent: effects on the session are real.
 * Returns the builtin's exit status. May set s->exit_requested (parent only). */
int osh_builtin_run(OshSession *s, const OshCmd *c, const int io[3], int in_parent);
/* Gate one effect through s->effect_hook (no hook: allowed). Returns OSH_E_OK, or the denial after writing a named
 * diagnostic to diagfd ("<op> <path>: denied (NAME); nothing was done"). Call immediately before the syscall. */
int osh_effect_check(const OshSession *s, int op, const char *path, int diagfd);
#endif
