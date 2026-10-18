#!/bin/sh
# fab_work_procs.sh -- requester and executor as two separate OS processes
# joined by two pipes (aienix#6 phase 2, step "separate process"). Not a
# network, not M6-A/M6-B: it proves the message codec, the state machines and
# the executor's durable journal work across a process boundary, nothing more.
#
#   fab_work_procs.sh <fab_work_test binary> <work dir>
# Prints FAB_WORK_PROCS_PASS on success.
set -eu
BIN=$1
DIR=$2
mkdir -p "$DIR/procs"
J="$DIR/procs/executor.journal"
rm -f "$J" "$DIR/procs/req2exec" "$DIR/procs/exec2req"
mkfifo "$DIR/procs/req2exec" "$DIR/procs/exec2req"
PEER=$("$BIN" --peer-id)

# Open order matters with named pipes: the executor opens req2exec for reading
# first, so the requester must open req2exec for writing before it opens
# exec2req for reading, or both sides block in open() forever.
"$BIN" --executor "$J" < "$DIR/procs/req2exec" > "$DIR/procs/exec2req" 2> "$DIR/procs/executor.err" &
EXEC_PID=$!
set +e
"$BIN" --requester "$PEER" > "$DIR/procs/req2exec" < "$DIR/procs/exec2req" 2> "$DIR/procs/requester.err"
REQ_RC=$?
wait "$EXEC_PID"
EXEC_RC=$?
set -e
cat "$DIR/procs/executor.err" "$DIR/procs/requester.err"
if [ "$REQ_RC" -ne 0 ] || [ "$EXEC_RC" -ne 0 ]; then
    echo "fab_work_procs: requester rc=$REQ_RC executor rc=$EXEC_RC"
    exit 1
fi
# The journal holds exactly the entries the executor journaled: one completed
# (328 bytes); the replay and the code-7 refusal added none.
SIZE=$(wc -c < "$J")
if [ "$SIZE" -ne 328 ]; then
    echo "fab_work_procs: journal is $SIZE bytes, expected 328 (one entry)"
    exit 1
fi
# The two processes were distinct: the executor's pid is not ours.
if [ "$EXEC_PID" -eq "$$" ]; then echo "fab_work_procs: executor ran in this process"; exit 1; fi
echo "fab_work_procs: requester pid $$ (shell), executor pid $EXEC_PID, journal $SIZE bytes"
echo "FAB_WORK_PROCS_PASS"
