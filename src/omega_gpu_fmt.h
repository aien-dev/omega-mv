/* omega_gpu_fmt.h -- bounded message formatting for the freestanding GPU code (native v1 P07 cut C session
 * part, aienos#286). The session and the tensor APIs format their error stages with it in both builds, so a
 * stage reads the same with or without libc. Conversions: %s %c %d %u %x %ld %lu %lld %llu %zu %%; anything
 * else is copied literally. Always NUL-terminates inside cap (cap > 0); truncates, never overflows. Returns
 * the number of characters written (without the NUL). */
#ifndef OMEGA_GPU_FMT_H
#define OMEGA_GPU_FMT_H
#include <stdarg.h>
#include <stddef.h>
int omega_gpu_fmt(char *out, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* The same with a va_list (L202: the platform services diagnostics line). */
int omega_gpu_vfmt(char *out, size_t cap, const char *fmt, va_list ap) __attribute__((format(printf, 3, 0)));
#endif
