#!/bin/sh
# Omega's download/bootstrap boundary. Compilation stays seed-native Omega.
set -eu
umask 077
fail() { printf 'omega: %s\n' "$2" >&2; exit "$1"; }
prefix=${HOME:?HOME must be set}/.local
while [ "$#" -gt 0 ]; do
    case $1 in
        --prefix) [ "$#" -ge 2 ] || fail 64 'missing --prefix directory'; prefix=$2; shift 2;;
        *) fail 64 'usage: install.sh [--prefix absolute-directory]';;
    esac
done
case $prefix in /*) ;; *) fail 64 'prefix must be absolute';; esac
case $prefix in *'
'*) fail 64 'newline in prefix is unsupported';; esac
[ "$(uname -s)/$(uname -m)" = Linux/aarch64 ] || fail 69 'this experimental download supports Linux ARM64 only'
for tool in curl sha256sum tar gzip readelf mktemp; do
    command -v "$tool" >/dev/null 2>&1 || fail 69 "required host utility missing: $tool"
done
# Immutable release URL and SHA are pinned together by the publication owner.
expected_sha='eaf37ff32d17166de403372069b52aeb98b9523ce154b8f065ae9e4c161c8480'
url=${OMEGA_DOWNLOAD_URL:-https://raw.githubusercontent.com/aien-dev/omega-mv/fd4c12224773253c063f732c7d9e64a5395104e1/releases/experimental/native-developer/omega-developer-linux-arm64-source.tar.gz}
# Optional mirrors never change the required digest. HTTP is loopback-only.
case $url in
    https://*|http://127.0.0.1:*/*|http://localhost:*/*) ;;
    *) fail 64 'download mirror must use HTTPS (or loopback HTTP for local verification)';;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/omega-download.XXXXXXXX") || fail 74 'cannot create private download directory'
cleanup() {
    case $work in ''|/) return;; esac
    # work was freshly created above, is never a caller-supplied directory.
    rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
printf 'Downloading pinned Omega developer source (Linux ARM64)...\n'
curl -fLsS --connect-timeout 10 --max-time 120 --proto '=http,https' --proto-redir '=https' -o "$work/source.tar.gz" "$url" || fail 74 'download failed; nothing installed'
(cd "$work" && printf '%s  source.tar.gz\n' "$expected_sha" | sha256sum --status -c -) || fail 65 'download checksum mismatch; nothing installed'
tar -xzf "$work/source.tar.gz" -C "$work" || fail 65 'verified source extraction failed'
[ -f "$work/omega/tools/install.sh" ] || fail 65 'verified package is missing its installer'
"$work/omega/tools/install.sh" --offline --prefix "$prefix"
printf '\nOmega is installed. Try:\n  "%s/bin/omega" help\n  "%s/bin/omega" run hello.om\n' "$prefix" "$prefix"
case :${PATH-}: in
    *:"$prefix/bin":*) ;;
    *) printf '\nYour shell PATH is unchanged. Add %s/bin to PATH, or use the full command path above.\n' "$prefix";;
esac
