#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
root=$PWD
mkdir -p build/seed/host
stage=$(mktemp -d "$root/build/seed/host/negative.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
count=0
runtime() {
    name=$1
    cat > "$stage/$name.om"
    seed/out/omc < "$stage/$name.om" > "$stage/$name"
    chmod +x "$stage/$name"
    code=0
    (cd "$stage"; "./$name") > "$stage/out" || code=$?
    test "$code" = 103 && test ! -s "$stage/out" || {
        echo "Wrong refusal: $name (exit $code)" >&2; exit 1;
    }
    count=$((count + 1))
}
compile_error() {
    name=$1
    want=$2
    code=0
    cat > "$stage/$name.om"
    seed/out/omc < "$stage/$name.om" > "$stage/out" 2> "$stage/err" || code=$?
    test "$code" = "$want" && test ! -s "$stage/out" || {
        echo "Wrong compile refusal: $name (exit $code)" >&2; exit 1;
    }
    count=$((count + 1))
}
runtime path-overflow <<'EOF'
text path "must-not-exist"
fn main to int
r = fopen path 18446744073709551615 1 194 384
ret 1
end
EOF
runtime path-zero <<'EOF'
global path uchar 3
fn main to int
put path 0 97
put path 2 98
r = fopen path 0 3 194 384
ret 1
end
EOF
runtime path-length <<'EOF'
global path uchar 4096
fn main to int
r = fopen path 0 len path 194 384
ret 1
end
EOF
runtime stat-short <<'EOF'
global buf uchar 127
fn main to int
r = fstat 0 buf 0 len buf
ret 1
end
EOF
runtime pipe-short <<'EOF'
global buf uchar 7
fn main to int
r = pipefds buf 0 len buf 0
ret 1
end
EOF
runtime packet-empty <<'EOF'
global buf uchar 2
fn main to int
r = execprog buf 0 len buf
ret 1
end
EOF
runtime packet-count <<'EOF'
global buf uchar 4
fn main to int
put buf 0 65
r = execprog buf 0 len buf
ret 1
end
EOF
runtime packet-truncated <<'EOF'
global buf uchar 5
fn main to int
put buf 0 1
put buf 2 2
put buf 4 97
r = execprog buf 0 len buf
ret 1
end
EOF
runtime packet-zero <<'EOF'
global buf uchar 6
fn main to int
put buf 0 1
put buf 2 2
put buf 4 97
r = execprog buf 0 len buf
ret 1
end
EOF
runtime packet-extra <<'EOF'
global buf uchar 6
fn main to int
put buf 0 1
put buf 2 1
put buf 4 97
r = execprog buf 0 len buf
ret 1
end
EOF
runtime rename-count <<'EOF'
global buf uchar 5
fn main to int
put buf 0 1
put buf 2 1
put buf 4 97
r = renames buf 0 len buf 0
ret 1
end
EOF
runtime arg-index <<'EOF'
global buf uchar 4
fn main to int
r = argread ulong argcnt buf 0 len buf
ret 1
end
EOF
runtime negative-sleep <<'EOF'
fn main to int
r = sleepns neg long 1
ret 1
end
EOF
runtime poll-truncation <<'EOF'
fn main to int
r = waitio 0 65536 0
ret 1
end
EOF
compile_error read-only 20 <<'EOF'
text buf "abcd"
fn main to int
r = recv 0 buf 0 len buf 0
ret 0
end
EOF
compile_error array-type 7 <<'EOF'
global buf ulong 32
fn main to int
r = fstat 0 buf 0 128
ret 0
end
EOF
compile_error descriptor-type 7 <<'EOF'
fn main to int
r = fclose ulong 0
ret 0
end
EOF
compile_error function-mode 9 <<'EOF'
fn probe to long
ret monons
end
EOF
test ! -e "$stage/must-not-exist"
test ! -e "$stage/a"
printf 'PASS %s bounds, packet, mutability, type and function-mode negative controls\n' "$count"
