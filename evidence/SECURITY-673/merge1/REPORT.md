# Reconciliation with concurrent owner work

Root independently reviewed and committed87788715. Concurrent security work published3076b31c,
including older copied strict-profile bytes plus rename/path cases. Both histories are preserved by
this merge; no force-push or deletion of the original dirty author tree occurs. The root integrated
campaign is named pure4 so the concurrently published pure3 remains historical and intact.

The coordinator retains its reviewed strict mode4, spoof mode5 and corrected supervisor, while
adding the existing rename/path corpus to normal mode. New file IDs34..36 avoid colliding with the
strict-profile paths23..33. No production compiler change occurs in this merge.

Inspection found the incoming path test claimed exact-capacity and unchanged short-buffer behavior
without asserting either. It now checks the exact `seed/omc/omc.om` argument bytes at exact capacity,
a sentinel immediately after the output and a short-buffer sentinel after exact error-105. Empty
path refusal is exact ENOENT. The rename consumer claimed no-replacement but passed flag0 and only
checked return status. It now uses flag1, verifies the old path is absent and destination contains
exactly x, recreates the source, and checks a second no-replace rename refuses EEXIST. These extend
the existing compiler boundary; no alternate path or packet implementation is introduced.

Commands from this checkout:

```
seed/out/omc < seed/qualification/security673/gate669.om > build/gate669-merged
readelf -h build/gate669-merged
chmod +x build/gate669-merged
build/gate669-merged
build/gate669-merged 4
```

Each exited0; scratch was preserved under `build/security673-native-merged-normal` and
`...-merged-pure`, with strict fixture directory renamed between runs. `normal.log` and `pure.log`
record every input, child command and exact exit; stderr is empty. identities.txt binds sources and
binary. These are author results, supplemented by the separately preserved independent review.
Same public/synthetic scope and private-input/crash-handler blockers apply as in pure4.
