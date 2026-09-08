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

### 2026-09-08 — `lib/src/style/eventFilters/ComboboxItemViewFilter.hpp`

`ComboboxItemViewFilter::eventFilter` no longer calls `QComboBox::view()` (nor
`setItemDelegate()`, which goes the same way) synchronously from its `ChildAdded`
handler; it defers the delegate installation one event-loop turn.

Both accessors run through `QComboBoxPrivate::viewContainer()`, which **creates** the
popup container on demand — and Qt emits exactly this `ChildAdded` from that container's
constructor (`QComboBoxPrivateContainer` → `QFrame` → `QWidgetPrivate::init` →
`QWidget::setParent`), i.e. before `QComboBoxPrivate::container` has been assigned. So
`view()` built a second container, whose constructor emitted another `ChildAdded`, and so
on: on Qt 6.10 the first `addItem()` on a combo that had never opened its popup recursed
until the stack overflowed and the process died with SIGSEGV. Captured stack:

    QComboBox::view() → QComboBoxPrivate::viewContainer()
      → QComboBoxPrivateContainer::QComboBoxPrivateContainer(...) → QFrame::QFrame
      → QWidgetPrivate::init → QWidget::setParent → sendThroughObjectEventFilters
      → ComboboxItemViewFilter::eventFilter → QComboBox::view() → ...

Jahshaka carried an app-side workaround for this from 2026-08-31 (`JahQlementineStyle`
in `src/ui/style/thememanager.cpp` deferred the popup item view's whole polish by a
tick). That workaround is REMOVED in the same commit: the popup is polished inline again,
exactly as upstream intended. Guarded by the `combo:` cases in `tests/theme`, which also
pin the popup's translucent frameless panel and its drop-shadow margins.

`ComboboxFilter` (same header) has the same `child == _comboBox->view()` shape but
filters the POPUP, not the combo, so its `view()` call always runs after the container
exists. Left untouched.

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

### 2026-09-08 (2) — ComboboxItemViewFilter: the deferred install is guarded

The deferral above turned out to be SELF-FEEDING: `ComboBoxDelegate` is a `QObject`
parented to the combo, so installing it emits `ChildAdded` on the combo — the very event
the filter reacts to — and scheduled the next install; one delegate per event-loop turn,
forever (the stage gate's watchdog measured ~55 s UI-thread stalls at boot; the lane's
own gate had not caught it because `theme.manager` never spun the loop). Two guards:
only WIDGET children (the popup container) trigger the install, and the install is
idempotent (`_delegatePending` + "already a ComboBoxDelegate" check). Covered by the
new `theme.manager` "installed ONCE — eight event-loop turns, no churn" case.

### 2026-09-08 (3) — WidgetWithFocusFrameEventFilter: the refresh is COALESCED, and the frame is DESTROYED with its widget

The ancestor-watching fix above was correct and expensive. Two changes, both in
`lib/src/style/eventFilters/WidgetWithFocusFrameEventFilter.hpp`:

1. **The re-derivation is deferred one event-loop turn and coalesced** (`_refreshPending`
   + `QTimer::singleShot(0, this, …)`, the same shape the combo filter uses), and it now
   derives the frame's parent and the ancestor watch list in ONE walk that early-outs when
   neither changed. A host that detaches a subtree and re-attaches it inside the same turn
   — which is what a properties panel rebuilding its layout does — used to pay a full
   `setWidget(nullptr)/setWidget()` cycle per focusable descendant per `ParentChange`,
   i.e. a `QWidget::setParent` of the frame plus install/remove of the frame's own event
   filters along the whole chain, twice per selection change. Deferred, the pair of
   `ParentChange`s collapses into one check that finds the ancestry back where it started
   and does nothing at all. Measured in Jahshaka (`ui.selection_cost`, 200 selection
   switches over a 15-node scene): **0 focus-frame re-derivations, down from a storm that
   grew the cost of a single selection from 105 ms to 1.43 s inside one session.**

2. **The frame is deleted with the filter.** `QFocusFrame` is born a child of the watched
   widget, but `setWidget()` REPARENTS it to the enclosing scroll area's viewport, so the
   widget's destruction no longer takes it along: Qt clears the frame's pointer and leaves
   an invisible widget behind that still filters events on every ancestor it watched. Any
   host that rebuilds a panel's rows therefore leaks one frame — and one more entry on the
   shared ancestors' event-filter lists — per row per rebuild, for the life of the process
   (measured: **+1188 live `QFocusFrame`s over 180 selection changes**, all of them on the
   scroll viewport). The filter is a child of the widget, so it dies exactly when the
   widget does; `_focusFrame` became a `QPointer` to cover the other destruction order.

Covered by `theme.manager` ("focus frame (cheap): …" cases — the first of them fails
against the previous revision) and by Jahshaka's `ui.selection_cost` suite. The
correctness cases of the earlier entry are unchanged and still green: an ancestor that is
orphaned and STAYS orphaned still takes the frame with it, one turn later.
