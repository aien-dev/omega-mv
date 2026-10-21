# omega-evidence

The evidence shelf of [Omega](https://github.com/aien-dev/omega-mv): every receipt Omega's gates have written
(logs, measurements, verdicts, chip runs), kept forever and only ever added to.

It used to be the `evidence/` folder inside Omega, 11,078 files and 199 MB, about four out of five files in that
repository. It moved here with its full history (ADR 0039: one thing per repo) so Omega holds the language and
this repo holds the record of what was measured.

## How to find an old receipt

A receipt that Omega cited as `evidence/<path>` is this repo's `<path>`, byte for byte. Omega pins one commit of
this shelf in its `evidence.lock`, so a receipt is addressed as shelf + commit + path and can always be fetched
the same way. `tools/evidence_shelf.sh` in Omega proves that every receipt it no longer carries is here unchanged.

## Rules

- Additions only. A reviewed receipt is never edited or deleted; a correction is a new file beside it.
- Each receipt names the Omega commit it was made from, so it can be checked against the code that produced it.
- Licence: AGPL-3.0, the same as Omega.
