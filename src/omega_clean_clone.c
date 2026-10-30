/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Drake Stapleton (AIEN / aien-dev)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "omega_clean_clone.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void clear(char *s, size_t n) {
    if (s && n) s[0] = '\0';
}

/* A path that fits in the buffers the runner passes to exec. Quotes, spaces
 * and ';' are ordinary characters: they never go through a shell. */
static int path_fits(const char *s) {
    if (!s || !s[0]) return 0;
    if (strlen(s) >= 4096) return 0;
    return 1;
}

/* Relative paths are inside the repo the gate was started from. An absolute
 * path is inside when it is the repo root or a child of it. */
static int is_inside_repo(const char *path, const char *repo) {
    if (!repo || !repo[0] || !path || !path[0]) return 0;
    if (path[0] != '/') return 1;
    size_t n = strlen(repo);
    if (n == 0) return 0;
    if (strncmp(path, repo, n) != 0) return 0;
    return path[n] == '\0' || path[n] == '/';
}

static int mkdir_one(const char *path) {
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
        errno = ENOTDIR;
    }
    return -1;
}

/* Create the last component and, when missing, its parent. */
static int mkdir_dir(const char *dir) {
    char parent[4096];
    int n = snprintf(parent, sizeof(parent), "%s", dir);
    if (n < 0 || (size_t)n >= sizeof(parent)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    char *sl = strrchr(parent, '/');
    if (sl && sl != parent) {
        *sl = '\0';
        if (mkdir_one(parent) != 0) return -1;
    }
    return mkdir_one(dir);
}

static int fail_create(char *err, size_t err_cap, const char *path) {
    int e = errno;
    if (!path || !path[0]) path = "(unknown)";
    snprintf(err, err_cap, "M18_CLEAN_CLONE_LOG_CREATE_FAILED: cannot create %s: %s",
             path, strerror(e));
    return OMEGA_M18_CLEAN_CLONE_LOG_CREATE_FAILED;
}

static int fail_checkout(char *err, size_t err_cap, const char *path) {
    int e = errno;
    if (!path || !path[0]) path = "(unknown)";
    snprintf(err, err_cap, "M18_CLEAN_CLONE_CHECKOUT_DIR_FAILED: cannot create %s: %s",
             path, strerror(e));
    return OMEGA_M18_CLEAN_CLONE_CHECKOUT_DIR_FAILED;
}

static int fail_plan(char *err, size_t err_cap, const char *why) {
    snprintf(err, err_cap, "M18_CLEAN_CLONE_LOG_PLAN_FAILED: %s", why);
    return OMEGA_M18_CLEAN_CLONE_PLAN_FAILED;
}

static int copy_path(char *dst, size_t cap, const char *src) {
    int n = snprintf(dst, cap, "%s", src);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

/* One place builds the log file name. A host mutant that puts the old fixed
 * path back is caught by M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP. */
static int set_log_name(char *log_path, size_t log_cap, const char *dir) {
    int n = snprintf(log_path, log_cap, "%s/clean_clone_m18.log", dir);
    if (n < 0 || (size_t)n >= log_cap) return -1;
    return 0;
}

static int make_private(const char *parent, char *parent_out, size_t cap, char *err, size_t err_cap) {
    char tmpl[4096];
    int n = snprintf(tmpl, sizeof(tmpl), "%s/clean-clone-XXXXXX", parent);
    if (n < 0 || (size_t)n >= sizeof(tmpl)) {
        return fail_plan(err, err_cap, "private directory path does not fit");
    }
    if (!mkdtemp(tmpl)) {
        int e = errno;
        snprintf(parent_out, cap, "%s", tmpl);
        errno = e;
        return fail_checkout(err, err_cap, tmpl);
    }
    if (copy_path(parent_out, cap, tmpl) != 0) {
        return fail_plan(err, err_cap, "private directory path does not fit");
    }
    return OMEGA_M18_CLEAN_CLONE_OK;
}

/* Open the log by entering its directory and opening the fixed file name.
 * The directory may come from OUT_DIR or TMPDIR. That string is a working
 * directory, not the path passed to open. */
static int open_created_log(const char *log_path) {
    static const char base[] = "clean_clone_m18.log";
    if (!log_path) {
        errno = EINVAL;
        return -1;
    }
    size_t n = strlen(log_path);
    size_t b = sizeof(base) - 1;
    if (n < b + 2 || log_path[n - b - 1] != '/' ||
        strcmp(log_path + (n - b), base) != 0) {
        errno = EINVAL;
        return -1;
    }
    size_t dlen = n - b - 1;
    char dir[4096];
    if (dlen >= sizeof(dir)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(dir, log_path, dlen);
    dir[dlen] = '\0';
    int cwd = open(".", O_RDONLY | O_CLOEXEC);
    if (cwd < 0) return -1;
    if (chdir(dir) != 0) {
        int e = errno;
        close(cwd);
        errno = e;
        return -1;
    }
    int fd = open("clean_clone_m18.log",
                  O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0644);
    int e = errno;
    if (fchdir(cwd) != 0) {
        int back = errno;
        close(cwd);
        if (fd >= 0) close(fd);
        errno = back;
        return -1;
    }
    close(cwd);
    if (fd < 0) errno = e;
    return fd;
}

static int create_log_file(const char *log_path, char *err, size_t err_cap) {
    int fd = open_created_log(log_path);
    if (fd < 0) return fail_create(err, err_cap, log_path);
    if (close(fd) != 0) return fail_create(err, err_cap, log_path);
    return OMEGA_M18_CLEAN_CLONE_OK;
}

/* The nested tool runs with its cwd inside the copy. A relative log path
 * would be created in the copy instead of the directory this function just
 * proved it could write. */
static int make_absolute(char *path, size_t cap) {
    if (!path || !path[0] || path[0] == '/') return 0;
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return -1;
    char joined[4096];
    int n = snprintf(joined, sizeof(joined), "%s/%s", cwd, path);
    if (n < 0 || (size_t)n >= sizeof(joined)) return -1;
    return copy_path(path, cap, joined);
}

/* Tell the parent that exec did not happen. The write end is close-on-exec,
 * so a successful exec closes it and the parent sees EOF instead of a byte. */
static void note_exec_failed(int fd) {
    unsigned char b = 1;
    ssize_t w = write(fd, &b, 1);
    (void)w;
    _exit(126);
}

/* file is the program name, always a literal at the call. argv may hold
 * paths. dir, when set, is the child's cwd. out_fd, when >= 0, becomes
 * the child's stdout and stderr. One environment value is set in the child only.
 * A program that exits does so with its own status (make exits 2 when a
 * recipe exits 127). A failure before exec is OMEGA_M18_CLEAN_CLONE_EXEC_FAILED. */
static int run_direct(const char *file, char *const argv[], const char *dir,
                      int out_fd, const char *env_name, const char *env_value) {
    if (!file || !file[0] || !argv || !argv[0]) return -1;
    int errpipe[2];
    if (pipe(errpipe) != 0) return -1;
    if (fcntl(errpipe[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(errpipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(errpipe[0]);
        close(errpipe[1]);
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(errpipe[0]);
        close(errpipe[1]);
        return -1;
    }
    if (pid == 0) {
        close(errpipe[0]);
        if (dir && chdir(dir) != 0) note_exec_failed(errpipe[1]);
        if (env_name && env_value && setenv(env_name, env_value, 1) != 0) note_exec_failed(errpipe[1]);
        if (out_fd >= 0) {
            if (dup2(out_fd, STDOUT_FILENO) < 0 || dup2(out_fd, STDERR_FILENO) < 0)
                note_exec_failed(errpipe[1]);
            if (out_fd > STDERR_FILENO) close(out_fd);
        }
        execvp(file, argv);
        note_exec_failed(errpipe[1]);
    }
    close(errpipe[1]);
    int saw_fail = 0;
    int read_err = 0;
    for (;;) {
        unsigned char b = 0;
        ssize_t got = read(errpipe[0], &b, 1);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) read_err = 1;
        else if (got == 1) saw_fail = 1;
        break;
    }
    close(errpipe[0]);
    int status = 0;
    for (;;) {
        pid_t w = waitpid(pid, &status, 0);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0) return -1;
        break;
    }
    if (read_err) return -1;
    if (saw_fail) return OMEGA_M18_CLEAN_CLONE_EXEC_FAILED;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int omega_m18_git_toplevel(char *out, size_t cap) {
    if (!out || cap < 2) return -1;
    out[0] = '\0';
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        if (fds[1] != STDOUT_FILENO) close(fds[1]);
        char *argv[] = {"git", "rev-parse", "--show-toplevel", NULL};
        execvp("git", argv);
        /* Setup or exec failed. The parent reports this as "no toplevel"
         * (-1), not as 127, which is also what a missing recipe command
         * makes make print. */
        _exit(126);
    }
    close(fds[1]);
    size_t n = 0;
    while (n + 1 < cap) {
        ssize_t r = read(fds[0], out + n, cap - 1 - n);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        n += (size_t)r;
    }
    close(fds[0]);
    int status = 0;
    for (;;) {
        pid_t w = waitpid(pid, &status, 0);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0) {
            out[0] = '\0';
            return -1;
        }
        break;
    }
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) n--;
    out[n] = '\0';
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || n == 0) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

int omega_m18_clean_clone_copy_tree(const char *src, const char *dst) {
    if (!path_fits(src) || !path_fits(dst)) return -1;
    char *argv[] = {"cp", "-r", (char *)src, (char *)dst, NULL};
    int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (devnull < 0) return -1;
    int rc = run_direct("cp", argv, NULL, devnull, NULL, NULL);
    close(devnull);
    return rc;
}

static int rm_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;
    if (remove(fpath) != 0) return -1;
    return 0;
}

static int rm_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) return 0;
        return -1;
    }
    if (nftw(path, rm_cb, 16, FTW_DEPTH | FTW_PHYS) != 0) return -1;
    return 0;
}

int omega_m18_clean_clone_run(const char *checkout_dir, const char *log_path,
                              const char *physics_dir) {
    if (!path_fits(checkout_dir) || !path_fits(log_path) || !path_fits(physics_dir)) return -1;
    char root[4096];
    if (omega_m18_git_toplevel(root, sizeof(root)) != 0) return -1;
    char dest[4096];
    int n = snprintf(dest, sizeof(dest), "%s/checkout", checkout_dir);
    if (n < 0 || (size_t)n >= sizeof(dest)) return -1;
    if (rm_tree(dest) != 0) return -1;
    int rc = omega_m18_clean_clone_copy_tree(root, dest);
    if (rc != 0) return rc == OMEGA_M18_CLEAN_CLONE_EXEC_FAILED ? rc : -1;
    int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (devnull < 0) return -1;
    char *clean_argv[] = {"make", "OUT_DIR=build", "clean", NULL};
    rc = run_direct("make", clean_argv, dest, devnull, NULL, NULL);
    if (rc != 0) {
        close(devnull);
        return rc;
    }
    char *build_argv[] = {"make", "-j", "OUT_DIR=build", NULL};
    rc = run_direct("make", build_argv, dest, devnull, "PHYSICS_DIR", physics_dir);
    close(devnull);
    if (rc != 0) return rc;
    int logfd = open_created_log(log_path);
    if (logfd < 0) return -1;
    char *tool_argv[] = {
        (char *)OMEGA_M18_CLEAN_CLONE_TOOL,
        (char *)OMEGA_M18_CLEAN_CLONE_TOOL_ARG,
        NULL
    };
    rc = run_direct(OMEGA_M18_CLEAN_CLONE_TOOL, tool_argv, dest, logfd,
                    OMEGA_M18_CLEAN_CLONE_NESTED_ENV, "1");
    close(logfd);
    return rc;
}

int omega_m18_clean_clone_plan(const char *out_dir, const char *tmpdir,
                               const char *repo_root, const char *temp_fallback,
                               const char *physics_dir,
                               char *checkout_dir, size_t checkout_cap,
                               char *log_path, size_t log_cap,
                               char *err, size_t err_cap) {
    clear(checkout_dir, checkout_cap);
    clear(log_path, log_cap);
    clear(err, err_cap);
    if (!checkout_dir || checkout_cap < 2 || !log_path || log_cap < 2 || !err || err_cap < 2) {
        return OMEGA_M18_CLEAN_CLONE_PLAN_FAILED;
    }
    if (!path_fits(physics_dir)) {
        return fail_plan(err, err_cap, "physics path is empty or does not fit");
    }
    if (out_dir && out_dir[0] && !path_fits(out_dir)) {
        return fail_plan(err, err_cap, "output directory path does not fit");
    }
    if (tmpdir && tmpdir[0] && !path_fits(tmpdir)) {
        return fail_plan(err, err_cap, "TMPDIR path does not fit");
    }
    if (temp_fallback && temp_fallback[0] && !path_fits(temp_fallback)) {
        return fail_plan(err, err_cap, "temp directory path does not fit");
    }

    int have_out = out_dir && out_dir[0];
    int have_tmp = tmpdir && tmpdir[0];
    int have_fb = temp_fallback && temp_fallback[0];
    int out_in = have_out && is_inside_repo(out_dir, repo_root);
    int tmp_in = have_tmp && is_inside_repo(tmpdir, repo_root);
    int fb_in = have_fb && is_inside_repo(temp_fallback, repo_root);

    char log_dir[4096];
    log_dir[0] = '\0';
    int shared = 0;

    if (have_out) {
        int n = snprintf(log_dir, sizeof(log_dir), "%s/clean-clone", out_dir);
        if (n < 0 || (size_t)n >= sizeof(log_dir) || set_log_name(log_path, log_cap, log_dir) != 0) {
            return fail_plan(err, err_cap, "log path does not fit");
        }
        if (mkdir_dir(log_dir) != 0) return fail_create(err, err_cap, log_path);
        int cst = create_log_file(log_path, err, err_cap);
        if (cst != OMEGA_M18_CLEAN_CLONE_OK) return cst;
        if (!out_in) {
            if (copy_path(checkout_dir, checkout_cap, log_dir) != 0) {
                return fail_plan(err, err_cap, "checkout path does not fit");
            }
            shared = 1;
        }
    } else if (have_tmp && !tmp_in) {
        int st = make_private(tmpdir, checkout_dir, checkout_cap, err, err_cap);
        if (st != OMEGA_M18_CLEAN_CLONE_OK) {
            if (checkout_dir[0] && log_path[0] == '\0') {
                set_log_name(log_path, log_cap, checkout_dir);
            }
            return st;
        }
        if (set_log_name(log_path, log_cap, checkout_dir) != 0) {
            return fail_plan(err, err_cap, "log path does not fit");
        }
        int cst = create_log_file(log_path, err, err_cap);
        if (cst != OMEGA_M18_CLEAN_CLONE_OK) return cst;
        shared = 1;
    } else {
        int n = snprintf(log_dir, sizeof(log_dir), "build/clean-clone");
        if (n < 0 || (size_t)n >= sizeof(log_dir) || set_log_name(log_path, log_cap, log_dir) != 0) {
            return fail_plan(err, err_cap, "log path does not fit");
        }
        if (mkdir_dir(log_dir) != 0) return fail_create(err, err_cap, log_path);
        int cst = create_log_file(log_path, err, err_cap);
        if (cst != OMEGA_M18_CLEAN_CLONE_OK) return cst;
        if (!repo_root || !repo_root[0]) {
            if (copy_path(checkout_dir, checkout_cap, log_dir) != 0) {
                return fail_plan(err, err_cap, "checkout path does not fit");
            }
            shared = 1;
        }
    }

    if (!shared) {
        const char *parent = NULL;
        if (have_tmp && !tmp_in) parent = tmpdir;
        else if (have_fb && !fb_in) parent = temp_fallback;
        if (!parent) {
            errno = ENOENT;
            return fail_checkout(err, err_cap, "private checkout directory");
        }
        int st = make_private(parent, checkout_dir, checkout_cap, err, err_cap);
        if (st != OMEGA_M18_CLEAN_CLONE_OK) return st;
    }

    if (make_absolute(log_path, log_cap) != 0 || !path_fits(log_path)) {
        return fail_plan(err, err_cap, "log path is not absolute or does not fit");
    }
    return OMEGA_M18_CLEAN_CLONE_OK;
}

void omega_m18_clean_clone_print_log_path(const char *log_path) {
    if (!log_path || !log_path[0]) return;
    fprintf(stderr, "M18 clean-clone log: %s\n", log_path);
}

int omega_m18_clean_clone_remove_checkout(const char *checkout_dir, const char *log_path) {
    if (!checkout_dir || !checkout_dir[0]) return 0;
    char path[4096];
    int n = snprintf(path, sizeof(path), "%s/checkout", checkout_dir);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    if (rm_tree(path) != 0) return -1;
    size_t cl = strlen(checkout_dir);
    int log_here = log_path && strncmp(log_path, checkout_dir, cl) == 0 &&
                   (log_path[cl] == '/' || log_path[cl] == '\0');
    if (!log_here) rmdir(checkout_dir);
    return 0;
}
