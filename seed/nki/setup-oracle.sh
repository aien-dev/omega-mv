#!/bin/sh
# Prepare the NKI simulator oracle for seed/nki/gate-irnki.sh in DIR (user files only, no
# root, no account). Interop oracle, not Omega-native; nothing here generates kernels.
# The nki wheel is published for x86_64 only, so on AArch64 it runs as an x86_64 CPython
# under qemu-user with an x86_64 glibc sysroot. Every download is pinned by sha256.
# Host tools used: curl, sha256sum, tar, dpkg-deb, unzip.
#   seed/nki/setup-oracle.sh DIR      then: NKI_ORACLE_DIR=DIR seed/nki/gate-irnki.sh
set -eu
[ $# -eq 1 ] || { echo 'usage: setup-oracle.sh DIR' >&2; exit 2; }
D=$1
mkdir -p "$D/dl"
D=$(cd "$D" && pwd)
U=http://archive.ubuntu.com/ubuntu/pool/main
PY=https://files.pythonhosted.org/packages
# sha256  file  url   (licences: README.md, "Oracle stack")
cat > "$D/dl/pins" <<EOF
6a79e922e50006b601f29e688407659da1c2b86ebcd4e9c7254cf234be8b89de cpython.tar.gz https://github.com/astral-sh/python-build-standalone/releases/download/20261026/cpython-3.12.16%2B20261026-x86_64-unknown-linux-gnu-install_only.tar.gz
c73711af02b97cd5e2667e735d9c06890c57165517110ca4e646c95a7083158d qemu-user.deb http://deb.debian.org/debian/pool/main/q/qemu/qemu-user_10.0.13+ds-0+deb13u1_arm64.deb
ff5557d99b51f761c4b7c92368b9cc45565eda17df9bf9eb4b134d09825008be libc6.deb $U/g/glibc/libc6_2.39-0ubuntu8.9_amd64.deb
aa7fadbe33b78bcf99885318040601c550c208929565b179891d9a3cc2aa68cd libgcc-s1.deb $U/g/gcc-14/libgcc-s1_14.2.0-4ubuntu2~24.04.1_amd64.deb
a51f8de7829211db961a31f02158058ad1a95f92ac6d0a5dff6350e2821c54c0 libstdcxx6.deb $U/g/gcc-14/libstdc++6_14.2.0-4ubuntu2~24.04.1_amd64.deb
376249c8de139568221dbb242318cc8cb8f02299e3a62b7432948e1754fbddf9 zlib1g.deb $U/z/zlib/zlib1g_1.3.dfsg-3.1ubuntu2.3_amd64.deb
fd83c01228a688733f1ded5201c678f0c53ecc1006ffbc404db9f7a899ac6249 numpy.whl $PY/8c/3d/1e1db36cfd41f895d266b103df00ca5b3cbe965184df824dec5c08c6b803/numpy-2.2.6-cp312-cp312-manylinux_2_17_x86_64.manylinux2014_x86_64.whl
1b255acada256d1fa8c35ed07b5f6d18bc21d1556f842fbc2d5718aea2cd9e55 ml_dtypes.whl $PY/d8/a9/b98b86426c24900b0c754aad006dce2863df7ce0bb2bcc2c02f9cc7e8489/ml_dtypes-0.5.3-cp312-cp312-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl
f0fa19c6845758ab08074a0cfa8b7aecb71c999ca73d62883bc25cc018c4e548 typing_extensions.whl $PY/18/67/36e9267722cc04a6b9f15c7f3441c2363321a3ea07da7ae0c0707beb2a9c/typing_extensions-4.15.0-py3-none-any.whl
03543beda64b5a74e88a90320a01bdb4b1c5865f50a14b2f17e96d4c596f0c4e nki.whl https://pip.repos.neuron.amazonaws.com/nki/nki-0.7.0%2B37713544100.gfede04bb-cp312-cp312-linux_x86_64.whl
EOF
while read -r sum file url; do
    f=$D/dl/$file
    [ -f "$f" ] && [ "$(sha256sum "$f" | cut -d ' ' -f 1)" = "$sum" ] && continue
    curl -fsSL -o "$f.part" "$url"
    [ "$(sha256sum "$f.part" | cut -d ' ' -f 1)" = "$sum" ] || { echo "setup-oracle: $file sha256 differs from pin" >&2; exit 1; }
    mv "$f.part" "$f"
done < "$D/dl/pins"
rm -rf "$D/python" "$D/qemu" "$D/sysroot" "$D/site"
tar -xzf "$D/dl/cpython.tar.gz" -C "$D"
dpkg-deb -x "$D/dl/qemu-user.deb" "$D/qemu"
mkdir -p "$D/sysroot" "$D/site"
for p in libc6 libgcc-s1 libstdcxx6 zlib1g; do dpkg-deb -x "$D/dl/$p.deb" "$D/sysroot"; done
[ -e "$D/sysroot/lib64" ] || ln -s usr/lib64 "$D/sysroot/lib64"
[ -e "$D/sysroot/lib" ] || ln -s usr/lib "$D/sysroot/lib"
for w in numpy ml_dtypes typing_extensions nki; do unzip -q -o "$D/dl/$w.whl" -d "$D/site"; done
echo "setup-oracle: ready; run NKI_ORACLE_DIR=$D seed/nki/gate-irnki.sh"
