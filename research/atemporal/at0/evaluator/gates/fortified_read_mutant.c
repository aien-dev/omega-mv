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

/* Isolation gate positive control (Agent 5 finding, omega#358): a hardened-libc file read.
 * With -D_FORTIFY_SOURCE the compiler rewrites fread/open into __fread_chk/__open_2; a gate that
 * matches only the plain names misses them. The references are written out explicitly so the
 * control does not depend on the compiler's fortify behaviour. The gate must FAIL this object. */
extern unsigned long __fread_chk(void *buf, unsigned long bufsize, unsigned long size, unsigned long n, void *stream);
extern int __open_2(const char *path, int flags);
int at0_hidden_file_read(void *stream) {
    char b[8];
    int fd = __open_2("/etc/hostname", 0);
    return fd + (int)__fread_chk(b, sizeof b, 1, sizeof b, stream);
}
