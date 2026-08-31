# Web-viewer runtime (vendored)

`three-webgpu.iife.js` is the pinned three.js WebGPU viewer runtime the web
exporter stamps into every exported `index.html` / `viewer.html`
(WEB_EXPORT_AUDIT §3 option 1, §4.1). It is a **build artifact vendored into
the repo** so the app build needs no node/npm.

- **Pin: three.js r185 (npm `three@0.185.1`)** — newest release in the audit's
  approved r18x line at implementation time (2026-08-31). Never float the pin;
  TSL/node-material internals move between releases. Upgrade deliberately and
  re-verify: PBR + punctual lights + shadows, GPU skinning, `transmission`
  (glass), `RectAreaLight` (LTC), equirect environment/PMREM IBL, `SkyMesh`.
- **License: MIT** — `THREE_LICENSE` is three.js's copyright/permission notice
  (also summarized in every export's `README.txt`). The exported folder is the
  user's data + MIT three.js; no GPL obligation attaches to it (audit §5) —
  never bundle app code into the viewer.
- `viewer.js` (ours) and `index_template.html` (ours) are the other half of the
  page; all three files ship inside the app binary via `viewer.qrc`.

## Rebuilding the bundle

```sh
mkdir bundle && cd bundle
curl -sL https://registry.npmjs.org/three/-/three-0.185.1.tgz | tar xz
mv package three
curl -sL https://registry.npmjs.org/@esbuild/linux-x64/-/linux-x64-0.28.2.tgz | tar xz
cat > entry.js <<'EOF'
export * from './three/build/three.webgpu.js';
export { OrbitControls } from './three/examples/jsm/controls/OrbitControls.js';
export { GLTFLoader } from './three/examples/jsm/loaders/GLTFLoader.js';
export { RectAreaLightTexturesLib } from './three/examples/jsm/lights/RectAreaLightTexturesLib.js';
export { SkyMesh } from './three/examples/jsm/objects/SkyMesh.js';
EOF
./package/bin/esbuild entry.js --bundle --minify --format=iife --global-name=THREE \
  "--alias:three/webgpu=./three/build/three.webgpu.js" \
  "--alias:three/tsl=./three/build/three.tsl.js" \
  "--alias:three=./three/build/three.webgpu.js" \
  --legal-comments=inline --outfile=three-webgpu.iife.js
```

The aliases route the addons' bare `three`/`three/tsl`/`three/webgpu` imports
into the single WebGPU build so the core is bundled exactly once (~1.36 MB
minified). The IIFE global is `THREE`; classic `<script>`, works from
`file://` — ES modules do not (audit §3).
