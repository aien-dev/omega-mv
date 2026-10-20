#include "omega_clean_clone.h"
#include "omega_evidence.h"
#include "omega_physics_dir.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* How much of a failing step's log reaches stderr. */
#define CLEAN_CLONE_TAIL_BYTES 6144

/* Environment the clone's make and omegatool must not inherit. */
#define CLEAN_CLONE_ENV "env -u OUT_DIR -u MAKEFLAGS -u MFLAGS -u MAKELEVEL"

static bool clean_clone_git_head(const char *repo_dir, char out[41]) {
    char cmd[PATH_MAX + 64];
    int n = snprintf(cmd, sizeof(cmd), "git -C '%s' rev-parse HEAD 2>/dev/null", repo_dir);
    if (n < 0 || (size_t)n >= sizeof(cmd)) return false;
    FILE *p = popen(cmd, "r");
    if (!p) return false;
    char *got = fgets(out, 41, p);
    pclose(p);
    if (!got) return false;
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = '\0';
    return len == 40 && strspn(out, "0123456789abcdef") == 40;
}

/* Print the last CLEAN_CLONE_TAIL_BYTES of a log to stderr, from a line start. */
static void clean_clone_print_tail(const char *log_path) {
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
    fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step %s failed: %s (clone kept at %s)\n",
            tag, step, why, dir);
    if (log_path) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: last lines of %s:\n", tag, log_path);
        clean_clone_print_tail(log_path);
    }
    fflush(stderr);
}

/* Run "<command> >log 2>&1" in sh; true when it exits 0, else reports the step. */
static bool clean_clone_step(const char *tag, const char *step, const char *dir,
                             const char *command) {
    char log_path[PATH_MAX];
    int n = snprintf(log_path, sizeof(log_path), "%s/%s.log", dir, step);
    if (n < 0 || (size_t)n >= sizeof(log_path)) return false;
    char full[8192];
    n = snprintf(full, sizeof(full), "{ %s ; } >'%s' 2>&1", command, log_path);
    if (n < 0 || (size_t)n >= sizeof(full)) {
        clean_clone_report(tag, step, "command too long", dir, NULL);
        return false;
    }
    int rc = system(full);
    if (rc == 0) return true;
    char why[64];
    if (rc == -1) snprintf(why, sizeof(why), "could not start sh");
    else if (WIFEXITED(rc)) snprintf(why, sizeof(why), "exit status %d", WEXITSTATUS(rc));
    else if (WIFSIGNALED(rc)) snprintf(why, sizeof(why), "killed by signal %d", WTERMSIG(rc));
    else snprintf(why, sizeof(why), "wait status 0x%x", (unsigned)rc);
    clean_clone_report(tag, step, why, dir, log_path);
    return false;
}

bool omega_clean_clone_run(const char *tag, const char *gates_flag, char out_sha[41]) {
    char src_sha[41] = {0};
    if (!clean_clone_git_head(".", src_sha)) {
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
    if (strchr(physics_dir, '\'')) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step physics_dir failed: path %s contains a quote\n", tag, physics_dir);
        return false;
    }

    char dir_template[64];
    int n = snprintf(dir_template, sizeof(dir_template), "/tmp/omega_clean_%s_XXXXXX", tag);
    if (n < 0 || (size_t)n >= sizeof(dir_template)) return false;
    char *dir = mkdtemp(dir_template);
    if (!dir) {
        perror("OMEGA_CLEAN_CLONE mkdtemp");
        return false;
    }
    char checkout[PATH_MAX];
    n = snprintf(checkout, sizeof(checkout), "%s/checkout", dir);
    if (n < 0 || (size_t)n >= sizeof(checkout)) return false;

    char cmd[PATH_MAX * 3];
    snprintf(cmd, sizeof(cmd), "git clone --quiet --no-hardlinks . '%s'", checkout);
    if (!clean_clone_step(tag, "1-clone", dir, cmd)) return false;
    snprintf(cmd, sizeof(cmd), "git -C '%s' checkout --quiet --detach %s", checkout, src_sha);
    if (!clean_clone_step(tag, "2-checkout", dir, cmd)) return false;

    char clone_sha[41] = {0};
    if (!clean_clone_git_head(checkout, clone_sha) || strcmp(clone_sha, src_sha) != 0) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step 2-checkout failed: clone HEAD %s is not the candidate %s "
                        "(clone kept at %s)\n", tag, clone_sha[0] ? clone_sha : "(unreadable)", src_sha, dir);
        return false;
    }

    snprintf(cmd, sizeof(cmd), CLEAN_CLONE_ENV " make -C '%s' clean", checkout);
    if (!clean_clone_step(tag, "3-make-clean", dir, cmd)) return false;
    snprintf(cmd, sizeof(cmd), CLEAN_CLONE_ENV " make -C '%s' -j PHYSICS_DIR='%s'", checkout, physics_dir);
    if (!clean_clone_step(tag, "4-make", dir, cmd)) return false;
    snprintf(cmd, sizeof(cmd), "cd '%s' && " CLEAN_CLONE_ENV " OMEGA_IN_CLEAN_CLONE=1 ./build/omegatool %s",
             checkout, gates_flag);
    if (!clean_clone_step(tag, "5-run-gates", dir, cmd)) return false;

    /* The built and qualified checkout must still be exactly the candidate. */
    if (!clean_clone_git_head(checkout, clone_sha) || strcmp(clone_sha, src_sha) != 0) {
        fprintf(stderr, "OMEGA_CLEAN_CLONE %s: step 6-verify-head failed: clone HEAD %s after the run is not "
                        "the candidate %s (clone kept at %s)\n", tag, clone_sha, src_sha, dir);
        return false;
    }

    printf("  OMEGA_CLEAN_CLONE %s: candidate %s cloned, built from scratch and %s passed\n",
           tag, src_sha, gates_flag);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "OMEGA_CLEAN_CLONE %s: could not remove %s\n", tag, dir);
    memcpy(out_sha, src_sha, 41);
    return true;
}
