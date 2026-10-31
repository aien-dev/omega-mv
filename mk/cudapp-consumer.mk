# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Source identity is checked on every native entry, including stale-output builds.
ifndef OMEGA_CUDAPP_CONSUMER_MK
OMEGA_CUDAPP_CONSUMER_MK := 1
CUDAPP_DIR ?= ../CUDApp
CUDAPP_STAGE = $(OUT_DIR)/cudapp
CUDAPP_SOURCE = $(CUDAPP_STAGE)/source
CUDAPP_LIB = $(CUDAPP_STAGE)/build/libcudapp.a
CUDAPP_HEADER_FLAGS = -I"$(CUDAPP_SOURCE)/src" -I"$(CUDAPP_SOURCE)/m16" -I"$(CUDAPP_SOURCE)/nvrm" \
	-I"$(CUDAPP_SOURCE)/third_party/nvidia-open-580.173.02/src/common/sdk/nvidia/inc" \
	-I"$(CUDAPP_SOURCE)/third_party/nvidia-open-580.173.02/kernel-open/common/inc" \
	-I"$(CUDAPP_SOURCE)/third_party/nvidia-open-580.173.02/kernel-open/nvidia-uvm" \
	-I"$(CUDAPP_SOURCE)/third_party/nvidia-open-580.173.02/src/nvidia/arch/nvalloc/unix/include" \
	-DOMEGA_CUDAPP_SOURCE='"$(abspath $(CUDAPP_SOURCE))"'
.PHONY: check-cudapp test-cudapp-stage-hermetic
check-cudapp:
	sh tools/cudapp-stage.sh cudapp.lock "$(CUDAPP_DIR)" "$(CUDAPP_STAGE)"

test-cudapp-stage-hermetic:
	sh tests/cudapp_stage_hermetic.sh "$(CUDAPP_DIR)"

$(CUDAPP_LIB): cudapp.lock | check-cudapp
	@test -f "$@"

# Required canonical source/header prerequisites may not exist until staging.
# A missing member is an error; it is never silently dropped from a read set.
$(CUDAPP_SOURCE)/%: | check-cudapp
	@test -f "$@"

$(OUT_DIR)/omegatool.o $(OUT_DIR)/omega_blackwell_gates.o $(OUT_DIR)/omega_world_gates.o \
$(OUT_DIR)/omega_vector.o $(OUT_DIR)/omega_accelerator.o $(OUT_DIR)/omega_accelerator_world.o \
$(OUT_DIR)/rx_resident_gpu.o: cudapp.lock $(CUDAPP_SOURCE)/src/cudapp_native.h | check-cudapp

endif
