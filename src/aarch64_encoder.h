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

#ifndef AARCH64_ENCODER_H
#define AARCH64_ENCODER_H

#include "aarch64_target.h"
#include <stddef.h>

int aarch64_emit_add_reg(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rn, uint8_t rm);
int aarch64_emit_sub_reg(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rn, uint8_t rm);
int aarch64_emit_mul_reg(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rn, uint8_t rm);
int aarch64_emit_and_reg(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rn, uint8_t rm);
int aarch64_emit_orr_reg(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rn, uint8_t rm);
int aarch64_emit_eor_reg(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rn, uint8_t rm);
int aarch64_emit_mov_reg(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rm);
int aarch64_emit_movz(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint16_t imm16, uint8_t shift);
int aarch64_emit_ret(uint8_t *buf, size_t *pos, size_t max_len);
int aarch64_emit_b(uint8_t *buf, size_t *pos, size_t max_len, int32_t imm26);
int aarch64_emit_b_cond(uint8_t *buf, size_t *pos, size_t max_len, Aarch64Cond cond, int32_t imm19);
int aarch64_emit_cbz(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rt, int32_t imm19);
int aarch64_emit_cbnz(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rt, int32_t imm19);
int aarch64_emit_movk(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint16_t imm16, uint8_t shift);
int aarch64_emit_adr(uint8_t *buf, size_t *pos, size_t max_len, uint8_t rd, int32_t imm21);
int aarch64_emit_ldr_uoff(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rt, uint8_t rn, uint16_t uoff);
int aarch64_emit_str_uoff(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rt, uint8_t rn, uint16_t uoff);
int aarch64_emit_ldrb_uoff(uint8_t *buf, size_t *pos, size_t max_len, uint8_t rt, uint8_t rn, uint16_t uoff);
int aarch64_emit_strb_uoff(uint8_t *buf, size_t *pos, size_t max_len, uint8_t rt, uint8_t rn, uint16_t uoff);
int aarch64_emit_ldr_post(uint8_t *buf, size_t *pos, size_t max_len, uint8_t rt, uint8_t rn, int16_t simm9);
int aarch64_emit_str_post(uint8_t *buf, size_t *pos, size_t max_len, uint8_t rt, uint8_t rn, int16_t simm9);
int aarch64_emit_ldr_x_post(uint8_t *buf, size_t *pos, size_t max_len, uint8_t rt, uint8_t rn, int16_t simm9);
int aarch64_emit_str_x_post(uint8_t *buf, size_t *pos, size_t max_len, uint8_t rt, uint8_t rn, int16_t simm9);
int aarch64_emit_subs_imm(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rn, uint16_t imm12);
int aarch64_emit_subs_reg(uint8_t *buf, size_t *pos, size_t max_len, bool sf, uint8_t rd, uint8_t rn, uint8_t rm);

#endif /* AARCH64_ENCODER_H */
