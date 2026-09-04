---
name: jahshaka-assets
description: Import files into Jahshaka's asset store, organize drawers, and bring assets into projects and scenes via the assets verbs. Use when the user asks to import models/images/audio/video, organize the asset library, or place an asset in the scene.
version: 3
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
really have the triangle count you expect?). After `addToScene` or
`importAndPlace`, use `describe_scene` and the `screenshot` tool to see the
placed asset.

`importAndPlace` refuses anything it does not understand rather than guessing:
a path that is not there, an option key you misspelled, a drawer *name* where
an id belongs, and a file that imports as an image/audio/video (those are not
placeable — the library asset still lands, and the error says so).
