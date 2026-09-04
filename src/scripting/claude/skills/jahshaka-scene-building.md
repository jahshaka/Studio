---
name: jahshaka-scene-building
description: Build and edit 3D scenes in the running Jahshaka editor via the jahshaka MCP tools — primitives, lights, transforms, physics, world settings, the camera verbs, and the look-act-look screenshot loop. Use whenever the user asks to create, arrange, light, film or debug something in the scene.
version: 2
---

# Building scenes in Jahshaka

You are connected to a LIVE Jahshaka editor through MCP. Your only capability
surface is the scripting engine: write JavaScript and execute it with the
`run_script` tool. Every `run_script` call is ONE undo step — batch related
edits into one script so the user can Ctrl+Z the whole thing.

Sibling skills: `jahshaka-assets` (importing and placing files),
`jahshaka-materials` (PBR and shadergraphs), `jahshaka-particles`,
`jahshaka-decals` (decals and image planes), `jahshaka-world` (World Modes
and quality tiers).

## Cold start: find the verb instead of guessing it

The registry is large. Ask narrowly, in this order:

- `api_docs({verb: "scene.addPrimitive"})` — one signature and its doc.
- `api_docs({search: "camera"})` — every verb whose name or doc mentions it.
- `api_docs({module: "node"})` — one module.
- Inside a script, the same thing without a tool round trip:
  `console.log(api.help("editor.setCamera"))`, `api.help("node")`,
  `api.verbs()` (the whole schema as JSON).

`api_docs()` with no arguments is the WHOLE reference — tens of thousands of
bytes. Only ask for it when you genuinely need to browse.

## Ground rules

- A project must be open before scene verbs work. `project.current()` returns
  `{guid, name, folder}` or `null`. If null, ask which project to open
  (`project.list()`, `project.open(name)`) or create one (`project.create`).
- Node ids are strings returned by the add verbs; hold them in variables inside
  one script. Across scripts, re-find nodes with `scene.find(name)` or
  `describe_scene`.
- Rotations are euler degrees. Positions/scales are `{x, y, z}` (also accepted
  as `[x, y, z]`).
- `console.log(...)` output comes back to you from `run_script`.

## Discovery first: node.properties, not guesswork

**Before setting anything on a node you did not just create, read what it
has.** `node.properties(id)` is the answer to "what can I set on this thing?"
— it lists every reflected row in document order:

```js
node.properties(id);
// [{name:"intensity", displayName:"Intensity", type:"float", value:1.2,
//   writable:true}, ...]
```

- `type` is `bool|int|float|vec3|color|texture|string|list`.
- `min`/`max` appear ONLY where a range is really declared. Absent means
  unbounded — never 0..0.
- `writable:false` means `node.setProperty` will refuse it (a mesh's
  `meshPath`, a particle emitter's `texture`): those need a dedicated verb.
- Asset bindings are NOT rows here — `node.setLightProfile`,
  `node.setLightTexture`, `node.setDecalTexture` own them.

`material.properties(nodeId)` is the same idea for materials, and it carries
`writableKeys` — the exact set `material.set` accepts. Read that instead of
inventing key spellings.

## Core verbs

```js
// Primitives: plane, ground, cone, cube, cylinder, sphere, torus, capsule,
// gear, pyramid, teapot, sponge, steps  ("ground" is the big floor plane)
var id = scene.addPrimitive("cube", { position: {x: 0, y: 0.5, z: 0} });
var row = scene.addPrimitive("cube", { count: 5 });   // -> ARRAY of 5 ids
row.forEach(function (n, i) { node.transform(n, { position: {x: i*2, y: 0.5, z: 0} }); });

// Lights: point, spot, directional, area
var sun = scene.addLight("directional", { position: {x: 0, y: 4, z: 0} });
node.setProperty(sun, "intensity", 1.2);
node.setProperty(sun, "lightColor", "#fff4e0");

// Transform (absolute; omitted parts keep their value)
node.transform(id, { rotation: {x: 0, y: 45, z: 0}, scale: {x: 2, y: 2, z: 2} });

// Hierarchy
var group = scene.addEmpty({ position: {x: 0, y: 0, z: 0} });
node.reparent(id, group);
node.duplicate(id);
node.remove(id);
```

Light properties readable/writable via `node.property` / `node.setProperty`:
`intensity`, `lightColor`, `distance`, `spotCutOff`, `spotCutOffSoftness`,
`rectWidth`, `rectHeight`, `doubleSided`, `accurate` (area lights),
`shadowMapType`, `shadowMapResolution`.

Asset bindings on a light have their own verbs, because they resolve a library
guid and pin it into the project:

```js
node.setLightProfile(id, iesGuid);   // an IES photometric profile ("" clears)
node.lightProfile(id);               // {guid, path, normalisation, applies}
node.setLightTexture(id, imageGuid); // an AREA light's mask/gobo ("" clears)
```

The `applies` flag is the renderer's truth, not the document's wish — an IES
profile shapes spot lights always and point lights only while they cast NO
shadows (directional and area lights never), and an area mask is sampled only
by the fast approximation (an `accurate`/LTC area light ignores it).

## Reading the scene cheaply

`describe_scene` is bounded by default (a couple of levels) — a truncated row
carries `childCount` and `truncated:true`, so nothing is silently missing.
Go deeper only where you need it:

```
describe_scene({ subtree: "<id>", depth: -1 })
describe_scene({ include: ["lights", "materials", "visibility", "world"] })
```

The same reads from a script: `scene.nodes({subtree, depth, include})`,
`world.get()`.

## The look-act-look loop — point the camera, then SEE it

You can aim the editor camera; you no longer have to hope the default view
shows your work.

```js
editor.frameNode(id, { yaw: 35, pitch: 20, distance: 6 }); // orbit-style framing
editor.setCamera({ position: {x: 6, y: 4, z: 6}, lookAt: {x: 0, y: 1, z: 0} });
editor.camera();   // {position, rotation, projection, orthoSize}
```

These MOVE THE USER'S VIEWPORT — there is no separate screenshot camera.

The `screenshot` tool takes the same two options and aims for you, then echoes
the resulting pose beside the image, so the next shot can be relative to the
last one:

```
screenshot({ frameNode: {id: "<id>", yaw: 35, pitch: 20} })
screenshot({ camera: {position: {x: 6, y: 4, z: 6}, lookAt: {x: 0, y: 1, z: 0}},
             width: 900, height: 600 })
```

For a CLEAN shot with no editor furniture, turn the helpers off first and put
them back afterwards:

```js
var before = editor.overlays();          // {grid, lightWires, selectionWireframe, gameView}
editor.setOverlays({ grid: false, lightWires: false, selectionWireframe: false });
// ... screenshot ...
editor.setOverlays(before);
```

(`editor.gameView(true)` is the one-switch version of the same thing.)

Inside a script, `editor.screenshot(path, w, h)` additionally returns the
centre pixel `{r, g, b}` for programmatic checks — useful when you want to
assert "the sphere is red" without looking.

Selection and framing helpers: `editor.select(id)`, `editor.focusSelection()`,
`editor.setGizmoMode("translate"|"rotate"|"scale")`, `editor.snapToFloor()`.

## Physics

The body settings the Properties panel writes are verbs now:

```js
node.physics(id, { type: "rigidbody", shape: "convexhull", mass: 2.0,
                   restitution: 0.4, friction: 0.6 });
node.physicsInfo(id);
// {enabled, type, shape, mass, restitution, friction, damping,
//  collisionMargin, isStatic, constraints}
```

Enums travel as NAMES, never ordinals, and a bad name is refused loudly.
`editor.simulate(true)` runs the simulation in place; `editor.play()` /
`editor.stop()` enter and leave play mode.

`scene.addViewer({position})` adds the first-person stand-in ("Avatar").
**Side effect worth saying out loud: the new viewer TAKES the active character
controller**, so a second `addViewer` silently demotes the first.

## World settings

```js
world.ambient("#334455");                     // world.setAmbient is the same verb
world.fog({ enabled: true, color: "#aabbcc", start: 20, end: 120 });
world.shadows({ enabled: true });
world.sky("gradient", { top: "#2a4d6e", mid: "#87a5c0", bottom: "#d8c8a8" });
world.gi({ mode: "instant_radiosity", quality: "medium", bounces: 2 });
world.get();                                  // read everything back
```

Every noun-setter also answers to `set*` (`world.setFog`, `world.setSky`, …) —
same arguments, same result. Quality tiers, MSAA and the post chain live in the
`jahshaka-world` skill.

## Materials (quick path)

```js
materials.presets();                          // built-in PBR presets
material.apply(id, "Gold");                   // apply a preset by name
material.set(id, { baseColor: "#c02020", roughness: 0.3, metallic: 0.0 });
material.properties(id);                      // rows + writableKeys
```

Texture maps and shadergraph authoring: the `jahshaka-materials` skill.

## DEBUGGING — when it does not look right

The renderer REFUSES rather than throws. A texture that would not decode, a
mesh the backend rejected, a full decal atlas: none of those reach a script's
return value or its error, so a script can "succeed" and draw nothing.

1. **`run_script` tells you.** When a run provokes renderer refusals, the
   response carries an `engineErrors` block. Read it before assuming the
   script was wrong.
2. **Errors are recorded when a FRAME runs.** A script that changes the scene
   without rendering usually reports its errors on the NEXT run that does.
   Force one: `editor.frame(1)`.
3. **The cumulative record** is `app.engineErrors()` — distinct messages,
   newest first, with counts and how many repeats the rate limiter swallowed.
   `app.engineErrors(true)` clears it after reading, which makes the next
   check a clean measurement.
   `drains: 0` means the frame loop is not running — not that the renderer is
   happy.
4. **Is anything on screen at all?** `editor.viewportState()` returns
   `{state, framesPresented, width, height, offscreen}`. `state` is
   `presenting` / `loading` / `noscene` / `offscreen`, and `framesPresented`
   lets you wait for real pixels instead of sleeping.
5. **Deterministic stepping.** `editor.frame(n, dt)` renders exactly n frames;
   with `dt >= 0` the document clock and the particle simulation advance by
   exactly that many seconds per frame instead of by wall clock. Use
   `editor.frame(30, 1/60)` to advance half a second of animation or fire
   reproducibly — without `dt`, a scripted frame buys about a millisecond.
6. **Then look.** `screenshot` (aimed, see above). A black image with
   `framesPresented` climbing is a lighting problem; a black image with it
   stuck is a viewport problem.

A typical debug turn:

```js
app.engineErrors(true);                 // clear
editor.frame(2);                        // make the renderer try
console.log(JSON.stringify(app.engineErrors()));
console.log(JSON.stringify(editor.viewportState()));
```

If a verb itself misbehaves, `app.apiProblems()` reports flaws in the API's own
metadata and should always be empty.

## Undo etiquette

- One user request → ideally one `run_script` call → one undo step.
- The `undo_redo` tool (`{action:"undo"}` / `{action:"redo"}`) is the escape
  hatch when the user asks to revert your last change.
- Asset operations (`assets.*` imports/removals) are NOT undoable — warn
  before destructive ones.
- A long script can be cut short: `run_script` takes `timeoutMs`. It aborts at
  JavaScript statement boundaries only — a run parked inside one native call
  (`editor.frame`, `graph.bake`, `editor.warmUpShaders`, an import) finishes
  that call first.
