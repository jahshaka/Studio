# Building Jahshaka on Linux

This guide builds **Jahshaka** (the app) and **IrisGL** (the 3D core and engine) with the
**Ogre-Next engine viewport** — the shipping renderer. The legacy OpenGL viewport still exists
behind a flag but is deprecated and will be removed; nothing here needs it.

Verified on Ubuntu 26.04 (GCC 15, Qt 6.10, CMake ≥ 3.16, Ninja). Other distributions work with
equivalent packages.

## 1. Install dependencies

```bash
# Toolchain + Qt 6
sudo apt-get install -y build-essential cmake ninja-build git \
     qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-svg-dev \
     qt6-httpserver-dev qt6-websockets-dev \
     libqt6concurrent6 libqt6sql6-sqlite

# Ogre-Next build dependencies — install ALL of these BEFORE configuring anything
# (CMake caches not-found results; a late install silently misconfigures the build)
sudo apt-get install -y libxrandr-dev libxaw7-dev rapidjson-dev libzzip-dev \
     libsdl2-dev glslang-tools spirv-tools vulkan-tools libshaderc-dev \
     libfreeimage-dev libxcb-randr0-dev libx11-xcb-dev libxcb1-dev \
     libxcb-keysyms1-dev libx11-dev libxt-dev libgl1-mesa-dev \
     libglu1-mesa-dev libfreetype-dev zlib1g-dev libvulkan-dev
```

A Vulkan-capable GPU and driver are required (`vulkaninfo` should succeed). CPU-only machines
can still build and run the test suite via lavapipe (`mesa-vulkan-drivers`).

`qt6-httpserver-dev` serves the in-app MCP endpoint (CLAUDE_EDITOR_SPEC phase 1);
`qt6-websockets-dev` is its CMake-level dependency (Qt6HttpServerConfig requires
Qt6WebSockets even though Jahshaka never opens a WebSocket).

## 2. Clone

```bash
git clone --recursive https://github.com/jahshaka/Studio.git jahshaka
cd jahshaka
```

`--recursive` brings IrisGL (with its assimp/bullet/zip submodules) and the pinned Ogre-Next
source. If you cloned without it:

```bash
git submodule update --init --recursive
```

## 3. Build Ogre-Next — once per machine

IrisGL ships Ogre-Next as a pinned submodule plus a small set of Jahshaka patches. One script
applies the patches, configures, builds and installs it (~10 minutes; installs to
`~/Developer/engines/ogre-next-install` by default, override with `OGRE_PREFIX=`):

```bash
./irisgl/scripts/build-ogre.sh
```

You never run this again unless the Ogre pin changes. Details, other platforms, and the
gotchas explained: `irisgl/docs/OGRE_BUILD.md`. The critical one: **if `libshaderc-dev` is
missing, Ogre configures "successfully" without Vulkan** — the script checks and fails loudly
for you.

## 4. Build Jahshaka + IrisGL

```bash
cmake -S . -B build-linux -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-linux -j$(nproc)
```

This builds IrisGL, the engine layer, and the app in one go. (For development builds use
`-DCMAKE_BUILD_TYPE=Debug -DDISABLE_BREAKPAD=ON` — breakpad swallows crash backtraces.)

## 5. Run

```bash
cd build-linux/bin
./Jahshaka --viewport=engine
```

Run from `build-linux/bin` — the app writes its log (`jahshaka-ogre.log`) and settings to the
working directory and finds its staged assets next to the executable.

`--viewport=engine` selects the Ogre-Next viewport (Vulkan, runs on X11/XWayland). It will be
the default once the legacy viewport is removed; until then, omitting it launches the
deprecated GL viewport, which is unsupported.

Headless verification without opening a window:

```bash
./Jahshaka --engine-selftest out.png    # renders the default scene, exit code 0 on success
```

## 6. Run the tests (optional)

```bash
cd build-linux
DISPLAY=:0 ctest --output-on-failure
```

The suite is fully headless (offscreen Vulkan) but a reachable X display is required for the
Vulkan platform plugin. On machines without a GPU:
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ctest ...` runs everything on lavapipe.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| CMake: "Ogre-Next not found at …" | Step 3 not run, or a custom `OGRE_PREFIX` — pass `-DOGRE_NEXT_PREFIX=<prefix>` |
| Ogre build: `undefined reference to FreeImage_*` | A dependency was installed after configuring — delete `irisgl/thirdparty/ogre-next/build` and re-run the script |
| App exits with a Vulkan error at startup | Check `vulkaninfo`; update GPU drivers |
| `git submodule update` fails on ogre-next | Network access to github.com/OGRECave required (large repo; the clone is blob-filtered) |
| A patch fails in build-ogre.sh after updating the submodule | Upstream changed a patched file — see `irisgl/docs/OGRE_BUILD.md`, "Updating Ogre" |
