# Preserve the module default goal

While wiring qualification, adding the plan-check rule before tree changed GNU make's implicit default from tree to test-plan. Parent caught this before executing or accepting the module candidate. The previous wiring-only tests did not check default behavior.

New actual query reads the real Makefile first and a separate print-only probe second, preserving its true .DEFAULT_GOAL without invoking tree/compiler actions. RED at caf7e2c3 reports test-plan despite complete target lists. Explicit .DEFAULT_GOAL := tree restores the original module default. The new default-only control catches the same error; all original plan controls remain. Six actual plan cases pass. This is static Make behavior verification, not a module executable qualification or a retried runtime gate.
