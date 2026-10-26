# Numeric Blackwell source slice, declared once. The numeric CPU/host test
# source lists in Makefile end with this run, in this order.
NUMERIC_BW_SRCS = src/omega_blackwell_encoder.c src/omega_blackwell_codegen.c src/omega_blackwell_matmul.c src/omega_blackwell_qmd.c src/omega_om_units.c
NUMERIC_BW_HDRS = src/omega_blackwell_qmd.h src/omega_blackwell_codegen.h src/omega_blackwell_encoder.h src/sha256.h
