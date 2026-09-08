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

### 2026-09-08 — `lib/src/style/eventFilters/WidgetWithFocusFrameEventFilter.hpp`

The focus frame now re-derives its parent whenever the watched widget's **ancestry**
changes, not only when the widget's own parent changes.

`QFocusFrame::setWidget()` walks up from the widget to choose the frame's parent — in
`SH_FocusFrame_AboveWidget` mode, which this style enables, that is the enclosing scroll
area's viewport, a `QToolBar`, or the window — and caches it. Qt refreshes that choice on
the *widget's* `ParentChange`, but not when an **intermediate ancestor** is reparented:
`qfocusframe.cpp`'s `else if (d->showFrameAboveWidget)` branch answers only
`Move`/`Resize`/`ZOrderChange` for the ancestors it watches. An application that detaches
a whole subtree while keeping the widgets alive therefore strands the frame in a hierarchy
the widget has left, and `QFocusFramePrivate::updateSize()` maps coordinates between two
unrelated widget trees on every geometry change of the live side.

In Jahshaka that is `SceneNodePropertiesWidget::clearLayout()`, which reuses the property
blades and orphans them with `setParent(nullptr)` instead of deleting them: Qt answered
with `QWidget::mapTo(): parent must be in parent hierarchy` **164,651 times in one
13-minute session** (318 in a scripted import + scene open). The warning was suppressed at
the app's log funnel from 2026-09-07 and MISATTRIBUTED to `Popover.cpp:538`; the
suppression was removed in the same commit as this fix.

The fix keeps upstream's late-attach behaviour and its exact parent derivation (the
`p->isWindow() || … || (isScrollArea = …)` short-circuit is copied deliberately) and adds
a filter on the ancestors between the widget and the frame's parent — the same span Qt
itself watches, plus that parent — so a `ParentChange` anywhere in the span re-runs the
derivation. Covered by `theme.manager` ("focus frame: …" cases), which fails on the
pristine import and passes with the fix.

### Upstream defects known but deliberately NOT patched

- `lib/src/widgets/Popover.cpp:538` — `_frame->mapTo(this, …)` inside `paintEvent`. It is
  harmless as written (`_frame` is a direct child of the popover, added to its layout in
  the constructor, so the map succeeds), and Jahshaka instantiates no `Popover` at all. It
  was once wrongly named as the source of the `mapTo` flood above; left exactly as
  upstream wrote it.
