/* SPDX-License-Identifier: Apache-2.0
 * Read-only verification oracle: never a generator, never shipped.
 * Links the UNCHANGED pinned C drawing kit (aien-dev/aienux
 * da55660ea7337f2b6a42095fcd1fb6ddebe1cc7c native/kernel/ui/paint.c and
 * allen_font.c, digests in native-pin.sha256) and calls its ck_glow. Only the
 * fixture parts live here: the case reader, the canvas reset and the gradient
 * background law shared with background.om.
 * Usage: oracle-glow glow-cases.tsv > frames.bin
 * Exit: 0 ok, 2 usage/IO, 3 malformed case, 4 canvas too large. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paint.h"

#define FB_CELLS 65536 /* glow.om: global fb uint 65536 */

static uint32_t fb[FB_CELLS];
static ck_canvas cv;
static ck_rect clip;
static int framed;

static int dump(void)
{
    if (!framed)
        return 0;
    for (int32_t i = 0; i < cv.w * cv.h; i++) {
        unsigned char b[4] = {fb[i] & 255u, fb[i] >> 8 & 255u, fb[i] >> 16 & 255u, fb[i] >> 24};
        if (fwrite(b, 1, 4, stdout) != 4)
            return 2;
    }
    return 0;
}

/* background.om bgpix/fbgrad: inside clip and canvas only */
static void grad(void)
{
    int32_t x0 = ck_max(clip.x, 0), y0 = ck_max(clip.y, 0);
    int32_t x1 = ck_min(clip.x + clip.w, cv.w), y1 = ck_min(clip.y + clip.h, cv.h);
    for (int32_t y = y0; y < y1; y++)
        for (int32_t x = x0; x < x1; x++)
            fb[y * cv.w + x] = CK_RGB((uint32_t)(x * 7) & 255u, (uint32_t)(y * 11) & 255u,
                                      (uint32_t)((x + y) * 3) & 255u);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    FILE *in = fopen(argv[1], "r");
    if (!in)
        return 2;
    char line[512], op[16];
    while (fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%15s", op) != 1)
            return 3;
        if (!strcmp(op, "frame")) {
            char name[64];
            long w, h;
            int rc = dump();
            if (rc)
                return rc;
            if (sscanf(line, "frame %63s %ld %ld", name, &w, &h) != 3 || w <= 0 || h <= 0)
                return 3;
            if (w * h > FB_CELLS)
                return 4;
            memset(fb, 0, sizeof fb);
            cv = (ck_canvas){fb, (int32_t)w, (int32_t)h};
            clip = (ck_rect){0, 0, (int32_t)w, (int32_t)h};
            framed = 1;
        } else if (!framed) {
            return 3;
        } else if (!strcmp(op, "clip")) {
            long x, y, w, h;
            if (sscanf(line, "clip %ld %ld %ld %ld", &x, &y, &w, &h) != 4)
                return 3;
            clip = (ck_rect){(int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h};
        } else if (!strcmp(op, "grad")) {
            grad();
        } else if (!strcmp(op, "glow")) {
            long cx, cy, r;
            unsigned long red, green, blue, a;
            if (sscanf(line, "glow %ld %ld %ld %lu %lu %lu %lu", &cx, &cy, &r, &red, &green, &blue, &a) != 7)
                return 3;
            ck_glow(&cv, &clip, (int32_t)cx, (int32_t)cy, (int32_t)r, CK_RGB(red, green, blue), (uint32_t)a);
        } else {
            return 3;
        }
    }
    if (ferror(in))
        return 2;
    int rc = dump();
    return rc ? rc : fflush(stdout) ? 2 : 0;
}
