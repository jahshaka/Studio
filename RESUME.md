# lane-livetextures — RESUME

Stopped 2026-09-10 on the owner's logout, mid-gate. Branch tips:
Studio `lane-livetextures` = **b07dbe52** (base 4a856340), irisgl
`lane-livetextures-irisgl` = **e8d21b4** (base 13f4055, pinned by f8b13d99).

## The identity, as decided and built

A live texture is a **session-only `ModelTypes::LiveTexture`** (appended to the
enum in `src/data/project.h`). Two halves, one owner each:

- **identity** — `LiveTextureCatalog` (`src/services/livetextures.{h,cpp}`):
  a static session table of {guid, name, width, height, mipmaps}. Rows are
  *mirrored* into `AssetManager` as `AssetLiveTexture` entries so
  `assets.list({scope:'session'})` and every AssetManager lookup see them, and
  that mirror is **re-asserted lazily** (`ensureRegistered`) rather than
  maintained, because opening a project calls `AssetManager::clearAssetList()`
  and a session identity has nothing to do with a project.
- **pixels + generation** — the document's `iris::Texture2D`
  (`Texture2D::createLive` / `writeLive`, registry `iris::LiveTextures` in
  `irisgl/document/assets/livetextures.{h,cpp}`). A material row stores the
  string `live://<guid>`; `PbrMaterial::loadTexture` resolves it through the
  registry and treats a **miss as ordinary** (log line, empty slot) — which is
  what a row written by another session looks like.

Never persisted: `SceneWriter` writes a live-referenced texture row as **absent**
(no dangling guid in any file) and the export walkers drop live references from
the dependency closure. Reopening such a scene comes back with an empty map, no
crash — asserted in `scripting.e2e.live_textures`.

## Done (all four suites passed before the stop)

- verbs `texture.createLive|write|info|list|remove` and
  `video.bind|unbind|play|pause|stop|seek|loop|step|state|list`;
  `material.set({reflectionMap})` + the `reflectionMap` row (A-5).
- mirror: `textureFor` live branch (createTexture-born, keyed by colour space) +
  `syncLiveTextures()` once per sync, one upload per **changed** generation;
  `reclaimUnused` drops the generation record with its texture;
  `buildEquirectCubeFaces()` factored out and shared with the sky.
- `ApiRegistry::validate` refuses verb names `destroy`/`toString`/`objectName`
  (a verb named `destroy` is silently shadowed by the JS object wrapper — that
  is why the verb is `texture.remove`).
- CRUD: `Texture2D::readData()` deleted (no callers);
  `Project::ModelTypesAsString` completed (13 entries vs a 19-value enum, indexed
  by the enum — out-of-bounds for LightProfile/Avatar/Animation) + bounds-guarded.
- suites, all green individually on Xvfb :64 @1920x1080:
  `scripting.e2e.live_textures`, `scripting.e2e.live_texture_pixels`
  (BORN 3,3,3 -> RED 106,3,3 -> BLUE 3,3,106 -> GREEN 4,77,4 -> magenta; six
  writes, engine texture count 4 -> 4), `scripting.e2e.video_texture`
  (generation == frames + 1 on tiny.mp4), `scripting.e2e.reflection_map`
  (hero 5,21,41 vs control 1,1,1; under the PCC hybrid both casts 0),
  plus `api.contract`.

## Mid-flight

1. b07dbe52's three edits (one doc string + two test scripts) have **not** been
   compiled or run. `docs/SCRIPTING.md` is stale by that one doc string, so
   `api.contract` fails until it is regenerated.
2. The MERGE-tier gate was killed at 67/287 with **no failures** — it must be
   re-run from scratch.

## Exact next commands

```bash
W=/home/jahshaka/Developer/jahshaka/.claude/worktrees/lane-livetextures
Xvfb :NN -screen 0 1920x1080x24 -nolisten tcp &          # own display, 1920x1080 is law
cmake --build $W/build-linux -j$(nproc) > /tmp/lt.log 2>&1; grep -E 'error:' /tmp/lt.log
S=<scratch>; mkdir -p $S/gate-home/data
HOME=$S/gate-home JAHSHAKA_DATA_ROOT=$S/gate-home/data DISPLAY=:NN \
  $W/build-linux/bin/Jahshaka --dump-api-docs $W/docs/SCRIPTING.md   # then commit the diff
cd $W/build-linux
HOME=$S/gate-home JAHSHAKA_DATA_ROOT=$S/gate-home/data DISPLAY=:NN ctest -j4 \
  --output-on-failure -R '^(scripting\.e2e\.(live_textures|live_texture_pixels|video_texture|reflection_map)|api\.contract)$'
# then the tier. gate-scope 4a856340..<tip> FALLS BACK (root + irisgl CMakeLists touched),
# so the MERGE tier. This base predates the label cleanup -> the OLD labels:
HOME=$S/gate-home JAHSHAKA_DATA_ROOT=$S/gate-home/data DISPLAY=:NN ctest -j4 \
  --output-on-failure -LE "shadercache|benchmark" -E "^gi\.ddgi_raster$"
DISPLAY=:NN env -u JAHSHAKA_DATA_ROOT ctest -R '^app\.data_root$' --output-on-failure
```

Finally: delete this file in the commit that closes the lane.
