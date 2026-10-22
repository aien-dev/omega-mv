# RV64 QEMU prerequisite, not an Omega execution backend

Omega #630 requires a qualified RV64GC lowering plus interpreter/ARM/x86 comparisons. **None of that backend qualification is claimed by this prerequisite.**

The Spark did not have `qemu-riscv64` or `qemu-system-riscv64` on PATH. To remove the user-mode emulator prerequisite without changing installed packages, services or other workers, the official Ubuntu arm64 package `qemu-user=1:8.2.2+ds-0ubuntu1.18` was downloaded and extracted into an owned user directory. Its archive hash is checked against apt's indexed SHA-256 and retained in `package.json`; actual QEMU version/binary identity are in `receipt.json`.

## Actual observations

`tests/universal/rv64_qemu_preflight.py` constructs minimal Linux RV64I ELF images solely as development controls. It encodes `addi a0, zero, exit_code`, `addi a7, zero, 93`, and `ecall` under an RX load segment, then invokes the independently implemented QEMU decoder with `-cpu rv64`.

- The exit-42 image actually exits 42.
- The distinct exit-43 image actually exits 43.
- A changed ELF architecture identifier actually refuses with exit 255 and `Invalid ELF image for this architecture`.

All execution is **EMULATED**. This establishes that the isolated tool executes the tested RV64I/Linux ABI control and rejects that wrong-ISA control. It is not a complete ISA, RV64GC extensions, ABI, machine-profile, Omega-generated code, memory/trap, QEMU system boot or physical RISC-V qualification. The fixture emitter is a test-only Python script, not a production Omega backend or bootstrap compiler.

## Reproduce

On AArch64 Ubuntu with apt metadata for the named package:

```
mkdir -p build/qemu-prerequisite/downloads
cd build/qemu-prerequisite/downloads
apt-get download qemu-user=1:8.2.2+ds-0ubuntu1.18
# Require the archive SHA-256 to equal the indexed package hash and recorded package.json.
dpkg-deb -x qemu-user_*.deb ../root
cd ../../..
python3 tests/universal/rv64_qemu_preflight.py \
  --qemu "$PWD/build/qemu-prerequisite/root/usr/bin/qemu-riscv64" \
  --out "$PWD/build/qemu-prerequisite/fresh-run"
```

Use an output directory that does not already exist. Existing generated attempts are never overwritten. Source, complete image bytes, exact commands/exits, tool/package hashes and limits are recorded. No sudo, package installation, system binfmt registration, firmware action or device operation is needed. Other host distributions require separately resolved dependency/version records, not an assumption this extracted package works everywhere.

System-mode QEMU and a real RISC-V board remain unavailable/unqualified here. Native Omega RV64 lowering remains the future #630 work after #625/#621/#623/#628 prerequisites. The existing #621 worker and GPU/firmware/Interplane owners were left untouched.
