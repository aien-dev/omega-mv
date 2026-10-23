#!/bin/sh
# R11 living run inside its own quiet hold (tests/runtime/rx_r11_aien.c
# quiet_flag_is_mine). With a fake HOME whose ~/workspace/.spark-quiet carries
# hold=q1-abc: no QUIETLOCK_HOLD, a prefix and an extension of the id must all
# be refused BECAUSE of the flag (someone else's measurement); QUIETLOCK_HOLD=q1-abc
# must run the living run.
# The binary decides the flag first and the load average (above 2) last, so the
# refusal reason separates the two. The living run is a timed measurement (it
# needs the X925 speed-up and zero timeouts), so the binary's load limit is
# right and stays. The owner case therefore takes a measurement window
# (spark-window: the quiet lock, so builds and other measurements stand aside),
# waits for the load average to fall below the binary's limit, and must then see
# the living run RUN. "Held back by load" or any other skip fails the gate: a
# gate never skips, waives or retries to green (L345, replaces the L336 rule
# that let a held-back owner pass).
# usage: r11_own_hold_test.sh <rx_r11_aien_test>
B=${1:?rx_r11_aien_test binary}; H=$(mktemp -d); fail=0
trap 'rm -rf "$H"' EXIT
mkdir -p "$H/workspace"
echo "other quietlock X start=2026-10-05T00:00:00Z expected_end=2099-01-01T00:00:00Z pid=1 hold=q1-abc" > "$H/workspace/.spark-quiet"
FLAG_REASON="an R15 timed measurement holds ~/workspace/.spark-quiet"
SETTLE_S=${R11_OWN_HOLD_SETTLE_S:-600}   # how long to wait for the load average after the window opens

# Run the owner case inside a measurement window. Already inside one (SPARK_WINDOW
# set by spark-window run): use it. No spark-window on this box: fail, never run unmeasured.
owner_run() {
    WIN=""
    trap '[ -z "$WIN" ] || spark-window close "$WIN" >/dev/null 2>&1' EXIT   # runs in the $(...) subshell; the lease (20 min) bounds a killed gate
    if [ -z "${SPARK_WINDOW:-}" ]; then
        if ! command -v spark-window >/dev/null 2>&1; then
            echo "spark-window not found: the living run needs a measurement window" >&2; return 99
        fi
        WIN=$(spark-window open omega-r11-own-hold --ledger "${R11_OWN_HOLD_LEDGER:-omega-r11-own-hold}" --gb10 none --lease 20 --wait) || return 98
    fi
    # the window silences new work; the 1-minute load average still carries the old work
    waited=0
    while [ "$waited" -lt "$SETTLE_S" ]; do
        awk '{ exit !($1 <= 1.5) }' /proc/loadavg && break
        sleep 5; waited=$((waited + 5))
    done
    echo "    window ${WIN:-$SPARK_WINDOW}, waited ${waited}s for load, loadavg now: $(cut -d' ' -f1-3 /proc/loadavg)" >&2
    if [ -n "$WIN" ]; then HOME=$H QUIETLOCK_HOLD=q1-abc spark-window run "$B" 2>&1
    else HOME=$H QUIETLOCK_HOLD=q1-abc "$B" 2>&1; fi
}

for hold in "" q1-ab q1-abcd q1-abc; do
    if [ "$hold" = q1-abc ]; then out=$(owner_run); else out=$(HOME=$H QUIETLOCK_HOLD=$hold "$B" 2>&1); fi
    if printf '%s\n' "$out" | grep -Fq "$FLAG_REASON"; then got=refused-by-flag
    elif printf '%s\n' "$out" | grep -Fq "living run not exercised: SKIPPED-LOADED"; then got=not-flag-held-back-by-load
    elif printf '%s\n' "$out" | grep -Fq "living run not exercised"; then got=not-flag-skipped-other
    elif printf '%s\n' "$out" | grep -Eq '^    requests: served [0-9]+'; then got=ran
    else got=not-flag-no-living-run-line; fi
    if [ "$hold" = q1-abc ]; then
        [ "$got" = ran ] && ok=1 || ok=0
    else
        [ "$got" = refused-by-flag ] && ok=1 || ok=0
    fi
    printf '%s\n' "$out" | grep -Eq '^checks [0-9]+ failures 0$' || ok=0
    if [ "$ok" = 1 ]; then echo "[+] hold='$hold': $got"; else echo "[-] hold='$hold': $got"; printf '%s\n' "$out" | grep -E 'living run|SKIPPED|^\[-\]|^checks' | head -5 | sed 's/^/      /'; fail=$((fail + 1)); fi
done
echo "failures $fail"; [ "$fail" = 0 ]
