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
runtime cwd-overflow <<'OM'
global buf uchar 8
fn main to int
r = cwd buf 18446744073709551615 1
ret 1
end
OM
runtime cwd-capacity <<'OM'
global buf uchar 8
fn main to int
r = cwd buf 1 8
ret 1
end
OM
runtime readlnk-empty <<'OM'
text path "/proc/self/cwd"
global buf uchar 8
fn main to int
r = readlnk path 0 0 buf 0 8
ret 1
end
OM
runtime readlnk-zero <<'OM'
global path uchar 3
global buf uchar 8
fn main to int
put path 0 97
put path 2 98
r = readlnk path 0 3 buf 0 8
ret 1
end
OM
runtime readlnk-length <<'OM'
global path uchar 4096
global buf uchar 8
fn main to int
for i ulong len path
put path i 97
end
r = readlnk path 0 len path buf 0 8
ret 1
end
OM
runtime readlnk-path-bounds <<'OM'
text path "/proc/self/cwd"
global buf uchar 8
fn main to int
r = readlnk path 18446744073709551615 1 buf 0 8
ret 1
end
OM
runtime readlnk-output-bounds <<'OM'
text path "/proc/self/cwd"
global buf uchar 8
fn main to int
r = readlnk path 0 len path buf 1 8
ret 1
end
OM
runtime sockopt-bounds <<'OM'
text buf "abcd"
fn main to int
r = sockopt neg int 1 1 2 buf 1 4
ret 1
end
OM
compile_error cwd-readonly 20 <<'OM'
text buf "abcd"
fn main to int
r = cwd buf 0 4
ret 0
end
OM
compile_error cwd-view 20 <<'OM'
fn probe buf view uchar 4 to long
ret cwd buf 0 4
end
fn main to int
ret 0
end
OM
compile_error cwd-type 7 <<'OM'
global buf uint 8
fn main to int
r = cwd buf 0 8
ret 0
end
OM
compile_error cwd-offset-type 7 <<'OM'
global buf uchar 8
fn main to int
r = cwd buf int 0 8
ret 0
end
OM
compile_error readlnk-readonly 20 <<'OM'
text path "/proc/self/cwd"
text buf "abcd"
fn main to int
r = readlnk path 0 len path buf 0 4
ret 0
end
OM
compile_error readlnk-view 20 <<'OM'
text path "/proc/self/cwd"
fn probe buf view uchar 4 to long
ret readlnk path 0 len path buf 0 4
end
fn main to int
ret 0
end
OM
compile_error readlnk-path-type 7 <<'OM'
global path ulong 8
global buf uchar 8
fn main to int
r = readlnk path 0 8 buf 0 8
ret 0
end
OM
compile_error readlnk-output-type 7 <<'OM'
text path "/proc/self/cwd"
global buf ulong 8
fn main to int
r = readlnk path 0 len path buf 0 8
ret 0
end
OM
compile_error sockopt-fd-type 7 <<'OM'
text buf "abcd"
fn main to int
r = sockopt long 0 1 2 buf 0 4
ret 0
end
OM
compile_error sockopt-level-type 7 <<'OM'
text buf "abcd"
fn main to int
r = sockopt 0 long 1 2 buf 0 4
ret 0
end
OM
compile_error sockopt-option-type 7 <<'OM'
text buf "abcd"
fn main to int
r = sockopt 0 1 long 2 buf 0 4
ret 0
end
OM
compile_error sockopt-array-type 7 <<'OM'
global buf uint 4
fn main to int
r = sockopt 0 1 2 buf 0 4
ret 0
end
OM
compile_error cwd-function-mode 9 <<'OM'
fn probe buf edit uchar 4096 to long
ret cwd buf 0 4096
end
OM
compile_error readlnk-function-mode 9 <<'OM'
fn probe path view uchar 4 buf edit uchar 8 to long
ret readlnk path 0 4 buf 0 8
end
OM
compile_error sockopt-function-mode 9 <<'OM'
fn probe buf view uchar 4 to long
ret sockopt 0 1 2 buf 0 4
end
OM
compile_error flock-fd-type 7 <<'OM'
fn main to int
r = flock long 0 2
ret 0
end
OM
compile_error flock-op-type 7 <<'OM'
fn main to int
r = flock 0 ulong 2
ret 0
end
OM
compile_error flock-arity 4 <<'OM'
fn main to int
r = flock 0
ret 0
end
OM
compile_error flock-name 16 <<'OM'
fn main to int
flock = int 1
ret 0
end
OM
compile_error flock-function-mode 9 <<'OM'
fn probe fd int to long
ret flock fd 2
end
OM
printf 'PASS %s total host negative controls, including cwd, readlnk, sockopt and flock\n' "$count"
