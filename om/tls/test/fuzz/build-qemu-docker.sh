#!/bin/sh
# Builds AFL++ v4.40c qemu_mode (afl-qemu-trace, CPU_TARGET=aarch64) inside ubuntu:24.04 because the
# host lacks glib-2.0 development headers. Host tooling only; output links the host's libglib-2.0.so.0.
set -eu
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential git python3 python3-venv ninja-build pkg-config libglib2.0-dev libpixman-1-dev flex bison wget ca-certificates meson >/dev/null
git config --global --add safe.directory '*'
cd /aflpp/qemu_mode
sed -i 's/make -j$(nproc)/make -j2/' build_qemu_support.sh
CPU_TARGET=aarch64 ./build_qemu_support.sh
