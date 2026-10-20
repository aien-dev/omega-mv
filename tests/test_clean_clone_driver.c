/* Driver for tools/test_clean_clone.sh: runs omega_clean_clone_run (the M18 and
 * M19 CLEAN_CLONE reproduction) from the cwd with the given omegatool flag.
 * Host only when the flag names a host-only suite (the self-test uses
 * --run-m6-gates). Prints CLEAN_CLONE_OK <sha> and exits 0 only on success. */
#include "omega_clean_clone.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <omegatool gates flag>\n", argv[0]);
        return 2;
    }
    char sha[41] = {0};
    if (!omega_clean_clone_run("selftest", argv[1], sha)) {
        printf("CLEAN_CLONE_FAIL\n");
        return 1;
    }
    printf("CLEAN_CLONE_OK %s\n", sha);
    return 0;
}
