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

/* Append-only verifier receipts. A receipt is never overwritten (O_EXCL). */
#ifndef PD0_RECEIPT_H
#define PD0_RECEIPT_H
#include <stddef.h>
#include <stdint.h>
#ifndef PD0_VERIFIER_COMMIT
#define PD0_VERIFIER_COMMIT "unknown"
#endif
/* writes <dir>/<kind>-<16 hex of input_hash>-<n>.receipt; returns 0 or PD0V_RECEIPT_IO; path_out gets the file name */
int pd0_receipt_write(const char *dir, const char *kind, const uint8_t input_hash[32], int code, const char *extra, char *path_out, size_t cap);
#endif
