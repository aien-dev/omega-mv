#!/bin/sh
# Native seed compiler, native Omega fixtures, shell orchestration only.
set -eu
cd "$(dirname "$0")/../.."
root=$PWD
mkdir -p build/seed/host
stage=$(mktemp -d "$root/build/seed/host/run.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
compile() {
    "$root/seed/out/omc" < "$root/seed/host/tests/$1.om" > "$stage/$1"
    chmod +x "$stage/$1"
}
compile files
(cd "$stage"; ./files)
test ! -e "$stage/probe.bin"
printf 'PASS native files, errno, exclusive creation, permissions, seek, sync and monotonic time\n'
compile args
"$stage/args" '' 'two words' 'π'
printf 'PASS native argument access, empty arguments, UTF-8 bytes and short-buffer refusal\n'
compile network
(cd "$stage"; timeout 10 ./network)
test ! -e "$stage/native.sock"
printf 'PASS native socket exchange, poll timeout, fork, accept and child reaping\n'
(cd "$stage"; timeout 10 ./network tcp)
printf 'PASS native TCP loopback exchange on an OS-assigned port\n'
cp "$stage/args" "$stage/exec-child"
compile process
(cd "$stage"; timeout 10 ./process)
printf 'PASS native exec with exact arguments, exec failure, pipes, descriptor duplication, sleep, kill and wait\n'
compile directories
(cd "$stage"; ./directories)
test ! -e "$stage/adir"
test ! -e "$stage/before"
test ! -e "$stage/after"
printf 'PASS native directory enumeration, file metadata and rename without overwriting\n'
compile errors
"$stage/errors"
printf 'PASS native syscall errors and nested expression register preservation\n'
compile location
(cd "$stage"; ./location)
test ! -e "$stage/regular-file"
printf 'PASS native cwd and readlnk offsets, counts, truncation, errno and private path copy\n'
compile sockopt
(cd "$stage"; timeout 10 ./sockopt)
printf 'PASS native socket options with independent bind behavior, readonly input and errno\n'
compile structfile
(cd "$stage"; ./structfile)
test ! -e "$stage/structfile.tmp"
printf 'PASS struct values and native file operations in one program\n'
compile random
"$stage/random"
printf 'PASS native random: exact count, non-zero, two calls differ, invalid flags refused\n'
compile environment
(cd "$stage"; env OMTEST=hello ./environment)
printf 'PASS native environment: count, entry bytes, short-buffer refusal\n'
compile chdir
(cd "$stage"; ./chdir)
test -f "$stage/sub/inside"
printf 'PASS native chdir: relative file created in the new directory; missing path refused\n'
compile environment_trap
set +e
(cd "$stage"; ./environment_trap)
erc=$?
set -e
[ "$erc" = 103 ] || { printf 'FAIL: environment_trap exit %s, wanted 103\n' "$erc" >&2; exit 1; }
printf 'PASS native environment: index at the count traps 3\n'
