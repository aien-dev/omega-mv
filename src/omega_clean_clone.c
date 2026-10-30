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
#include <unistd.h>

static void clear(char *s, size_t n) {
    if (s && n) s[0] = '\0';
}

/* A path that will be wrapped in single quotes for /bin/sh. */
static int shell_ok(const char *s) {
    if (!s || !s[0]) return 0;
    if (strlen(s) >= 4096) return 0;
    for (const char *p = s; *p; p++) {
        if (*p == '\'' || *p == '\n' || *p == '\r') return 0;
    }
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

/* Private directory under parent. On success parent_out holds the real path
 * (the XXXXXX suffix has been replaced). */
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

static int create_log_file(const char *log_path, char *err, size_t err_cap) {
    int fd = open(log_path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return fail_create(err, err_cap, log_path);
    if (close(fd) != 0) return fail_create(err, err_cap, log_path);
    return OMEGA_M18_CLEAN_CLONE_OK;
}

/* The shell cds into the copy before it opens the log. A relative log path
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

static int build_command(char *command, size_t command_cap, const char *checkout_dir,
                         const char *log_path, const char *physics_dir, char *err, size_t err_cap) {
    int n = snprintf(command, command_cap,
                     "root=$(git rev-parse --show-toplevel) && "
                     "rm -rf -- '%s/checkout' && "
                     "cp -r \"$root\" '%s/checkout' && "
                     "cd '%s/checkout' && "
                     "make OUT_DIR=build clean >/dev/null 2>&1 && "
                     "make -j OUT_DIR=build PHYSICS_DIR='%s' >/dev/null 2>&1 && "
                     "OMEGA_IN_CLEAN_CLONE=1 ./build/omegatool --run-m18-gates >'%s' 2>&1",
                     checkout_dir, checkout_dir, checkout_dir, physics_dir, log_path);
    if (n < 0 || (size_t)n >= command_cap) {
        command[0] = '\0';
        return fail_plan(err, err_cap, "clean-clone command does not fit");
    }
    return OMEGA_M18_CLEAN_CLONE_OK;
}

int omega_m18_clean_clone_plan(const char *out_dir, const char *tmpdir,
                               const char *repo_root, const char *temp_fallback,
                               const char *physics_dir,
                               char *checkout_dir, size_t checkout_cap,
                               char *log_path, size_t log_cap,
                               char *command, size_t command_cap,
                               char *err, size_t err_cap) {
    clear(checkout_dir, checkout_cap);
    clear(log_path, log_cap);
    clear(command, command_cap);
    clear(err, err_cap);
    if (!checkout_dir || checkout_cap < 2 || !log_path || log_cap < 2 ||
        !command || command_cap < 2 || !err || err_cap < 2) {
        return OMEGA_M18_CLEAN_CLONE_PLAN_FAILED;
    }
    if (!shell_ok(physics_dir)) {
        return fail_plan(err, err_cap, "physics path is empty or not safe to pass to the shell");
    }
    if (out_dir && out_dir[0] && !shell_ok(out_dir)) {
        return fail_plan(err, err_cap, "output directory is not safe to pass to the shell");
    }
    if (tmpdir && tmpdir[0] && !shell_ok(tmpdir)) {
        return fail_plan(err, err_cap, "TMPDIR is not safe to pass to the shell");
    }
    if (temp_fallback && temp_fallback[0] && !shell_ok(temp_fallback)) {
        return fail_plan(err, err_cap, "temp directory is not safe to pass to the shell");
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

    if (make_absolute(log_path, log_cap) != 0 || !shell_ok(log_path)) {
        return fail_plan(err, err_cap, "log path is not absolute or not safe to pass to the shell");
    }
    return build_command(command, command_cap, checkout_dir, log_path, physics_dir, err, err_cap);
}

void omega_m18_clean_clone_print_log_path(const char *log_path) {
    if (!log_path || !log_path[0]) return;
    fprintf(stderr, "M18 clean-clone log: %s\n", log_path);
}

static int rm_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;
    if (remove(fpath) != 0) return -1;
    return 0;
}

int omega_m18_clean_clone_remove_checkout(const char *checkout_dir, const char *log_path) {
    if (!checkout_dir || !checkout_dir[0]) return 0;
    char path[4096];
    int n = snprintf(path, sizeof(path), "%s/checkout", checkout_dir);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    struct stat st;
    if (lstat(path, &st) == 0) {
        if (nftw(path, rm_cb, 16, FTW_DEPTH | FTW_PHYS) != 0) return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    size_t cl = strlen(checkout_dir);
    int log_here = log_path && strncmp(log_path, checkout_dir, cl) == 0 &&
                   (log_path[cl] == '/' || log_path[cl] == '\0');
    if (!log_here) rmdir(checkout_dir);
    return 0;
}
