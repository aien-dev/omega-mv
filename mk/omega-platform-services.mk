# L202 (aienos#286 cut A): process-wide platform services and the shared push-buffer encoding.
#
# test-gpu-encoding         src/omega_gpu_encoding.h equals physics nvrm_mthd / nvrm_gp_entry over full sweeps,
#                           and a mutant control (one wrong header, one wrong entry) must fail.
# test-platform-services    the contract test on the hosted table and on the freestanding objects (stub default,
#                           install, check refusals, typed lock and task handles, config unset vs empty).
# test-rx-seat-fake         (Makefile) the resident seat over the simulated driver.
.PHONY: test-gpu-encoding test-platform-services

build/test_omega_gpu_encoding: tests/test_omega_gpu_encoding.c src/omega_gpu_encoding.h $(PHYSICS_DIR)/nvrm/nvrm.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/test_omega_gpu_encoding.c $(PHYSICS_DIR)/nvrm/nvrm.c
build/test_omega_gpu_encoding_mutant: tests/test_omega_gpu_encoding.c src/omega_gpu_encoding.h $(PHYSICS_DIR)/nvrm/nvrm.c
	@mkdir -p build
	$(CC) $(CFLAGS) -DOMEGA_ENC_MUTANT -o $@ tests/test_omega_gpu_encoding.c $(PHYSICS_DIR)/nvrm/nvrm.c
test-gpu-encoding: build/test_omega_gpu_encoding build/test_omega_gpu_encoding_mutant
	./build/test_omega_gpu_encoding
	@if ./build/test_omega_gpu_encoding_mutant > build/test_omega_gpu_encoding_mutant.out 2>&1; then \
		echo "test-gpu-encoding: FAIL the mutant control passed"; exit 1; fi; \
	grep -q "FAIL mthd(5, 0x3a8, 4097)" build/test_omega_gpu_encoding_mutant.out && \
	grep -q "FAIL gp_entry(0x0, 1234567)" build/test_omega_gpu_encoding_mutant.out && \
	echo "test-gpu-encoding: mutant control refused (both planted differences found)"

PSVC_HOSTED_SRCS = src/omega_platform_services.c src/omega_platform_services_hosted.c src/omega_gpu_fmt.c
build/test_omega_platform_services: tests/test_omega_platform_services.c $(PSVC_HOSTED_SRCS) src/omega_platform_services.h
	@mkdir -p build
	gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Isrc -pthread -o $@ tests/test_omega_platform_services.c $(PSVC_HOSTED_SRCS)
# The same test against the freestanding objects: no default table, the stub refuses, an installed test table works.
build/test_omega_platform_services_fs: tests/test_omega_platform_services.c $(GPU_FS_LINK) src/omega_platform_services.h
	gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Isrc -DOMEGA_PSVC_TEST_FREESTANDING -pthread -o $@ \
		tests/test_omega_platform_services.c $(GPU_FS_LINK) -lm
test-platform-services: build/test_omega_platform_services build/test_omega_platform_services_fs
	./build/test_omega_platform_services
	./build/test_omega_platform_services_fs
