#!/bin/sh
# R11 living run inside its own quiet hold (tests/runtime/rx_r11_aien.c
# quiet_flag_is_mine). With a fake HOME whose ~/workspace/.spark-quiet carries
# hold=q1-abc: no QUIETLOCK_HOLD, a prefix and an extension of the id must all
# be refused BECAUSE of the flag (someone else's measurement); QUIETLOCK_HOLD=q1-abc
# must NOT be refused because of the flag.
# The property is who owns the flag, not how busy the machine is. The binary
# decides the flag first and the load average (above 2) last, so the refusal
# reason separates the two: a busy judge refuses the owner for load, which says
# nothing about the flag. The owner case reports whether the living run ran or
# was held back by load; either answer passes, the line tells which happened.
# usage: r11_own_hold_test.sh <rx_r11_aien_test>
B=${1:?rx_r11_aien_test binary}; H=$(mktemp -d); fail=0
trap 'rm -rf "$H"' EXIT
mkdir -p "$H/workspace"
echo "other quietlock X start=2026-10-05T00:00:00Z expected_end=2099-01-01T00:00:00Z pid=1 hold=q1-abc" > "$H/workspace/.spark-quiet"
FLAG_REASON="an R15 timed measurement holds ~/workspace/.spark-quiet"
for hold in "" q1-ab q1-abcd q1-abc; do
    out=$(HOME=$H QUIETLOCK_HOLD=$hold "$B" 2>&1)
    if printf '%s\n' "$out" | grep -Fq "$FLAG_REASON"; then got=refused-by-flag
    elif printf '%s\n' "$out" | grep -Fq "living run not exercised: SKIPPED-LOADED"; then got=not-flag-held-back-by-load
    elif printf '%s\n' "$out" | grep -Fq "living run not exercised"; then got=not-flag-skipped-other
    else got=ran; fi
    if [ "$hold" = q1-abc ]; then
        case $got in
            ran) ok=1 ;;
            not-flag-held-back-by-load) ok=1 ;;
            *) ok=0 ;;
        esac
    else
        [ "$got" = refused-by-flag ] && ok=1 || ok=0
    fi
    printf '%s\n' "$out" | grep -Eq '^checks [0-9]+ failures 0$' || ok=0
    if [ "$ok" = 1 ]; then echo "[+] hold='$hold': $got"; else echo "[-] hold='$hold': $got"; fail=$((fail + 1)); fi
done
echo "failures $fail"; [ "$fail" = 0 ]
