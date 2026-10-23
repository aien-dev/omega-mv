#!/bin/sh
# Native Apple hardware behavior oracle. No compiler or target construction.
set -eu
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
    echo 'NOT_RUN: native Darwin arm64 hardware required' >&2
    exit 77
fi
for tool in /usr/bin/codesign /usr/bin/otool /usr/bin/shasum /usr/bin/perl; do
    if [ ! -x "$tool" ]; then
        echo "NOT_RUN: missing host oracle $tool" >&2
        exit 77
    fi
done
if [ ! -f SHA256SUMS ]; then
    echo 'Run from the exported mac-replay/final directory.' >&2
    exit 64
fi
/usr/bin/shasum -a 256 -c SHA256SUMS
# Fresh receipt directory: preserve every prior attempt and its signed artifacts.
receipt=$(mktemp -d native-receipt.XXXXXX)
printf '%s\n' "$receipt" > last-receipt-path.txt
uname -a > "$receipt/host.txt"
sw_vers >> "$receipt/host.txt"
/usr/bin/shasum -a 256 /usr/bin/codesign /usr/bin/otool /usr/bin/perl > "$receipt/host-tools.sha256"
/usr/bin/codesign --version > "$receipt/codesign-version.txt" 2>&1 || true
ulimit -c 0
count=0
sign() {
    name=$1
    cp "$name.macho" "$receipt/$name.signed"
    chmod 755 "$receipt/$name.signed"
    if ! /usr/bin/codesign --force --sign - "$receipt/$name.signed" > "$receipt/$name.sign.stdout" 2> "$receipt/$name.sign.stderr"; then
        echo "FAIL $name: signing (see $receipt)" >&2
        exit 1
    fi
    if ! /usr/bin/codesign --verify --strict --verbose=4 "$receipt/$name.signed" > "$receipt/$name.verify.stdout" 2> "$receipt/$name.verify.stderr"; then
        echo "FAIL $name: strict signature verification (see $receipt)" >&2
        exit 1
    fi
    /usr/bin/otool -hv -l "$receipt/$name.signed" > "$receipt/$name.otool.txt"
    /usr/bin/shasum -a 256 "$receipt/$name.signed" >> "$receipt/signed.sha256"
}
run() {
    name=$1
    set +e
    # Perl supplies only a per-child alarm; program instructions run natively.
    /usr/bin/perl -e 'alarm 5; exec @ARGV or die "exec: $!\n"' "./$receipt/$name.signed" < /dev/null > "$receipt/$name.stdout" 2> "$receipt/$name.stderr"
    status=$?
    set -e
    printf '%s\n' "$status" > "$receipt/$name.exit"
}
for row in return42:42 sum:45 branch:23 overflow:101 bitwise:58 divide:25 divzero:102 shift:24 hello:0 largest:203; do
    name=${row%:*}
    expected=${row#*:}
    sign "$name"
    run "$name"
    if [ "$status" -ne "$expected" ] || [ -s "$receipt/$name.stderr" ]; then
        echo "FAIL $name: exit=$status expected=$expected (see $receipt)" >&2
        exit 1
    fi
    if [ "$name" = hello ]; then
        printf 'hello omega mac\n' > "$receipt/hello.expected"
        cmp "$receipt/hello.expected" "$receipt/hello.stdout"
    else
        test ! -s "$receipt/$name.stdout"
    fi
    count=$((count+1))
    printf 'PASS native %s exit %s\n' "$name" "$status" | tee -a "$receipt/gate.log"
done
set +e
/usr/bin/perl -e 'alarm 5; exec @ARGV or die "exec: $!\n"' "./$receipt/hello.signed" < /dev/null 1>&- 2> "$receipt/hello.closed.stderr"
status=$?
set -e
printf '%s\n' "$status" > "$receipt/hello.closed.exit"
test "$status" -eq 74
test ! -s "$receipt/hello.closed.stderr"
count=$((count+1))
printf 'PASS native hello closed stdout exit 74\n' | tee -a "$receipt/gate.log"
# This control changes the ADR text address by four bytes. It must still be a
# signed, loadable native ARM64 executable and finish exit0, but write wrong bytes.
# Do not count a compile, signing, loader or signal failure as a caught mutant.
sign control-hello-address
run control-hello-address
test "$status" -eq 0
test ! -s "$receipt/control-hello-address.stderr"
test -s "$receipt/control-hello-address.stdout"
test "$(wc -c < "$receipt/control-hello-address.stdout" | tr -d ' ')" -eq 16
if cmp -s "$receipt/hello.expected" "$receipt/control-hello-address.stdout"; then
    echo 'FAIL: native compiled address control escaped the behavior gate' >&2
    exit 1
fi
count=$((count+1))
printf 'PASS native compiled hello-address control caught\n' | tee -a "$receipt/gate.log"
printf 'PASS %s native behavior cases. Bounded L615 milestone only; issue634 OPEN; independent review pending.\n' "$count" | tee -a "$receipt/gate.log"
