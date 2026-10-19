/*
 * omegese_lib.h -- omegese.c as a linked library, for an engine that masks every decode step
 * (OMEGESE.md section 6; sovereign-core crate aien-omegese-constraint). One query costs one
 * function call instead of one process.
 *
 * Each call answers exactly what the command line prints: the same bytes on stdout, the same exit
 * code. The library is the unchanged omegese.c compiled into omegese_lib.c, with every piece of
 * parser state put back to its start value before each call, so a call never sees the one before
 * it (test_omegese_lib.sh checks that against the command on every prefix of the dev programs).
 *
 * Not thread-safe: the parser state is global. One call at a time per process.
 */
#ifndef OMEGESE_LIB_H
#define OMEGESE_LIB_H
#include <stddef.h>

enum { OMEGESE_NEXT_MACHINE = 0, OMEGESE_COMPLETE = 1 };

/* `omegese next --machine <prefix>` (mode OMEGESE_NEXT_MACHINE) or `omegese complete <prefix>`
 * (OMEGESE_COMPLETE) on src[0..len). *out gets what the command prints on stdout (malloc'd,
 * NUL-terminated, *out_len bytes without the NUL; free it with omegese_lib_free); the return value is
 * the command's exit code: 0 answered, 1 refused (one REFUSED line). 2: a prefix the command
 * cannot read (larger than its input limit, or an unknown mode); *out then holds the reason. */
int omegese_lib_query(int mode, const char *src, size_t len, char **out, size_t *out_len);

void omegese_lib_free(char *p);

#endif
