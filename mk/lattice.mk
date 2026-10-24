# SI LATTICE L1 (omega-mv #698): sparse meaning x method x realization
# candidate lattice over rx_costmodel. Picked up by `-include mk/*.mk`.
# New files only: src/runtime/rx_lattice.{c,h},
# tests/runtime/rx_lattice_{fixture.h,unit.c,bench.c}.
#
# rx_lattice.o is a calculator like rx_costmodel.o: built alone first, it must
# not reference anything that maps, forks or runs code, or any authority or
# generation operation.
#
# test-lattice: AArch64 host; modeled time, real arm outputs, all mutants.
# bench-lattice: AArch64 host; measured wall time of serial / lcf / q3d on
# the same objective with real pinned CPU lanes. No GPU is used.
.PHONY: test-lattice bench-lattice
RX_LAT_OBJ = $(OUT_DIR)/rx_lattice.o
RX_LAT_MV_SRCS = src/sha256.c src/omega_evidence.c src/omega_canonical.c src/omega_validate.c \
	src/omega_core.c src/omega_codec.c src/aarch64_encoder.c src/aarch64_decoder.c \
	src/omega_realize.c src/omega_realize_synth.c src/omega_program.c src/omega_machine.c \
	src/omega_exec.c src/omega_verify.c src/omega_matvec.c src/omega_matvec_quad.c \
	src/runtime/rx_costmodel.c
RX_LAT_UNIT = $(OUT_DIR)/rx_lattice_unit
RX_LAT_BENCH = $(OUT_DIR)/rx_lattice_bench

$(RX_LAT_OBJ): src/runtime/rx_lattice.c src/runtime/rx_lattice.h src/runtime/rx_costmodel.h | $(OUT_DIR)
	$(CC) $(CFLAGS) -c -o $@ src/runtime/rx_lattice.c
	@if nm -u $@ | grep -E 'rx_gen_|aienos_cap_|rx_caproot_|rx_capadmin_|rx_world_|mmap|mprotect|fork|exec|dlopen|system|pthread|open|write' ; then \
		echo "rx_lattice.o references an operation a calculator must not have"; \
		rm -f $@; exit 1; fi

$(RX_LAT_UNIT): tests/runtime/rx_lattice_unit.c tests/runtime/rx_lattice_fixture.h $(RX_LAT_OBJ) \
	$(RX_LAT_MV_SRCS) | $(OUT_DIR)
	$(CC) $(CFLAGS) -Itests -o $@ tests/runtime/rx_lattice_unit.c $(RX_LAT_OBJ) $(RX_LAT_MV_SRCS) -lm

$(RX_LAT_BENCH): tests/runtime/rx_lattice_bench.c tests/runtime/rx_lattice_fixture.h $(RX_LAT_OBJ) \
	$(RX_LAT_MV_SRCS) | $(OUT_DIR)
	$(CC) $(CFLAGS) -Itests -pthread -o $@ tests/runtime/rx_lattice_bench.c $(RX_LAT_OBJ) $(RX_LAT_MV_SRCS) -lm

test-lattice: $(RX_LAT_UNIT)
	./$(RX_LAT_UNIT)

bench-lattice: $(RX_LAT_BENCH)
	./$(RX_LAT_BENCH)
