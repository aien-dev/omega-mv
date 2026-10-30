#!/bin/sh
# tests/receipt_commit.sh -- commit identity for generated qualification receipts.
# Sourced by the gate runners. Sets QUALIFIED_COMMIT to the commit the tree is actually at and
# fails closed when a caller passes a different sha or the tree is dirty, so a receipt can never
# carry the identity of a commit whose bytes it did not come from (the M8/M9 defect from 469f53c1).
# RECEIPT_COMMIT stays a placeholder: a receipt cannot know the commit that will contain it.
receipt_commit_resolve() {
    _root=$1
    _head=$(git -C "$_root" rev-parse --verify HEAD 2>/dev/null) || {
        echo "receipt_commit: $_root is not a git checkout; refusing to stamp a receipt" >&2
        return 1
    }
    if [ -n "$(git -C "$_root" status --porcelain --untracked-files=no -- src tools tests 2>/dev/null)" ]; then
        echo "receipt_commit: src/, tools/ or tests/ differ from HEAD $_head; commit first" >&2
        return 1
    fi
    if [ -n "${QUALIFIED_IMPLEMENTATION_COMMIT:-}" ] && [ "$QUALIFIED_IMPLEMENTATION_COMMIT" != "$_head" ]; then
        echo "receipt_commit: QUALIFIED_IMPLEMENTATION_COMMIT=$QUALIFIED_IMPLEMENTATION_COMMIT is not HEAD $_head" >&2
        return 1
    fi
    QUALIFIED_COMMIT=$_head
    RECEIPT_COMMIT="${RECEIPT_COMMIT:-pending-receipt-commit}"
    return 0
}
