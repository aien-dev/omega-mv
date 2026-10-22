#!/bin/sh
set -eu
sed "s/^int main(void)/int l519_visibility_main(void)/" tests/gpu_session_visibility_oracle.c > build/L533/visibility_base.c
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Itests -DOMEGA_GPU_NO_POSIX -o build/L533/visibility-independent evidence/L533-REVIEW/visibility_probe.c build/L533/gpu-visibility-fs/omega_gpu_freestanding.o
build/L533/visibility-independent
