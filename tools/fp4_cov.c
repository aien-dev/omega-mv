/* expert coverage of the calibration set: per layer, min / p10 / median routed-token count over 128 experts, and experts below 8/32/128 tokens */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
enum { D = 2048, TOPK = 8, REC = D + 8 + 8 + 8 * 768 };
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
int main(int argc, char **argv) {
    for (int L = 0; L < 48; L++) {
        char p[600]; snprintf(p, sizeof p, "%s/act_%02d.bin", argv[1], L); FILE *f = fopen(p, "rb"); if (!f) { perror(p); return 1; }
        int cnt[128] = { 0 }; long n = 0; static float rec[REC];
        while (fread(rec, 4, REC, f) == REC) { const int32_t *ids = (const int32_t *)(rec + D); for (int j = 0; j < TOPK; j++) if (ids[j] >= 0 && ids[j] < 128) cnt[ids[j]]++; n++; }
        fclose(f); int s[128]; memcpy(s, cnt, sizeof s); qsort(s, 128, 4, cmp);
        int b8 = 0, b32 = 0, b128 = 0; for (int e = 0; e < 128; e++) { b8 += cnt[e] < 8; b32 += cnt[e] < 32; b128 += cnt[e] < 128; }
        printf("layer %2d tokens %ld min %d p10 %d median %d max %d  experts<8 %d <32 %d <128 %d\n", L, n, s[0], s[12], s[64], s[127], b8, b32, b128);
    }
    return 0;
}
