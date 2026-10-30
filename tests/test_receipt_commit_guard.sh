#!/bin/sh
# tests/test_receipt_commit_guard.sh -- receipt_commit_resolve stamps HEAD and fails closed otherwise.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
. "$ROOT/tests/receipt_commit.sh"
HEAD=$(git -C "$ROOT" rev-parse HEAD)
fail() { echo "FAIL: $1"; exit 1; }

unset QUALIFIED_IMPLEMENTATION_COMMIT RECEIPT_COMMIT
receipt_commit_resolve "$ROOT" || fail "clean tree at HEAD refused"
[ "$QUALIFIED_COMMIT" = "$HEAD" ] || fail "QUALIFIED_COMMIT $QUALIFIED_COMMIT is not HEAD $HEAD"
[ "$RECEIPT_COMMIT" = "pending-receipt-commit" ] || fail "RECEIPT_COMMIT not a placeholder"
echo "PASS: HEAD $HEAD stamped"

QUALIFIED_IMPLEMENTATION_COMMIT=$HEAD receipt_commit_resolve "$ROOT" || fail "matching override refused"
echo "PASS: override equal to HEAD accepted"

if QUALIFIED_IMPLEMENTATION_COMMIT=d25349e4bf4a820dbcff0dd6a593d1db7b454a0b receipt_commit_resolve "$ROOT" 2>/dev/null; then
    fail "foreign sha accepted"
fi
echo "PASS: foreign sha refused (negative control)"

tmp=$(mktemp -d) && trap 'rm -rf "$tmp"' EXIT
if receipt_commit_resolve "$tmp" 2>/dev/null; then fail "non-git directory accepted"; fi
echo "PASS: non-git tree refused (negative control)"

git -C "$ROOT" worktree add -q "$tmp/wt" HEAD || fail "worktree"
printf '\n' >> "$tmp/wt/tests/receipt_commit.sh"
if receipt_commit_resolve "$tmp/wt" 2>/dev/null; then git -C "$ROOT" worktree remove --force "$tmp/wt"; fail "dirty tree accepted"; fi
git -C "$ROOT" worktree remove --force "$tmp/wt"
echo "PASS: dirty tree refused (negative control)"
