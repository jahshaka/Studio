---
name: jahshaka-assets
description: Import files into Jahshaka's asset store, browse the library visually, organize drawers, and bring assets into projects and scenes via the assets verbs. Use when the user asks to import models/images/audio/video, find or organize library assets, or place an asset in the scene.
version: 4
---

# Assets in Jahshaka

Asset work goes through `run_script` (JavaScript on the live editor);
`api_docs({module:"assets"})` has the current signatures.

**Asset mutations are PERMANENT — no undo.** Imports, removals and drawer
deletes cannot be Ctrl+Z'd. Confirm with the user before `assets.remove` or
`assets.deleteDrawer`.

There are two scopes: the global **store** (the shared library) and the open
**project** (which *pins* store assets rather than copying them). There is
exactly ONE way to get a file on disk into a scene, and one verb that does it:

```js
var r = assets.importAndPlace("/path/to/prop.obj", { position: {x:0, y:0, z:0} });
// -> { assetGuid, projectGuid, nodeId }
```

It is these three calls, in one — reach for them separately only when you need
a step in between (filing in a drawer is an option on `importAndPlace` already):

```js
var g = assets.importFile("/path/to/prop.obj");   // 1. into the store
var p = assets.addToProject(g);                   // 2. pin it into this project
var nodeId = assets.addToScene(p, { position: {x:0, y:0, z:0} });  // 3. place it
```

**Steps 1 and 2 are NOT undoable; only the placement is** — so an undo after
`importAndPlace` removes the node and leaves the library asset. Say so if the
user asks to "undo the import".

**`scene.addMesh(path)` always FAILS.** It appears in older notes; it used to
write the disk path where the scene reader expects an asset guid, so the object
came back empty on the next open and never exported. Its error message names
`assets.importAndPlace`.

## Importing

```js
// Meshes (obj, fbx, dae, blend, glb, gltf, ply, stl) into the global store:
var guid = assets.import("/path/to/model.glb");

// Anything the library supports — models, images, audio, video —
// optionally filed straight into a drawer:
var guid2 = assets.importFile("/path/to/texture.png", drawerId);
```

Only paths the user gives you are reachable — there is no file browser here;
ask for the path when you don't have it.

## Finding things — LOOK at the library

`browse_assets` is the only tool that carries pictures: it returns the matching
rows as JSON *and* each asset's stored thumbnail as an image, captioned with
its name and guid so rows and pictures line up. Use it when the user says
"the wooden chair" and you have to work out which guid that is.

```
browse_assets({ query: "chair" })
browse_assets({ type: "texture", drawer: 3, limit: 12 })
```

Images are budgeted (a small default, a hard maximum, thumbnails downscaled),
so ask narrowly. An asset with no stored thumbnail still appears in the rows,
flagged — `assets.refreshThumbnail(guid)` renders one.

It adds no capability: everything it filters on, a script can ask for too —
only the PIXELS need a tool, because a script result is JSON.

```js
assets.list({ query: "chair", drawer: 3, limit: 10 });
```

## Inspecting

```js
assets.list();                          // store assets (default)
assets.list({ scope: "project" });      // the open project's assets
assets.list({ type: "Object" });        // filter by type name
assets.metadata(guid);                  // per-type rich metadata: models get
                                        // vertices/triangles/meshes/materials/
                                        // textures; images width/height; audio
                                        // and video duration etc.
assets.dependencies(guid);              // the asset + all dependencies
assets.builtins();                      // reserved primitives/materials/shaders
                                        // (guids collide across kinds — always
                                        // pair guid with kind)
```

## Drawers (library organization)

```js
assets.drawers();                       // [{id, name, parent}] — parent -1 is
                                        // top level; drawer 0 = Uncategorized
var d = assets.createDrawer("Props");
var sub = assets.createDrawer("Furniture", d);   // nested
assets.renameDrawer(sub, "Chairs");
assets.moveDrawer(sub, -1);             // to top level
assets.moveToDrawer(guid, d);           // file an asset (0 = Uncategorized)
assets.deleteDrawer(d);                 // PERMANENT; subtree's assets move to
                                        // Uncategorized
```

## Into the project and the scene

```js
// Pin a store asset (and its dependency closure) into the open project;
// returns the guid to use from here on:
var pguid = assets.addToProject(storeGuid);

// Instantiate a project object asset into the scene (this one IS undoable,
// like dragging from the asset browser):
var nodeId = assets.addToScene(pguid, { position: {x: 0, y: 0, z: 0} });

// All three at once, for a file the user just handed you — optionally filed
// in a drawer on the way through:
var r = assets.importAndPlace("/path/to/chair.glb",
                              { position: {x: 2, y: 0, z: 0}, drawer: d });
console.log(r.assetGuid, r.projectGuid, r.nodeId);

// Fix a stale/blank thumbnail:
assets.refreshThumbnail(guid);
```

## Verify

After imports, confirm with `assets.list` + `assets.metadata` (did the mesh
really have the triangle count you expect?), or `browse_assets({query: name})`
to see the thumbnail. After `addToScene` or `importAndPlace`, use
`describe_scene` and the `screenshot` tool — aim it at the new node with
`screenshot({frameNode: {id: r.nodeId}})` instead of hoping the current view
contains it.

An import that "worked" but shows nothing is usually a RENDERER refusal, not a
script error: check the `engineErrors` block on the `run_script` response, or
`app.engineErrors()` after `editor.frame(1)`. The `jahshaka-scene-building`
skill's Debugging section has the loop.

`importAndPlace` refuses anything it does not understand rather than guessing:
a path that is not there, an option key you misspelled, a drawer *name* where
an id belongs, and a file that imports as an image/audio/video (those are not
placeable — the library asset still lands, and the error says so).
