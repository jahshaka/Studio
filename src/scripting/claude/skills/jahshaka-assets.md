---
name: jahshaka-assets
description: Import files into Jahshaka's asset store, organize drawers, and bring assets into projects and scenes via the assets verbs. Use when the user asks to import models/images/audio/video, organize the asset library, or place an asset in the scene.
version: 1
---

# Assets in Jahshaka

Asset work goes through `run_script` (JavaScript on the live editor);
`api_docs({module:"assets"})` has the current signatures.

**Asset mutations are PERMANENT — no undo.** Imports, removals and drawer
deletes cannot be Ctrl+Z'd. Confirm with the user before `assets.remove` or
`assets.deleteDrawer`.

There are two scopes: the global **store** (the shared library) and the open
**project** (its private copies). The flow is: import → store, file in a
drawer, `addToProject`, then `addToScene`.

## Importing

```js
// Meshes (obj, fbx, dae, blend, glb, gltf) into the global store:
var guid = assets.import("/path/to/model.glb");

// Anything the library supports — models, images, audio, video —
// optionally filed straight into a drawer:
var guid2 = assets.importFile("/path/to/texture.png", drawerId);

// Import a mesh file DIRECTLY into the scene (no store round-trip):
var nodeId = scene.addMesh("/path/to/prop.obj", { position: {x:0, y:0, z:0} });
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
// Copy a store asset (files + DB rows + dependencies, fresh guids) into the
// open project; returns the PROJECT-side guid — use that from here on:
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
