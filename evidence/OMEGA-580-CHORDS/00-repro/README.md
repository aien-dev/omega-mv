# omega-mv#580 roll test failure: diagnosis (2026-10-19)

Failure (10:11 and 10:15, ~/yardsticks/aien-model/M1/roll_cmdlist.txt): gpu_cmdlist_test chip, 5 of 5 checks failed,
first "baseline launch: marker wait timed out (latched)", then everything after it refused by the latch.

| run | binary | physics objects | load1 | result |
|---|---|---|---|---|
| chip.log (11:32) | fresh build in ~/workspace/omega-580, PHYSICS_DIR=physics-6d7cf0d4 | 6d7cf0d4 | 11.9 | 31 checks, 0 failed, PASS |
| chip-m1-binary.log (11:43) | the exact m1-passport binary of 10:08:37 | nvrm.o, m16_native.o from ../physics (e95e3ed) | 30.3 | 5 of 5 FAIL, same lines as 10:11 |

binaries.sha256: the two binaries differ. Object by object, only build/nvrm.o and build/m16_native.o differ; their .d files
in ~/workspace/m1-passport/build name ../physics/nvrm/nvrm.c (Makefile default PHYSICS_DIR ?= ../physics), compiled 09:30.
~/workspace/physics is at e95e3ed, which does NOT contain 6d7cf0d4 ("GB10 CPU-rewritten GPU-read memory must be
GPU-uncached (GPFIFO/USERD, pushbuffer)"): there the GPFIFO ring and USERD are GPU-cached, so the chip never sees the new
GPPut, the GPU stays idle (sensors 10:15:33 to 10:15:53: gpu_util 0 while the test waited) and the 20 s marker wait times out.

Root cause: make tracks mtimes only. The 10:08 `make PHYSICS_DIR=physics-6d7cf0d4` kept nvrm.o and m16_native.o because the
pinned sources (Oct 3) are older than the stale objects (09:30). check-physics-lock checked the directory, not the objects.
Not load, not the persistent-device change, not the cmdlist code. "alloc without open device" was the session refusing after
the latch with the wrong text.

Fix (01-fix/): build/physics.stamp (hash of physics HEAD and nvrm/m16 sources) is a prerequisite of every object, and
tests/test_physics_stamp.sh fails on the old Makefile (physics_stamp_RED.log) and passes on the new (physics_stamp_GREEN.log).
The latched alloc now says "process latched" (tests/test_omega_gpu_session_platform.c asserts it, 13/13, 6 of 6 mutants).
