# Hosted numerics retain Omega CPU/provenance units; GPU code comes from CUDA++.
NUMERIC_BW_SRCS = src/omega_om_units.c
NUMERIC_BW_HDRS = $(CUDAPP_SOURCE)/src/omega_blackwell_qmd.h $(CUDAPP_SOURCE)/src/omega_blackwell_codegen.h $(CUDAPP_SOURCE)/src/omega_blackwell_encoder.h src/sha256.h
