# #249 E06 extension (ledger L059): checked elimination ranked by cost. New files only:
# tools/rw249/*, tests/research/rw249/*. Picked up by `-include mk/*.mk`; not part of `all` or `test`.
#   make rw249        $(OUT_DIR)/rw249/{rwcheck,rwsearch,rwprobe}
#   make test-rw249   contracts, negatives, cache, unknown-cost, end-to-end, and mutants M1..M8 (each must go red)
ifndef RW249_MK
RW249_MK := 1
.PHONY: rw249 test-rw249
RW249_DIR = $(OUT_DIR)/rw249
RW249_CFLAGS = -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc
RW249_CORE = src/sha256.c src/omega_canonical.c src/omega_validate.c src/omega_core.c src/omega_codec.c \
	src/aarch64_encoder.c src/aarch64_decoder.c src/omega_realize.c src/omega_realize_synth.c \
	src/omega_machine.c src/omega_exec.c src/omega_verify.c src/omega_program.c src/omega_synthesis.c \
	src/omega_library.c
RW249_HDRS = src/omega_program.h src/omega_synthesis.h src/runtime/rx_costmodel.h

$(RW249_DIR)/rwcheck: tools/rw249/rwcheck.c $(RW249_CORE) $(RW249_HDRS)
	@mkdir -p $(RW249_DIR)
	$(CC) $(RW249_CFLAGS) -o $@ tools/rw249/rwcheck.c $(RW249_CORE) -lm

$(RW249_DIR)/rwsearch: tools/rw249/rwsearch.c $(RW249_CORE) src/runtime/rx_costmodel.c $(RW249_HDRS)
	@mkdir -p $(RW249_DIR)
	$(CC) $(RW249_CFLAGS) -o $@ tools/rw249/rwsearch.c $(RW249_CORE) src/runtime/rx_costmodel.c -lm

$(RW249_DIR)/rwprobe: tools/rw249/rwprobe.c $(RW249_CORE) $(RW249_HDRS)
	@mkdir -p $(RW249_DIR)
	$(CC) $(RW249_CFLAGS) -o $@ tools/rw249/rwprobe.c $(RW249_CORE) -lm

rw249: $(RW249_DIR)/rwcheck $(RW249_DIR)/rwsearch $(RW249_DIR)/rwprobe

test-rw249: rw249
	bash tests/research/rw249/test_rw249.sh $(abspath $(RW249_DIR))
endif
