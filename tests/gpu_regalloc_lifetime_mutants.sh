#!/bin/bash
# Compile real allocator mutations in private outputs. Build errors/timeouts are failures.
set -euo pipefail
d=${1:?output directory}
mkdir -p "$d"
cc=${CC:-cc}
for mutant in no-reuse no-reuse-pair no-reuse-quad premature-exhaustion false-peak misalignment out-of-range first-def last-use width input-count; do
 MUTANT=$mutant perl -0777 -pe '
  my $n;
  if ($ENV{MUTANT} eq "no-reuse") {
   $n=s/gpr_busy\[freed \+ k\] = false;/(void)freed; \/\* L520 no-reuse mutant *\//g;
  } elsif ($ENV{MUTANT} eq "no-reuse-pair") {
   $n=s/gpr_busy\[freed \+ k\] = false;/if (bsize != 2) gpr_busy[freed + k] = false;/g;
  } elsif ($ENV{MUTANT} eq "no-reuse-quad") {
   $n=s/gpr_busy\[freed \+ k\] = false;/if (bsize != 4) gpr_busy[freed + k] = false;/g;
  } elsif ($ENV{MUTANT} eq "premature-exhaustion") {
   $n=s/p < BW_PHYS_GPR_MAX; p\+\+/p < BW_PHYS_GPR_MAX - 1; p++/g;
  } elsif ($ENV{MUTANT} eq "false-peak") {
   $n=s/ra->peak_gpr_usage = \(uint32_t\)\(max_phys_used \+ 1\);/ra->peak_gpr_usage = (uint32_t)(max_phys_used + 2);/g;
  } else {
   my %injection = (
    "misalignment" => "ra->vreg_to_phys[0]++; ra->intervals[0].phys_reg++;",
    "out-of-range" => "ra->vreg_to_phys[0] = BW_PHYS_GPR_MAX; ra->intervals[0].phys_reg = BW_PHYS_GPR_MAX;",
    "first-def" => "ra->intervals[0].first_def++;",
    "last-use" => "ra->intervals[0].last_use++;",
    "width" => "ra->intervals[0].bundle_size *= 2;",
    "input-count" => "ra->num_vregs--;"
   );
   my $inject = $injection{$ENV{MUTANT}} // die "unknown mutant";
   $n=s/(ra->peak_gpr_usage = \(uint32_t\)\(max_phys_used \+ 1\);)/$1 $inject/g;
  }
  die "expected exactly one replacement, got $n" unless $n == 1;
 ' src/omega_blackwell_codegen.c > "$d/$mutant.c"
 set -x
 "$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -o "$d/$mutant" tests/gpu_regalloc_lifetime_oracle.c "$d/$mutant.c" > "$d/$mutant.build.log" 2>&1
 rc=0
 timeout 30 "$d/$mutant" > "$d/$mutant.run.log" 2>&1 || rc=$?
 set +x
 printf '%s\n' "$rc" > "$d/$mutant.exit"
 if [[ $rc != 1 ]] || ! rg -q 'allocation mismatch|disjoint reuse failed|exact capacity refused|one-more refusal failed|mixed-width fragmented reuse failed|fragmented pair exhaustion accepted' "$d/$mutant.run.log"; then
  echo "FAIL $mutant: expected behavioral assertion exit1, actual $rc" >&2
  cat "$d/$mutant.run.log" >&2
  exit 1
 fi
 echo "PASS killed $mutant exit=$rc: $(head -1 "$d/$mutant.run.log")"
 sha256sum "$d/$mutant.c" "$d/$mutant"
done
printf '%s\n' 'allocator compiled behavioral controls: PASS 11/11; no compile/timeout counted as kill'
