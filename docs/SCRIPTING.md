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
| `editor.frame(n=1) -> bool` | engine | Renders exactly n frames synchronously (document->engine sync + renderOneFrame) — the deterministic stepping the test suites use. |
| `editor.screenshot(path, w=256, h=256) -> {path, width, height, center:{r,g,b}}` | engine | Offscreen render of the editor scene to a PNG; returns the centre pixel so scripts can assert on it. Headless-safe. |
| `editor.beginBatch() -> bool` | document | Opens a nested undo macro inside the script's run (finer-grained grouping). |
| `editor.endBatch() -> bool` | document | Closes the macro opened by editor.beginBatch(). |

## app

| verb | needs | description |
|---|---|---|
| `app.desktop(n=0) -> current` | window | Switches to desktop 1-4; app.desktop() just returns the current one. |
| `app.space(name) -> bool` | window | Switches the main window space: desktop, player, editor, materials, assets, publish. player and editor need an open project. |

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
| `world.sky(type, {...}) -> bool` | document | Sets the sky. Types: color {color}; gradient {top, mid, bottom, offset}; realistic {luminance, reileigh, mieCoefficient, mieDirectionalG, turbidity, sunPosX, sunPosY, sunPosZ}; equirectangular {texture}; cubemap {front, back, left, right, top, bottom} (textures = asset guids or file names in the project). |
| `world.get() -> {ambient, gravity, fog, shadows, gi, sky}` | document | Reads the current world settings. |

## assets

| verb | needs | description |
|---|---|---|
| `assets.list({scope: 'store'\|'project', type}) -> [{guid, name, type, drawer}]` | document | Store assets (default) or the open project's assets, optionally filtered by type name. A type-filtered project listing sweeps every folder (materials registered under Presets/ included); unfiltered it lists the root folder. drawer is the containing drawer's id (0 = Uncategorized). |
| `assets.metadata(guid) -> {guid, name, type, imported, kind, format, fileSize, ...}` | document | Rich per-type metadata for a store asset. Models: vertices, triangles, meshes, materials, textures; images: width, height; audio (wav): duration (ms), sampleRate, channels, bitsPerSample; video: duration (ms), width, height, frameRate, videoCodec; every kind: format + fileSize. Computed at import since the metadata feature landed; for older rows the first call computes it from the store files and persists it (lazy backfill). |
| `assets.import(path) -> guid` | document | Imports a mesh file (obj, fbx, dae, blend, glb, gltf) into the global asset store. NOT undoable. |
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
| `assets.refreshThumbnail(guid) -> bool` | document | Rebuilds an asset's thumbnail synchronously and writes it to the database. Objects and materials render on the engine (engine required); images re-thumbnail from the source file, videos re-grab a first-second frame, and audio/file rows reset to their type icon (document-only). |
| `assets.dependencies(guid) -> [guid]` | document | The asset plus all its dependencies, recursively. |

## materials

| verb | needs | description |
|---|---|---|
| `materials.presets() -> [{name, type, guid}]` | document | The built-in material presets (PBR only in engine mode); guid is the reserved id when one exists. |
| `materials.createGraph(name) -> guid` | document | Creates a new effect-graph asset in the open project from the shader template and opens it as the current graph. |
| `materials.loadGraph(guidOrPath) -> {nodes, master}` | document | Opens an effect graph (a Shader asset guid, or a .effect/.shader file path) as the current graph for graph.* verbs. |
| `materials.regenerate(shaderGuid) -> bool` | document | Re-evaluates and re-bakes a stored shader asset's maps into BakedMaps/<guid>/ (the 'cache deleted / app upgraded' recovery) and refreshes materials in the open scene that use that cache. |

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

