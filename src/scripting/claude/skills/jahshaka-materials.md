---
name: jahshaka-materials
description: Author PBR materials in Jahshaka — presets, per-property edits, texture maps, and node-based shadergraph authoring via the materials/material/graph verbs. Use when the user asks about materials, textures, colors, metal/roughness, or effect graphs.
version: 2
---

# Materials in Jahshaka

All material work goes through `run_script` (JavaScript on the live editor).
`api_docs({module:"material"})`, `{module:"materials"}` and `{module:"graph"}`
give the current signatures; `api_docs({verb:"material.set"})` gives one.

Jahshaka's engine materials are PBR metallic-roughness. Material edits via
`material.set` are undoable per property; graph edits save into the project's
shader asset.

## Ask the material what it accepts

Do not guess key spellings — the legacy shader names (`diffuseTexture`,
`normalTexture`, …) are refused by name on a PBR material:

```js
material.properties(nodeId);
// {class: "PbrMaterial",
//  rows: [{name:"roughness", displayName:"Roughness", type:"float",
//          value:0.5, min:0, max:1}, ...],
//  writableKeys: ["baseColor","roughness","metallic", ...,
//                 "baseColorMap","normalMap", ...]}
```

`writableKeys` is the exact set `material.set` accepts — read it rather than
deriving keys from `rows`. `min`/`max` are present only where a range is
really declared (metallic and roughness 0..1, emissiveIntensity 0..10), which
is where a slider value actually means something.

## Presets and direct properties

```js
materials.presets();               // [{name, type, guid}] — PBR presets:
                                   // e.g. Default, Silver, Gold, Glass, ...
material.apply(nodeId, "Silver");  // by name or reserved guid; also registers
                                   // the material as a project asset

// Read what a node has:
material.get(nodeId);              // {property: value} editor-facing keys

// Set PBR keys directly (undoable per property):
material.set(nodeId, {
  baseColor: "#8844ff",
  roughness: 0.25,        // 0 = mirror-sharp, 1 = fully rough
  metallic: 1.0,          // 0 = dielectric, 1 = metal
});

// Texture maps: *Map keys take file paths or asset guids
material.set(nodeId, {
  baseColorMap: "/path/to/albedo.png",
  normalMap: "/path/to/normal.png",
  roughnessMap: "/path/to/rough.png",
});
```

`materials.createFromImage(textureGuid)` mints the standard image material for
a Texture asset (the image as baseColorMap, roughness 1, metallic 0) when you
want a material rather than a whole graph.

Tips that match the engine's behavior:
- Glass: apply the `Glass` preset — it maps to true transparency that keeps
  specular highlights.
- Metals want a non-white `baseColor` (that IS the metal tint) and
  `metallic: 1`.
- Verify with the `screenshot` tool after applying, aimed at the object:
  `screenshot({frameNode: {id: nodeId, pitch: 15}})`. Lighting and IBL affect
  how a material reads far more than its raw numbers.
- A map that silently does nothing is usually a RENDERER refusal (an image
  that would not decode, a mesh with no tangents for a normal map). Those never
  reach the script's error — check the `engineErrors` block on the
  `run_script` response, or `app.engineErrors()` after `editor.frame(1)`.

## Shadergraph (node-based) authoring

The graph verbs operate on "the current graph" — create or load one first:

```js
// New effect-graph asset in the open project (opens it as current):
var guid = materials.createGraph("MyEffect");

// Or load an existing one (Shader asset guid, or .effect/.shader path):
materials.loadGraph(guid);         // -> {nodes, master}

graph.nodeTypes();                 // every creatable node type
                                   // (+ masters: PbrMaterial, Material)
graph.nodes();                     // [{id, type, master}]

// Build: add nodes, set values, connect into the PBR master
var master = graph.addNode("PbrMaterial");   // adds AND sets the master
var col = graph.addNode("color");
graph.setValue(col, { r: 0.8, g: 0.2, b: 0.1, a: 1 });
graph.connect(col, 0, master, "Base Color"); // sockets by index or name

// Evaluate the graph to concrete PBR values (CPU evaluator, GL-free):
graph.evaluate();                  // {values, unsupported, hasPbrMaster}

// Apply the evaluated result to a mesh node:
graph.toMaterial(nodeId);

// Persist the graph back into its shader asset (asset-guid graphs only):
graph.save();
```

Workflow notes:
- `graph.evaluate().unsupported` lists nodes the CPU evaluator cannot fold —
  if your target values land there, prefer direct `material.set`.
- `graph.setValue` accepts numbers, `{r,g,b,a}` colors and `{x,y,z}` vectors,
  through the exact code path the editor's own value widgets use.
- Socket names are the visible labels (e.g. "Base Color"); indices work too.
- One `run_script` call per authoring step keeps the undo story sane; graph
  saves are asset writes (not undoable).
