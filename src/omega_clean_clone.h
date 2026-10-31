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

/* Run prog (looked up on PATH; callers pass a string literal) with argv in
 * directory dir (NULL: the current directory). stdout and stderr go to out_rel,
 * a path opened by the child AFTER it changes to dir, so callers pass a fixed
 * relative name such as "../qualification.log"; NULL means /dev/null.
 * OUT_DIR is removed from the child environment; when in_clean_clone is true
 * the child also gets OMEGA_IN_CLEAN_CLONE=1.
 * Returns the child's exit code (126: output file could not be opened, 127:
 * program could not be started), 128 + signal if it was killed, or -1 if the
 * child could not be created. */
int omega_cc_run(const char *dir, const char *prog, char *const argv[], const char *out_rel, bool in_clean_clone);

/* Run prog with argv in dir and capture the first line of its stdout (newline
 * stripped) into out. stderr goes to /dev/null. Returns true only when the
 * program exits 0 and printed a line that fit. */
bool omega_cc_capture_line(const char *dir, const char *prog, char *const argv[], char *out, size_t out_size);

/* Remove path and everything under it without following symlinks.
 * Refuses an empty path, "/", a relative path, or a path outside the plain
 * charset. Returns 0 when the tree is gone (or was never there), -1 otherwise. */
int omega_cc_remove_tree(const char *path);

#endif
