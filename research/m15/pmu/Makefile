CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
SANFLAGS ?= -fsanitize=address,undefined

.PHONY: all test check clean

all: pmu_probe test_contract

pmu_probe: pmu_probe.c pmu_contract.h
	$(CC) $(CFLAGS) -pthread pmu_probe.c -o pmu_probe

test_contract: test_contract.c pmu_contract.h
	$(CC) $(CFLAGS) $(SANFLAGS) test_contract.c -o test_contract

test: test_contract pmu_probe
	./test_contract
	$(CC) $(CFLAGS) -S pmu_probe.c -o pmu_probe.s
	python3 -c 's=open("pmu_probe.s").read(); idx_p=s.index("pmu_workload_predictable:"); idx_d=s.index("pmu_workload_difficult:"); end_d=s.index(".size	pmu_workload_difficult", idx_d); body_p=s[idx_p:idx_d]; body_d=s[idx_d:end_d]; assert "csel" not in body_p.split("cbz",1)[1], "if-converted predictable"; assert "csel" not in body_d.split("cbz",1)[1], "if-converted difficult"; assert any(b in body_p for b in ("bne", "beq", "tbz", "tbnz")), "missing branch in predictable"; assert any(b in body_d for b in ("bne", "beq", "tbz", "tbnz")), "missing branch in difficult"; print("assembly branch survival check PASS")'
	./pmu_probe --discover-only | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["schema_version"]=="gb10-pmu-contract/v1"; assert d["status"]=="NOT_RUN"; assert d["host_measurement"]=="NOT_RUN"; print("discover-only JSON schema PASS")'

check: test

clean:
	rm -f pmu_probe test_contract pmu_probe.s
