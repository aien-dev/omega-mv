/*
 * float_slice_ref.h -- interface of the gate G1 C reference (float_slice_ref.c).
 * Values are bit patterns in the low bits of a uint64_t; integer results are
 * sign-extended (signed) or zero-extended (unsigned) to 64 bits.
 */
#ifndef FLOAT_SLICE_REF_H
#define FLOAT_SLICE_REF_H

#include <stdint.h>

enum { R_F16, R_BF16, R_F8, R_F32, R_F64 };                    /* float types */
enum { R_ADD, R_SUB, R_MUL, R_DIV, R_MIN, R_MAX };              /* ref_bin */
enum { R_NEG, R_ABS, R_SQRT };                                  /* ref_un */

uint64_t ref_bin(int t, int op, uint64_t a, uint64_t b);
uint64_t ref_fma(int t, uint64_t a, uint64_t b, uint64_t c);   /* a * b + c, one rounding */
uint64_t ref_un(int t, int op, uint64_t a);
uint64_t ref_cmp(int t, uint64_t a, uint64_t b);               /* == 1, != 2, < 4, <= 8, > 16, >= 32 */
uint64_t ref_cvt(int from, int to, uint64_t a);
uint64_t ref_itof(int to, int sgn, uint64_t a);                /* a: the integer, extended to 64 bits */
uint64_t ref_ftoi(int from, int sgn, unsigned w, uint64_t a);

#endif /* FLOAT_SLICE_REF_H */
