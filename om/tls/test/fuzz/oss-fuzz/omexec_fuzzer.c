// SPDX-License-Identifier: Apache-2.0
// HOST TOOLING, not part of the Omega library: a libFuzzer entry point that runs one compiled
// om/tls fuzz target (aarch64, under qemu-aarch64-static on the amd64 OSS-Fuzz/ClusterFuzzLite
// builders) on the input and aborts when the target exits >= 100 (an Omega trap), so the
// infrastructure records it as a crash. libFuzzer sees coverage of this driver only, not of the
// .om code: under this driver the campaign is black-box (stated in NOTES and the PR).
// Driver $OUT/fz_X runs $OUT/fz_X.om.bin with argument $OUT/fz_X.arg's content (if present).
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char tgt[4096], arg[4096];

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  snprintf(tgt, sizeof tgt, "%s.om.bin", (*argv)[0]);
  char af[4096];
  snprintf(af, sizeof af, "%s.arg", (*argv)[0]);
  FILE *f = fopen(af, "r");
  if (f) {
    if (fgets(arg, sizeof arg, f)) arg[strcspn(arg, "\n")] = 0;
    fclose(f);
  }
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int p[2];
  if (pipe(p)) abort();
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[0], 0); close(p[0]); close(p[1]);
    int n = open("/dev/null", O_WRONLY); dup2(n, 1); dup2(n, 2);
    if (arg[0]) execl("/usr/bin/qemu-aarch64-static", "qemu-aarch64-static", tgt, arg, (char *)0);
    else execl("/usr/bin/qemu-aarch64-static", "qemu-aarch64-static", tgt, (char *)0);
    _exit(127);
  }
  close(p[0]);
  for (size_t o = 0; o < size;) { ssize_t w = write(p[1], data + o, size - o); if (w <= 0) break; o += (size_t)w; }
  close(p[1]);
  int st = 0;
  waitpid(pid, &st, 0);
  if (WIFSIGNALED(st) || (WIFEXITED(st) && WEXITSTATUS(st) >= 100 && WEXITSTATUS(st) != 127)) abort();
  return 0;
}
