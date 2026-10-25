# Birth the Omega compiler from seed-0. The chain is seed0.hex (hand-written)
# -> seed0 -> hex1 -> asm2 -> om4 -> omc, and omc compiles itself to a fixpoint.
# seed/out/omc is that compiler. No C compiler writes a byte of it.
.PHONY: omega-from-seed
omega-from-seed:
	sh seed/omega-from-seed.sh
