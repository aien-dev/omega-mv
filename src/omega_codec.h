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

#ifndef OMEGA_CODEC_H
#define OMEGA_CODEC_H

#include "omega_types.h"

/* Binary serialization / deserialization of graph */
int omega_graph_serialize_binary(const OmegaGraph *graph, uint8_t *out_buf, size_t max_len, size_t *out_len);
int omega_graph_deserialize_binary(const uint8_t *in_buf, size_t in_len, OmegaGraph *out_graph);

/* Human-readable representation (NON-CANONICAL) */
int omega_graph_format_text(const OmegaGraph *graph, char *out_str, size_t max_len);
int omega_graph_parse_text(const char *in_str, OmegaGraph *out_graph, char *err_msg, size_t err_msg_len);

#endif /* OMEGA_CODEC_H */
