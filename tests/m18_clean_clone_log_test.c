/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Drake Stapleton (AIEN / aien-dev)
 *
 * Host-only check of M18 clean-clone path selection (omega#526). No GPU.
 */
#include "omega_clean_clone.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fails;

static void check(int cond, const char *name) {
    if (cond) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        g_fails++;
    }
}

static int uses_fixed_tmp(const char *log) {
    if (log && strstr(log, "/tmp/clean_clone_m18.log")) return 1;
    if (log && strstr(log, "/tmp/omega_clean_m18")) return 1;
    return 0;
}

static int starts_with(const char *s, const char *prefix) {
    size_t n = strlen(prefix);
    return s && strncmp(s, prefix, n) == 0;
}

static void write_file(const char *path) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
}

static int plan(const char *out, const char *tmp, const char *repo, const char *fb,
                char *checkout, char *log, char *err) {
    memset(checkout, 0, 4096);
    memset(log, 0, 4096);
    memset(err, 0, 4608);
    return omega_m18_clean_clone_plan(out, tmp, repo, fb, "/opt/physics",
                                      checkout, 4096, log, 4096, err, 4608);
}

static void expect_not_fixed(const char *log) {
    check(!uses_fixed_tmp(log), "M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <work-dir>\n", argv[0]);
        return 2;
    }
    const char *work = argv[1];
    if (mkdir(work, 0755) != 0 && errno != EEXIST) {
        perror(work);
        return 2;
    }
    char tmpdir[512], outdir[512], repo[512], inside[512], blocked[512], defcwd[512], fb[512];
    snprintf(tmpdir, sizeof tmpdir, "%s/tmpdir", work);
    snprintf(outdir, sizeof outdir, "%s/outdir", work);
    snprintf(repo, sizeof repo, "%s/fakerepo", work);
    snprintf(inside, sizeof inside, "%s/fakerepo/build/step", work);
    snprintf(blocked, sizeof blocked, "%s/blocked", work);
    snprintf(defcwd, sizeof defcwd, "%s/defcwd", work);
    snprintf(fb, sizeof fb, "%s/fallback", work);
    if (mkdir(tmpdir, 0755) != 0 || mkdir(outdir, 0755) != 0 || mkdir(repo, 0755) != 0 ||
        mkdir(defcwd, 0755) != 0 || mkdir(fb, 0755) != 0) {
        perror("mkdir");
        return 2;
    }
    char inside_build[576];
    snprintf(inside_build, sizeof inside_build, "%s/build", repo);
    if (mkdir(inside_build, 0755) != 0 || mkdir(inside, 0755) != 0) {
        perror("mkdir inside");
        return 2;
    }

    char checkout[4096], log[4096], err[4608];
    char cwd[512];
    if (!getcwd(cwd, sizeof cwd)) return 2;

    /* TMPDIR set, no OUT_DIR: log and copy live under TMPDIR. */
    int st = plan(NULL, tmpdir, NULL, NULL, checkout, log, err);
    expect_not_fixed(log);
    if (!uses_fixed_tmp(log)) {
        check(st == OMEGA_M18_CLEAN_CLONE_OK, "M18_CLEAN_CLONE_TMPDIR_OK");
        check(starts_with(log, tmpdir) && strstr(log, "/clean-clone-") &&
              strstr(log, "/clean_clone_m18.log"),
              "M18_CLEAN_CLONE_LOG_UNDER_TMPDIR");
        check(starts_with(checkout, tmpdir), "M18_CLEAN_CLONE_CHECKOUT_UNDER_TMPDIR");
        /* The nested tool name and the environment flag are fixed program
         * arguments. The log is a file path, not a word in a shell command. */
        check(st == OMEGA_M18_CLEAN_CLONE_OK && log[0] == '/' && access(log, W_OK) == 0 &&
              strcmp(OMEGA_M18_CLEAN_CLONE_TOOL, "./build/omegatool") == 0 &&
              strcmp(OMEGA_M18_CLEAN_CLONE_TOOL_ARG, "--run-m18-gates") == 0 &&
              strcmp(OMEGA_M18_CLEAN_CLONE_NESTED_ENV, "OMEGA_IN_CLEAN_CLONE") == 0,
              "M18_CLEAN_CLONE_COMMAND_USES_LOG");
        check(access(log, W_OK) == 0, "M18_CLEAN_CLONE_LOG_CREATED");
        char co[4096], nest[4096], marker[4096];
        snprintf(co, sizeof co, "%s/checkout", checkout);
        snprintf(nest, sizeof nest, "%s/nested", co);
        snprintf(marker, sizeof marker, "%s/keep-me", nest);
        check(mkdir(co, 0755) == 0 && mkdir(nest, 0755) == 0, "M18_CLEAN_CLONE_CHECKOUT_SETUP");
        write_file(marker);
        check(omega_m18_clean_clone_remove_checkout(checkout, log) == 0, "M18_CLEAN_CLONE_REMOVE_OK");
        check(access(log, F_OK) == 0, "M18_CLEAN_CLONE_LOG_KEPT");
        check(access(co, F_OK) != 0, "M18_CLEAN_CLONE_CHECKOUT_REMOVED");

        /* The gate prints the path it used. */
        char printed[512];
        snprintf(printed, sizeof printed, "%s/printed", work);
        int fd = open(printed, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        int saved = dup(STDERR_FILENO);
        dup2(fd, STDERR_FILENO);
        omega_m18_clean_clone_print_log_path(log);
        fflush(stderr);
        dup2(saved, STDERR_FILENO);
        close(fd);
        close(saved);
        FILE *pf = fopen(printed, "r");
        char line[4608];
        line[0] = '\0';
        if (pf) {
            if (!fgets(line, sizeof line, pf)) line[0] = '\0';
            fclose(pf);
        }
        char expect[4608];
        snprintf(expect, sizeof expect, "M18 clean-clone log: %s\n", log);
        check(strcmp(line, expect) == 0, "M18_CLEAN_CLONE_LOG_PATH_PRINTED");
    }

    /* OUT_DIR outside the repo wins over TMPDIR. Both the log and the copy sit there. */
    st = plan(outdir, tmpdir, NULL, NULL, checkout, log, err);
    expect_not_fixed(log);
    if (!uses_fixed_tmp(log)) {
        char want[576];
        snprintf(want, sizeof want, "%s/clean-clone/clean_clone_m18.log", outdir);
        char want_co[576];
        snprintf(want_co, sizeof want_co, "%s/clean-clone", outdir);
        check(st == OMEGA_M18_CLEAN_CLONE_OK && strcmp(log, want) == 0 && access(log, W_OK) == 0,
              "M18_CLEAN_CLONE_LOG_UNDER_OUT_DIR");
        check(strcmp(checkout, want_co) == 0, "M18_CLEAN_CLONE_OUT_DIR_COMMAND");
    }

    /* OUT_DIR inside the repo: the log still goes to that output/clean-clone
     * directory, and the copy goes under TMPDIR so it is not copied into itself. */
    st = plan(inside, tmpdir, repo, NULL, checkout, log, err);
    expect_not_fixed(log);
    if (!uses_fixed_tmp(log)) {
        char want[640];
        snprintf(want, sizeof want, "%s/clean-clone/clean_clone_m18.log", inside);
        check(st == OMEGA_M18_CLEAN_CLONE_OK && strcmp(log, want) == 0,
              "M18_CLEAN_CLONE_LOG_UNDER_OUT_DIR_INSIDE_REPO");
        check(starts_with(checkout, tmpdir) && strstr(checkout, "/clean-clone-") != NULL,
              "M18_CLEAN_CLONE_INSIDE_REPO_CHECKOUT_UNDER_TMPDIR");
        check(log[0] == '/' && checkout[0] == '/' && strstr(log, checkout) == NULL,
              "M18_CLEAN_CLONE_INSIDE_REPO_COMMAND");
    }

    /* Neither OUT_DIR nor TMPDIR: the log is under build/clean-clone, not a fixed host path. */
    if (chdir(defcwd) != 0) return 2;
    st = plan(NULL, NULL, NULL, NULL, checkout, log, err);
    expect_not_fixed(log);
    if (!uses_fixed_tmp(log)) {
        check(st == OMEGA_M18_CLEAN_CLONE_OK && log[0] == '/' &&
              strstr(log, "/build/clean-clone/clean_clone_m18.log") != NULL &&
              access(log, W_OK) == 0,
              "M18_CLEAN_CLONE_LOG_UNDER_BUILD");
    }
    if (chdir(cwd) != 0) return 2;

    /* The output path is not a directory, so the log cannot be created.
     * A mode-only refusal is not enough here: the test may be running as
     * root, which ignores directory mode. The error must name the log and
     * must not look like a regression count. */
    {
        int bfd = open(blocked, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (bfd < 0) return 2;
        close(bfd);
    }
    st = plan(blocked, NULL, NULL, NULL, checkout, log, err);
    expect_not_fixed(log);
    if (!uses_fixed_tmp(log)) {
        int distinct = st == OMEGA_M18_CLEAN_CLONE_LOG_CREATE_FAILED &&
                       strncmp(err, "M18_CLEAN_CLONE_LOG_CREATE_FAILED:", 34) == 0 &&
                       strstr(err, "REGRESSION") == NULL &&
                       strstr(err, log) != NULL;
        check(distinct, "M18_CLEAN_CLONE_LOG_CREATE_FAILED_DISTINCT");
    }

    /* A directory name with a space, a quote and ';' is a path. cp receives
     * it as one argument. A shell would have treated the quote as syntax and
     * run the touch. */
    {
        char hostile[768], src[768], hello[800], dest[800], copied[800];
        char marker_cwd[512], marker_work[768];
        snprintf(hostile, sizeof hostile, "%s/out x';touch m18_pwned_marker;", work);
        snprintf(src, sizeof src, "%s/tiny-src", work);
        snprintf(hello, sizeof hello, "%s/hello", src);
        snprintf(dest, sizeof dest, "%s/copied", hostile);
        snprintf(copied, sizeof copied, "%s/hello", dest);
        if (!getcwd(marker_cwd, sizeof marker_cwd)) return 2;
        size_t ml = strlen(marker_cwd);
        if (ml + 20 >= sizeof marker_cwd) return 2;
        memcpy(marker_cwd + ml, "/m18_pwned_marker", 18);
        snprintf(marker_work, sizeof marker_work, "%s/m18_pwned_marker", work);
        unlink(marker_cwd);
        unlink(marker_work);
        if (mkdir(hostile, 0755) != 0 || mkdir(src, 0755) != 0) return 2;
        write_file(hello);
        st = plan(hostile, NULL, NULL, NULL, checkout, log, err);
        expect_not_fixed(log);
        char want[800];
        snprintf(want, sizeof want, "%s/clean-clone/clean_clone_m18.log", hostile);
        int copy_rc = omega_m18_clean_clone_copy_tree(src, dest);
        int literal = st == OMEGA_M18_CLEAN_CLONE_OK && strcmp(log, want) == 0 &&
                      access(log, W_OK) == 0 && copy_rc == 0 && access(copied, F_OK) == 0 &&
                      access(marker_cwd, F_OK) != 0 && access(marker_work, F_OK) != 0;
        char hostile_tmp[768];
        snprintf(hostile_tmp, sizeof hostile_tmp, "%s/tmp x';touch m18_pwned_marker;", work);
        if (mkdir(hostile_tmp, 0755) != 0) return 2;
        int st_tmp = plan(NULL, hostile_tmp, NULL, NULL, checkout, log, err);
        literal = literal && st_tmp == OMEGA_M18_CLEAN_CLONE_OK &&
                  starts_with(log, hostile_tmp) && strstr(log, "/clean_clone_m18.log") != NULL &&
                  access(log, W_OK) == 0 && access(marker_cwd, F_OK) != 0;
        check(literal, "M18_CLEAN_CLONE_HOSTILE_PATH_NOT_A_SHELL");
    }

    /* cp is not on PATH. The failure must be the exec sentinel, not 127,
     * which is the status make prints when a recipe command is missing
     * (GNU make itself then exits 2). */
    {
        char tiny[768], gone[768];
        snprintf(tiny, sizeof tiny, "%s/tiny-src", work);
        snprintf(gone, sizeof gone, "%s/exec-fail-dst", work);
        const char *old_path = getenv("PATH");
        char *saved = old_path ? strdup(old_path) : NULL;
        int set_ok = setenv("PATH", "/no-such-m18-path", 1) == 0;
        int copy_rc = omega_m18_clean_clone_copy_tree(tiny, gone);
        if (saved) {
            setenv("PATH", saved, 1);
            free(saved);
        } else {
            unsetenv("PATH");
        }
        check(set_ok && copy_rc == OMEGA_M18_CLEAN_CLONE_EXEC_FAILED && copy_rc != 127,
              "M18_CLEAN_CLONE_EXEC_FAILED_NOT_127");
    }

    if (getenv("M18_REQUIRE_TMP_UNWRITABLE")) {
        int probe = open("/tmp/m18_clean_clone_ro_probe", O_CREAT | O_WRONLY | O_EXCL, 0644);
        int tmp_blocked = (probe < 0);
        if (probe >= 0) {
            close(probe);
            unlink("/tmp/m18_clean_clone_ro_probe");
        }
        st = plan(NULL, tmpdir, NULL, NULL, checkout, log, err);
        int ok = tmp_blocked && st == OMEGA_M18_CLEAN_CLONE_OK && !uses_fixed_tmp(log) &&
                 starts_with(log, tmpdir) && access(log, W_OK) == 0;
        check(ok, "M18_CLEAN_CLONE_LOG_TMP_UNWRITABLE_STILL_SELECTS");
    }

    return g_fails ? 1 : 0;
}
