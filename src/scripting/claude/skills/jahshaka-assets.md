---
name: jahshaka-assets
description: Import files into Jahshaka's asset store, organize drawers, and bring assets into projects and scenes via the assets verbs. Use when the user asks to import models/images/audio/video, organize the asset library, or place an asset in the scene.
version: 2
---

# Assets in Jahshaka

Asset work goes through `run_script` (JavaScript on the live editor);
`api_docs({module:"assets"})` has the current signatures.

**Asset mutations are PERMANENT — no undo.** Imports, removals and drawer
deletes cannot be Ctrl+Z'd. Confirm with the user before `assets.remove` or
`assets.deleteDrawer`.

There are two scopes: the global **store** (the shared library) and the open
**project** (which *pins* store assets rather than copying them). There is
exactly ONE way to get a file on disk into a scene, and it is three calls:

```js
var g = assets.importFile("/path/to/prop.obj");   // 1. into the store
var p = assets.addToProject(g);                   // 2. pin it into this project
var nodeId = assets.addToScene(p, { position: {x:0, y:0, z:0} });  // 3. place it
```

**Do not look for a one-call shortcut.** `scene.addMesh(path)` exists in older
notes and always FAILS now: it used to write the disk path where the scene
reader expects an asset guid, so the object came back empty on the next open
and never exported. Steps 1 and 2 are not undoable; step 3 is.

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

// Fix a stale/blank thumbnail:
assets.refreshThumbnail(guid);
```

## Verify

After imports, confirm with `assets.list` + `assets.metadata` (did the mesh
really have the triangle count you expect?). After `addToScene`, use
`describe_scene` and the `screenshot` tool to see the placed asset.
