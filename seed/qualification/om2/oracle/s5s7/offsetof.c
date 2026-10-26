/* SPDX-License-Identifier: Apache-2.0
 * OM-2 qualification, lane s5s7 (#748, ledger L836): offsetof ORACLE for contract section 5
 * (spec/omegese-om2.md lines 163-165). Outside the delivered closure.
 * Build and run from the worktree root (see PROVENANCE.txt for the recorded run):
 *   gcc -std=c11 -Wall -Wextra -Werror -I/tmp/zl/main/src \
 *       -o build/om2-qual/s5s7-offsetof seed/qualification/om2/oracle/s5s7/offsetof.c
 *   build/om2-qual/s5s7-offsetof build/om2-qual/s5s7-shactx.bin
 * stdout: the offsets and sizes the layout line must equal; argv[1]: the 104-byte image of a
 * sha256_ctx with state[i] = 0x10203040 + i, count = 0x0123456789abcdef, buffer[j] = 3j+1. */
/* fdopen/open are POSIX, and the documented build uses -std=c11 (strict ISO). */
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sha256.h"

int main(int argc, char **argv)
{
    sha256_ctx c;
    FILE *f;
    int i, bad = 0;

    printf("sizeof sha256_ctx %zu\n", sizeof(sha256_ctx));
    printf("state offset %zu size %zu count %zu elem %zu\n", offsetof(sha256_ctx, state),
           sizeof c.state, sizeof c.state / sizeof c.state[0], sizeof c.state[0]);
    printf("count offset %zu size %zu\n", offsetof(sha256_ctx, count), sizeof c.count);
    printf("buffer offset %zu size %zu count %zu elem %zu\n", offsetof(sha256_ctx, buffer),
           sizeof c.buffer, sizeof c.buffer / sizeof c.buffer[0], sizeof c.buffer[0]);
    printf("alignof %zu\n", _Alignof(sha256_ctx));
    /* the OM-2 line (erratum E5: count is a scalar): layout shactx 104 / state uint 8 at 0 /
     * count ulong at 32 / buf uchar 64 at 40, all little */
    bad |= sizeof(sha256_ctx) != 104;
    bad |= offsetof(sha256_ctx, state) != 0 || sizeof c.state != 32 || sizeof c.state[0] != 4;
    bad |= offsetof(sha256_ctx, count) != 32 || sizeof c.count != 8;
    bad |= offsetof(sha256_ctx, buffer) != 40 || sizeof c.buffer != 64;
    {
        unsigned int one = 1;
        unsigned char b;
        memcpy(&b, &one, 1);
        printf("little endian %d\n", b == 1);
        bad |= b != 1;
    }
    printf("layout matches contract %s\n", bad ? "NO" : "YES");
    memset(&c, 0, sizeof c);
    for (i = 0; i < 8; i++)
        c.state[i] = 0x10203040u + (unsigned)i;
    c.count = 0x0123456789abcdefull;
    for (i = 0; i < 64; i++)
        c.buffer[i] = (unsigned char)(3 * i + 1);
    if (argc > 1) {
        /* 0600, never the 0666 fopen would leave under a permissive umask: the image is
         * oracle output, not something other users should write to. */
        int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0)
            return 2;
        f = fdopen(fd, "wb");
        if (!f) {
            close(fd);
            return 2;
        }
        if (fwrite(&c, 1, sizeof c, f) != sizeof c || fclose(f) != 0)
            return 2;
    }
    return bad;
}
