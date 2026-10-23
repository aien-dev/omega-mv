# tools/gen/abstract (GEN-3, aien-spark/omega#478): GEN record schema v1 (RECORD_SCHEMA.md), the strict
# validator, the abstraction extractor and its corpus (tests/one_layer/generalization/corpus).
# Picked up by `-include mk/*.mk`; not part of `all` or `test`. CPU only, no model, no GPU.
#
# gen-abstract         build/gen_abstract/{recheck,gabs,oracle,famsig}
# test-gen-abstract    tools/gen/abstract/test_gen_abstract.sh: recheck, corpus, extract, verify, probes, mutants
#
# famsig is GEN-1's tool (tools/gen/protocol, FAMSIG v1, aien-spark/omega#476). famsig_build reads it with git show
# from the commit pinned in tools/gen/abstract/FAMSIG.pin (sha256 of every file checked, fetched once if the clone
# is shallow) and recompiles unless the existing binary was built from exactly that pin; it runs on every make.
ifndef GEN_ABSTRACT_MK
GEN_ABSTRACT_MK := 1
.PHONY: gen-abstract test-gen-abstract
GENABS_DIR = $(OUT_DIR)/gen_abstract
GENABS_CFLAGS = -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc

$(GENABS_DIR)/recheck: tools/gen/abstract/recheck.c src/sha256.c src/sha256.h
	@mkdir -p $(GENABS_DIR)
	$(CC) $(GENABS_CFLAGS) -o $@ tools/gen/abstract/recheck.c src/sha256.c

$(GENABS_DIR)/gabs: tools/gen/abstract/gabs.c
	@mkdir -p $(GENABS_DIR)
	$(CC) $(GENABS_CFLAGS) -o $@ tools/gen/abstract/gabs.c

$(GENABS_DIR)/oracle: tests/one_layer/generalization/corpus/oracle.c
	@mkdir -p $(GENABS_DIR)
	$(CC) $(GENABS_CFLAGS) -o $@ tests/one_layer/generalization/corpus/oracle.c

.PHONY: $(GENABS_DIR)/famsig
$(GENABS_DIR)/famsig: tools/gen/abstract/famsig_build tools/gen/abstract/FAMSIG.pin src/sha256.c src/sha256.h
	bash tools/gen/abstract/famsig_build $(OUT_DIR)

gen-abstract: $(GENABS_DIR)/recheck $(GENABS_DIR)/gabs $(GENABS_DIR)/oracle $(GENABS_DIR)/famsig

test-gen-abstract: gen-abstract omegese omega-emit checker2 one-layer-acceptance-tools $(OUT_DIR)/compiler/oscc
	bash tools/gen/abstract/test_gen_abstract.sh $(abspath $(OUT_DIR))
endif
