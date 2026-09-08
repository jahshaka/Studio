# Qlementine — vendored into the Jahshaka Studio repository

This directory was a git submodule pinned to upstream `v1.4.2` until 2026-09-08. It is
now **first-class vendored source**: ordinary tracked files in the Studio repository,
ours to edit.

| | |
|---|---|
| Upstream project | Qlementine — a modern QStyle for desktop Qt applications |
| Upstream URL | https://github.com/oclero/qlementine |
| Upstream author | Olivier Cléro (`oclero`) |
| Licence | MIT — see `LICENSE`, kept verbatim and unmodified |
| Absorbed commit | `13f72eb8b53bafd9ac24e5562d8ddc28d5440469` (tag `v1.4.2`, 2026-01-12, "Merge pull request #130 from oclero/dev") |
| Absorbed on | 2026-09-08 |

The absorption commit is a **pristine import**: every one of the 206 files upstream
tracked at `13f72eb` was committed byte-for-byte (blob hashes and file modes verified
identical against the submodule's own object store), and the library it builds is
byte-identical to the one the submodule built. Every divergence from upstream therefore
shows up as an ordinary commit in this repository's history, and `git log` over this
directory *is* the divergence log.

## Why we forked

Owner decision, 2026-09-08 (`SPECS/ENGINEERING_DEBT_SPEC.md`, "ADDENDUM 5 — qlementine:
FORK INTO THE REPO"): *"i would fork the library and add it to our repo really since we
will need to fine tune it once the app is working properly."*

1. Defects that hit Jahshaka hard need fixing at the source rather than being papered
   over in app code or a log filter.
2. Sustained theme fine-tuning is expected once the app stabilises, and a pinned
   upstream submodule is the wrong shape for a library we intend to diverge from.

## Working rules

- The **LICENSE** stays verbatim; the MIT attribution to Olivier Cléro stays in every
  file header we touch.
- Every edit to this tree gets an entry in the divergence log below: file, reason, and
  the upstream behaviour it changes — so a future upstream bump can be replayed
  deliberately instead of archaeologically.
- Upstream is now a *reference* to cherry-pick from, not a pin to follow.

## Divergence log

Newest first.

*(empty at import — this file lands with the pristine v1.4.2 tree)*
