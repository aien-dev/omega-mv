/* float_g2_ref.h -- gate G2 C reference kernels (float_g2_ref.c); each returns the result's bit pattern. */
#ifndef FLOAT_G2_REF_H
#define FLOAT_G2_REF_H

#include <stdint.h>

uint64_t g2_dot_f32(uint64_t seed, uint64_t k);
uint64_t g2_dot_fma_f32(uint64_t seed, uint64_t k);
uint64_t g2_dot_f64(uint64_t seed, uint64_t k);
uint64_t g2_dot_f16(uint64_t seed, uint64_t k);
uint64_t g2_dot_bf16(uint64_t seed, uint64_t k);
uint64_t g2_dot_f8e4m3(uint64_t seed, uint64_t k);
uint64_t g2_softmax_f32(uint64_t seed, uint64_t k); /* element k of the softmax */

#endif /* FLOAT_G2_REF_H */
