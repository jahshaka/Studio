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

`libfreetype-dev` is no longer load-bearing: Ogre's Overlay component was the only consumer
and `irisgl/scripts/build-ogre.sh` now pins `OGRE_BUILD_COMPONENT_OVERLAY=OFF`. It is kept in
the list because it costs nothing and other packages pull it in anyway. That script pins the
**whole** component set explicitly (including `PLANAR_REFLECTIONS=ON`, which Jahshaka links) —
previously most components rode upstream defaults, two of which were probe-dependent, so two
boxes could end up with different engine installs from the same commit.

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

### Vendored-source patches

Vendored trees are never edited in place. Our changes to **assimp** live as patch files in
`irisgl/thirdparty/assimp-patches/` and are applied automatically at **configure time**,
before assimp is added to the build — you will see lines like

```
-- vendor patch applied: 0001-fbx-meshgeometry-guard-oob-vertex-mapping.patch
```

on the first configure and `vendor patch already applied: …` on every one after. Nothing to
run by hand; the assimp submodule simply ends up in the applied-not-committed state, exactly
like Ogre-Next after `build-ogre.sh`. To apply them manually (after re-syncing the submodule,
say):

```bash
cmake -DSRC=irisgl/thirdparty/assimp -DPATCHES=irisgl/thirdparty/assimp-patches \
      -P irisgl/cmake/ApplyVendorPatches.cmake
```

If a patch stops applying, configure fails loudly with `PATCH DOES NOT APPLY` — that means
upstream touched our lines. Read their change and adapt or drop the patch; do not edit the
vendored source. The whole law, and what each patch is for:
`irisgl/thirdparty/assimp-patches/README.md`.

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

## 7. Crash dumps on a dev box

Every fatal signal already writes a `crash-<unixtime>.log` next to the app's log
(`src/app/crashhandler.cpp`; the working directory, normally `build-linux/bin`). That happens
with no setup at all, and Linux builds are linked with `-rdynamic` so the frames carry function
names rather than bare offsets. Decode one, or the newest one, with:

```bash
scripts/debug-crash.sh                        # newest crash log (+ core, if any)
scripts/debug-crash.sh --log build-linux/bin/crash-1772500000.log
```

`addr2line` adds `file:line` and resolves the frames `-rdynamic` cannot (statics and
anonymous-namespace functions never reach `.dynsym`). Keep the build tree — the log is useless
against a different binary.

**Core dumps are a separate, opt-in thing, and `ulimit` alone will not give you one.** On a
stock Ubuntu box:

```bash
cat /proc/sys/kernel/core_pattern     # |/usr/share/apport/apport -p%p -s%s ... -- %E
ulimit -c                             # 0
```

Apport keeps reports for *packaged* binaries only, so a Jahshaka crash produces nothing in
`/var/crash`. And per `man 5 core`, **`RLIMIT_CORE` is ignored while `core_pattern` is a pipe**
— raising `ulimit -c` changes nothing until the pattern stops being a pipe. Both settings are
needed, in this order:

```bash
# 1. stop piping to apport; write cores into the crashing process's cwd (needs root)
sudo sysctl -w kernel.core_pattern=core.%p        # not persistent, by design

# 2. now the limit matters — per shell, so set it in the shell you launch from
ulimit -c unlimited

cd build-linux/bin && ./Jahshaka                  # crash → ./core.<pid>
scripts/debug-crash.sh                            # gdb over the newest core
```

Deliberately NOT automated and NOT persisted: `core_pattern` is machine-wide state that also
governs every other program on the box, so it is a decision the box's owner makes, not
something a build script or a test suite changes underneath them. To go back:
`sudo sysctl -w kernel.core_pattern='|/usr/share/apport/apport -p%p -s%s -c%c -d%d -P%P -u%u -g%g -- %E'`
(or just reboot — the sysctl above is not persistent).

`gdb` is required for the core path (`sudo apt install gdb`). `scripts/debug-crash.sh` prints
the current `core_pattern`/`ulimit` state at the top of every run, so a run that produced no
core tells you why.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| CMake: "Ogre-Next not found at …" | Step 3 not run, or a custom `OGRE_PREFIX` — pass `-DOGRE_NEXT_PREFIX=<prefix>` |
| Ogre build: `undefined reference to FreeImage_*` | A dependency was installed after configuring — delete `irisgl/thirdparty/ogre-next/build` and re-run the script |
| App exits with a Vulkan error at startup | Check `vulkaninfo`; update GPU drivers |
| `git submodule update` fails on ogre-next | Network access to github.com/OGRECave required (large repo; the clone is blob-filtered) |
| A patch fails in build-ogre.sh after updating the submodule | Upstream changed a patched file — see `irisgl/docs/OGRE_BUILD.md`, "Updating Ogre" |
| Configure fails with `PATCH DOES NOT APPLY` | An assimp patch no longer applies (upstream moved, or the submodule tree is dirty) — see `irisgl/thirdparty/assimp-patches/README.md` |
| The app crashed but there is no core file | Expected — see §7. `crash-*.log` is written regardless; cores need the `core_pattern` change first |
| Crash-log frames are bare `Jahshaka(+0x…)` offsets | The binary was linked without `-rdynamic` (an old build dir). Re-configure; `scripts/debug-crash.sh` decodes them either way |
