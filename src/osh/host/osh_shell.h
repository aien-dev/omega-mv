/*
 * osh_shell.h -- the osh driver loop (aien-architecture#158): feeds input lines to the lexer, parser and expander units,
 * answers the expander's variable questions from the session, and hands each finished pipeline record to the host
 * execution service. Host code is replaceable scaffolding (ADR 0024); it never tokenizes, parses or expands.
 */
#ifndef OSH_SHELL_H
#define OSH_SHELL_H
#include <stddef.h>
#include <stdint.h>

#include "osh_core.h"
#include "osh_host.h"

#ifdef OSH_DIFF_TEST_HOOKS
/* TEST-ONLY (tests/osh/diff, omega#568): a read-only observer of every request record the core produced, the result the
 * host gave it, and every core refusal. It receives const views only and cannot change the record, the result or the
 * session. Compiled only with -DOSH_DIFF_TEST_HOOKS; `make test-osh-diff` checks the shipped osh does not contain it. */
typedef struct {
    void (*on_pipeline)(void *ctx, const uint64_t *rec, size_t nrec, const uint64_t *out, size_t nout, const OshResult *res);
    void (*on_refusal)(void *ctx, uint64_t code, uint64_t offset);
    void *ctx;
} OshDiffHooks;
#endif

typedef struct {
    OshCore core;
    OshSession s;
    int native;          /* 1 native AArch64 units, 0 interpreter (OSH_INTERP=1) */
    int interactive;     /* prompts and error recovery */
    char *arg0;          /* $0 */
    char **pos;          /* $1.. */
    int npos;
    uint64_t *w;         /* the workspace */
    uint8_t *buf;        /* bytes of the list in progress */
    size_t blen, bcap;
    unsigned long lists, refused;
#ifdef OSH_DIFF_TEST_HOOKS
    OshDiffHooks diff;   /* TEST-ONLY observer, all NULL unless a test sets it */
#endif
} OshShell;

/* Compile the units (embedded sources) and set up the session from envp. 0 ok, else -1 with a message on stderr. */
int osh_shell_init(OshShell *sh, char *const *envp, int native, int interactive, const char *arg0, char **pos, int npos);
void osh_shell_free(OshShell *sh);

/* Interactive run entry points temporarily own SIGINT/QUIT/TSTP/TTIN/TTOU
 * actions and restore the caller's actions on return, including error returns.
 * Setup/restore failure returns 70. The embedder must serialize process-wide
 * signal-action changes; these entry points do not provide thread isolation.
 * Interactive fd input atomically waits with the caller signal mask and
 * discards the current list on SIGINT. SIGINT blocked by the caller remains
 * blocked. Interactive fds must be below FD_SETSIZE; larger fds refuse with2.
 *
 * Run a -c string from memory: a last line with no newline is ended by end of input itself (bash -c
 * 'echo a\' prints a\). Returns the shell's exit status. */
int osh_shell_run_string(OshShell *sh, const char *text, size_t len);
/* Run a script file's text from memory: a last line with no newline gets one, as from a file or pipe. */
int osh_shell_run_script(OshShell *sh, const char *text, size_t len);
/* Run commands read from fd, one byte at a time so children inherit the unread rest. Returns the exit status. */
int osh_shell_run_fd(OshShell *sh, int fd);

/* the three unit sources, embedded at build time (build/osh/osh_units.c) */
extern const unsigned char osh_unit_lex[], osh_unit_parse[], osh_unit_expand[];
extern const size_t osh_unit_lex_len, osh_unit_parse_len, osh_unit_expand_len;
#endif
