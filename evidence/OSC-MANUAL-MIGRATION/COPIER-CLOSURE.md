# Copier included in the end-to-end native closure gate

`tests/seed-migration/native-closure.sh` previously built, inspected and fingerprinted four native helper programs but omitted the stream copier already consumed by the bootstrap and R6 gate. The independent receipt verifier was first extended to require the copier's binary/source plus the copied compiler; the original gate failed that requirement (`copier-closure-red.txt`).

The gate now compiles the unchanged `streamcopy.om`, checks its ELF, executes it on stage3, requires native byte equality for the copy, and checks the copied ELF before emitting any success message. Native SHA256 fingerprints now cover nine binaries and five source files. `hash-receipts.py` independently compares the exact set of fourteen labels and hashes; missing, duplicate or extra labels refuse.

Three negative integration controls exercise an empty-output native copier, a native copier that returns an error, and a byte appended to the copied compiler. Both substituted programs must compile to nonempty ELF before their refusal counts. All three stop the actual gate with exit 2 and no PASS message. The existing stage3 corruption control still refuses independently.

`copier-closure-source-replay.txt` records a fresh committed-source birth, the extended hash gate and negative controls. The default repository target is recorded separately against the locked isolated physics checkout.

## Boundary

The initial host SHA256 check authenticating the compiler before execution remains deliberately independent. Private directories, file permissions and cleanup still use host shell utilities. The gate is a bounded native consistency/ELF-shape check, not a proof that arbitrary static executables contain no C, nor a production deployment authorization. Primitive boundary/fault suites remain separate requirements.

This integrates an existing Omega replacement into its actual end-to-end verification caller; no new production primitive or live retirement is claimed. Whole-system migration, native process/file orchestration and NVIDIA driver replacement remain open.
