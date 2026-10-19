#!/bin/sh
# R11 living run inside its own quiet hold (tests/runtime/rx_r11_aien.c
# quiet_flag_state / quiet_flag_is_mine). With a fake HOME whose
# ~/workspace/.spark-quiet carries hold=q1-abc: no QUIETLOCK_HOLD, a prefix and
# an extension of the id must all be refused because of the flag (someone
# else's measurement); QUIETLOCK_HOLD=q1-abc must be let through to the living
# run.
#
# What is judged, and what is not. The program also refuses the living run
# when the machine is loaded (1-minute load average above 2). That is a second,
# unrelated reason, and on the shared Spark judge it is always true, so it
# used to hide the answer this test is about. The test therefore raises that
# limit for its own runs (R11_LOAD_LIMIT; it can only raise it) and reads
# the two decisions the program prints and takes:
#   "quiet flag: <none | this run's own hold | another measurement's hold>"
#   "[*] living run: ..." (printed when the living run starts)
# Refused cases: the flag line must say another measurement's hold, the reason
# given must be the flag, and the living run must not start. Own-hold case: the
# flag line must say this run's own hold and the living run must start.
# The physical verdict of that living run (its speed checks) is judged here
# only when the machine was calm when it began (1-minute load average <= 2,
# the condition the living run is specified for): then "checks N failures 0"
# is required, exactly as before. On a loaded machine its verdict is printed
# and left to test-r11, which refuses to run it loaded; a loaded machine is
# never a pass for that part, it is simply not this test's question.
# usage: r11_own_hold_test.sh <rx_r11_aien_test>
B=${1:?rx_r11_aien_test binary}; H=$(mktemp -d); fail=0
trap 'rm -rf "$H"' EXIT
mkdir -p "$H/workspace"
echo "other quietlock X start=2026-10-05T00:00:00Z expected_end=2099-01-01T00:00:00Z pid=1 hold=q1-abc" > "$H/workspace/.spark-quiet"
FLAG_WHY='holds ~/workspace/.spark-quiet'
for hold in "" q1-ab q1-abcd q1-abc; do
    calm=$(awk '{ if ($1 <= 2.0) print "yes"; else print "no" }' /proc/loadavg)
    out=$(HOME=$H QUIETLOCK_HOLD=$hold R11_LOAD_LIMIT=1000000 "$B" 2>&1)
    flag=$(printf '%s\n' "$out" | sed -n 's/^quiet flag: //p' | head -n 1)
    printf '%s\n' "$out" | grep -Fq '[*] living run:' && started=yes || started=no
    printf '%s\n' "$out" | grep -Fq "$FLAG_WHY" && why_flag=yes || why_flag=no
    if [ "$hold" = q1-abc ]; then
        # let through: the flag is ours, the flag is not the reason, the living run starts
        if [ "$flag" = "this run's own hold" ] && [ "$why_flag" = no ] && [ "$started" = yes ]; then
            got=ran
        else
            got="refused or not started (flag='$flag', flag reason $why_flag, started $started)"
        fi
        want=ran
    else
        # refused because of the flag: it says so, gives it as the reason, and does not start
        if [ "$flag" = "another measurement's hold" ] && [ "$why_flag" = yes ] && [ "$started" = no ]; then
            got=refused
        else
            got="not refused by the flag (flag='$flag', flag reason $why_flag, started $started)"
        fi
        want=refused
    fi
    if [ "$got" = "$want" ] && [ "$want" = ran ]; then
        line=$(printf '%s\n' "$out" | grep -E '^checks [0-9]+ failures [0-9]+$' | tail -n 1)
        if [ "$calm" = yes ]; then
            case "$line" in
                "checks "*" failures 0") ;;
                *) got="ran, but the calm-machine verdict was '$line', want failures 0" ;;
            esac
        else
            echo "[~] hold='$hold': machine loaded, living run verdict not judged here ('$line'); test-r11 judges it on a calm machine"
        fi
    fi
    if [ "$got" = "$want" ]; then echo "[+] hold='$hold': $got"; else echo "[-] hold='$hold': $got, want $want"; fail=$((fail + 1)); fi
done
echo "failures $fail"; [ "$fail" = 0 ]
