# VR controller models — provenance

## What is here

| File | What it is |
|---|---|
| `meta-quest-touch-pro/left.obj`, `right.obj` | The Meta Quest Touch Pro controllers, ONE mesh each, positions + normals, in the WebXR **grip space** — which is the OpenXR grip pose's frame (metres, Y up, −Z forward out of the fist). 3,305 vertices / 4,106 triangles each. |
| `make-controller-obj.py` | The script that produced them from upstream, checked in so the bake is reproducible. |

Drawn by `SceneMirror::syncVrProxies` (irisgl/mirror/scenemirror.cpp) as the wearer's
controller proxies, **unlit grey, no textures**, on `kHelperBit | kVrHelperBit` — in every VR
eye and in the desktop editor's picture, in no capture and in no user's screenshot. The
hand-made wand beside them in that file is the fallback for every interaction profile we have
no model for (the simple controller, WMR, bare hands, nothing bound).

## Upstream

* Project: **immersive-web/webxr-input-profiles**, package `@webxr-input-profiles/assets`
  version **1.0.13** (`packages/assets`), profile `meta-quest-touch-pro`.
* Files taken, 2026-09-17:
  * `https://cdn.jsdelivr.net/npm/@webxr-input-profiles/assets@1.0.13/dist/profiles/meta-quest-touch-pro/left.glb`
    — sha256 `5f64ff585815292bd7e35ac0db67e269cd10dd9f215f8a5f14b29de4c83819f9` (297,644 bytes)
  * `https://cdn.jsdelivr.net/npm/@webxr-input-profiles/assets@1.0.13/dist/profiles/meta-quest-touch-pro/right.glb`
    — sha256 `de508862ac1460814b9236441982dfe2611ac5b1002d48503186e35a09d38272` (300,304 bytes)
* The bake, exactly:
  ```
  python3 make-controller-obj.py left.glb  meta-quest-touch-pro/left.obj
  python3 make-controller-obj.py right.glb meta-quest-touch-pro/right.obj
  ```
  Results checked in: `left.obj` sha256 `f80c7672309e9220bd7be06cb92e19fc103304adfdc82071859c5824517537b5`,
  `right.obj` sha256 `08a3fa8b62b85cd8871b3a079c63060f7de3c2eaecdf1e5ae33d76604191e180`.

**WHY A DERIVED FILE AND NOT THE .glb.** The upstream asset places its six parts (body,
trigger, squeeze, thumbstick, two buttons) by NODE TRANSFORM, and this tree's one assimp read
site hands a caller `aiScene::mMeshes` with no node tree at all
(`GraphicsHelper::loadAllMeshesFromAssimpScene`) — the six parts loaded that way land on top of
each other at the origin. Baking the transforms once at vendoring keeps the load path the
ordinary one (a model read through the choke point, as the gizmo's own models are read) instead
of adding a second assimp read path with `aiProcess_PreTransformVertices` for a helper mesh.
Nothing else was changed: no vertex was moved relative to another, no part removed, no skin
applied. The textures were dropped because a helper is drawn unlit (no texture crosses the
helper boundary) — the upstream files' single albedo map is not used.

**THE GRIP ORIGIN, MEASURED** (the offset convention this asset assumes). With the node
transforms baked, the mesh's AABB about the file origin is

| | x | y | z |
|---|---|---|---|
| left  | −0.0287 … +0.0392 | −0.0411 … +0.0226 | −0.0516 … +0.0743 |
| right | −0.0392 … +0.0287 | −0.0411 … +0.0226 | −0.0516 … +0.0743 |

i.e. the origin sits INSIDE the handle, 2.3 cm below the top and 4.1 cm above the bottom of the
grip, with 5.2 cm of controller forward (−Z, the tracking ring) and 7.4 cm back. That is the
WebXR grip space definition, and WebXR's grip space is the OpenXR **grip pose** on every
OpenXR-backed runtime — so the model is drawn at `VrStatus::input[h].grip` with NO offset of
ours. The two hands are mirrored in x, which is why each hand has its own file.

## Licence

`@webxr-input-profiles/assets` is MIT (the package's own LICENCE, reproduced in full):

```
MIT License

Copyright (c) 2019 Amazon

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

**TRADEMARKS — the upstream caveat, verbatim** (`packages/assets/README.md`): "Some assets
portray registered trademarks of the device manufacturers in the interest of accurately
depicting the corresponding physical device. The license _does not_ grant permission to use
these trademarks in derivative works (such as alternate controller skins), and they may not be
used to endorse or promote products derived from this software without specific prior written
permission." We therefore draw the device as it is (an accurate depiction of the controller the
wearer is holding), make no skin of it, and claim no endorsement by the manufacturer.

The vendoring precedent and its rules are qlementine's
(`thirdparty/qlementine/PROVENANCE.md`): the upstream identity, the exact version, the exact
change, and the licence in the tree beside the files.
