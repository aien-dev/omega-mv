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
 * osh_parse_ref.h -- a tiny independent C reference parser for osh R1, used ONLY for differential testing of the
 * OSC parser (osh_parse.osc). Written from the grammar (osh_parse.osc.in header, design E-parser-design.md section 3)
 * as a recursive-descent parser over the reference tokenizer's tokens, not from the .osc state machine. Not a shell.
 */
#ifndef OSH_PARSE_REF_H
#define OSH_PARSE_REF_H
#include <stddef.h>
#include <stdint.h>

#include "osh_lex_ref.h"

#define OSH_PREF_CMDS 32
#define OSH_PREF_PIPES 16

typedef struct {
    unsigned status;     /* 0 list complete, 100 need more input, else a refusal code (203..208, 231..242, 248) */
    uint64_t err_off;    /* byte offset of a refusal */
    unsigned ncmd, npipe;
    uint64_t cmd[OSH_PREF_CMDS][8];  /* {first token, end token, nassign, nword, nredir, pipeline, position, 0} */
    uint64_t pipe[OSH_PREF_PIPES][4]; /* {first command, ncmds, connector_after, first token} */
    unsigned next_tok;   /* status 0: index of the first token the list did not consume */
} OshRefParse;

/* Parse the first list of the token stream lx (which must have status 0, a complete lexer run over in[0..n)). eoi
 * is the same flag the lexer was run with. */
void osh_parse_ref(const uint8_t *in, size_t n, const OshRefLex *lx, int eoi, OshRefParse *out);

#endif /* OSH_PARSE_REF_H */
