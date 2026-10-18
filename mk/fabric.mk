# Fabric interface F5-0 (src/fabric/fabric.h): membership, capability
# advertisement, leases and loss detection keyed by AienMachineId, over the
# in-process loopback transport. Files under src/fabric/ and tests/fabric/.
# Picked up by `-include mk/*.mk`. Not part of `all` or `test` (that runs only
# the omegatool gates); CI runs test-fabric and test-fabric-living (rx-host.yml).
# Lane 32: every Fabric build here is a TEST build (-DAIEN_TEST_BUILD=1): the
# loopback transport, HMAC stand-in and dispatcher refuse to compile without
# it, and only the R13 test-build variant links them (RX_FABRIC_TEST_SRCS); the
# production program carries no Fabric until a real transport exists (TRUST-1).
# It uses the Capability Graph only
# through src/runtime/rx_capq.h and links the same objects test-capability-graph
# links (rx_capq.o, rx_graph.o, the pinned AIENOS capability library).
#
# Heterogeneous compute (aienix#6, omega#536): fab_work.c (PST1 task envelope,
# HTK1 request, HTR1 receipt, requester state machine, executor idempotence
# journal), fab_route.c (pre-dispatch route check, steps 1-3 of the dispatcher)
# and fab_auth_ed25519.c (FabAuth over the vendored AIENOS Ed25519) are
# PRODUCTION-buildable: FAB_PROD_SRCS compiles without AIEN_TEST_BUILD and
# fab-work-purity checks them (the signer may reference only aienos_ed25519_*).
#
# test-fabric         purity check, then the exit-gate test plain and with ASan/UBSan
# test-fabric-work    fab-work-purity, then tests/fabric/fab_work_test.c (PST1 vectors from
#                     aien-protocols PR #36, HTK1/HTR1 goldens, matmul oracle, state-machine
#                     negatives, Ed25519 membership) plain and ASan/UBSan, then
#                     tests/fabric/fab_work_procs.sh (requester and executor as two OS processes)
# fabric-purity       fabric objects reference no authority, World, socket or process call
# fabric-receipt      digest-named receipt under evidence/F5-0 (clean tree only)
# test-fabric-living  Lane 13: a second machine's Skill found and used by the
#                     COMPOSITION-2 causal path (rx_compose) through the Fabric
#                     dispatcher (fab_dispatch.c); lease loss, forgery and wrong
#                     machine refused; plain and ASan/UBSan, two runs each. The
#                     same scenario runs inside the R13 test-build variant.
ifndef FABRIC_MK
FABRIC_MK := 1
.PHONY: test-fabric test-fabric-work test-fabric-living fabric-purity fab-work-purity fabric-receipt
FAB_DIR = $(OUT_DIR)/fabric
FAB_PROD_CFLAGS = -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc -Isrc/runtime -Isrc/fabric
FAB_CFLAGS = $(FAB_PROD_CFLAGS) -DAIEN_TEST_BUILD=1
FAB_ASAN = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
FAB_SRCS = src/fabric/fabric.c src/fabric/fab_hmac.c src/fabric/fab_loopback.c
FAB_PROD_SRCS = src/fabric/fab_work.c src/fabric/fab_route.c src/fabric/fab_auth_ed25519.c
FAB_SIG_SRCS = src/searchtrace/sig/ed25519.c src/searchtrace/sig/sha512.c src/searchtrace/sig/ct.c
FAB_LIVING_SRCS = $(FAB_SRCS) src/fabric/fab_dispatch.c src/fabric/fab_route.c
FAB_HDRS = src/fabric/fabric.h src/fabric/fab_hmac.h src/fabric/fab_loopback.h src/fabric/fab_dispatch.h \
	src/fabric/fab_work.h src/fabric/fab_route.h src/fabric/fab_auth_ed25519.h \
	src/runtime/rx_skillroute.h \
	src/runtime/rx_capq.h src/runtime/aien_machine_id.h src/runtime/rx_jspace.h
# Routing discovers and carries records; it never grants authority, never
# touches the World, and F5-0 opens no socket and starts no process.
FAB_FORBIDDEN = ' U (aienos_|rx_caproot_|rx_world_|rx_aegis|argus_|ag_cap|socket$$|connect$$|bind$$|listen$$|accept$$|sendto$$|recvfrom$$|fork$$|exec|system$$|dlopen$$|mmap$$)'
# Same support set test-capability-graph links for rx_capq.o.
FAB_LINK_SRCS = src/runtime/rx_caproot.c src/runtime/rx_world.c src/runtime/rx_coherent.c \
	src/runtime/rx_native_bind.c src/runtime/rx_aegis.c src/sha256.c src/omega_evidence.c \
	src/omega_core.c src/omega_canonical.c src/runtime/aien_machine_id.c

$(FAB_DIR)/%.o: src/fabric/%.c $(FAB_HDRS)
	@mkdir -p $(FAB_DIR)
	$(CC) $(FAB_CFLAGS) -c -o $@ $<

fabric-purity: $(FAB_DIR)/fabric.o $(FAB_DIR)/fab_hmac.o $(FAB_DIR)/fab_loopback.o $(FAB_DIR)/fab_dispatch.o $(FAB_DIR)/fab_route.o
	@if nm -u $^ | grep -E $(FAB_FORBIDDEN) ; then \
		echo "fabric object references an operation the Fabric must not have"; exit 1; fi
	@echo "fabric-purity: fabric.o, fab_hmac.o, fab_loopback.o, fab_dispatch.o and fab_route.o reference no forbidden symbol"

# Production objects: compiled WITHOUT AIEN_TEST_BUILD (so a test-only header
# dragged in would refuse), same forbidden list, plus no file/heap/clock call;
# the Ed25519 signer may reference only the vendored aienos_ed25519_* functions.
$(FAB_DIR)/prod/%.o: src/fabric/%.c $(FAB_HDRS)
	@mkdir -p $(FAB_DIR)/prod
	$(CC) $(FAB_PROD_CFLAGS) -c -o $@ $<

FAB_WORK_FORBIDDEN = ' U (rx_caproot_|rx_world_|rx_aegis|argus_|ag_cap|socket$$|connect$$|bind$$|listen$$|accept$$|sendto$$|recvfrom$$|fork$$|exec|system$$|dlopen$$|mmap$$|malloc$$|calloc$$|free$$|fopen$$|open$$|read$$|write$$|time$$|clock_gettime$$|getenv$$)'
fab-work-purity: $(FAB_DIR)/prod/fab_work.o $(FAB_DIR)/prod/fab_route.o $(FAB_DIR)/prod/fab_auth_ed25519.o
	@if nm -u $^ | grep -E $(FAB_WORK_FORBIDDEN) ; then \
		echo "fab_work/fab_route/fab_auth_ed25519 object references an operation it must not have"; exit 1; fi
	@if nm -u $(FAB_DIR)/prod/fab_work.o $(FAB_DIR)/prod/fab_route.o | grep -E ' U aienos_' ; then \
		echo "fab_work.o / fab_route.o must not reference AIENOS authority code"; exit 1; fi
	@if nm -u $(FAB_DIR)/prod/fab_auth_ed25519.o | grep -E ' U aienos_' | grep -vE ' U aienos_ed25519_(sign|verify|public_key)$$' ; then \
		echo "fab_auth_ed25519.o may reference only aienos_ed25519_sign/verify/public_key"; exit 1; fi
	@echo "fab-work-purity: fab_work.o, fab_route.o and fab_auth_ed25519.o build without AIEN_TEST_BUILD and reference no forbidden symbol"

test-fabric: fabric-purity
	@$(MAKE) --no-print-directory $(RX_CAPQ_OBJ) $(RX_GRAPH_OBJ) $(AIENOS_CAP_LIB)
	$(CC) $(CFLAGS) $(FAB_CFLAGS) -pthread -o $(FAB_DIR)/fabric_test tests/fabric/fabric_test.c \
		$(FAB_SRCS) $(FAB_LINK_SRCS) $(RX_CAPQ_OBJ) $(RX_GRAPH_OBJ) $(AIENOS_CAP_LIB) -lm
	$(CC) $(CFLAGS) $(FAB_CFLAGS) $(FAB_ASAN) -pthread -o $(FAB_DIR)/fabric_test_asan \
		tests/fabric/fabric_test.c $(FAB_SRCS) $(FAB_LINK_SRCS) $(RX_CAPQ_OBJ) $(RX_GRAPH_OBJ) \
		$(AIENOS_CAP_LIB) -lm
	$(abspath $(FAB_DIR)/fabric_test)
	$(abspath $(FAB_DIR)/fabric_test_asan) > $(FAB_DIR)/asan.out
	@grep -q '^F5_0_FABRIC_LOOPBACK_PASS$$' $(FAB_DIR)/asan.out && echo "test-fabric: ASan/UBSan run PASS"

# Same link set as test-fabric minus omega_evidence.c (GPU header; nothing in the
# fabric set references it): this is the set a hosted worker compiles too.
FAB_WORK_LINK_SRCS = $(filter-out src/omega_evidence.c,$(FAB_LINK_SRCS))
FAB_WORK_TEST_SRCS = tests/fabric/fab_work_test.c $(FAB_SRCS) $(FAB_PROD_SRCS) $(FAB_SIG_SRCS) $(FAB_WORK_LINK_SRCS)
test-fabric-work: fab-work-purity fabric-purity
	@$(MAKE) --no-print-directory $(RX_CAPQ_OBJ) $(RX_GRAPH_OBJ) $(RX_SKILLROUTE_OBJ) $(AIENOS_CAP_LIB)
	$(CC) $(CFLAGS) $(FAB_CFLAGS) -pthread -o $(FAB_DIR)/fab_work_test $(FAB_WORK_TEST_SRCS) \
		$(RX_SKILLROUTE_OBJ) $(RX_CAPQ_OBJ) $(RX_GRAPH_OBJ) $(AIENOS_CAP_LIB) -lm
	$(CC) $(CFLAGS) $(FAB_CFLAGS) $(FAB_ASAN) -pthread -o $(FAB_DIR)/fab_work_test_asan $(FAB_WORK_TEST_SRCS) \
		$(RX_SKILLROUTE_OBJ) $(RX_CAPQ_OBJ) $(RX_GRAPH_OBJ) $(AIENOS_CAP_LIB) -lm
	$(abspath $(FAB_DIR)/fab_work_test) tests/fabric/vectors/pst
	$(abspath $(FAB_DIR)/fab_work_test_asan) tests/fabric/vectors/pst > $(FAB_DIR)/work_asan.out
	@grep -q '^FAB_WORK_PASS$$' $(FAB_DIR)/work_asan.out && echo "test-fabric-work: ASan/UBSan run PASS"
	tests/fabric/fab_work_procs.sh $(abspath $(FAB_DIR)/fab_work_test) $(abspath $(FAB_DIR))

test-fabric-living: fabric-purity
	@$(MAKE) --no-print-directory $(RX_CAPQ_OBJ) $(RX_GRAPH_OBJ) $(RX_SKILLROUTE_OBJ) $(OUT_DIR)/rx_cortex.o $(AIENOS_CAP_LIB)
	$(CC) $(CFLAGS) $(FAB_CFLAGS) -Itests/runtime -Itests/fabric -pthread -o $(FAB_DIR)/fabric_living_test \
		tests/fabric/fabric_living_test.c $(FAB_LIVING_SRCS) $(RX_COMPOSE_SRCS) $(RX_COMPOSE_LINK)
	$(CC) $(CFLAGS) $(FAB_CFLAGS) $(FAB_ASAN) -Itests/runtime -Itests/fabric -pthread \
		-o $(FAB_DIR)/fabric_living_test_asan tests/fabric/fabric_living_test.c $(FAB_LIVING_SRCS) \
		$(RX_COMPOSE_SRCS) $(RX_COMPOSE_LINK)
	$(abspath $(FAB_DIR)/fabric_living_test)
	$(abspath $(FAB_DIR)/fabric_living_test_asan) > $(FAB_DIR)/living_asan.out
	@grep -q '^FABRIC_LIVING_PASS$$' $(FAB_DIR)/living_asan.out && echo "test-fabric-living: ASan/UBSan run PASS"

fabric-receipt:
	tests/fabric/fabric_receipt.sh
endif
