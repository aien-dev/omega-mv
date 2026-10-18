#include "omega_clean_clone.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

bool omega_cc_path_is_plain(const char *s) {
    if (!s || !*s) return false;
    for (const char *p = s; *p; p++) {
        char c = *p;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '/' || c == '.' || c == '_' || c == '+' || c == '-';
        if (!ok) return false;
    }
    return true;
}

/* Child side: environment, directory, output, then exec. Never returns. */
static void cc_child(const char *dir, char *const argv[], int out_fd, bool in_clean_clone) {
    if (unsetenv("OUT_DIR") != 0) _exit(127);
    if (in_clean_clone && setenv("OMEGA_IN_CLEAN_CLONE", "1", 1) != 0) _exit(127);
    if (dir && chdir(dir) != 0) _exit(127);
    if (dup2(out_fd, STDOUT_FILENO) < 0) _exit(127);
    if (dup2(out_fd, STDERR_FILENO) < 0) _exit(127);
    if (out_fd != STDOUT_FILENO && out_fd != STDERR_FILENO) close(out_fd);
    execvp(argv[0], argv);
    _exit(127);
}

static int cc_wait(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

int omega_cc_run(const char *dir, char *const argv[], const char *out_path, bool in_clean_clone) {
    if (!argv || !argv[0]) return -1;
    int out_fd = out_path ? open(out_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)
                          : open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (out_fd < 0) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(out_fd);
        return -1;
    }
    if (pid == 0) cc_child(dir, argv, out_fd, in_clean_clone);
    close(out_fd);
    return cc_wait(pid);
}

bool omega_cc_capture_line(const char *dir, char *const argv[], char *out, size_t out_size) {
    if (!argv || !argv[0] || !out || out_size == 0) return false;
    out[0] = '\0';
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return false;
    int err_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (err_fd < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        close(err_fd);
        return false;
    }
    if (pid == 0) {
        if (dir && chdir(dir) != 0) _exit(127);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        if (dup2(err_fd, STDERR_FILENO) < 0) _exit(127);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    close(err_fd);
    size_t used = 0;
    bool overflow = false;
    char buf[256];
    for (;;) {
        ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i++) {
            if (used + 1 < out_size) out[used++] = buf[i];
            else overflow = true;
        }
    }
    close(fds[0]);
    out[used] = '\0';
    int rc = cc_wait(pid);
    char *nl = strpbrk(out, "\r\n");
    if (nl) *nl = '\0';
    else if (overflow) return false;
    return rc == 0 && out[0] != '\0';
}

static int cc_remove_entry(const char *path, const struct stat *sb, int type, struct FTW *ftw) {
    (void)sb;
    (void)ftw;
    int rc = (type == FTW_DP) ? rmdir(path) : unlink(path);
    if (rc != 0 && errno != ENOENT) {
        fprintf(stderr, "clean clone: cannot remove %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

int omega_cc_remove_tree(const char *path) {
    if (!omega_cc_path_is_plain(path) || path[0] != '/' || strcmp(path, "/") == 0) {
        fprintf(stderr, "clean clone: refusing to remove '%s' (not a plain absolute path)\n",
                path ? path : "(null)");
        return -1;
    }
    struct stat st;
    if (lstat(path, &st) != 0) return (errno == ENOENT) ? 0 : -1;
    /* FTW_DEPTH: children before their directory. FTW_PHYS: never follow a symlink,
     * so a link inside the copy cannot reach outside it. FTW_MOUNT: stay on one
     * file system. */
    return nftw(path, cc_remove_entry, 32, FTW_DEPTH | FTW_PHYS | FTW_MOUNT) == 0 ? 0 : -1;
}
