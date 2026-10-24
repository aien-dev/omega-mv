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

/*
 * osh_lex_ref.h -- a tiny independent C reference tokenizer for osh R1, used ONLY for differential
 * testing of the OSC lexer (osh_lex.osc). Written from the token rules (ABI draft section 5 and 7, design
 * E-parser-design.md section 2), not from the .osc. Not a shell, not for production.
 */
#ifndef OSH_LEX_REF_H
#define OSH_LEX_REF_H
#include <stddef.h>
#include <stdint.h>

#define OSH_REF_TOKEN_CAP 128
#define OSH_REF_LINE_CAP 4096

typedef struct {
    uint64_t kind, flags, nseg, line, start, len, aux;
} OshRefTok;

typedef struct {
    OshRefTok tok[OSH_REF_TOKEN_CAP];
    unsigned ntok;
    unsigned status;   /* 0 line complete, 100 incomplete, else a refusal code (201, 202, 220..232, 236, 246..250) */
    uint64_t err_off;  /* byte offset of a refusal */
} OshRefLex;

/* Tokenize the whole buffer in one pass. Same observable result as running the OSC lexer to completion
 * over the same bytes, however they were split into calls. eoi = the host set the end-of-input flag: a pending
 * word or operator is finished, a final lone backslash is a literal byte (bash), and an open quote or `${` is
 * SYNTAX_EOF (248) at offset n. */
void osh_lex_ref(const uint8_t *in, size_t n, int eoi, OshRefLex *out);


#endif /* OSH_LEX_REF_H */
