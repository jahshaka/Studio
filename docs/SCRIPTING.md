# Jahshaka scripting — verb reference

API version 0.1.0. GENERATED from the ApiRegistry (`--dump-api-docs`) — do not edit by hand.

Every verb is callable from the script console (Editor, bottom dock), from
`./Jahshaka --script file.js`, and headless. The **needs** column is the
headless matrix: *document* verbs run with no engine at all (`--headless`),
*engine* verbs need the engine viewport up (a reachable DISPLAY is enough —
no visible window), *window* verbs are only meaningful with the editor UI.

Each script run is one undo step (Ctrl+Z reverts the whole script) unless
wrapped differently with `editor.beginBatch()`/`editor.endBatch()`.
Asset/store operations are NOT undoable — asset mutations are permanent.

## project

| verb | needs | description |
|---|---|---|
| `project.create(name) -> guid` | document | Creates a project (folder, DB row, default scene saved into the blob) on the current desktop and opens it in the editor. |
| `project.open(guidOrName) -> bool` | document | Opens a project by guid or exact name: preloads its assets synchronously, reads the scene blob, switches to the editor. |
| `project.save() -> bool` | document | Saves the open scene into the project's DB blob. Works headless (blob-only; the thumbnail refreshes only when a viewport can render one). |
| `project.close() -> bool` | document | Closes the open project (physics restored, autosave per settings, undo stack reset) and returns to the desktop. |
| `project.rename(guid, newName) -> bool` | document | Renames a project in the database. |
| `project.remove(guid) -> bool` | document | Deletes a project: its folder tree, its DB row and its asset/dependency rows. Refuses to delete the open project. NOT undoable. |
| `project.list({desktop}) -> [{guid, name, desktop, x, y}]` | document | Lists projects; desktop 1-4 filters, omit for all. |
| `project.moveToDesktop(guid, desktop) -> bool` | document | Moves a project tile to desktop 1-4. |
| `project.setPosition(guid, x, y) -> bool` | document | Sets a tile's freeform position (normalized 0..1) on its desktop. |
| `project.current() -> {guid, name, folder} \| null` | document | The open project, or null. |
| `project.exportWeb(dir) -> {dir, indexHtml, glb, nodes, materials, extensions, warnings, ...}` | document | Exports the open scene for the web (glTF 2.0 + self-contained WebGPU viewer): index.html (double-clickable), viewer.html + scene.glb (served path), README.txt. dir defaults to <project>/exports/web. Document-only; works headless. |
| `project.previewWeb(dir) -> {browser, mode}` | document | Opens an existing web export (see exportWeb) in a Chromium-family browser as a chromeless --app window, or the default browser when none is found. mode is 'kiosk' or 'browser'. |
| `project.exportManifest(dir) -> {dir, manifest, assets, totalBytes}` | document | Writes a manifest v2 (jah.manifest.json) describing the open project's assets — guids, types, dependency edges, file names, sizes and sha256 content ids — without copying any bytes. dir defaults to <project>/exports. The catalog half of the unified export (ASSET_PIPELINE_SPEC §3.3); project.exportArchive materializes the files in the final half. |
| `project.exportArchive(path) -> {path, assets, objects}` | document | Exports the open project as a self-contained archive: catalog snapshot + manifest v2 + the pinned CAS objects. A reference-based project leaves the machine whole. |
| `project.importArchive(path) -> {guid, name, assets, objects}` | document | Imports a project archive as a NEW project: rows, objects ingested CAS-first, fresh pins. Does not open it. |

## scene

| verb | needs | description |
|---|---|---|
| `scene.nodes() -> [{id, name, type, parent, position, rotation, scale}]` | document | Every node in the open scene, depth-first from the root. |
| `scene.find(name) -> id \| null` | document | The first node with this exact name, or null. |
| `scene.root() -> id` | document | The scene root's id. |
| `scene.addPrimitive(name, {position, rotation, scale, parent}) -> id` | document | Adds a built-in primitive (plane, cone, cube, cylinder, sphere, torus, capsule, gear, pyramid, teapot, sponge, steps). Undoable. |
| `scene.addLight(type, {position, ...}) -> id` | document | Adds a light: point, spot, directional or area. Undoable. |
| `scene.addEmpty({position, parent}) -> id` | document | Adds an empty group node. Undoable. |
| `scene.addMesh(path, {position, ...}) -> id` | document | Imports a mesh file (obj, fbx, dae, ...) straight into the scene — no dialog, the path is the argument. Undoable. |
| `scene.addImagePlane(textureGuid, {position?, doubleSided?}) -> id` | document | Spawns an image plane for a Texture asset (IMAGE_PLANE_SPEC option A): a plane sized to the image's aspect (long side 1 m), facing the editor camera at creation, with a basic PBR material carrying the image as baseColorMap (roughness 1, metallic 0; images with an alpha channel blend). Bytes resolve pin-first through the CAS. doubleSided defaults true. Undoable. |

## node

| verb | needs | description |
|---|---|---|
| `node.remove(id) -> bool` | document | Deletes the node (undoable; its DB asset row is only dropped once the delete can no longer be undone). |
| `node.duplicate(id) -> newId` | document | Duplicates the node under the same parent. Undoable. |
| `node.reparent(id, parentId) -> bool` | document | Moves the node under a new parent, keeping its world pose; cycles are refused. Undoable. |
| `node.transform(id, {position, rotation, scale}) -> {position, rotation, scale}` | document | Sets any of position/rotation/scale (absolute; rotation in euler degrees; omitted parts keep their value) and returns the result. Undoable. |
| `node.property(id, key) -> value` | document | Reads a reflected property (position, rotation, scale; lights add intensity, lightColor, distance, spotCutOff, spotCutOffSoftness, rectWidth, rectHeight). |
| `node.setProperty(id, key, value) -> bool` | document | Writes a reflected property (same keys as node.property). Direct document write — not undoable yet. |
| `node.info(id) -> {id, name, type, parent, position, rotation, scale}` | document | Everything scene.nodes() reports, for one node. |
| `node.boneNames(id) -> [string]` | document | The node's rig, in bone-index order (the index its vertex weights name). Empty for anything unrigged. |
| `node.skinningMode(id) -> "gpu" \| "none"` | document | How the node deforms: "gpu" when it carries a rig (the vertex shader skins position, normal and tangent from bone matrices), "none" when it is static. Diagnostic. |

## editor

| verb | needs | description |
|---|---|---|
| `editor.select(id \| null) -> bool` | document | Selects a node everywhere (viewport, hierarchy, properties); null or no argument deselects. |
| `editor.selection() -> id \| null` | document | The selected node's id, or null. |
| `editor.gizmoMode() -> "translate" \| "rotate" \| "scale"` | engine | The active transform gizmo mode (W/E/R in the viewport; Space cycles). |
| `editor.setGizmoMode("translate"\|"rotate"\|"scale") -> bool` | engine | Switches the transform gizmo, exactly like the W/E/R keys and the toolbar buttons. |
| `editor.focusSelection() -> bool` | engine | Frames the selected node in the editor camera (the F key): bounds-aware distance, current view direction kept. |
| `editor.gameView(enabled) -> bool` | engine | Game View (the G key): hides every in-viewport editor helper — grid, light wires, selection outline, gizmo. Docks stay; not persisted. |
| `editor.isGameView() -> bool` | engine | Whether Game View is active. |
| `editor.setView("top"\|"bottom"\|"left"\|"right"\|"front"\|"back"\|"perspective") -> bool` | engine | Snaps the editor camera to a canonical view (the toolbar Views dropdown / X, Y, Z keys). Each view remembers its camera between visits: "perspective" returns to its remembered free/orbit pose, each ortho view to its own pan and zoom (a first visit gets the standard axis framing). Session-only memory; works in both camera modes. |
| `editor.view() -> string` | engine | The last canonical view requested via editor.setView ("perspective" until one is set). Informational — free orbiting afterwards does not reset it. |
| `editor.camera() -> {position:{x,y,z}, rotation:{x,y,z,scalar}, projection:"perspective"\|"orthogonal", orthoSize}` | engine | The editor camera's current pose: local position, local rotation quaternion, projection mode and ortho zoom. Read-only — the pixel-free way to assert camera moves (focus, view switches). |
| `editor.cameraMode() -> "free" \| "orbit"` | engine | The active camera controller: "free" (fly camera) or "orbit" (arcball). |
| `editor.setCameraMode("free"\|"orbit") -> bool` | engine | Switches the camera controller, like the toolbar's Free Camera / Arc Ball buttons. (The toolbar buttons do not yet reflect a script-driven switch.) |
| `editor.snapSize() -> number` | document | The translate snap size (world units) — also the ground grid's spacing. Editor-global, persisted. |
| `editor.setSnapSize(size) -> bool` | document | Sets the translate snap / grid size ([ and ] step it in the viewport). Refuses size <= 0; clamped to 0.01..100. |
| `editor.snapToFloor() -> bool` | engine | Drops the selection straight down onto the first scene surface below its bounds (the End key); y=0 plane when nothing is hit. Undoable. |
| `editor.undo() -> bool` | document | Undoes the last completed undo step. Inside a script the run's own macro is still open, so this reaches the step before the script. |
| `editor.redo() -> bool` | document | Redoes the last undone step. |
| `editor.play() -> bool` | document | Enters play mode (PlayBack drives physics, animations and controllers in place). |
| `editor.stop() -> bool` | document | Leaves play mode back to editing. |
| `editor.simulate(enabled=true) -> bool` | document | Starts/stops the in-place physics simulation without entering play mode. |
| `editor.frame(n=1, dt=-1) -> bool` | engine | Renders exactly n frames synchronously (document->engine sync + renderOneFrame) — the deterministic stepping the test suites use. With `dt` >= 0 the document's clock advances by exactly that many seconds per frame instead of by the wall clock, which is what makes stepping deterministic IN PLAY MODE (without it, each stepped frame charged the document for however long the previous statement took). |
| `editor.screenshot(path, w=256, h=256, probes=[]) -> {path, width, height, center:{r,g,b}, probes:[{x,y,r,g,b}]}` | engine | Offscreen render of the editor scene to a PNG; returns the centre pixel, plus the pixel at each probe point ({x,y} in normalized 0..1 image coordinates), so scripts can assert on colours. Headless-safe. |
| `editor.beginBatch() -> bool` | document | Opens a nested undo macro inside the script's run (finer-grained grouping). |
| `editor.endBatch() -> bool` | document | Closes the macro opened by editor.beginBatch(). |
| `editor.importAssets([paths]) -> bool` | window | Starts the interactive THREADED import of the given files — the same ImportBatchRunner + progress dialog the project panel's Import button and drops use — and returns once the batch has started (it does not wait). assets.importFile is the synchronous, dialog-free verb. |

## app

| verb | needs | description |
|---|---|---|
| `app.desktop(n=0) -> current` | window | Switches to desktop 1-4; app.desktop() just returns the current one. |
| `app.space(name) -> bool` | window | Switches the main window space: desktop, player, editor, materials, assets, publish, avatar. player and editor need an open project. |
| `app.quit() -> bool` | window | Closes the main window through the normal close path (autosave/unsaved-changes rules apply, background work is shut down). The verb returns before the window actually closes. |

## desktop

| verb | needs | description |
|---|---|---|
| `desktop.viewMode() -> mode` | window | Returns the current desktop's view mode: 'rows', 'freeform' or 'sliders' (persisted per desktop). |
| `desktop.setViewMode(mode) -> bool` | window | Sets the current desktop's view mode: 'rows', 'freeform' or 'sliders'. Persists per desktop; switching is lossless (each mode keeps its own layout). |
| `desktop.moveTile(guid, row, index=-1) -> bool` | window | Sliders mode: moves the project tile into filmstrip row 1..N at the insert index (0-based within the row; -1 appends). Tiles after the index shift right. The assignment persists. |
| `desktop.tiles() -> [{guid, name, row, index}]` | window | Lists the current desktop's tiles with their slider assignment (row 1..N, index 0-based; -1/-1 when never assigned). |

## world

| verb | needs | description |
|---|---|---|
| `world.ambient(color) -> bool` | document | Sets the ambient light colour ("#rrggbb" or {r,g,b}). |
| `world.gravity(value) -> bool` | document | Sets world gravity (drives the physics world too). |
| `world.fog({enabled, color, start, end}) -> bool` | document | Sets any subset of the fog settings. |
| `world.shadows({enabled}) -> bool` | document | Toggles shadow rendering. |
| `world.gi({mode, quality, bounces, light, boundsMin, boundsMax, pccGrid, autoRefresh}) -> bool` | document | Global illumination: mode off\|instant_radiosity\|vct\|vct_pcc_hybrid, quality low\|medium\|high, bounces 1-4, light = driving light guid ('' = auto, instant_radiosity only), boundsMin/boundsMax = lit volume corners (equal = fit the scene), pccGrid = {x,y,z} reflection-probe counts 1-8 per axis (hybrid only). |
| `world.antiAliasing() -> int` | document | Reads the anti-aliasing (MSAA) sample count. With the engine viewport live this is the ACHIEVED count (the driver may clamp the request); otherwise the scene's requested value. |
| `world.setAntiAliasing(samples) -> int` | document | Sets the scene's anti-aliasing: 1 (off), 2, 4 or 8 MSAA samples. Returns the achieved sample count (the driver may clamp; with no engine viewport, the requested value). |
| `world.shadowResolution() -> int` | document | Reads the shadow-map atlas base resolution in pixels. With the engine viewport live this is the value the renderer is actually using; otherwise the scene's setting, or 0 when it is on Auto with no shadow-casting light to derive from. |
| `world.setShadowResolution(pixels) -> int` | document | Sets the scene's shadow-map atlas base resolution: 0 = Auto (derive from the largest per-light Shadow Size), otherwise 256..8192 pixels. There is ONE atlas for every light in the scene, sized R x 3.5R at 32-bit depth: 1024 costs ~14 MB, 2048 ~56 MB, 4096 ~224 MB, 8192 ~896 MB of VRAM. Returns the applied value after clamping. |
| `world.ambientFromSky(enabled) -> bool` | document | Sky-driven ambient light: when on (the default), the ambient hemisphere colours are integrated from the live sky (equirect, gradient, realistic or cubemap) instead of being the flat Ambient Color; the Ambient Color then becomes the per-channel strength/tint of that sky ambient (white = full strength, black = none). Single-colour skies always use the flat colour. |
| `world.sky(type, {...}) -> bool` | document | Sets the sky. Types: color {color}; gradient {top, mid, bottom, offset}; realistic {luminance, reileigh, mieCoefficient, mieDirectionalG, turbidity, azimuth, elevation \| sunPosX, sunPosY, sunPosZ, detail}; equirectangular {texture}; cubemap {front, back, left, right, top, bottom} (textures = asset guids or file names in the project). For the realistic sky, azimuth (degrees clockwise from +Z) and elevation (degrees above the horizon) are the readable way to place the sun and win over raw sunPos*; turbidity is Preetham's 1..20 haze; detail is the equirect bake width (256, 512 or 1024). |
| `world.get() -> {ambient, gravity, fog, shadows, gi, sky}` | document | Reads the current world settings. |

## assets

| verb | needs | description |
|---|---|---|
| `assets.list({scope: 'store'\|'project'\|'session', type}) -> [{guid, name, type, drawer}]` | document | Store assets (default) or the open project's assets, optionally filtered by type name. A type-filtered project listing sweeps every folder (materials registered under Presets/ included); unfiltered it lists the root folder. drawer is the containing drawer's id (0 = Uncategorized). Scope 'session' lists the live session registrations (the AssetManager entries project open + add-to-project hydrate — what the editor's drag-drop paths look up); drawer is absent there. |
| `assets.metadata(guid) -> {guid, name, type, imported, kind, format, fileSize, ...}` | document | Rich per-type metadata for a store asset. Models: vertices, triangles, meshes, materials, textures; images: width, height; audio (wav): duration (ms), sampleRate, channels, bitsPerSample; video: duration (ms), width, height, frameRate, videoCodec; every kind: format + fileSize. Computed at import since the metadata feature landed; for older rows the first call computes it from the store files and persists it (lazy backfill). |
| `assets.import(path) -> guid` | document | Imports a mesh file (obj, fbx, dae, blend, glb, gltf, ply, stl) into the global asset store. NOT undoable. |
| `assets.importFile(path, drawerId?) -> guid` | document | Imports any library-supported file (models, images, audio, video) into the asset store, optionally filed in a drawer. Images/audio/video are headless-safe (video decodes through Qt Multimedia's ffmpeg backend, no display needed). NOT undoable. |
| `assets.drawers() -> [{id, name, parent}]` | document | The asset drawers (nested collections). parent -1 = top level; Uncategorized is drawer 0. |
| `assets.createDrawer(name, parentId?) -> id` | document | Creates a drawer, optionally nested under an existing one (default: top level). Returns the new drawer's id. |
| `assets.renameDrawer(id, name) -> bool` | document | Renames a drawer. The virtual root (-1) is not renamable. |
| `assets.deleteDrawer(id) -> bool` | document | Deletes a drawer and its sub-drawers; the subtree's assets move to Uncategorized. Drawer 0 and the root are refused. |
| `assets.moveDrawer(id, parentId) -> bool` | document | Reparents a drawer (parentId -1 = top level). Cycles are refused. |
| `assets.moveToDrawer(guid, id) -> bool` | document | Files a store asset in a drawer (0 = Uncategorized). |
| `assets.addToProject(storeGuid) -> guid` | document | Copies a store asset (files + DB rows + dependencies, fresh guids) into the open project; returns the project-side guid. NOT undoable. |
| `assets.addToScene(guid, {position}) -> nodeId` | document | Instantiates a project object asset into the scene (undoable, like a drag from the asset browser). |
| `assets.builtins() -> [{guid, name, kind}]` | document | The reserved built-ins: primitives, materials and shaders with their reserved guids. Guids collide across kinds — always pair guid with kind. |
| `assets.remove(guid, {keepShared: true}) -> bool` | document | Deletes a store asset: its rows, its store folder, and (keepShared false) its dependency assets too. PERMANENT — no undo. |
| `assets.refreshThumbnail(guid) -> bool` | document | Rebuilds an asset's thumbnail synchronously and writes it to the database. Objects, materials and shader graphs render on the engine (engine required; a shader renders the material its graph evaluates to, on the preview sphere); images re-thumbnail from the source file, videos re-grab a first-second frame, and audio/file rows reset to their type icon (document-only). |
| `assets.thumbnail(guid) -> {guid, empty, bytes, width, height, centre: {r, g, b}}` | document | The thumbnail stored for an asset, as facts rather than pixels: byte size of the PNG blob, its decoded dimensions and the colour of its centre pixel (0-255). empty is true when the row carries no image. Document-only — it reads the database, it does not render. |
| `assets.exportRaw(guid, dir, {dependencies: true, hash: true}) -> {dir, manifest, files, assets, totalBytes, warnings}` | document | Exports a store asset's files (and, by default, its dependencies' files) as loose files with their original names into dir, plus a jah.manifest.json (manifest v2: guids, types, dependency edges, sizes, sha256 content ids — hashing skippable via {hash: false}). Identical bytes are written once; assets with no stored files still get manifest entries. The unified-export front half (ASSET_PIPELINE_SPEC §3.3); .jaf export joins it in the final half. |
| `assets.dependencies(guid) -> [guid]` | document | The asset plus all its dependencies, recursively. |
| `assets.storeRoot() -> path` | document | The active asset-store root directory (the assets/storeRoot setting; the AppData default when unset). |
| `assets.setStoreRoot(path, {move, force}) -> bool` | document | Repoints the asset store. Empty path returns to the default root. {move: true} copies the current store's contents to the new root first (verified; the old tree is retained). Without move, the target must already contain this library's store ({force: true} skips that check). Throws on failure; nothing changes on a failed call. |
| `assets.storeStatus() -> {root, online, missing}` | document | Store reachability: the active root, whether it is reachable (offline mode keeps the catalog fully usable), and how many library rows have no folder under it. |
| `assets.importSettings(guid) -> {sourceOid, importer, importerVersion, assimp, settings}` | document | The determinism record the ONE import pipeline stamped on the asset: content id of the source, importer name/version, assimp version and the request settings. |
| `assets.checkConsistency(guid) -> {consistent, expected, produced, ...}` | document | Re-runs the import pipeline's convert stage on the stored source and diffs the produced object set against the catalog (Unity -consistencyCheck): non-deterministic importers surface here. |
| `assets.verify({dbPath, root}) -> report` | document | Re-hashes every catalogued object against its oid: reports corrupt (bit-rot) and missing objects with counts and bytes. Defaults to the live library. |
| `assets.rebuildCatalog(dbPath, {root}) -> report` | document | Reconstructs catalog rows (assets + files + asset_files) from the store's sidecar/*.json into the given database — the disaster-recovery path. dbPath is REQUIRED (rebuilding into the live catalog is not implied); existing guids are left untouched; thumbnails are regenerable, not recovered. |

## materials

| verb | needs | description |
|---|---|---|
| `materials.presets() -> [{name, type, guid}]` | document | The built-in material presets (PBR only in engine mode); guid is the reserved id when one exists. |
| `materials.createGraph(name) -> guid` | document | Creates a new effect-graph asset in the open project from the shader template and opens it as the current graph. |
| `materials.loadGraph(guidOrPath) -> {nodes, master}` | document | Opens an effect graph (a Shader asset guid, or a .effect/.shader file path) as the current graph for graph.* verbs. |
| `materials.regenerate(shaderGuid) -> bool` | document | Re-evaluates and re-bakes a stored shader asset's maps into BakedMaps/<guid>/ (the 'cache deleted / app upgraded' recovery) and refreshes materials in the open scene that use that cache. |
| `materials.createFromImage(textureGuid, {graph}) -> materialGuid` | document | Creates the standard image material asset for a Texture (IMAGE_PLANE_SPEC option B1): a PBR .material with the image as baseColorMap (roughness 1, metallic 0; alpha images blend), a Material→Texture dependency row and an image-derived thumbnail. Created in the library; with a project open it is also pinned into the project (bin-visible, droppable). Direct image add-to-project runs this automatically; re-creating for the same image returns a fresh asset. With {graph: true} (B2, needs an open project) it instead creates an editable Shader GRAPH asset — texture → textureSampler → PbrMaster.BaseColor — returning the shader guid (opens in the Materials page; applies via the drawer/graph.toMaterial). NOT undoable. |

## material

| verb | needs | description |
|---|---|---|
| `material.apply(nodeId, presetOrGuid) -> bool` | document | Applies a built-in preset (by name or reserved guid) or a saved project material asset (by guid) to a node. A container node (an imported model's root) applies to every mesh under it, each with its own material instance. Also registers preset applies as a project asset, like the presets panel. Undoable. |
| `material.set(nodeId, {baseColor, roughness, metallic, baseColorMap, ...}) -> bool` | document | Sets material properties on a mesh node (PBR keys; *Map keys take texture paths or asset guids). Undoable per property. |
| `material.get(nodeId) -> {property: value}` | document | Reads the node material's editor-facing properties. |

## graph

| verb | needs | description |
|---|---|---|
| `graph.nodes() -> [{id, type, master}]` | document | The current graph's nodes. |
| `graph.nodeTypes() -> [type]` | document | Every node type the library can create (plus the master type PbrMaterial). |
| `graph.addNode(type) -> id` | document | Adds a node to the current graph ('PbrMaterial' adds and sets the master). |
| `graph.connect(fromId, fromSocket, toId, toSocket) -> bool` | document | Connects an output socket to an input socket; sockets by index or name (e.g. 'Base Color'). |
| `graph.setValue(nodeId, value) -> bool` | document | Sets a node's value through the same path the editor uses (numbers, {r,g,b,a} colors, {x,y,z} vectors). |
| `graph.getValue(nodeId) -> value` | document | Reads a node's value back. |
| `graph.evaluate() -> {values, unsupported, approximated, animated, hasPbrMaster}` | document | Folds the current graph to PBR material values (the evaluator is GL-free by design). Pure math chains fold; approximated lists nodes evaluated against the fake fragment context (worldNormal, fresnel, time at t=0, ...). |
| `graph.bakeInfo() -> {perSocket: {socketName: class}}` | document | Classifies each master input: 'uniform' \| 'passthrough' \| 'baked' \| 'unsupported' \| 'unconnected'. |
| `graph.bake({resolution?, time?}) -> {values, maps, passthrough, approximated, unsupported, animated, msElapsed}` | document | Full-quality synchronous bake of the current graph: UV-varying chains render per texel into <project>/BakedMaps/<guid>/ PNGs (hash-cached, headless-capable - CPU only), uniform chains fold, bare textures pass through. Map values are project-relative paths. |
| `graph.toMaterial(nodeId) -> bool` | document | Evaluates the current graph and applies the resulting PBR material to a mesh node. |
| `graph.save() -> bool` | document | Serializes the current graph back into its shader asset (only for graphs opened from an asset guid). |
| `graph.selectNode(id) -> bool` | document | Selects a node. When the Effects page has a node with this id its canvas selection (and the properties panel) follows; otherwise the id must belong to the current script graph. |
| `graph.selectedNode() -> id\|null` | document | The selected node's id: the Effects page's canvas selection when one exists, else the script-side selection. |
| `graph.deselect() -> bool` | document | Clears the selection (canvas and script-side). |
| `graph.settings() -> {name, blendMode, bakeResolution}` | document | The current graph's material settings; blendMode is one of 'Opaque' \| 'Masked' \| 'Translucent' \| 'Additive' \| 'Modulate'. |
| `graph.setBlendMode(mode) -> bool` | document | Sets the master material's blend mode ('Opaque' \| 'Masked' \| 'Translucent' \| 'Additive' \| 'Modulate' — the Unreal set; 'Blend' is accepted as the legacy name for 'Translucent'). Material state only: bakes are unaffected, the evaluated material's alphaMode changes. |

## avatar

| verb | needs | description |
|---|---|---|
| `avatar.loadPreview(path) -> {name, file, bones, meshes, vertices, influences, clips:[{name, rawName, length}]}` | document | Loads a rigged model file (fbx/glb/obj/...) into the Avatar page's own preview — no library row, no project pin, no database write, no undo command. Embedded textures are extracted to a per-session scratch dir. Replaces whatever was loaded (one subject at a time). |
| `avatar.loadAnimation(path) -> {file, name, added, clips:[...], match:{channels, boneChannels, matched}}` | document | Loads a SEPARATE animation file onto the character already in the preview and appends its clips to the list — the Mixamo workflow (one character download, then one file per animation). Accepts both export shapes: a with-skin animation file (its mesh is ignored) and an animation-only file (zero meshes, which the mesh loaders reject outright). Clips accumulate; nothing is switched — call avatar.setClip to play one. Clip names come from the ANIMATION file's base name when the file uses a junk name, which every Mixamo export does. THROWS when the file animates a different rig (the clip->bone join is by scene-node name, so a foreign clip would load and move nothing): the message names the bones that do not exist on the loaded rig. |
| `avatar.clearPreview() -> bool` | document | Removes the previewed model and deletes its scratch extract dir. |
| `avatar.preview() -> {name, file, bones, meshes, vertices, influences, clips, activeClip, time, duration, playing, looping, meshVisible, skeletonVisible} \| undefined` | document | Everything the page shows about the loaded model, including transport and toggle state. Undefined (falsy) when nothing is loaded. |
| `avatar.setMeshVisible(on) -> bool` | document | Shows or hides the skinned mesh. Independent of the skeleton toggle: all four combinations are valid. |
| `avatar.setSkeletonVisible(on) -> bool` | document | Shows or hides the bone-line overlay. Independent of the mesh toggle. |
| `avatar.clips() -> [{name, rawName, length, looping, active, source, external}]` | document | Every clip the preview knows about: the ones the character file carried, plus every one avatar.loadAnimation has added since (`external`, with `source` naming the file it came from). `name` is the display name: every Mixamo clip is literally called 'mixamo.com', so junk names fall back to the source file's base name (`rawName` keeps what the file said). |
| `avatar.history() -> [{file, name, loaded}]` | document | The character files loaded in this session — what the page's left column lists. Session-local and not persisted (the avatar library is Part 1's). |
| `avatar.forget(path) -> bool` | document | Drops a file from the session list (the left column's right-click Delete). Clears the preview when it is the loaded one. Deletes nothing on disk. |
| `avatar.setRootMotion(on) -> bool` | document | Root motion for the preview. Off (the default) plays locomotion clips IN PLACE — the horizontal translation of the clip's root-most animated bone is pinned to its first key, so a walk cycle walks on the spot instead of leaving the frame. On plays the clip exactly as authored. Vertical motion is never stripped, so a jump still leaves the ground. |
| `avatar.setClip(name) -> bool` | document | Makes `name` (display or raw) the active clip and rewinds to 0 — including a clip loaded from a separate file by avatar.loadAnimation. What the ANIMATIONS list double-click calls. The transport state carries over: switching while playing keeps playing, from the start of the new clip. Every bone is put back on its rest pose first, so a clip that does not mention a bone cannot inherit the previous clip's pose for it. |
| `avatar.playClip(name) -> bool` | document | Starts the preview transport. With a name, selects that clip first; without one, resumes the active clip. Drives the module's OWN preview document — never the editor scene's clock. |
| `avatar.pause() -> bool` | document | Stops advancing time, keeping the current pose. |
| `avatar.stop() -> bool` | document | Pauses and rewinds to time 0. |
| `avatar.setLooping(on) -> bool` | document | Loops the active clip (default on). |
| `avatar.setTime(seconds) -> bool` | document | Scrubs the preview to `seconds` and re-evaluates the pose immediately. |
| `avatar.time() -> number` | document | The preview's current time in seconds. |
| `avatar.bones() -> [{name, parent, position:{x,y,z}}]` | engine | The rig as the preview resolves it: one entry per bone that has a scene node, `parent` being the NEAREST ancestor that is also a bone (assimp pivot nodes sit between real bones, and Bone::parentBone is empty for such rigs). World-space positions AT THE CURRENT TIME, read back from the engine's evaluated skeleton — clip evaluation is the engine's, so a pose only exists where an engine does. Under --headless the rig's shape (names, parents, hierarchy) is still reported but the positions are the REST pose. |
| `avatar.snapshot(path, w=256, h=256, probes=[]) -> {path, width, height, center:{r,g,b}, probes:[{x,y,r,g,b}]}` | engine | Offscreen render of the Avatar page's preview scene to a PNG, with the centre pixel and each probe point ({x,y} normalized 0..1) returned so scripts can assert on colours — the way a script (or an MCP session) proves the skeleton-only view from outside the app. |

