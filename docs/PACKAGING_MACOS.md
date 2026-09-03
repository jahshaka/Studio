# Packaging Jahshaka for macOS — the self-contained .app / .dmg

This produces a redistributable `Jahshaka.app` that runs on a clean Mac (macOS 13+,
Apple Silicon) with **nothing installed** — no Qt, no Vulkan SDK, no Xcode: every
dylib, the Vulkan loader, MoltenVK, and all content ship inside the bundle, ad-hoc
signed. Output is a `.dmg` and a `.zip` (~195 MB each).

Development and packaging are **two separate lanes**: your everyday Debug build
(`docs/BUILDING_MACOS.md`) is never touched by packaging. Package only when you want an
artifact to hand to someone.

Prerequisite: a working dev setup per `docs/BUILDING_MACOS.md` — the packaging step
reuses the same Qt install, Vulkan SDK, and Ogre engine install. The engine install must
have been built with the 13.0 deployment floor (any `build-ogre.sh` run from 2026-09
onward does this automatically; older installs fail the verify gate with a clear
message — just re-run `build-ogre.sh`).

## 1. Make a release build

```bash
source ~/VulkanSDK/<version>/setup-env.sh
export PATH="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin:$HOME/Qt/Tools/Ninja:$PATH"

cmake -S . -B build-macos-rel -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDISABLE_BREAKPAD=ON \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_PREFIX_PATH="$HOME/Qt/<version>/macos" \
      -DOGRE_NEXT_PREFIX=<your-ogre-install-prefix>
cmake --build build-macos-rel -j"$(sysctl -n hw.ncpu)"
```

`RelWithDebInfo` is the shipping configuration: optimized, with symbols. (Debug builds
cannot ship — they write settings inside the bundle, which breaks the code signature.)

## 2. Build the bundle

```bash
BUILD_DIR=build-macos-rel DIST=dist ./scripts/make-macos-bundle.sh
```

The script (idempotent; each step prints a check):

1. Builds a **clean bundle skeleton** from the release build — never a copy of the build
   output, which accumulates logs and settings.
2. Runs **macdeployqt**: Qt frameworks and plugins, IrisGL/assimp/Ogre-linked dylibs,
   install-path rewriting to bundle-relative `@rpath`s.
3. Adds what macdeployqt cannot know about: the Ogre dylibs, the two **dlopen'd Ogre
   plugins** — `RenderSystem_Vulkan` and, since the particle adoption,
   **`Plugin_ParticleFX2`** (the particle definitions live in `libOgreNextMain`, but
   every emitter and affector *factory* is registered by that plugin's `install()`;
   without it the app draws no particles at all, silently) — the **Vulkan loader +
   MoltenVK**, and an **ICD manifest** at `Contents/Resources/vulkan/icd.d/` whose
   `library_path` points at the bundled MoltenVK — the app's startup code looks there
   first, so no environment is needed.
4. Strips dev-machine rpaths from every Mach-O (`install_name_tool`).
5. Symlinks the `app/`, `scenes/` and `media/` content trees from `Contents/MacOS` into
   `Contents/Resources` (payload directly under `MacOS/` breaks codesign's resource
   sealing).
6. Prunes: SQL plugins that reference Homebrew/Postgres paths (self-containment holes),
   the legacy downloader, non-arm64 slices (`lipo -thin arm64`).
7. **Ad-hoc codesigns** the bundle (`codesign -s -`). Mandatory: unsigned arm64 binaries
   do not launch on Apple Silicon at all. Ad-hoc means users right-click → Open (or
   Privacy & Security → "Open Anyway") on first launch of a downloaded copy; the app is
   not notarized.
8. Packages `dist/Jahshaka-<version>-arm64.dmg` (hdiutil) and `.zip` (ditto, preserving
   symlinks and signature).

Flags: `--no-dmg` / `--no-zip` to skip packaging steps; `BUILD_DIR=`/`DIST=` to override
locations.

## 3. Verify — always, before shipping anything

```bash
BUNDLE=dist/Jahshaka.app ./scripts/verify-macos-bundle.sh
```

36 assertions in two stages. Static: every Mach-O in the bundle must reference only
bundle-internal or `/usr/lib`//`/System/Library` paths; no stray rpaths; the ICD
manifest is relative; both dlopen'd plugins are present by name; `libOgreNextPlanarReflections`
is present by name (an engine
install built before the component pin in `irisgl/scripts/build-ogre.sh` is the one
way it goes missing); `codesign -v --deep --strict` passes. Runtime: the bundle is
copied to a temp dir and made to render the engine selftest under a **fully emptied
environment with a scratch home** — proving it finds Vulkan/Qt/media inside itself —
with loader-level evidence that the *bundled* MoltenVK loaded, that user data went to
the scratch home (not into the bundle), and that the signature is still valid after
the run.

Green verify = the artifact in `dist/` is what you ship.

## What a user does

1. Open the `.dmg`, drag `Jahshaka.app` to Applications.
2. First launch of a downloaded copy: right-click → Open (or System Settings →
   Privacy & Security → "Open Anyway") — once. Normal double-click thereafter.

## Design record

The measured findings and rejected alternatives behind every step above (why symlinked
resources, why the loader is bundled rather than direct-linking MoltenVK, deployment
floor, Gatekeeper behavior) are recorded in the project's `MACOS_BUNDLE_SPEC` document.
Future work recorded there: notarization/Developer ID, universal (x86_64) binaries.
