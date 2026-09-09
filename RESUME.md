# lane-clipboard — RESUME (paused 2026-09-10, owner logout)

Branches: Studio `lane-clipboard` tip **b3331a58** · irisgl `lane-clipboard-irisgl` tip
**d6b4a41** (pinned by the Studio commit). Base: Studio 56d9aa2a / irisgl dc85bb8.
Worktree builds clean (`build-linux`, ASan ON, `-DJAH_TEST_DISPLAY=:62`); nothing is WIP —
the code compiles and every suite named below was run green by hand.

## The clipboard format, as shipped

One line, UTF-8, leading with its own marker (QJsonDocument sorts keys, so the two identity
keys are written by hand around the body; the reader is order-agnostic):

```json
{"format":"jahshaka.clipboard","version":1,"app":"0.9.1b","sceneFormat":2,
 "created":"2026-09-10T01:08:00Z",
 "source":{"storeId":"<store.json id>","projectGuid":"<guid>","storeRoot":"/abs/path"},
 "items":[{"kind":"node","node":{ ...writeSceneNode object... },"parent":"<guid>","index":3}],
 "assets":{"<asset guid>":{"name":"tiny.png","type":"texture","typeId":2,"parent":"",
   "viewFilter":2,"dependencies":[],
   "files":[{"role":"source","name":"tiny.png","ext":"png","size":76,
             "oid":"<sha256>","inline":"<base64>"}]}}}
```

MIME: `application/x-jahshaka-clipboard` AND `text/plain`, same bytes (D2 c).
Item kinds implemented: `node`, `asset`. Unknown kinds are read, carried and skipped with a
reason. `blob`/`properties` (base64) ride an `asset` entry for rows that have them.

## Done (green by hand, no ctest gate run)

* P0 complete: `src/io/clipboardformat.*`, `src/io/assetrefs.*` (the key table + both remaps),
  `src/services/clipboard{service,backend,resolver}.*`, `src/services/assetclosure.*`,
  `src/scripting/modules/clipboardapi.*` (8 verbs), `editor.copy/paste/clipboard` reduced to
  deprecated aliases, `SceneEditService::copyNodes/paste/clipboard()` + `mClipboard` DELETED,
  Ctrl+C/Ctrl+X/Ctrl+V routing, tree context-menu Cut/Copy/Paste rows, docs regenerated.
* P1 complete: closure (inline under `clipboard/inlineLimitBytes`, 4 MB), resolver steps 1-5,
  Binding pins, missing report + `allowMissing`, `exportNodeTo` adopts the walker.
* irisgl: `SceneNode::remapNodeReferences` (socket owner + physics constraint endpoints +
  virtual per-type hook; `CameraNode::focusTarget` overrides it), used by both duplicate()
  and the paste path — the recorded gap, closed once.
* Suites written and RUN GREEN individually: `services.clipboard` (44 checks),
  `scripting.e2e.clipboard`, `scripting.e2e.clipboard_xproject`, `scripting.e2e.clipboard_assets`,
  plus `scripting.e2e.multiselect_edit` unchanged (re-run green).

## Not done — the exact next step

The GATE. Nothing else is outstanding.

```
Xvfb :62 -screen 0 1920x1080x24 -nolisten tcp &          # own display, 1920x1080 (law)
cd /home/jahshaka/Developer/jahshaka/.claude/worktrees/lane-clipboard
scripts/gate-scope.sh 56d9aa2a..b3331a58                  # SCOPED selection first
DISPLAY=:62 JAHSHAKA_DATA_ROOT=<scratch>/data HOME=<scratch> \
  ctest --test-dir build-linux -j4 --output-on-failure \
        -LE "shadercache|benchmark" -E "^gi\.ddgi_raster$"     # MERGE tier
# app.data_root must be run with `env -u JAHSHAKA_DATA_ROOT`
```

Expect the scope to include `api.contract` (docs moved), `services.*`, `scripting.e2e.*`,
the export suites (`exportNodeTo` changed) and the multiselect suites.

## Left for P2+ (spec §7, unchanged)

`material` / `graph` / `anim` item kinds and their paste targets (P2/P3), the Assets-page and
viewport "Paste Here" UI (P4/P5), the two rig probes (two instances on one Xvfb; the QLineEdit
Ctrl+V yield probe). Also open, both pre-existing and recorded in the spec: the textual guid
replace in `Database::importProject/importAsset/importAssetBundle`, and the archiver/raw
exporter still running their own sweeps instead of `assetclosure`.

## Merge overlaps

`src/shell/mainwindow.cpp` (service construction + the four edit chords), `editorapi.cpp`
(three verb bodies + docs), `src/services/sceneeditservice.{h,cpp}` (deletions +
regenerateGuids), `services.h` (one member), `CMakeLists.txt`, `docs/SCRIPTING.md`
(regenerate, never merge by hand), `tests/{services,scripting}/CMakeLists.txt`.
`scenemirror.cpp` NOT touched (lane-livetextures' hunk untouched).
