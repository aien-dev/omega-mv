# tools/omegese: Omegese, the AI-writer program language (docs/one_layer/OMEGESE.md). Omegese lowers to the
# omega_emit op list, so the IR, checker2 and the signed receipt chain are unchanged.
# Picked up by `-include mk/*.mk`; not part of `all` or `test`.
#
# omegese        build/omegese/omegese
# test-omegese   tools/omegese/test_omegese.sh twice: on the -O2 build, then on an ASan and UBSan build with
#              smaller fuzz counts, whose negative control is two canary builds (a read after free and a
#              signed overflow) that must be caught. The script checks the dev programs (omegese ->
#              omega_emit -> checker2 ACCEPT), refusals with reasons, the loop-guard negative control, the
#              exact next oracle (completeness, soundness and truncation fuzz) and, when OMEGESE_TOKCOUNT
#              and OMEGESE_TOKENIZER are set, the tokenizer gate (every fixed word is one token)
ifndef OMEGESE_MK
OMEGESE_MK := 1
.PHONY: omegese test-omegese
OMEGESE_DIR = $(OUT_DIR)/omegese
OMEGESE_CFLAGS = -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE
OMEGESE_SAN = -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all

$(OMEGESE_DIR)/omegese: tools/omegese/omegese.c
	@mkdir -p $(OMEGESE_DIR)
	$(CC) $(OMEGESE_CFLAGS) -O2 -o $@ tools/omegese/omegese.c

$(OMEGESE_DIR)/omegese_asan: tools/omegese/omegese.c
	@mkdir -p $(OMEGESE_DIR)
	$(CC) $(OMEGESE_CFLAGS) $(OMEGESE_SAN) -o $@ tools/omegese/omegese.c

$(OMEGESE_DIR)/omegese_canary%: tools/omegese/omegese.c
	@mkdir -p $(OMEGESE_DIR)
	$(CC) $(OMEGESE_CFLAGS) $(OMEGESE_SAN) -DOMEGESE_CANARY=$* -o $@ tools/omegese/omegese.c

omegese: $(OMEGESE_DIR)/omegese

test-omegese: omegese omega-emit checker2 $(OMEGESE_DIR)/omegese_asan $(OMEGESE_DIR)/omegese_canary1 $(OMEGESE_DIR)/omegese_canary2
	bash tools/omegese/test_omegese.sh $(abspath $(OMEGESE_DIR)/omegese) $(abspath $(OUT_DIR)/omega_emit/omega_emit) $(abspath $(C2_DIR)/checker2)
	OMEGESE_SANITIZED=1 OMEGESE_CANARIES="$(abspath $(OMEGESE_DIR)/omegese_canary1) $(abspath $(OMEGESE_DIR)/omegese_canary2)" \
	OMEGESE_WALKS=10 OMEGESE_MUTANTS=1 \
	bash tools/omegese/test_omegese.sh $(abspath $(OMEGESE_DIR)/omegese_asan) $(abspath $(OUT_DIR)/omega_emit/omega_emit) $(abspath $(C2_DIR)/checker2)

# libomegese      build/omegese/libomegese.a: omegese.c unchanged, compiled through omegese_lib.c into a
#                 library with one entry point (omegese_lib.h), for an engine that asks next at every
#                 decode step without starting a process (sovereign-core aien-omegese-constraint links it)
# test-omegese-lib  tools/omegese/test_omegese_lib.sh on the -O2 library, then on an ASan and UBSan build:
#                 the reset list covers every variable of omegese.c, and on every prefix of the dev,
#                 nested, trial2 and worked-example programs the library answers exactly as the command
.PHONY: libomegese test-omegese-lib
OMEGESE_LIB_SRC = tools/omegese/omegese_lib.c tools/omegese/omegese_lib.h tools/omegese/omegese.c

$(OMEGESE_DIR)/omegese_lib.o: $(OMEGESE_LIB_SRC)
	@mkdir -p $(OMEGESE_DIR)
	$(CC) $(OMEGESE_CFLAGS) -O2 -fPIC -c -o $@ tools/omegese/omegese_lib.c

$(OMEGESE_DIR)/libomegese.a: $(OMEGESE_DIR)/omegese_lib.o
	rm -f $@
	$(AR) rcs $@ $<

$(OMEGESE_DIR)/test_omegese_lib: tools/omegese/test_omegese_lib.c $(OMEGESE_DIR)/libomegese.a
	$(CC) $(OMEGESE_CFLAGS) -O2 -o $@ tools/omegese/test_omegese_lib.c $(OMEGESE_DIR)/libomegese.a

$(OMEGESE_DIR)/test_omegese_lib_asan: tools/omegese/test_omegese_lib.c $(OMEGESE_LIB_SRC)
	@mkdir -p $(OMEGESE_DIR)
	$(CC) $(OMEGESE_CFLAGS) $(OMEGESE_SAN) -o $@ tools/omegese/test_omegese_lib.c tools/omegese/omegese_lib.c

libomegese: $(OMEGESE_DIR)/libomegese.a

test-omegese-lib: omegese $(OMEGESE_DIR)/test_omegese_lib $(OMEGESE_DIR)/test_omegese_lib_asan
	bash tools/omegese/test_omegese_lib.sh $(abspath $(OMEGESE_DIR)/omegese) $(abspath $(OMEGESE_DIR)/test_omegese_lib) $(abspath $(OMEGESE_DIR)/lib-scratch)
	bash tools/omegese/test_omegese_lib.sh $(abspath $(OMEGESE_DIR)/omegese) $(abspath $(OMEGESE_DIR)/test_omegese_lib_asan) $(abspath $(OMEGESE_DIR)/lib-scratch-asan)
endif
