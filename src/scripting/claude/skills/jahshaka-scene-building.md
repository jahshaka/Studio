---
name: jahshaka-scene-building
description: Build and edit 3D scenes in the running Jahshaka editor via the jahshaka MCP tools — primitives, lights, transforms, world settings, and the screenshot-verify loop. Use whenever the user asks to create, arrange, or light something in the scene.
version: 1
---

# Building scenes in Jahshaka

You are connected to a LIVE Jahshaka editor through MCP. Your only capability
surface is the scripting engine: write JavaScript and execute it with the
`run_script` tool. Every `run_script` call is ONE undo step — batch related
edits into one script so the user can Ctrl+Z the whole thing.

The full, always-current verb reference is one call away: `api_docs` (optionally
`api_docs({module:"scene"})` for one module). When unsure about a signature,
check it there instead of guessing.

## Ground rules

- A project must be open before scene verbs work. Check with
  `project.current()` — it returns `{guid, name, folder}` or `null`. If null,
  ask the user which project to open (`project.list()`, `project.open(name)`)
  or create one (`project.create(name)`).
- Node ids are strings returned by the add verbs; hold them in variables inside
  one script. Across scripts, re-find nodes with `scene.find(name)` or
  `describe_scene`.
- Rotations are euler degrees. Positions/scales are `{x, y, z}` (also accepted
  as `[x, y, z]`).
- `console.log(...)` output comes back to you from `run_script` — use it to
  return ids and values.

## Core verbs

```js
// Primitives: plane, cone, cube, cylinder, sphere, torus, capsule, gear,
// pyramid, teapot, sponge, steps
var id = scene.addPrimitive("cube", { position: {x: 0, y: 0.5, z: 0} });

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
`rectWidth`, `rectHeight` (area lights).

## World settings

```js
world.ambient("#334455");
world.fog({ enabled: true, color: "#aabbcc", start: 20, end: 120 });
world.shadows({ enabled: true });
world.setAntiAliasing(4);                    // 1 (off), 2, 4, 8 MSAA
world.sky("gradient", { top: "#2a4d6e", mid: "#87a5c0", bottom: "#d8c8a8" });
world.sky("color", { color: "#101018" });
world.gi({ mode: "instant_radiosity", quality: "medium", bounces: 2 });
world.get();                                  // read everything back
```

## Materials (quick path)

```js
materials.presets();                          // list built-in PBR presets
material.apply(id, "Gold");                   // apply a preset by name
material.set(id, { baseColor: "#c02020", roughness: 0.3, metallic: 0.0 });
```

For texture maps and shadergraph authoring, use the `jahshaka-materials` skill.

## The verify loop — SEE your work

After meaningful edits, verify visually instead of assuming:

1. `describe_scene` — the scene graph plus current selection as JSON.
2. The `screenshot` tool — a render of the editor viewport. Look at it: is the
   object where you said? Is the light visible? Fix and re-shoot.
3. `editor.screenshot(path, w, h)` inside a script additionally returns the
   centre pixel `{r, g, b}` for programmatic checks.

Selection and framing helpers: `editor.select(id)`, `editor.focusSelection()`,
`editor.setGizmoMode("translate"|"rotate"|"scale")`, `editor.snapToFloor()`.

## Undo etiquette

- One user request → ideally one `run_script` call → one undo step.
- The `undo_redo` tool (`{action:"undo"}` / `{action:"redo"}`) is the escape
  hatch when the user asks to revert your last change.
- Asset operations (`assets.*` imports/removals) are NOT undoable — warn before
  destructive ones.
