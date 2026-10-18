#ifndef OMEGA_CLEAN_CLONE_H
#define OMEGA_CLEAN_CLONE_H

#include <stdbool.h>
#include <stddef.h>

/* Process and file helpers for the M18 and M19 clean-clone gates (omega#526, #565).
 *
 * No shell anywhere: every child program is started with fork + execvp and an
 * argument vector, and trees are removed with nftw + unlink/rmdir. A path is
 * never pasted into a command string, so a quote or shell metacharacter in
 * TMPDIR, PHYSICS_DIR or a clone path cannot change what runs.
 *
 * Fail closed: a path outside the plain charset (letters, digits, / . _ + -)
 * is refused before anything runs, because it is also handed to make as
 * PHYSICS_DIR=<path> and make expands '$' in variable values. */

/* True when every byte of s is in [A-Za-z0-9/._+-] and s is non-empty. */
bool omega_cc_path_is_plain(const char *s);

/* Run argv[0] (looked up on PATH) with argv in directory dir.
 * stdout and stderr go to out_path (created or truncated), or to /dev/null when
 * out_path is NULL. OUT_DIR is removed from the child environment; when
 * in_clean_clone is true the child also gets OMEGA_IN_CLEAN_CLONE=1.
 * Returns the child's exit code, 128 + signal if it was killed, or -1 if it
 * could not be started. */
int omega_cc_run(const char *dir, char *const argv[], const char *out_path, bool in_clean_clone);

/* Run argv in dir and capture the first line of its stdout (newline stripped)
 * into out. stderr goes to /dev/null. Returns true only when the program exits
 * 0 and printed a line that fit. */
bool omega_cc_capture_line(const char *dir, char *const argv[], char *out, size_t out_size);

/* Remove path and everything under it without following symlinks.
 * Refuses an empty path, "/", a relative path, or a path outside the plain
 * charset. Returns 0 when the tree is gone (or was never there), -1 otherwise. */
int omega_cc_remove_tree(const char *path);

#endif
