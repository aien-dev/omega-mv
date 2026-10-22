#!/usr/bin/env bash
# External replay harness only. No production runtime or compiler changes.
set -euo pipefail
umask 077
out=${1:?usage: public-preview.sh existing-output-directory}
case $out in /*) ;; *) exit 64;; esac
[ -d "$out" ]
root=$(mktemp -d "$out/public-preview.XXXXXXXX")
export HOME="$root/home" TMPDIR="$root/tmp" LC_ALL=C
mkdir "$HOME" "$TMPDIR" "$root/project"
unset OMEGA_DOWNLOAD_URL
preview=https://raw.githubusercontent.com/aien-dev/omega-mv/fec281df6829b9b4efa069c02037c69e4e4ff679/releases/experimental/native-developer/install.sh
asset=https://raw.githubusercontent.com/aien-dev/omega-mv/fd4c12224773253c063f732c7d9e64a5395104e1/releases/experimental/native-developer/omega-developer-linux-arm64-source.tar.gz
printf 'Root: %s\nHost: %s\nPreview: %s\nPayload: %s\n' "$root" "$(uname -sm)" "$preview" "$asset"
printf 'profile sentinel\n' > "$HOME/.profile"
sha256sum "$HOME/.profile" > "$root/profile.sha256"
# Public URLs, no token, private staging access or payload mirror override.
curl -fsSL "$preview" -o "$root/install.sh"
printf '%s  %s\n' e4d6707b4023f8db9ffbd15fe603e8c67523603ba7d1c13640d6f75256cc1a36 "$root/install.sh" | sha256sum -c -
printf 'COMMAND: curl -fsSL %s | sh\n' "$preview"
set +e
curl -fsSL https://raw.githubusercontent.com/aien-dev/omega-mv/fec281df6829b9b4efa069c02037c69e4e4ff679/releases/experimental/native-developer/install.sh | sh
pipe_status=("${PIPESTATUS[@]}")
set -e
printf 'Literal public pipeline statuses: curl=%s sh=%s\n' "${pipe_status[0]}" "${pipe_status[1]}"
[ "${pipe_status[*]}" = '0 0' ]
[ -z "$(find "$TMPDIR" -mindepth 1 -print -quit)" ]
cat > "$root/project/hello.om" <<'OM'
text msg "PUBLIC-PREVIEW\n"
fn main to int
w = write 1 msg 0 len msg
ret 0
end
OM
printf 'PUBLIC-PREVIEW\n' > "$root/project/expected.txt"
cd "$root/project"
for name in omega om omg; do
    cmd="$HOME/.local/bin/$name"
    [ "$(readlink -f "$cmd")" = "$(readlink -f "$HOME/.local/bin/omega")" ]
    printf 'COMMAND: %s check hello.om\n' "$cmd"
    "$cmd" check hello.om
    printf 'COMMAND: %s build hello.om -o hello-%s\n' "$cmd" "$name"
    "$cmd" build hello.om -o "hello-$name"
    readelf -h "hello-$name" > "hello-$name.elf.txt"
    grep -q 'Machine:.*AArch64' "hello-$name.elf.txt"
    "./hello-$name" > "built-$name.txt"
    cmp expected.txt "built-$name.txt"
    printf 'COMMAND: %s run hello.om\n' "$cmd"
    "$cmd" run hello.om > "run-$name.txt"
    cmp expected.txt "run-$name.txt"
    cat "run-$name.txt"
    printf 'PASS %s check/build/native-execution/run\n' "$name"
done
sha256sum "$HOME/.local/lib/omega-native/omc" "$HOME/.local/lib/omega-native/bridge" "$HOME/.local/lib/omega-native/math" hello-omega hello-om hello-omg > "$root/binaries.sha256"
printf 'COMMAND: omega uninstall\n'
"$HOME/.local/bin/omega" uninstall
for name in omega om omg; do [ ! -e "$HOME/.local/bin/$name" ] && [ ! -L "$HOME/.local/bin/$name" ]; done
[ ! -e "$HOME/.local/lib/omega-native" ]
printf 'PASS uninstall all aliases and owned payload\n'
# Same literal public bootstrap for refusal cases; retain both curl/sh statuses.
expect_bootstrap() {
    local expected=$1
    shift
    printf 'COMMAND: curl -fsSL %s | sh -s --' "$preview"
    printf ' %q' "$@"
    printf '\n'
    set +e
    curl -fsSL "$preview" | sh -s -- "$@"
    local statuses=("${PIPESTATUS[@]}")
    set -e
    printf 'Refusal pipeline statuses: curl=%s sh=%s expected=%s\n' "${statuses[0]}" "${statuses[1]}" "$expected"
    [ "${statuses[0]}" = 0 ] && [ "${statuses[1]}" = "$expected" ]
    [ -z "$(find "$TMPDIR" -mindepth 1 -print -quit)" ]
}
for name in omega om omg; do
    prefix="$HOME/collision-$name"
    mkdir -p "$prefix/bin"
    printf 'unrelated-%s\n' "$name" > "$prefix/bin/$name"
    cp "$prefix/bin/$name" "$root/sentinel-$name"
    expect_bootstrap 73 --prefix "$prefix"
    cmp "$prefix/bin/$name" "$root/sentinel-$name"
    [ "$(find "$prefix/bin" -mindepth 1 | wc -l)" -eq 1 ]
    [ ! -e "$prefix/lib" ]
    printf 'PASS unrelated %s preserved\n' "$name"
done
# Valid HTTPS transport, wrong bytes: checksum refusal must precede extraction.
export OMEGA_DOWNLOAD_URL=https://raw.githubusercontent.com/aien-dev/omega-mv/fd4c12224773253c063f732c7d9e64a5395104e1/releases/experimental/native-developer/SOURCE.json
printf 'Checksum negative-control URL: %s\n' "$OMEGA_DOWNLOAD_URL"
expect_bootstrap 65 --prefix "$HOME/hash-refusal"
[ ! -e "$HOME/hash-refusal" ]
printf 'PASS checksum mismatch refused before installation\n'
unset OMEGA_DOWNLOAD_URL
expect_bootstrap 64 --unknown
sha256sum -c "$root/profile.sha256"
[ -z "$(find "$TMPDIR" -mindepth 1 -print -quit)" ]
printf 'PASS invalid arguments; unchanged profile; empty TMPDIR\n'
printf 'PASS PUBLIC PREVIEW: 3 alias project paths, uninstall, 3 collisions, hash refusal, argument refusal\n'
