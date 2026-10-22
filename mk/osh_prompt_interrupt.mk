# #344 prompt-read lost-wakeup regression. No shipped test hooks.
OSH_INTERRUPT_SRCS = $(filter-out src/osh/host/osh_main.c,$(OSH_BIN_SRCS)) tests/osh/host/test_prompt_interrupt.c
OSH_INTERRUPT_FLAGS = $(OSH_XFLAGS) -Wl,--wrap=read -Wl,--wrap=__read_chk -Wl,--wrap=fflush -Wl,--wrap=pselect -Wl,--wrap=sigprocmask
$(OSH_DIR)/test_prompt_interrupt: $(OSH_INTERRUPT_SRCS) $(OSH_XHDRS) src/osh/host/osh_shell.h
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_INTERRUPT_FLAGS) -O2 -o $@ $(OSH_INTERRUPT_SRCS)
$(OSH_DIR)/test_prompt_interrupt_asan: $(OSH_INTERRUPT_SRCS) $(OSH_XHDRS) src/osh/host/osh_shell.h
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_INTERRUPT_FLAGS) $(OSC_ASAN) -o $@ $(OSH_INTERRUPT_SRCS)
.PHONY: test-osh-prompt-interrupt
test-osh-prompt-interrupt: $(OSH_DIR)/test_prompt_interrupt $(OSH_DIR)/test_prompt_interrupt_asan
	$(OSH_DIR)/test_prompt_interrupt > $(OSH_DIR)/prompt-interrupt.out; rc=$$?; tail -1 $(OSH_DIR)/prompt-interrupt.out; test $$rc -eq 0
	$(OSH_DIR)/test_prompt_interrupt_asan > $(OSH_DIR)/prompt-interrupt-asan.out; rc=$$?; tail -1 $(OSH_DIR)/prompt-interrupt-asan.out; test $$rc -eq 0
$(OSH_DIR)/test_prompt_pty: tests/osh/host/test_prompt_pty.c
	@mkdir -p $(OSH_DIR)
	$(CC) -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -Werror -O2 -o $@ $< -lutil
.PHONY: test-osh-prompt-pty
test-osh-prompt-pty: $(OSH_DIR)/osh $(OSH_DIR)/test_prompt_pty
	$(OSH_DIR)/test_prompt_pty $(abspath $(OSH_DIR)/osh) > $(OSH_DIR)/prompt-pty.out; rc=$$?; tail -1 $(OSH_DIR)/prompt-pty.out; test $$rc -eq 0
