# mk/late/path.mk -- PATH-1 (moved out of omega's Makefile with omega-programs, L396).
# Included at the end of omega's Makefile, where it was read before the move.

# PATH-1 (spec/path-semantic-object.md section 15). BOOTSTRAP / REFERENCE oracle
# and TEST HARNESS only; no runtime, physics or GPU links.
PATH1_TEST_BIN := $(OUT_DIR)/tests-path/test_path_identity
.PHONY: test-path
$(PATH1_TEST_BIN): src/path/rx_path.c src/path/rx_path.h src/sha256.c src/sha256.h \
		src/omega_types.h tests/path/test_path_identity.c
	mkdir -p $(dir $@)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Isrc -Isrc/path -o $@ \
		src/path/rx_path.c src/sha256.c tests/path/test_path_identity.c

test-path: $(PATH1_TEST_BIN)
	sh tests/path/check_path_boundary.sh
	./$(PATH1_TEST_BIN)
	CC="$(CC)" sh tests/path/mutate.sh
