/*
 * osc_fp.h -- exact software rounding for the OSC-4 float slice formats
 * (docs/osc/OSC-4-FLOATS.md). Used by the reference interpreter and by the
 * front end for literals. Integer arithmetic only: a value is (sign, magnitude,
 * binary exponent) and is rounded once, to nearest with ties to even.
 *
 * Formats, all held as bit patterns in the low bits of a 64-bit cell:
 *   f16     IEEE binary16: 1 sign, 5 exponent (bias 15), 10 fraction
 *   bf16    bfloat16:      1 sign, 8 exponent (bias 127), 7 fraction
 *   f8e4m3  OCP FP8 E4M3:  1 sign, 4 exponent (bias 7), 3 fraction; no infinity;
 *                          S.1111.111 is NaN; largest finite 448 (S.1111.110)
 *   f32 / f64              IEEE binary32 / binary64 (narrowing targets only here)
 *
 * NaN rule (Arm ARM DDI0487 M.d J1.4 FPConvertNaN): narrowing keeps the sign,
 * sets the quiet bit and keeps the top fraction bits below the quiet bit.
 * f8e4m3 has one NaN per sign, so any NaN narrows to S.1111.111.
 * Overflow: f16 / bf16 / f32 round to infinity; f8e4m3 saturates to +-448
 * (and +-inf narrows to +-448), the "satfinite" rule of the GPU convert.
 */
#ifndef OSC_FP_H
#define OSC_FP_H

#include <stdbool.h>
#include <stdint.h>
#include "osc_ir.h"

/* value (-1)^neg * mag * 2^e2, rounded once to `to` (F16, BF16, F8E4M3, F32 or F64). */
uint64_t osc_fp_round(OscScalar to, bool neg, unsigned __int128 mag, int e2);

/* An f64 bit pattern narrowed to `to` with one rounding (NaN per the rule above). */
uint64_t osc_fp_from_f64(OscScalar to, uint64_t f64bits);

/* The value of a float of type `from` as f64 bits: exact for every non-NaN value;
 * a NaN is converted as FPConvertNaN{N,64} (quietened, payload kept). For BF16 and
 * F8E4M3 this goes through their exact f32 widening (osc_fp_widen32). */
uint64_t osc_fp_to_f64(OscScalar from, uint64_t bits);

/* BF16 / F8E4M3 widened to f32 bits, exactly. BF16: the 16 bits shifted up, so a
 * signalling NaN stays signalling. F8E4M3: NaN -> sign | 0x7FC00000 (quiet, no payload). */
uint32_t osc_fp_widen32(OscScalar from, uint64_t bits);

/* NaN / infinity tests on a bit pattern of type s (F8E4M3 has no infinity). */
bool osc_fp_is_nan(OscScalar s, uint64_t bits);
bool osc_fp_is_inf(OscScalar s, uint64_t bits);

/* f16 arithmetic with one rounding, NaN propagation as FPProcessNaNs (signalling
 * NaN first, first operand first, quietened). sub: OscFBin ADD..DIV. */
uint64_t osc_fp_f16_bin(unsigned sub, uint64_t a, uint64_t b);
uint64_t osc_fp_f16_sqrt(uint64_t a);
/* a * b + c with one rounding (FMADD Hd, Ha, Hb, Hc semantics, Arm ARM J1.4 FPMulAdd). */
uint64_t osc_fp_f16_fma(uint64_t a, uint64_t b, uint64_t c);

#endif /* OSC_FP_H */
