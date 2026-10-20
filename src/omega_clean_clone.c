#include "omega_clean_clone.h"
#include "omega_evidence.h"
#include "omega_physics_dir.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* How much of a failing step's log reaches stderr. */
#define CLEAN_CLONE_TAIL_BYTES 6144

/* The programs a step may run. The child calls execvp or execv with a literal
 * name chosen from this switch, so no program name ever comes from data. */
enum cc_prog { CC_GIT, CC_MAKE, CC_OMEGATOOL, CC_RM };

/* Only letters, digits and / . _ + - pass. make reads PHYSICS_DIR=<path> as a
 * variable value, where '$' would be expanded, so anything else is refused. */
static bool cc_path_is_plain(const char *s) {
    if (!s || !*s) return false;
    for (const char *p = s; *p; p++) {
        char c = *p;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '/' || c == '.' || c == '_' || c == '+' || c == '-';
        if (!ok) return false;
    }
    return true;
}

/* Child side of a step: environment, directory, log, then exec. Never returns.
 * Arguments travel as argv entries; no shell parses any path. The log is opened
 * by the child after it changes directory, at an absolute path built by us. */
static void cc_child(enum cc_prog prog, char *const argv[], const char *work_dir, const char *log_path,
                     bool gates_env) {
    unsetenv("OUT_DIR");
    unsetenv("MAKEFLAGS");
    unsetenv("MFLAGS");
    unsetenv("MAKELEVEL");
    if (gates_env && setenv("OMEGA_IN_CLEAN_CLONE", "1", 1) != 0) _exit(127);
    if (work_dir && chdir(work_dir) != 0) _exit(127);
    int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) _exit(126);
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(127);
    if (fd > STDERR_FILENO) close(fd);
    switch (prog) {
    case CC_GIT: execvp("git", argv); break;
    case CC_MAKE: execvp("make", argv); break;
    case CC_OMEGATOOL: execv("./build/omegatool", argv); break;
    case CC_RM: execvp("rm", argv); break;
    }
    _exit(127);
}

/* Run git rev-parse HEAD in repo_dir and read the 40-hex sha it prints. */
static bool clean_clone_head(const char *repo_dir, char out[41]) {
    int fds[2];
    if (pipe(fds) != 0) return false;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(fds[1]);
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) dup2(nul, STDERR_FILENO);
        char *argv[] = { "git", "-C", (char *)repo_dir, "rev-parse", "HEAD", NULL };
        execvp("git", argv);
        _exit(127);
    }
    close(fds[1]);
    char line[64];
    size_t used = 0;
    for (;;) {
        ssize_t n = read(fds[0], line + used, sizeof(line) - 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
        if (used >= sizeof(line) - 1) break;
    }
    close(fds[0]);
    line[used] = '\0';
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return false;
    line[strcspn(line, "\r\n")] = '\0';
    if (strlen(line) != 40 || strspn(line, "0123456789abcdef") != 40) return false;
    memcpy(out, line, 41);
    return true;
}

/* Print the lines that name the failure (gcc's "file:line:col: error:" lines and
 * make's "make: ***" lines), up to a fixed count. make -j keeps printing
 * compile commands after the first error, so the tail alone can miss it. */
#define CLEAN_CLONE_ERROR_LINES 20
static void clean_clone_print_errors(const char *log_path) {
    FILE *f = fopen(log_path, "rb");
    if (!f) return;
    char line[1024];
    int shown = 0;
    while (shown < CLEAN_CLONE_ERROR_LINES && fgets(line, sizeof line, f)) {
        if (strstr(line, ": error") || strstr(line, ": fatal error") || strstr(line, "make: ***")) {
            fputs(line, stderr);
            if (strchr(line, '\n') == NULL) fputc('\n', stderr);
            shown++;
        }
    }
    fclose(f);
    if (shown > 0) fprintf(stderr, "  (%d error line(s) above; log tail below)\n", shown);
}

/* Print the last CLEAN_CLONE_TAIL_BYTES of a log to stderr, from a line start. */
static void clean_clone_print_tail(const char *log_path) {
    clean_clone_print_errors(log_path);
    FILE *f = fopen(log_path, "rb");
    if (!f) {
        fprintf(stderr, "  (log %s unreadable)\n", log_path);
        return;
    }
    bool cut = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > CLEAN_CLONE_TAIL_BYTES) {
            fseek(f, size - CLEAN_CLONE_TAIL_BYTES, SEEK_SET);
            cut = true;
        } else {
            fseek(f, 0, SEEK_SET);
        }
    }
    char buf[CLEAN_CLONE_TAIL_BYTES + 1];
    size_t n = fread(buf, 1, CLEAN_CLONE_TAIL_BYTES, f);
    fclose(f);
    buf[n] = '\0';
    const char *start = buf;
    if (cut) {
        const char *nl = strchr(buf, '\n');
        if (nl) start = nl + 1;
    }
    if (*start == '\0') {
        fprintf(stderr, "  (log is empty)\n");
        return;
    }
    fputs(start, stderr);
    if (n > 0 && buf[n - 1] != '\n') fputc('\n', stderr);
}

static void clean_clone_report(const char *tag, const char *step, const char *why,
                               const char *dir, const char *log_path) {
    fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step %s failed: %s (clone kept at %s)\n", tag, step, why, dir);
    if (log_path) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: last lines of %s:\n", tag, log_path);
        clean_clone_print_tail(log_path);
    }
    fflush(stderr);
}

static bool step_log(char *buf, size_t size, const char *dir, const char *step) {
    int n = snprintf(buf, size, "%s/%s.log", dir, step);
    return n >= 0 && (size_t)n < size;
}

/* Run one step with argv, its output in log_path; true when it exits 0. */
static bool clean_clone_step(const char *tag, const char *step, const char *dir, const char *work_dir,
                             enum cc_prog prog, char *const argv[], const char *log_path, bool gates_env) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        clean_clone_report(tag, step, "could not start the step", dir, log_path);
        return false;
    }
    if (pid == 0) cc_child(prog, argv, work_dir, log_path, gates_env);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            clean_clone_report(tag, step, "could not wait for the step", dir, log_path);
            return false;
        }
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    char why[96];
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
        snprintf(why, sizeof why, "exit status 127 (program not found or not runnable)");
    else if (WIFEXITED(status))
        snprintf(why, sizeof why, "exit status %d", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        snprintf(why, sizeof why, "killed by signal %d", WTERMSIG(status));
    else
        snprintf(why, sizeof why, "wait status 0x%x", (unsigned)status);
    clean_clone_report(tag, step, why, dir, log_path);
    return false;
}

bool omega_clean_clone_run(const char *tag, const char *gates_flag, char out_sha[41]) {
    char src_sha[41] = {0};
    if (!clean_clone_head(".", src_sha)) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step source_head failed: git rev-parse HEAD in the source tree\n", tag);
        return false;
    }
    if (omega_evidence_tree_dirty()) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step source_clean failed: source tree at %s is dirty "
                        "(git status --porcelain not empty); a clean clone cannot reproduce it\n", tag, src_sha);
        return false;
    }

    /* The PHYSICS_DIR this process runs with (omega_physics_dir.h), as an
     * absolute path, so the clone in /tmp builds against the same pinned
     * physics checkout instead of the Makefile default ../physics. */
    char physics_dir[PATH_MAX], physics_err[OMEGA_PHYSICS_ERR_SIZE];
    if (!omega_physics_dir_resolve_pinned(physics_dir, sizeof(physics_dir), 0, physics_err, sizeof(physics_err))) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step physics_dir failed: %s\n", tag, physics_err);
        return false;
    }
    if (!cc_path_is_plain(physics_dir)) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step physics_dir failed: path %s has characters outside "
                        "letters, digits and / . _ + - (make would read them)\n", tag, physics_dir);
        return false;
    }
    char pd_arg[PATH_MAX + 16];
    if (snprintf(pd_arg, sizeof pd_arg, "PHYSICS_DIR=%s", physics_dir) >= (int)sizeof pd_arg) return false;

    char dir_template[64];
    int n = snprintf(dir_template, sizeof dir_template, "/tmp/omega_clean_%s_XXXXXX", tag);
    if (n < 0 || (size_t)n >= sizeof dir_template) return false;
    char *dir = mkdtemp(dir_template);
    if (!dir) {
        perror("OMEGA_CLEAN_CLONE mkdtemp");
        return false;
    }
    char checkout[PATH_MAX], log[PATH_MAX];
    n = snprintf(checkout, sizeof checkout, "%s/checkout", dir);
    if (n < 0 || (size_t)n >= sizeof checkout) return false;

    if (!step_log(log, sizeof log, dir, "1-clone")) return false;
    char *clone_argv[] = { "git", "clone", "--quiet", "--no-hardlinks", ".", checkout, NULL };
    if (!clean_clone_step(tag, "1-clone", dir, NULL, CC_GIT, clone_argv, log, false)) return false;

    if (!step_log(log, sizeof log, dir, "2-checkout")) return false;
    char *co_argv[] = { "git", "checkout", "--quiet", "--detach", src_sha, NULL };
    if (!clean_clone_step(tag, "2-checkout", dir, checkout, CC_GIT, co_argv, log, false)) return false;

    char clone_sha[41] = {0};
    if (!clean_clone_head(checkout, clone_sha) || strcmp(clone_sha, src_sha) != 0) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step 2-checkout failed: clone HEAD %s is not the candidate %s "
                        "(clone kept at %s)\n", tag, clone_sha[0] ? clone_sha : "(unreadable)", src_sha, dir);
        return false;
    }

    if (!step_log(log, sizeof log, dir, "3-make-clean")) return false;
    char *mc_argv[] = { "make", "-C", checkout, "clean", NULL };
    if (!clean_clone_step(tag, "3-make-clean", dir, NULL, CC_MAKE, mc_argv, log, false)) return false;

    if (!step_log(log, sizeof log, dir, "4-make")) return false;
    char *mk_argv[] = { "make", "-C", checkout, "-j", pd_arg, NULL };
    if (!clean_clone_step(tag, "4-make", dir, NULL, CC_MAKE, mk_argv, log, false)) return false;

    if (!step_log(log, sizeof log, dir, "5-run-gates")) return false;
    char *run_argv[] = { "./build/omegatool", (char *)gates_flag, NULL };
    if (!clean_clone_step(tag, "5-run-gates", dir, checkout, CC_OMEGATOOL, run_argv, log, true)) return false;

    /* The built and qualified checkout must still be exactly the candidate. */
    if (!clean_clone_head(checkout, clone_sha) || strcmp(clone_sha, src_sha) != 0) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step 6-verify-head failed: clone HEAD %s after the run is not "
                        "the candidate %s (clone kept at %s)\n", tag, clone_sha, src_sha, dir);
        return false;
    }

    printf("  OMEGA_CLEAN_CLONE %s: candidate %s cloned, built from scratch and %s passed\n",
           tag, src_sha, gates_flag);
    char *rm_argv[] = { "rm", "-rf", dir, NULL };
    if (!clean_clone_step(tag, "cleanup", dir, NULL, CC_RM, rm_argv, "/dev/null", false))
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: could not remove %s\n", tag, dir);
    memcpy(out_sha, src_sha, 41);
    return true;
}
