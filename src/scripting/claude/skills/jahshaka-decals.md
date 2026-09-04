---
name: jahshaka-decals
description: Put images into a Jahshaka scene — projected decals (stickers, grime, posters painted onto existing geometry) and image planes (a flat quad showing a picture). Use when the user asks to project, stencil or stick an image onto something, or to show a photo/logo/reference image in the scene.
version: 1
---

# Decals and image planes

Two different answers to "put this image in the scene". Pick deliberately:

| | **Decal** | **Image plane** |
|---|---|---|
| What it is | a projector box that PAINTS onto whatever geometry is inside it | a flat quad with the image as its material |
| Use for | grime, tyre marks, bullet holes, a logo on a curved wall | a photo, a reference board, a poster hanging in space |
| Follows surfaces | yes — it wraps whatever it covers | no — it is its own flat object |
| Verb | `scene.addDecal` | `scene.addImagePlane` |

Both take a **Texture asset guid**, not a file path. If the user hands you a
file, import it first — `assets.importFile(path)` returns the guid (see the
`jahshaka-assets` skill), and `browse_assets({query: "..."})` finds one that is
already in the library, thumbnail and all.

## Decals

```js
var d = scene.addDecal(textureGuid, {
  position: {x: 0, y: 1, z: 0},
  width: 2, height: 2, depth: 1,     // extents of the projector box
  roughness: 0.6, metalness: 0.0
});
```

Geometry that matters:

- The decal projects down the node's **-Y** (the same convention lights use).
  Rotate the node to aim it: a decal on a vertical wall wants a 90° pitch.
- `width` is the local-X extent, `height` the local-Z extent (the image's V
  axis), `depth` the projection thickness — how far through the scene it
  paints. Too little depth and it misses the surface entirely; too much and it
  bleeds onto things behind.
- **The node's own scale multiplies all three**, so a scaled decal node is a
  second way to size it.
- The image's ALPHA is the mask. `ignoreAlphaDiffuse: true` keeps the alpha out
  of the diffuse contribution.
- `textureGuid` may be empty: the decal then draws its wire box and projects
  nothing until an image is bound.

Editing afterwards:

```js
node.properties(d);                       // decalTexture, width, height, depth,
                                          // metalness (0..1), roughness (0..1),
                                          // ignoreAlphaDiffuse
node.setProperty(d, "depth", 2.5);
node.setDecalTexture(d, otherGuid);       // "" clears it
node.decalTexture(d);                     // {guid, path}
```

`decalTexture` is an ASSET BINDING: `node.setDecalTexture` owns it (it pins the
image into the project as a dependency). Do not try to set it with
`node.setProperty`.

**There is deliberately no per-decal opacity or colour tint** — the renderer
packs four floats per decal and neither fits. Fade a decal by authoring the
image's alpha, not by looking for a verb.

## Image planes

```js
var p = scene.addImagePlane(textureGuid, {
  position: {x: 0, y: 1.5, z: -2},
  doubleSided: true            // the default
});
```

- The plane is sized to the image's aspect with a **1 m long side** — scale the
  node to make it bigger.
- It faces the editor camera at creation, so aim the view before adding one if
  the orientation matters.
- It carries a basic PBR material with the image as `baseColorMap`
  (roughness 1, metallic 0); an image with an alpha channel blends.
- Because it is an ordinary mesh with an ordinary material, everything in the
  `jahshaka-materials` skill applies: `material.set(p, {roughness: 0.4})`,
  `material.properties(p)`, and so on.

`materials.createFromImage(textureGuid)` mints the same standard image material
as a reusable project asset, when you want the material without the plane.

## Verifying

Both are easy to get wrong in ways that render as "nothing happened":

```js
editor.frame(1);                          // make the renderer actually try
console.log(JSON.stringify(app.engineErrors()));
```

- A decal that shows nothing: check `depth` (does the box reach the surface?),
  the -Y aim, and whether the image has an alpha channel that is masking
  everything away. A FULL DECAL ATLAS is a renderer refusal — it appears in
  `engineErrors`, never as a script error.
- An image plane that is black: the texture guid resolved to nothing, or the
  scene has no light. `material.get(p)` shows what the plane actually got.

Then look, aimed: `screenshot({frameNode: {id: d, pitch: 30, distance: 5}})`.
Both verbs are undoable as part of the run's single undo step; the image PIN
into the project is not.
