# tools/omega_bytes (ledger L098): run hand-written instruction words on the CPU (a64) and on
# the GB10 (gb10) with the least machinery between. Picked up by `-include mk/*.mk`; not part
# of `all` or `test`.
#
# omega-bytes        build/omega_bytes/omega_bytes
# test-omega-bytes   host only: refusal controls, both a64 gates (16 argument sets each against
#                    the C reference), every gb10 word file named, encoder word file up to date.
#                    The chip gate is tools/omega_bytes/run_chip.sh (needs the GB10 lock).
ifndef OMEGA_BYTES_MK
OMEGA_BYTES_MK := 1
.PHONY: omega-bytes test-omega-bytes
OBY_DIR = $(OUT_DIR)/omega_bytes
OBY_CFLAGS = -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc -Isrc/compiler
OBY_OSC_LIB = $(filter-out src/compiler/oscc_main.c,$(wildcard src/compiler/*.c))
OBY_K = tools/omega_bytes/kernels

$(OBY_DIR)/omega_bytes: tools/omega_bytes/omega_bytes.c src/aarch64_decoder.c $(OBY_OSC_LIB) $(OUT_DIR)/libomega_gpu.a
	@mkdir -p $(OBY_DIR)
	$(CC) $(OBY_CFLAGS) -o $@ tools/omega_bytes/omega_bytes.c src/aarch64_decoder.c $(OBY_OSC_LIB) \
		$(OUT_DIR)/libomega_gpu.a -lpthread -lm

omega-bytes: $(OBY_DIR)/omega_bytes

test-omega-bytes: $(OBY_DIR)/omega_bytes
	! $(OBY_DIR)/omega_bytes check a64 $(OBY_K)/a64_refused.words
	! $(OBY_DIR)/omega_bytes check gb10 $(OBY_K)/gb10_refused.words
	! $(OBY_DIR)/omega_bytes a64 $(OBY_K)/a64_refused.words 1
	$(OBY_DIR)/omega_bytes a64 $(OBY_K)/a64_mul3sub2.words --gate mul3sub2
	$(OBY_DIR)/omega_bytes a64 $(OBY_K)/a64_vadd64.words --gate vadd64
	! $(OBY_DIR)/omega_bytes a64 $(OBY_K)/a64_mul3sub2.words --gate vadd64 >/dev/null
	$(OBY_DIR)/omega_bytes check gb10 $(OBY_K)/gb10_vadd_hand.words
	$(OBY_DIR)/omega_bytes check gb10 $(OBY_K)/gb10_vadd_mutant.words
	$(OBY_DIR)/omega_bytes check gb10 $(OBY_K)/gb10_empty.words
	$(OBY_DIR)/omega_bytes check gb10 $(OBY_K)/gb10_vadd_encoder.words
	$(OBY_DIR)/omega_bytes emit-encoder-vecadd | cmp - $(OBY_K)/gb10_vadd_encoder.words
	@echo "test-omega-bytes: PASS (host)"
endif
