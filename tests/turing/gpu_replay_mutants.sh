#!/bin/sh
# Host-only behavioral mutants of admission. Mutants must compile, then fail.
set -eu
out=${1:?private output directory required}
mkdir -p "$out"
killed=0
mutant() {
    name=$1; expression=$2
    sed "$expression" tools/turing_gpu_replay.c > "$out/$name.c"
    if cmp -s tools/turing_gpu_replay.c "$out/$name.c"; then
        echo "unchanged mutant: $name" >&2; exit 1
    fi
    make OUT_DIR="$out/$name-build" \
        TGR_SOURCES="tests/turing/test_gpu_replay.c $out/$name.c" \
        "$out/$name-build/tests-turing/gpu_replay" > "$out/$name-build.log" 2>&1
    if "$out/$name-build/tests-turing/gpu_replay" > "$out/$name-run.log" 2>&1; then
        echo "SURVIVED: $name" >&2; exit 1
    fi
    killed=$((killed+1)); echo "killed: $name"
}
mutant digest 's/if (memcmp(digest,c->receipt_sha256,32))/if (0)/'
mutant binding 's/if (!binding_equal(\&q->binding,\&o->binding))/if (0 \&\& !binding_equal(\&q->binding,\&o->binding))/'
mutant binary 's/if (memcmp(c->binary,o->binary,32))/if (0)/'
mutant freshness 's/q->now-o->measured_at>q->max_age/0/'
mutant registers 's/o->registers>q->max_registers/0/'
mutant cost 's/if (!o->samples/if (0 \&\& (!o->samples/;s/o->submit_host_ns>o->total_ns) return TGR_COST/o->submit_host_ns>o->total_ns)) return TGR_COST/'
mutant incumbent 's/if(!allowed\[0\])/if(0)/'
mutant tie 's/original\[0\]=q->incumbent;/original[0]=0;/;s/if(i!=q->incumbent)/if(i!=0)/'
test "$killed" = 8
echo "gpu replay mutants: $killed/8 killed (all compiled)"
