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
compile trunc
(cd "$stage"; ./trunc)
printf 'PASS native ftrunc: write, shorten, rewind, read 4 then EOF; closed fd refused\n'
compile stat
(cd "$stage"; ./stat)
printf 'PASS native stat: size field, missing path errno, 128-byte structure\n'
compile stat_short
set +e
(cd "$stage"; ./stat_short)
src=$?
set -e
[ "$src" = 103 ] || { printf 'FAIL: stat short output exit %s, wanted 103\n' "$src" >&2; exit 1; }
printf 'PASS native stat: short output traps 3 before the kernel\n'
compile links
(cd "$stage"; ./links)
printf 'PASS native link/symlnk: hard link nlink 2, symlink target, unlink\n'
compile links_short
set +e
(cd "$stage"; ./links_short)
lrc=$?
set -e
[ "$lrc" = 103 ] || { printf 'FAIL: one-record link list exit %s, wanted 103\n' "$lrc" >&2; exit 1; }
printf 'PASS native link: one-record packed list traps 3\n'
compile chmod
(cd "$stage"; ./chmod)
printf 'PASS native chmod: 0600 mode in st_mode; missing path errno\n'
compile environment_trap
set +e
(cd "$stage"; ./environment_trap)
erc=$?
set -e
[ "$erc" = 103 ] || { printf 'FAIL: environment_trap exit %s, wanted 103\n' "$erc" >&2; exit 1; }
printf 'PASS native environment: index at the count traps 3\n'
