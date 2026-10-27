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

/* File input for the AT-0 candidate engine: reads a case file into memory and hands the
 * bytes to the validator. This is the only component that touches the file system; the
 * parser (at0_case.c) and every physics component work on bytes and structs alone. */
#include "at0_model.h"
#include <stdio.h>
#include <stdlib.h>

#define AT0_IO_FILE_CAP (4096 * 256)      /* same byte budget as the parser's line table */

at0_status at0_case_read_file(const char *path, at0_case *out)
{
    if (!path || !out) return AT0_ERR_ARGUMENT;
    FILE *f = fopen(path, "rb");
    if (!f) return AT0_ERR_IO;
    size_t cap = 65536, len = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) { fclose(f); return AT0_ERR_INTERNAL; }
    for (;;) {
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
        if (len == cap) {
            if (cap > (size_t)AT0_IO_FILE_CAP) { free(buf); fclose(f); return AT0_CASE_PARSE_ERROR; }
            cap *= 2;
            uint8_t *nb = realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return AT0_ERR_INTERNAL; }
            buf = nb;
        }
    }
    int err = ferror(f);
    fclose(f);
    if (err) { free(buf); return AT0_ERR_IO; }
    at0_status st = at0_case_parse(buf, len, out);
    free(buf);
    return st;
}
