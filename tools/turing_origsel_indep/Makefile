# SPDX-License-Identifier: Apache-2.0
CC ?= cc
CFLAGS = -O2 -std=c11 -ffp-contract=off -fno-fast-math -Wall -Wextra

origsel_indep: origsel_indep.c
	$(CC) $(CFLAGS) -o $@ origsel_indep.c

selftest: origsel_indep
	./origsel_indep --selftest

clean:
	rm -f origsel_indep

.PHONY: selftest clean
