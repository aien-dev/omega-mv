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

#ifndef AARCH64_DECODER_H
#define AARCH64_DECODER_H

#include "aarch64_target.h"
#include <stddef.h>

typedef enum {
    DECODED_INVALID = 0,
    DECODED_ADD,
    DECODED_SUB,
    DECODED_MUL,
    DECODED_AND,
    DECODED_ORR,
    DECODED_EOR,
    DECODED_MOVZ,
    DECODED_RET,
    DECODED_B,
    DECODED_B_COND,
    DECODED_CBZ,
    DECODED_CBNZ,
    DECODED_MOVK,
    DECODED_ADR,
    DECODED_LDR,
    DECODED_STR,
    DECODED_LDRB,
    DECODED_STRB,
    DECODED_SUBS
} DecodedOp;

typedef struct {
    DecodedOp op;
    bool sf;
    uint8_t rd;
    uint8_t rn;
    uint8_t rm;
    uint16_t imm16;
    uint8_t hw;        /* MOVZ/MOVK: 16-bit chunk index (shift = hw * 16) */
    int32_t branch_imm;
    Aarch64Cond cond;
} DecodedInsn;

int aarch64_decode_instruction(uint32_t insn, DecodedInsn *out_dec);
int aarch64_validate_code_buffer(const uint8_t *code, size_t len, char *err_msg, size_t err_msg_len);

#endif /* AARCH64_DECODER_H */
