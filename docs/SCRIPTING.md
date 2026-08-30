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
| `assets.list({scope: 'store'\|'project', type}) -> [{guid, name, type, drawer}]` | document | Store assets (default) or the open project's assets, optionally filtered by type name. drawer is the containing drawer's id (0 = Uncategorized). |
| `assets.import(path) -> guid` | document | Imports a mesh file (obj, fbx, dae, blend, glb, gltf) into the global asset store. NOT undoable. |
| `assets.importFile(path, drawerId?) -> guid` | document | Imports any library-supported file (models, images, audio) into the asset store, optionally filed in a drawer. Images/audio are headless-safe. NOT undoable. |
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
| `assets.refreshThumbnail(guid) -> bool` | engine | Re-renders an object or material asset's thumbnail synchronously and writes it to the database. |
| `assets.dependencies(guid) -> [guid]` | document | The asset plus all its dependencies, recursively. |

## materials

| verb | needs | description |
|---|---|---|
| `materials.presets() -> [{name, type, guid}]` | document | The built-in material presets (PBR only in engine mode); guid is the reserved id when one exists. |
| `materials.createGraph(name) -> guid` | document | Creates a new effect-graph asset in the open project from the shader template and opens it as the current graph. |
| `materials.loadGraph(guidOrPath) -> {nodes, master}` | document | Opens an effect graph (a Shader asset guid, or a .effect/.shader file path) as the current graph for graph.* verbs. |

## material

| verb | needs | description |
|---|---|---|
| `material.apply(nodeId, presetOrGuid) -> bool` | document | Applies a built-in preset (by name or reserved guid) to a mesh node. Also registers the material as a project asset, like the presets panel. |
| `material.set(nodeId, {baseColor, roughness, metallic, baseColorMap, ...}) -> bool` | document | Sets material properties on a mesh node (PBR keys; *Map keys take texture paths or asset guids). Undoable per property. |
| `material.get(nodeId) -> {property: value}` | document | Reads the node material's editor-facing properties. |

## graph

| verb | needs | description |
|---|---|---|
| `graph.nodes() -> [{id, type, master}]` | document | The current graph's nodes. |
| `graph.nodeTypes() -> [type]` | document | Every node type the library can create (plus the master types PbrMaterial and Material). |
| `graph.addNode(type) -> id` | document | Adds a node to the current graph ('PbrMaterial' adds and sets the master). |
| `graph.connect(fromId, fromSocket, toId, toSocket) -> bool` | document | Connects an output socket to an input socket; sockets by index or name (e.g. 'Base Color'). |
| `graph.setValue(nodeId, value) -> bool` | document | Sets a node's value through the same path the editor uses (numbers, {r,g,b,a} colors, {x,y,z} vectors). |
| `graph.getValue(nodeId) -> value` | document | Reads a node's value back. |
| `graph.evaluate() -> {values, unsupported, hasPbrMaster}` | document | Folds the current graph to PBR material values (the evaluator is GL-free by design). |
| `graph.toMaterial(nodeId) -> bool` | document | Evaluates the current graph and applies the resulting PBR material to a mesh node. |
| `graph.save() -> bool` | document | Serializes the current graph back into its shader asset (only for graphs opened from an asset guid). |

