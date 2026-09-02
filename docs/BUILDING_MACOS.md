# Building Jahshaka on macOS

This guide builds **Jahshaka** (the app) and **IrisGL** (the 3D core and engine) with the
**Ogre-Next engine viewport** on macOS. Rendering runs on Vulkan via **MoltenVK** — the
Vulkan-over-Metal layer — using the same Vulkan RenderSystem as Linux, with a
Metal-surface window backend (`ogre-patches/0007`) for the on-screen viewport.

Verified on Apple Silicon (M4 Pro), macOS 26, Xcode 17 / AppleClang 17, Qt 6.11.2,
Vulkan SDK 1.4.357.1. The built app runs on macOS 13+. Intel Macs are untested.

No package manager is required — no Homebrew, no MacPorts.

## 1. Install dependencies

1. **Xcode** (or the full Command Line Tools). `clang++ --version` should report AppleClang.

2. **Qt via the online installer** ([qt.io](https://www.qt.io/download-qt-installer)) into
   `~/Qt`. Select the current **Qt 6.x for macOS** plus these Additional Libraries:
   - **Qt HttpServer** (serves the in-app MCP endpoint)
   - **Qt WebSockets** (CMake-level dependency of HttpServer)
   - **Qt Multimedia**

   Also tick **CMake** and **Ninja** under Build Tools — the installer provides both under
   `~/Qt/Tools/`, and this guide uses them (they are not on `PATH` by default).

3. **LunarG Vulkan SDK** ([vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home#mac))
   into `~/VulkanSDK` (per-user, no sudo). This provides MoltenVK, the Vulkan loader,
   `vulkaninfo`, and — critically — **shaderc/glslang**, without which Ogre silently
   configures with *no* Vulkan RenderSystem. Sanity-check before building anything:

   ```bash
   source ~/VulkanSDK/<version>/setup-env.sh
   vulkaninfo --summary        # must list your GPU with driverName = MoltenVK
   ```

4. **rapidjson headers** (header-only; the equivalent of apt's `rapidjson-dev`). Unpack a
   release so the headers land at `<somewhere>/rapidjson/include/rapidjson/...` and point
   `RAPIDJSON_HOME` at `<somewhere>/rapidjson` when building Ogre (below). If you keep it
   at `<prefix-parent>/deps/rapidjson` beside your Ogre install prefix, the build script
   finds it automatically.

## 2. Clone

```bash
git clone --recursive https://github.com/jahshaka/Studio.git jahshaka
cd jahshaka
git checkout main && git submodule sync --recursive && git submodule update --init --recursive
```

**Trap:** a fresh clone lands on `master`, which is an old pre-engine tree with a
different layout. `main` is the real tree — always check it out and re-sync submodules.

## 3. Build Ogre-Next (once per machine)

```bash
source ~/VulkanSDK/<version>/setup-env.sh          # the script refuses to run without VULKAN_SDK
export PATH="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin:$HOME/Qt/Tools/Ninja:$PATH"

OGRE_PREFIX=<where-you-want-the-engine-install> ./irisgl/scripts/build-ogre.sh
```

The script is platform-aware: on macOS it builds the pinned Ogre-Next submodule with
Jahshaka's patches, Vulkan-only (no X11, no GL3Plus, no FreeImage — the bundled STBI
codec handles images), as plain dylibs with baked rpaths (so nothing depends on
`DYLD_LIBRARY_PATH`, which macOS SIP strips across shells), at a macOS 13.0 deployment
floor. Takes a few minutes.

## 4. Configure, build, test, run

```bash
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDISABLE_BREAKPAD=ON \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_PREFIX_PATH="$HOME/Qt/<version>/macos" \
      -DOGRE_NEXT_PREFIX=<your-ogre-install-prefix>
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"

# 10-second proof (headless render of the default scene; exit 0 = working):
cd build-macos/bin && ./Jahshaka.app/Contents/MacOS/Jahshaka --engine-selftest out.png

# Test suites (no display rig needed on macOS):
cd .. && ctest --output-on-failure

# The editor:
open bin/Jahshaka.app
```

The app builds as a bundle; the binary is `Jahshaka.app/Contents/MacOS/Jahshaka` and the
engine log (`jahshaka-ogre.log`) is written beside it. `build-macos/bin/app` is a
build-generated symlink into the bundle so the standalone test binaries share the app's
content tree.

## Known macOS notes

- **One known failing test case**: `test_engine`'s `pbr_texture_scale_tiles_uvs` —
  raw-pixel (non-batched) textures sample incorrectly under MoltenVK on the PBR path.
  File-loaded textures are unaffected. Tracked; do not treat as a regression.
- **Keep `VULKAN_SDK` env sourced** for building and for running test binaries directly
  (the Vulkan loader finds MoltenVK through the SDK's ICD manifest). The app itself
  self-resolves the ICD and launches fine from Finder with no environment.
- Retina/HiDPI rendering is implemented but was developed on a 1x display; report
  anything blurry or mis-scaled on 2x displays.
- To produce a redistributable `.app`/`.dmg`, see `docs/PACKAGING_MACOS.md`.
