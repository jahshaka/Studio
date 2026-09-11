/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <algorithm>

#include "services/services.h"
#include "services/projectservice.h"
#include "scripting/modules/appapi.h"
#include "scripting/modules/moduleshared.h"

#include "shell/mainwindow.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/thememanager.h"
#include "ui/pages/projectmanager.h"
#include "scripting/apiregistry.h"
#include "services/engineerrorpump.h"
#include "services/loadtimeline.h"
#include "services/mainthreadheartbeat.h"
#include "services/mainthreadwatchdog.h"
#include "bridge/enginehost.h"
#include "viewport/enginerenderdriver.h"
#include "services/framepacing.h"
#include "data/settingsmanager.h"
#include "services/jahlog.h"
#include "services/apppaths.h"
#include "services/assetstorepaths.h"
#include "data/constants.h"
#include "services/ogresamples.h"
#include "irisgl/core/irisutils.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStyle>
#include <QWidget>
#include <QScreen>
#include <QRect>

QVector<VerbInfo> AppApi::verbs() const
{
    return {
        { "desktop", "app.desktop(n=0) -> current",
          "Switches to desktop 1-4; app.desktop() just returns the current one.",
          Needs::Window },
        { "space", "app.space(name) -> bool",
          "Switches the main window space: desktop, player, editor, materials, assets, publish, avatar. player and editor need an open project.",
          Needs::Window },
        { "columns", "app.columns() -> {space, left:{width, min}, right:{width, min}, "
                     "metrics:{leftWidth, leftMin, rightWidth, rightMin}}",
          "THE ACTIVE PAGE'S COLUMNS, measured. Every page's left and right columns are sized "
          "from ONE set of constants (src/ui/style/panelmetrics.h — the editor's widths, owner "
          "2026-09-11: \"all right columns (Materials, Avatars, Assets) and left columns unify "
          "on the Editor's widths\"); this reports the width each column actually has on screen "
          "and the minimum it can actually be dragged to, so the law can be asserted per page "
          "instead of trusted. `metrics` is what PanelMetrics says those numbers should be. A "
          "space with no columns (desktop, player) answers {space} alone.",
          Needs::Window },
        { "openTimings", "app.openTimings() -> [{stage, ms, items?, label?}]",
          "The millisecond ledger of the most recent scene open (services/loadtimeline.h): one entry per stage, "
          "the first entry being {stage:'total', ms, label}, plus 'counter:*' entries for the work that "
          "accumulates inside the stages (assimp parses, database sweeps, the engine push). Empty before "
          "the first open of the session.",
          Needs::Document },
        { "heartbeat", "app.heartbeat(intervalMs=250) -> bool",
          "Starts (or, with 0, stops) a main-thread heartbeat probe: a timer that ticks on the UI thread and "
          "records the WORST gap between ticks. The measurable definition of 'the window stayed responsive' — "
          "a blocked UI thread cannot tick. Restarting resets the statistics.",
          Needs::Window },
        { "heartbeatStats", "app.heartbeatStats() -> {running, intervalMs, ticks, maxGapMs, sinceLastTickMs}",
          "The heartbeat probe's readings (see app.heartbeat). maxGapMs is the longest the UI thread went "
          "without servicing its event loop since the probe started.",
          Needs::Window },
        { "watchdogStats", "app.watchdogStats() -> {supported, running, enabled, stallMs, reports, lastStallMs}",
          "The main-thread watchdog (services/mainthreadwatchdog.h): a thread of our own that polls the "
          "heartbeat's last-tick atomic and, when the UI thread has not ticked for stallMs, makes THAT thread "
          "print its own backtrace (a watchdog thread calling backtrace() would photograph itself). 'reports' "
          "counts the stalls this session reported — at most one per stall, capped and cooled down. A "
          "DEVELOPMENT-BUILD feature: 'supported' is false in a release build, and a dev build can still turn "
          "it off with the watchdog_enabled preference or --watchdog=off.",
          Needs::Window },
        { "blockUiThread", "app.blockUiThread(ms) -> bool",
          "Blocks the UI thread for ms milliseconds WITHOUT servicing the event loop — a deliberate freeze, so "
          "the watchdog and the heartbeat can be tested through the registry instead of through a private "
          "sleep. Development builds only: returns false and does nothing in a release build. The verb itself "
          "does not return until the freeze is over, which is the point.",
          Needs::Window },
        { "shaderCache", "app.shaderCache() -> {enabled, dir, fingerprint, sizeBytes, files, "
                         "pipelineCacheLoaded, pipelineCacheReason, microcodeLoaded, "
                         "microcodeEntries, hlmsCachesLoaded, "
                         "compiledThisRun, loadedThisRun, expectedShaders, lastSaved}",
          "The persistent shader cache (SHADER_CACHE_SPEC.md): what is on disk and what this run "
          "did with it. compiledThisRun counts shaders the compiler actually built; loadedThisRun "
          "counts shaders served straight from the cache, so a warm launch shows the second number "
          "high and the first near zero. expectedShaders is what the LAST RUN needed in total — "
          "the startup progress counter's denominator, 0 before any cache has been written. "
          "pipelineCacheLoaded means the DRIVER accepted the pipeline blob, not merely that we "
          "offered it one: pipelineCacheReason says which of absent / accepted / outdated / "
          "rejected / silent happened ('outdated' is the ordinary cost of a driver update; "
          "'silent' means the render system has the broken-pipeline-cache workaround on and this "
          "layer does nothing at all on this device). The counters work whether or not the cache "
          "itself is enabled.",
          Needs::Engine },
        { "clearShaderCache", "app.clearShaderCache() -> bool",
          "Deletes every cached shader artifact. The running session is unaffected (its shaders are "
          "already in memory); the NEXT launch is cold. Our r.InvalidateCachedShaders — the same "
          "thing --clear-shader-cache does before the engine starts.",
          Needs::Engine },
        { "saveShaderCache", "app.saveShaderCache() -> bool",
          "Writes the shader cache now instead of waiting for the burst-settle watchdog or a clean "
          "quit. A no-op returning true when nothing new has been compiled. Mostly for tests: the "
          "app saves on its own.",
          Needs::Engine },
        { "warmUpSet", "app.warmUpSet(action?) -> {path, exists, enabled, sizeBytes, "
                       "shape:{samples, shadows}, recorded?, saved?, built?}",
          "The recorded warm-up set (SHADER_CACHE_SPEC.md §2.7b) — this machine's list of the "
          "shader permutations previous sessions actually used. Not shaders and not SPIR-V: a list "
          "of vertex formats, render queues and one representative material each, which is why it "
          "is tiny and why it is the only cached artifact that is platform- and driver-independent. "
          "With no argument it reports. 'record' adds every live scene to the set and writes it; "
          "'apply' replays it, compiling every permutation against degenerate 4-vertex buffers so "
          "nothing is loaded from disk, and reports how many shaders that built. The app records on "
          "every world OPEN and CLOSE and again on quit, and applies at startup, on its own; these "
          "are for tests and for recording a set deliberately from a scene built for the purpose. "
          "'enabled' mirrors the shader-cache preference — with the cache off nothing is recorded "
          "or replayed automatically, because the set lives in the cache directory and is derived "
          "data of the same kind. 'shape' is the PASS the last session's editor drew with (MSAA "
          "sample count and whether shadows were on); the startup warm-up view is built to match, "
          "because a permutation depends on the pass as much as on the renderable.",
          Needs::Engine },
        { "engineErrors", "app.engineErrors(reset?) -> {drains, recorded, suppressed, entries:[{message, count, suppressed, firstMs, lastMs}]}",
          "What the renderer refused to do, and did not otherwise tell anyone. The engine swallows "
          "failures by design (every backend call is wrapped and returns a refusal value instead of "
          "throwing), and almost nothing checks those return values — so a texture that will not "
          "decode, a mesh with no tangents or a full decal atlas draw the wrong picture in silence. "
          "EngineErrorPump drains the engine's error sink once per rendered frame and this is its "
          "record: the distinct messages, newest first, with how often each occurred and how many "
          "repeats the 5-second rate limiter swallowed. drains counts the pump calls themselves, so "
          "a zero there means the frame loop is not running, not that the renderer is happy. Pass "
          "true to clear the record after reading it.",
          Needs::Document },
        { "frameStats", "app.frameStats() -> {running, intervalMs, ticks, rendered, skipped, workMs, worstMs, slowFrames, enabledViews}",
          "What the ONE render loop (EngineRenderDriver) has been doing, cumulatively since the engine "
          "started. `ticks` counts timer fires, `rendered` the ticks that actually called the engine, "
          "`skipped` the ticks that had no enabled View to draw and therefore submitted nothing — sitting "
          "on a page with no viewport should advance `skipped` and leave `rendered` still. `enabledViews` "
          "is the engine's live answer to the same question the loop asks each tick. Note the scripted "
          "stepping verb editor.frame(n) bypasses the driver entirely, so it moves none of these. "
          "`workMs` is THE HONEST PERFORMANCE NUMBER on this architecture and the reason to prefer it "
          "over any FPS reading: the loop is a TIMER (paced from the display since the perf wave — see "
          "app.pacing), so a healthy editor reports whatever that timer allows whatever the scene "
          "costs, and only starts dropping once the budget is already blown. workMs "
          "is how long the frame's work actually took, averaged over the last ~60 rendered ticks; "
          "`worstMs` is the worst single tick since startup and `slowFrames` counts the ticks that "
          "crossed the 100 ms hitch threshold (the ones that also log `[open-profile] slow frame`). "
          "Read app.renderStats() beside this for the renderer's own view of the same frames.",
          Needs::Document },
        { "pacing", "app.pacing(mode?) -> {mode, modes, intervalMs, refreshHz, vsync, running}",
          "How fast the ONE render loop is allowed to tick, and whether the frame waits for the "
          "display (fps audit F1; services/framepacing.h). 'display' (the default) derives the "
          "timer interval from the refresh rate of the screen the window is on and keeps vsync on, "
          "so the ceiling is the panel's rate instead of the 62.5 fps a hardcoded 16 ms timer used "
          "to impose; 'unlimited' runs the loop with no wait and vsync OFF, which tears and is the "
          "only honest way to measure what the renderer can actually do. Passing a mode sets it "
          "(and rebuilds each on-screen window's swapchain, so it is a deliberate action, not "
          "something to do per frame); passing nothing just reports. 'intervalMs' is what the timer "
          "is really running at — 0 in unlimited — and 'refreshHz' is what the host last told the "
          "driver about its screen (0 = unknown, which falls back to 16 ms). The setting persists "
          "as viewport/pacing and is the same one Preferences > Viewport > Frame Pacing writes.",
          Needs::Window },
        { "renderStats", "app.renderStats() -> {metricsRecording, fps, frameMs, lastMs, p95Ms, p99Ms, bestMs, worstMs, draws, batches, triangles, vertices, instances, incompletePsoRequests, forwardPlusLights, forwardPlusBudget, forwardPlusOverBudget}",
          "What the RENDERER measured, straight off the engine boundary — the numbers behind the F3 "
          "stats overlay, and the read-back answer for an agent that wants to know what a frame costs "
          "(a screenshot cannot carry them; the overlay is deliberately absent from offscreen renders). "
          "The timings come from Ogre's own FrameStats, which our render loop feeds: `fps`/`frameMs` are "
          "the rolling average, `lastMs` the latest (noisy) sample, `p95Ms`/`p99Ms` the percentiles, "
          "`bestMs`/`worstMs` the extremes. READ THE HONESTY NOTE ON app.frameStats: `fps` here measures "
          "the loop's timer (and vsync), not the renderer's headroom — frameStats().workMs is the number that "
          "diagnoses anything. The geometry counters are the FRAME's totals, not one view's (Ogre "
          "snapshots them per camera at the end of that camera's pass, so with two on-screen views the "
          "second includes the first). They are LAZY: recording costs integer adds per draw call and is "
          "off until something asks, so the very first call reports metricsRecording=false with zeroed "
          "counters and every call after a rendered frame reports real ones. "
          "`incompletePsoRequests` is how many pipeline objects the last frame GAVE UP on for "
          "running out of its compile budget — objects using one do not appear that frame. It "
          "is ALWAYS 0 here and that is deliberate (THREADING_ADOPTION_SPEC.md P4, decision "
          "D-E): the budget is a process-wide knob whose own documentation warns that "
          "one-shot shader techniques may end up uninitialised, which describes every "
          "thumbnail, IBL bake and offscreen pixel suite in this app, so it is left off. A "
          "non-zero value means somebody turned it on.",
          Needs::Engine },
        { "engineObjects", "app.engineObjects() -> {views, enabledViews, scenes, updatedScenes, stagingScenes, nodes, meshes, materials, textures, datablocks}",
          "A CENSUS of what the renderer is HOLDING — the companion to app.renderStats(), which "
          "only says what a frame cost (fps audit F11). `views` and `scenes` are the engine's own "
          "objects, `enabledViews` the subset renderOneFrame actually draws; `updatedScenes` is "
          "how many SCENES the last frame updated — the frame loop walks a scene only while an "
          "enabled view draws it, so in an editor holding preview, player, asset and staging "
          "scenes this is normally 1 and `scenes` is not (THREADING_ADOPTION_SPEC.md P3); "
          "`updatedScenes` climbing to meet `scenes` means the gate stopped working and every "
          "idle scene is being walked 60 times a second again. `stagingScenes` counts the "
          "SceneManagers the engine holds that are NOT Scenes — the document's staging "
          "manager, where every node that is not in a rendered scene lives (everything an "
          "importer builds, everything the undo stack holds). It is a row of its own rather "
          "than part of `scenes` because it has no view, no workspace, no worker threads and "
          "no place in the frame loop, and a census that mixes two kinds of object answers no "
          "question. `nodes`, `meshes`, "
          "`materials` and `textures` are the per-scene registries of ids this boundary handed out "
          "and still honours, SUMMED over every live scene; `datablocks` is process-wide (Hlms "
          "datablocks belong to the one HlmsManager, not to a scene) and includes the backend's own "
          "defaults, so only its delta means anything. NOTHING HERE IS A TIMING: the point is that "
          "in a scene nobody is editing every one of these numbers is FLAT, which makes an object "
          "leak — a view created per readback and never destroyed, a mesh record outliving its "
          "document node, a datablock per material push — assertable without a per-machine "
          "baseline. That is exactly what the perf.epic_steady_state gate does with it. Cheap: "
          "container sizes plus one walk of the (tiny) view and scene vectors.",
          Needs::Engine },
        { "memoryStats", "app.memoryStats() -> {gpuPoolCapacityBytes, gpuPoolFreeBytes, gpuPools, gpuPoolsIncludeTextures, sceneManagers, simdNodes, simdObjects, simdNodeDepths, residentBytes}",
          "What the renderer's MEMORY POOLS hold (riders lane R4). The GPU rows are the "
          "VaoManager's buffer pools — everything allocated from the driver (on Vulkan that "
          "includes textures, `gpuPoolsIncludeTextures`) and how much of it is unused; a pool "
          "that empties is returned to the driver by the engine on its own a few frames later, "
          "so `gpuPoolCapacityBytes` falling after a project closes is the number to watch. "
          "The SIMD rows are the scene managers' SoA node/object pools, which grow to the "
          "high-water mark of nodes ever alive and shrink only on app.reclaimMemory(): the "
          "backend exposes no byte count for them, so the rows are the counts that size them "
          "plus the process's resident set (`residentBytes`, Linux), where a shrink shows. "
          "Cheap — reads the pool tables, renders nothing.",
          Needs::Engine },
        { "textureMemory", "app.textureMemory({top?, resident?}) -> {count, totalBytes, residentBytes, pooledBytes, renderTargetBytes, entries:[{name, resource, width, height, depth, slices, mipmaps, msaa, format, bytes, renderTarget, uav, manual, pooled, residency}]}",
          "WHICH TEXTURES hold the GPU memory app.memoryStats reports: every texture the "
          "renderer's texture manager knows, LARGEST FIRST, with the totals over all of them. "
          "On Vulkan textures live in the same pools as the buffers, so this is the "
          "attribution behind `gpuPoolCapacityBytes` (a default scene boots at ~3.2 GB of "
          "pool capacity — shadow atlases, GI volumes, render targets and the library's "
          "images, and this says which). `top` limits the entry list (default 50, 0 = all); "
          "the totals always cover everything. `resident: true` lists only textures that "
          "are on the GPU (residency 'Resident'); a texture 'OnStorage' is declared and "
          "costs nothing yet. `bytes` is each texture's own footprint (all mips, slices, "
          "MSAA); a `pooled` texture is a slice of a master array and carries none of the "
          "pool's waste. Cheap — walks the entry table, renders nothing.",
          Needs::Engine },
        { "reclaimMemory", "app.reclaimMemory() -> {before: {...}, after: {...}}",
          "RECLAIM: shrinks every scene manager's SIMD pools to what is live (they never shrink "
          "by themselves), and returns app.memoryStats() from before and after so the delta is "
          "in the answer. The editor calls it after a project closes and after an import batch "
          "finishes; it helps only after a large REMOVAL, never after growth. The GPU pools "
          "need no call (see app.memoryStats). Safe between frames; no pixel changes.",
          Needs::Engine },
        { "profiling", "app.profiling([on]) -> bool",
          "The engine's opt-in PASS PROFILER (`--profile` on the command line is the same "
          "switch at boot). With no argument, reads it. On, every view logs one line per ~120 "
          "frames to the ogre log: the CPU submission time of each compositor pass — avg and "
          "max per frame, by the pass's profiling id, top entries first. That is what the "
          "render thread spent recording the pass, including any wait it did inside it; it is "
          "NOT GPU time (the backend pin has no timestamp-query surface). Off by default and "
          "free when off: no listener exists.",
          Needs::Engine },
        { "threading", "app.threading() -> {multithreadedShaderCompilation, shaderThreadingMode, sceneWorkerThreads, hlmsThreads}",
          "WHAT THE ENGINE IS THREADING (SPECS/THREADING_ADOPTION_SPEC.md P1). "
          "`multithreadedShaderCompilation` is the render system's OWN answer to "
          "supportsMultithreadedShaderCompilation() — true means Ogre compiles shaders and "
          "pipeline objects across the scene's worker pool instead of one at a time on the "
          "thread that hit the missing permutation. It is the one number in this map that "
          "cannot be inferred from Studio's source, because the switch that sets it lives in "
          "the ENGINE INSTALL (build-ogre.sh's OGRE_SHADER_COMPILATION_THREADING_MODE=2), not "
          "in this binary: A TREE THAT FORGOT TO RE-RUN `irisgl/scripts/build-ogre.sh` READS "
          "false HERE AND IS OTHERWISE INDISTINGUISHABLE FROM A CORRECT ONE — it builds, it "
          "runs, every suite passes, and it compiles its shaders on one core for ever. "
          "`shaderThreadingMode` is the build-time mode Studio itself was compiled against "
          "(2 = force-enabled, 1 = the backwards-compatible API, which in a shared build means "
          "off). `sceneWorkerThreads` maps each live scene's name to its worker-pool size — the "
          "editor scene gets the machine (capped at 8), previews and thumbnails deliberately "
          "get fewer — and `hlmsThreads` is the largest of them, the ceiling on how many "
          "threads any one pass can compile on and the count the shader disk cache is applied "
          "with at startup. Cheap: two engine reads and a walk of the scene list.",
          Needs::Engine },
        { "textureStreaming", "app.textureStreaming() -> {multiLoadThreads, doneStreaming, loadRequests, metadataCacheEntries, channelCacheEntries, waitTimeouts, waitWorstMs, waitBudgetMs}",
          "WHAT THE TEXTURE LOADER IS DOING (SPECS/THREADING_ADOPTION_SPEC.md P2). Since the "
          "batched-loading phase, loading a texture SCHEDULES it and the frame edge waits once "
          "for all of them, instead of the caller blocking on each texture in turn — so N "
          "textures in a scene decode concurrently rather than one at a time. "
          "`multiLoadThreads` is how many threads the decode pool has (0 = the feature is off "
          "and Ogre's single background streaming thread does everything; the default is half "
          "the machine's threads clamped to 2..6, and JAH_TEXTURE_MULTILOAD overrides it for "
          "measurement). `doneStreaming` is true when nothing is queued or in flight — it is "
          "normally true, because renderOneFrame waits, so a false here means you asked in the "
          "middle of an open. `loadRequests` is a MONOTONIC counter of load requests this "
          "process has made: only differences mean anything, and it moving across a render is "
          "exactly the condition the offscreen readbacks' double-render guard tests. "
          "`metadataCacheEntries` and `channelCacheEntries` are the two halves of the persistent "
          "texture cache — resolution/format/pool per path (Ogre's own, which lets the main "
          "thread reserve the right pool slice before anything is decoded) and our path -> "
          "channel-count sidecar (which is what stops every image being decoded twice). Both are "
          "derived data in the shader cache's directory and both die with the Clear Cache "
          "button. NOTE metadataCacheEntries is the one expensive field: the backend exposes no "
          "size() for that map, so asking exports it to count the rows. "
          "`waitTimeouts` IS THE HEALTH FIELD and it MUST be 0: the frame's texture wait is "
          "bounded (Engine.h's texture-wait contract), and a non-zero count means the wait gave "
          "up on a pending set that had stopped shrinking and at least one frame was drawn "
          "without its textures. `waitWorstMs` is the longest single wait this process has "
          "performed and `waitBudgetMs` the resolved no-progress budget (JAH_TEXTURE_WAIT_MS "
          "overrides it). All three are plain members — free to ask.",
          Needs::Engine },
        { "waitForTextures", "app.waitForTextures() -> {waitedMs, loadRequests, doneStreaming}",
          "Blocks until every scheduled texture load has finished, and reports how long that "
          "took in `waitedMs` (0 when there was nothing to wait for). NOT NEEDED before an "
          "ordinary frame — the render loop already waits at the head of every frame, which is "
          "what keeps the pixel suites byte-exact. It is here for two callers: a script that "
          "wants a provably complete picture before a readback that does not go through the "
          "engine's own guard, and the A/B harness behind the batched-loading measurement, for "
          "which `waitedMs` IS the number. `loadRequests` is the monotonic counter after the "
          "wait, so a script can bracket an open with two calls and say how many textures it "
          "cost.",
          Needs::Engine },
        { "ogreSamples", "app.ogreSamples() -> [{name, title, note, available, path, running, portArchive, portAvailable}]",
          "The Ogre-Next samples this program ports (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3), each with "
          "whether ITS ORIGINAL BINARY can be launched on this machine. A curated inventory, not a "
          "directory scan: `available` is false on every tree that did not opt into building Ogre's "
          "samples (OGRE_SAMPLES=1 ./irisgl/scripts/build-ogre.sh), which is the normal case — the "
          "originals are the reference picture beside our ports, a developer convenience, never a "
          "shipping dependency. `portArchive` is where our port of that scene lives in scenes/ogre "
          "and `portAvailable` says whether it has been authored yet. `note` is what the port "
          "cannot show, and belongs on the tile beside it.",
          Needs::Document },
        { "launchOgreSample", "app.launchOgreSample(name, {fullscreen, width, height}) -> {launched, path, pid, reason}",
          "Runs Ogre's ORIGINAL binary for `name` (the base name, e.g. 'PbsMaterials') as a separate "
          "process, for a side-by-side against our port. Never throws: a tree without the samples "
          "built, a headless session, or a copy of that sample this session already started all "
          "return launched:false with a `reason` to show. Windowed 1920x1080 by DEFAULT and "
          "deliberately — their Vulkan config defaults Full Screen to Yes and Video Mode to the "
          "largest mode, so an unseeded launch takes over the whole display and reads as a frozen "
          "editor. The sample runs from its own build directory (its resources2.cfg points into the "
          "Ogre source tree) and gets its own Ogre::Root in its own process.",
          Needs::Document },
        { "apiProblems", "app.apiProblems() -> [string]",
          "Everything wrong with the scripting API's OWN metadata, as sentences: a verb with no "
          "doc string or signature, a duplicate name, a module that registers nothing, or — the "
          "one that actually bites — a verb advertised in a module's verbs() list with no "
          "invokable method behind it, which makes api.help() and the docs page promise something "
          "no script can call. An empty list is the contract: it means every one of the modules "
          "installed in THIS session describes itself completely. Read it beside the api.contract "
          "test, which proves docs/SCRIPTING.md still matches the registry that produced it.",
          Needs::Document },
        { "dataRoot", "app.dataRoot() -> {root, overridden, settingsFile, database, assetStore, projects}",
          "Where THIS run keeps its data (services/apppaths.h): the library database, the asset "
          "store, the shader cache, the settings file and the PROJECTS root (`projects` is the "
          "folder that holds `Projects/`, which is where project.create writes). `overridden` is "
          "true when --data-root or JAHSHAKA_DATA_ROOT chose the root, which is the only case in "
          "which the settings file and the projects follow it — an ordinary run keeps "
          "jahsettings.ini in its historical place (applicationDirPath in a Debug build) and its "
          "projects under the `default_directory` preference. READ-ONLY on purpose: a setter "
          "would have to move a live database and a live asset store while they are open.",
          Needs::Document },
        { "lastError", "app.lastError() -> string | null",
          "Why the last verb answered falsy. Verbs REFUSE by returning their documented falsy "
          "value (false / 0 / null) rather than throwing — an exception would abort the whole "
          "script over an answer it asked for — and the reason is recorded here. Thrown "
          "precondition errors land here too. Null when nothing has failed yet; never cleared, "
          "so read it right after the call you are diagnosing.",
          Needs::Document },
        { "window", "app.window() -> {x, y, width, height, minWidth, minHeight, visible, fullScreen, fits, screen:{name, width, height, availWidth, availHeight}}",
          "The main window's geometry and the screen it is on, in pixels — the coordinates a rig "
          "synthesising mouse input works in. `fits` is width/height against the screen's AVAILABLE "
          "rectangle (panels excluded): false means part of the window cannot be reached. Empty map "
          "in a session with no window. `minWidth`/`minHeight` are the layout's floor — Qt will not "
          "resize a window below them, so a window can be bigger than its screen without anything "
          "being wrong with the geometry code.",
          Needs::Window },
        { "resizeWindow", "app.resizeWindow(width, height) -> {x, y, width, height, minWidth, minHeight, visible, fullScreen, fits, screen}",
          "Resizes the main window to `width` x `height` pixels and answers app.window() — so the "
          "size the window ACTUALLY took is in the reply: Qt will not go below the layout's floor "
          "(`minWidth`/`minHeight`), and asking for less leaves the window at the floor rather than "
          "failing. For layout checks at a size the rig's screen is not (a 1366x768 laptop on a "
          "1920x1080 display — ui.window_minimum). Remembered exactly like a user's drag would be "
          "(the geometry is saved on quit). Leaves full screen or maximized first. A layout settles on the "
          "event loop, so measure columns in a LATER request than the resize.",
          Needs::Window },
        { "theme", "app.theme() -> {id, classic, style, font:{family, pointSize, pixelSize}}",
          "The theme this session runs (appearance/theme, applied at startup — a change takes "
          "effect at the next launch): 'qlementine-dark' (the default: the Qlementine QStyle owns "
          "every stock widget) or 'classic' (the archived per-widget stylesheets). `style` is the "
          "QStyle's class; `font` is the application font the theme set.",
          Needs::Window },
        { "styleSheets", "app.styleSheets({visibleOnly?, window?, full?}) -> {theme, classic, widgets, styled, "
                         "themeOwned, raw, sheets:[{path, class, name, window, visible, owner, sheet}]}",
          "THE LIVE THEME WALK (theme sweep, lane 16): every widget alive right now with a non-empty "
          "styleSheet(), classified. owner 'theme' = a sheet ThemeManager handed out (the theme's own "
          "chrome); 'raw' = anything else. Under Qlementine a raw sheet interposes QStyleSheetStyle "
          "over the style for that widget and its whole subtree — the dark-on-dark / platform-light "
          "hybrids the sweep removed — so the theme.sheets suite asserts zero raw sheets outside a "
          "named allowlist on every page and dialog. Under Classic every sheet is by design and the "
          "counts are informational. visibleOnly limits the walk to shown widgets; window limits it "
          "to one top-level window by objectName. `sheet` is the first 160 characters, simplified "
          "(full: the whole sheet, verbatim — what a before/after comparison of the Classic theme "
          "diffs).",
          Needs::Window },
        { "dialogs", "app.dialogs() -> [{name, open}]",
          "The dialogs app.dialog can open, by name, and whether each is open right now.",
          Needs::Window },
        { "dialog", "app.dialog(name, open=true) -> {name, open, window, title, x, y, width, height}",
          "Opens (or, with open=false, closes) one of the app's dialogs by name — see app.dialogs. "
          "ALWAYS non-modal, even for dialogs the editor runs modally: a verb cannot wait inside "
          "exec(), so this shows the same widget tree with no result to consume. For the theme walk "
          "(a dialog built on demand exists only while it is open), for rigs that photograph the "
          "UI, and for a client that wants to put a dialog in front of the user. Returns {} for an "
          "unknown name.",
          Needs::Window },
        { "quit", "app.quit() -> bool",
          "Closes the main window through the normal close path (autosave/unsaved-changes rules apply, background work is shut down). The verb returns before the window actually closes.",
          Needs::Window },
    };
}

QVariantList AppApi::openTimings()
{
    return LoadTimeline::lastRun();
}

bool AppApi::heartbeat(int intervalMs)
{
    if (intervalMs <= 0) { MainThreadHeartbeat::stop(); return true; }
    MainThreadHeartbeat::start(intervalMs);
    return true;
}

QVariantMap AppApi::heartbeatStats()
{
    return MainThreadHeartbeat::stats();
}

QVariantMap AppApi::watchdogStats()
{
    return MainThreadWatchdog::stats();
}

bool AppApi::blockUiThread(int ms)
{
    if (!MainThreadWatchdog::isSupported())
        return fail("app.blockUiThread: development builds only");
    if (ms <= 0) return fail("app.blockUiThread: a positive duration is required");
    return MainThreadWatchdog::blockCallingThread(ms);
}

QVariantMap AppApi::shaderCache()
{
    QVariantMap m;
    auto engine = EngineHost::instance().engine();
    if (!engine) { m["enabled"] = false; return m; }
    const jahshaka::engine::ShaderCacheStats s = engine->shaderCacheStats();
    m["enabled"]             = s.enabled;
    m["dir"]                 = QString::fromStdString(s.dir);
    m["fingerprint"]         = QString::fromStdString(s.fingerprint);
    m["sizeBytes"]           = QVariant::fromValue(qulonglong(s.sizeBytes));
    m["files"]               = s.files;
    m["pipelineCacheLoaded"] = s.pipelineCacheLoaded;
    m["pipelineCacheReason"] = QString::fromStdString(s.pipelineCacheReason);
    m["microcodeLoaded"]     = s.microcodeLoaded;
    m["microcodeEntries"]    = s.microcodeEntries;
    m["hlmsCachesLoaded"]    = s.hlmsCachesLoaded;
    m["compiledThisRun"]     = s.compiledThisRun;
    m["loadedThisRun"]       = s.loadedThisRun;
    m["expectedShaders"]     = s.expectedShaders;
    m["lastSaved"]           = QVariant::fromValue(qlonglong(s.lastSavedUnixMs));
    return m;
}

bool AppApi::clearShaderCache()
{
    JAH_LOG(JahLog::shader, Display,
            QStringLiteral("shader cache: cleared on request — the NEXT launch is cold"));
    // Two halves on purpose: the engine drops what it wrote, and the host
    // removes the directory itself — clearing must work in a session whose
    // engine never started (a headless run) exactly as in one where it did.
    bool ok = EngineHost::clearShaderCacheOnDisk();
    if (auto engine = EngineHost::instance().engine()) ok = engine->clearShaderCache() && ok;
    return ok;
}

bool AppApi::saveShaderCache()
{
    auto engine = EngineHost::instance().engine();
    if (!engine) return fail("app.saveShaderCache: the engine is not running");
    return engine->saveShaderCache();
}

QVariantMap AppApi::warmUpSet(const QString &action)
{
    QVariantMap m;
    const QString path = EngineHost::warmUpSetPath();
    m["path"] = path;
    auto engine = EngineHost::instance().engine();
    const QString what = action.trimmed().toLower();
    if (!what.isEmpty() && !engine) { fail("app.warmUpSet: the engine is not running"); return m; }

    if (what == QLatin1String("record")) {
        const bool recorded = engine->recordWarmUpSet();
        m["recorded"] = recorded;
        if (recorded) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            m["saved"] = engine->saveWarmUpSet(path.toStdString());
        }
    } else if (what == QLatin1String("apply")) {
        // Needs a scene to host the degenerate renderables. The editor's own
        // scene is the natural one and leaves nothing behind (createWarmUp /
        // destroyWarmUp are paired inside the engine).
        m["built"] = 0u;
        if (!QFileInfo::exists(path)) return fail("app.warmUpSet: no set recorded yet"), m;
        m["built"] = engine->applyWarmUpSet(path.toStdString(), nullptr);
    } else if (!what.isEmpty()) {
        return fail(QStringLiteral("app.warmUpSet: unknown action '%1' (record, apply)").arg(action)), m;
    }

    const QFileInfo info(path);
    m["exists"] = info.exists();
    m["sizeBytes"] = QVariant::fromValue(qulonglong(info.exists() ? info.size() : 0));
    // The two things a caller needs to reason about the AUTOMATIC recording and
    // replay, both of which used to be invisible: whether they happen at all
    // (audit F12 — they used to happen even with the cache off) and what pass
    // shape the startup replay will use (audit F1b — it used to be an empty,
    // lightless, shadowless 1x scene whatever the editor drew).
    m["enabled"] = EngineHost::shaderCacheEnabled();
    const EngineHost::WarmUpShape shape = EngineHost::warmUpShape();
    QVariantMap shapeMap;
    shapeMap["samples"] = shape.samples;
    shapeMap["shadows"] = shape.shadows;
    m["shape"] = shapeMap;
    return m;
}

bool AppApi::quit()
{
    if (!host.mainWindow) return fail("app: not available in this session");
    // Deferred: let the calling script (and its undo macro) finish first.
    QMetaObject::invokeMethod(host.mainWindow, [w = host.mainWindow]() { w->close(); },
                              Qt::QueuedConnection);
    return true;
}

int AppApi::desktop(int n)
{
    if (!host.projectManager) { fail("app: not available in this session"); return 0; }
    if (n >= 1) host.projectManager->switchDesktop(n);
    return host.projectManager->getCurrentDesktop();
}

bool AppApi::space(const QString &name)
{
    if (!host.mainWindow) return fail("app: not available in this session");
    const QString s = name.trimmed().toLower();
    WindowSpaces space;
    if (s == "desktop")                          space = WindowSpaces::DESKTOP;
    else if (s == "player")                      space = WindowSpaces::PLAYER;
    else if (s == "editor")                      space = WindowSpaces::EDITOR;
    else if (s == "materials" || s == "effects") space = WindowSpaces::EFFECT;
    else if (s == "assets")                      space = WindowSpaces::ASSETS;
    else if (s == "publish")                     space = WindowSpaces::PUBLISH;
    else if (s == "avatar")                      space = WindowSpaces::AVATAR;
    else return fail(QStringLiteral("app.space: unknown space '%1' (desktop, player, editor, materials, assets, publish, avatar)").arg(name));

    const bool sceneOpen = host.services && host.services->project && host.services->project->isSceneOpen();
    if ((space == WindowSpaces::PLAYER || space == WindowSpaces::EDITOR) && !sceneOpen)
        return fail(QStringLiteral("app.space: '%1' needs an open project").arg(s));

    host.mainWindow->switchSpace(space);

    // audit D15: the verb once reported success while the page stayed put —
    // never claim a switch the window didn't make
    if (host.mainWindow->getWindowSpace() != space)
        return fail(QStringLiteral("app.space: the window refused to switch to '%1'").arg(s));
    return true;
}

QVariantMap AppApi::engineErrors(bool reset)
{
    // No engine guard: the pump's record outlives any particular engine and the
    // most useful moment to read it is exactly when the renderer has fallen
    // over. Needs::Document for the same reason.
    const QVariantMap out = EngineErrorPump::instance().report();
    if (reset) EngineErrorPump::instance().reset();
    return out;
}

QVariantList AppApi::ogreSamples()
{
    // Needs::Document: this is a question about FILES ON DISK, answerable in a
    // headless run — and answering it there is the point, because the honest
    // answer everywhere but a comparison tree is "not built".
    QVariantList out;
    const QDir scenes(IrisUtils::getAbsoluteAssetPath(Constants::SAMPLES_FOLDER));
    for (const ogresamples::Entry &e : ogresamples::catalog()) {
        QVariantMap m;
        m.insert("name", e.name);
        m.insert("title", e.title);
        m.insert("note", e.note);
        const QString bin = ogresamples::binaryPath(e.name);
        m.insert("available", !bin.isEmpty());
        m.insert("path", bin);
        m.insert("running", ogresamples::isRunning(e.name));
        const QString archive =
            scenes.absoluteFilePath(QStringLiteral("ogre/") + e.name + QStringLiteral(".zip"));
        m.insert("portArchive", archive);
        m.insert("portAvailable", QFileInfo::exists(archive));
        out.append(m);
    }
    return out;
}

QVariantMap AppApi::launchOgreSample(const QString &name, const QVariantMap &options)
{
    // REFUSAL, NOT EXCEPTION. Every failure here is an ordinary state of the
    // world (samples not built, no display, already running), so the verb
    // reports rather than throws — the button shows `reason` as a tooltip and
    // a script can branch on `launched`.
    ogresamples::LaunchOptions opts;
    if (options.contains("width"))      opts.width = options.value("width").toInt();
    if (options.contains("height"))     opts.height = options.value("height").toInt();
    if (options.contains("fullscreen")) opts.fullscreen = options.value("fullscreen").toBool();

    const ogresamples::LaunchResult r = ogresamples::launch(name, opts);
    QVariantMap out;
    out.insert("launched", r.launched);
    out.insert("path", r.path);
    out.insert("pid", r.pid);
    if (!r.reason.isEmpty()) out.insert("reason", r.reason);
    return out;
}

QVariantList AppApi::apiProblems()
{
    // ApiRegistry::validate() has existed since the scripting engine shipped
    // and had exactly ONE caller: the scripting unit test, whose binary links
    // only the scripting core and therefore validates a fake module
    // (AI_SURFACE_PROGRAM_SPEC §1, row 4). This verb is what runs it over the
    // real 14 modules — including modules contributed by StudioModules, which
    // no unit test can see.
    if (!host.registry) { fail("app.apiProblems: no script registry in this session"); return {}; }
    QVariantList out;
    for (const QString &problem : host.registry->validate()) out.append(problem);
    return out;
}

QVariantMap AppApi::dataRoot()
{
    // READ-ONLY, and the reason is in the verb's doc: the database and the
    // asset store are OPEN by the time a script can call this, so "set the data
    // root" is a relocation of live state and not a setting. The override is a
    // launch-time decision (--data-root / JAHSHAKA_DATA_ROOT).
    QVariantMap out;
    const QString root = AppPaths::dataRoot();
    out["root"] = root;
    out["overridden"] = AppPaths::isOverridden();
    out["settingsFile"] = SettingsManager::getDefaultManager()->settings->fileName();
    out["database"] = QDir(root).filePath(Constants::JAH_DATABASE);
    out["assetStore"] = AssetStorePaths::root();
    // THE PROJECTS (S-extra2): the root that used to ignore the override
    // entirely, so every scripted run wrote its project folders into the
    // developer's Documents. Read from the service so the verb reports the
    // path project.create will really use, preference included.
    out["projects"] = host.services && host.services->project
                          ? host.services->project->projectsRoot()
                          : AppPaths::projectsRoot(QString(), Constants::PROJECT_FOLDER);
    return out;
}

// THE WINDOW ITSELF (hygiene lane, 2026-09-09). Everything in this map is
// pixels the user has to reach with a mouse, which is exactly what a rig
// driving xdotool needs and what no other verb reports: the window's size and
// position, and the screen rectangle it is supposed to fit inside.
//
// It exists because the fresh-profile size is a real defect class: mainwindow.ui
// authors 1612x1530 and nothing clamped it before MainWindow::fitToScreen, so on
// a 1080p desktop — or a WM-less Xvfb, where nothing resizes anything ever — the
// bottom of the window simply was not on the screen.
// The reason behind the last falsy answer (hygiene lane, 2026-09-09). Verbs
// REFUSE by returning their documented falsy value — false, 0, null — instead
// of throwing, because a refusal is an answer and an exception aborts the whole
// run. The message is not lost: it lands here.
QVariant AppApi::lastError()
{
    if (host.lastError.isEmpty()) return jsNull();
    return host.lastError;
}

QVariantMap AppApi::columns()
{
    QVariantMap out;
    if (!host.mainWindow) {
        fail("app.columns: this verb needs the editor window (a --script/--headless run has no "
             "pages)");
        return out;
    }
    QString space;
    switch (host.mainWindow->getWindowSpace()) {
    case WindowSpaces::DESKTOP: space = QStringLiteral("desktop"); break;
    case WindowSpaces::EDITOR:  space = QStringLiteral("editor"); break;
    case WindowSpaces::PLAYER:  space = QStringLiteral("player"); break;
    case WindowSpaces::EFFECT:  space = QStringLiteral("materials"); break;
    case WindowSpaces::ASSETS:  space = QStringLiteral("assets"); break;
    case WindowSpaces::PUBLISH: space = QStringLiteral("publish"); break;
    case WindowSpaces::AVATAR:  space = QStringLiteral("avatar"); break;
    default:                    space = QStringLiteral("unknown"); break;
    }
    out.insert("space", space);
    // What the law SAYS, beside what the page DID — a caller comparing the two
    // does not have to carry a copy of the constants.
    QVariantMap metrics;
    metrics.insert("leftWidth", PanelMetrics::leftColumnWidth);
    metrics.insert("leftMin", PanelMetrics::leftColumnMinWidth);
    metrics.insert("rightWidth", PanelMetrics::rightColumnWidth);
    metrics.insert("rightMin", PanelMetrics::rightColumnMinWidth);
    out.insert("metrics", metrics);
    const MainWindow::ColumnMetrics m = host.mainWindow->activeColumns();
    if (!m.valid) return out;
    if (m.leftWidth > 0 || m.leftMin > 0) {
        QVariantMap left;
        left.insert("width", m.leftWidth);
        left.insert("min", m.leftMin);
        out.insert("left", left);
    }
    if (m.rightWidth > 0 || m.rightMin > 0) {
        QVariantMap right;
        right.insert("width", m.rightWidth);
        right.insert("min", m.rightMin);
        out.insert("right", right);
    }
    return out;
}

QVariantMap AppApi::window()
{
    QVariantMap out;
    QWidget *w = host.mainWindow;
    if (!w) return out;                 // no window in this session: an empty map, not a throw
    const QRect g = w->frameGeometry().isValid() ? w->frameGeometry() : w->geometry();
    out.insert("x", g.x());
    out.insert("y", g.y());
    out.insert("width", w->width());
    out.insert("height", w->height());
    out.insert("visible", w->isVisible());
    out.insert("fullScreen", w->isFullScreen());
    // The floor Qt will not go below. A widget cannot be resized under its
    // layout's minimum size hint, so a caller asking "why is this window wider
    // than the screen" needs this number to tell a clamp that failed from a
    // window that simply cannot be that small.
    const QSize minimum = w->minimumSizeHint().expandedTo(w->minimumSize());
    out.insert("minWidth", minimum.width());
    out.insert("minHeight", minimum.height());
    if (QScreen *s = w->screen()) {
        const QRect avail = s->availableGeometry();
        const QRect full = s->geometry();
        QVariantMap screen;
        screen.insert("name", s->name());
        screen.insert("width", full.width());
        screen.insert("height", full.height());
        screen.insert("availWidth", avail.width());
        screen.insert("availHeight", avail.height());
        out.insert("screen", screen);
        // The one derived answer worth having, because it is the assertion
        // every caller would otherwise write itself (and get wrong on a
        // multi-monitor desktop, where a window may legitimately overhang).
        out.insert("fits", w->width() <= avail.width() && w->height() <= avail.height());
    }
    return out;
}

QVariantMap AppApi::resizeWindow(int width, int height)
{
    QWidget *w = host.mainWindow;
    if (!w) {
        fail("app.resizeWindow: this session has no main window");
        return {};
    }
    if (width <= 0 || height <= 0) {
        fail(QStringLiteral("app.resizeWindow: %1 x %2 is not a window size").arg(width).arg(height));
        return {};
    }
    if (w->isFullScreen() || w->isMaximized()) w->showNormal();
    w->resize(width, height);
    return window();
}

QVariantMap AppApi::frameStats()
{
    // No engine guard, same reasoning as engineErrors: "the loop is not running"
    // is one of the answers this verb exists to give, so it must be readable
    // when there is no engine at all.
    QVariantMap out;
    EngineRenderDriver *driver = EngineHost::instance().driver();
    const EngineRenderDriver::Stats s = driver ? driver->stats() : EngineRenderDriver::Stats{};
    out.insert("running", driver ? driver->isRunning() : false);
    out.insert("intervalMs", driver ? driver->intervalMs() : 0);
    out.insert("ticks", QVariant::fromValue(s.ticks));
    out.insert("rendered", QVariant::fromValue(s.rendered));
    out.insert("skipped", QVariant::fromValue(s.skipped));
    out.insert("workMs", s.workMs);
    out.insert("worstMs", s.worstMs);
    out.insert("slowFrames", QVariant::fromValue(s.slowFrames));
    auto engine = EngineHost::instance().engine();
    out.insert("enabledViews", engine ? engine->hasEnabledViews() : false);
    return out;
}

QVariantMap AppApi::pacing(const QString &mode)
{
    // No engine guard, like frameStats: "there is no loop" is a legitimate
    // answer, and the persisted preference is readable either way.
    QVariantMap out;
    EngineRenderDriver *driver = EngineHost::instance().driver();
    if (!driver) { fail("app.pacing: no render loop in this session"); return out; }
    if (!mode.isEmpty()) {
        bool ok = false;
        const framepacing::Mode m = framepacing::modeFromName(mode, &ok);
        if (!ok) {
            fail(QStringLiteral("app.pacing: unknown mode '%1' (expected %2)")
                     .arg(mode, framepacing::modeNames().join(QStringLiteral(", "))));
            return out;
        }
        const framepacing::Mode was = driver->pacingMode();
        driver->setPacingMode(m);
        // A graphics setting changing mid-session is exactly the kind of thing
        // that makes two frame-rate readings incomparable — record it
        // (SESSION_LOG_SPEC §5, the quality-tier row's substitute: no tier
        // concept exists in this tree, so the individual settings are logged).
        if (was != m)
            JAH_LOG(JahLog::render, Display,
                    QStringLiteral("pacing: %1 -> %2")
                        .arg(framepacing::modeName(was), framepacing::modeName(m)));
        // Persisted here rather than in the driver: the driver is a render
        // loop, and the settings file belongs to the shell. Preferences writes
        // the same key (services/framepacing.h::settingsKey).
        if (SettingsManager *s = SettingsManager::getDefaultManager())
            s->setValue(framepacing::settingsKey(), framepacing::modeName(m));
    }
    out.insert("mode", framepacing::modeName(driver->pacingMode()));
    out.insert("modes", framepacing::modeNames());
    out.insert("intervalMs", driver->intervalMs());
    out.insert("refreshHz", driver->refreshHz());
    out.insert("vsync", framepacing::vsyncFor(driver->pacingMode()));
    out.insert("running", driver->isRunning());
    return out;
}

QVariantMap AppApi::renderStats()
{
    // Needs::Engine, unlike frameStats: every number here comes out of the
    // backend, so "there is no engine" has no honest answer to give — an empty
    // map with a refusal beats a map full of zeros that reads like a stalled
    // renderer.
    QVariantMap out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.renderStats: no engine in this session"); return out; }
    jahshaka::engine::RenderStats s;
    if (!engine->renderStats(s)) {
        fail("app.renderStats: the engine could not report its counters");
        return out;
    }
    out.insert("metricsRecording", s.metricsRecording);
    out.insert("fps", s.fps);
    out.insert("frameMs", s.frameMs);
    out.insert("lastMs", s.lastMs);
    out.insert("p95Ms", s.p95Ms);
    out.insert("p99Ms", s.p99Ms);
    out.insert("bestMs", s.bestMs);
    out.insert("worstMs", s.worstMs);
    out.insert("draws", QVariant::fromValue(qulonglong(s.draws)));
    out.insert("batches", QVariant::fromValue(qulonglong(s.batches)));
    out.insert("triangles", QVariant::fromValue(qulonglong(s.triangles)));
    out.insert("vertices", QVariant::fromValue(qulonglong(s.vertices)));
    out.insert("instances", QVariant::fromValue(qulonglong(s.instances)));
    out.insert("incompletePsoRequests", s.incompletePsoRequests);
    // The Forward+ census. `forwardPlusOverBudget` == 0 PROVES no light was
    // dropped from any cell; non-zero says a full cell would have dropped that
    // many. See RenderStats for why an exact drop count needs an Ogre patch.
    out.insert("forwardPlusLights", s.forwardPlusLights);
    out.insert("forwardPlusBudget", s.forwardPlusBudget);
    out.insert("forwardPlusOverBudget", s.forwardPlusOverBudget);
    return out;
}

QVariantMap AppApi::engineObjects()
{
    // Needs::Engine, same reasoning as renderStats: every number comes out of
    // the boundary, and a map full of zeros would read like an engine holding
    // nothing rather than like an engine that is not there.
    QVariantMap out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.engineObjects: no engine in this session"); return out; }
    jahshaka::engine::ObjectCounts c;
    if (!engine->objectCounts(c)) {
        fail("app.engineObjects: the engine could not report its object counts");
        return out;
    }
    out.insert("views", c.views);
    out.insert("enabledViews", c.enabledViews);
    out.insert("scenes", c.scenes);
    out.insert("updatedScenes", c.updatedScenes);
    out.insert("stagingScenes", c.stagingScenes);
    out.insert("nodes", c.nodes);
    out.insert("meshes", c.meshes);
    out.insert("materials", c.materials);
    out.insert("textures", c.textures);
    out.insert("datablocks", c.datablocks);
    return out;
}

namespace {
QVariantMap memoryStatsToMap(const jahshaka::engine::MemoryStats &m)
{
    QVariantMap out;
    out.insert("gpuPoolCapacityBytes", QVariant::fromValue(qulonglong(m.gpuPoolCapacityBytes)));
    out.insert("gpuPoolFreeBytes", QVariant::fromValue(qulonglong(m.gpuPoolFreeBytes)));
    out.insert("gpuPools", m.gpuPools);
    out.insert("gpuPoolsIncludeTextures", m.gpuPoolsIncludeTextures);
    out.insert("sceneManagers", m.sceneManagers);
    out.insert("simdNodes", m.simdNodes);
    out.insert("simdObjects", m.simdObjects);
    out.insert("simdNodeDepths", m.simdNodeDepths);
    out.insert("residentBytes", QVariant::fromValue(qulonglong(m.residentBytes)));
    return out;
}
}   // namespace

QVariantMap AppApi::memoryStats()
{
    QVariantMap out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.memoryStats: no engine in this session"); return out; }
    jahshaka::engine::MemoryStats m;
    if (!engine->memoryStats(m)) {
        fail("app.memoryStats: the engine could not report its pools");
        return out;
    }
    return memoryStatsToMap(m);
}

QVariantMap AppApi::textureMemory(const QVariantMap &options)
{
    QVariantMap out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.textureMemory: no engine in this session"); return out; }
    std::vector<jahshaka::engine::TextureMemoryEntry> entries;
    if (!engine->textureMemory(entries)) {
        fail("app.textureMemory: the engine could not walk its textures");
        return out;
    }
    const QVariant topValue = scriptmod::normalizeJs(options.value(QStringLiteral("top")));
    int top = 50;
    if (topValue.isValid() && !topValue.isNull()) {
        bool numeric = false;
        const double asked = topValue.toDouble(&numeric);
        if (!numeric || asked < 0) {
            fail("app.textureMemory: top must be a number >= 0 (0 = every entry)");
            return out;
        }
        top = int(asked);
    }
    const bool residentOnly = scriptmod::normalizeJs(options.value(QStringLiteral("resident"))).toBool();

    std::sort(entries.begin(), entries.end(),
              [](const jahshaka::engine::TextureMemoryEntry &a,
                 const jahshaka::engine::TextureMemoryEntry &b) {
                  if (a.bytes != b.bytes) return a.bytes > b.bytes;
                  return a.name < b.name;
              });
    qulonglong total = 0, resident = 0, pooled = 0, rtt = 0;
    QVariantList rows;
    for (const auto &e : entries) {
        const bool isResident = e.residency == "Resident";
        total += e.bytes;
        if (isResident) resident += e.bytes;
        if (e.pooled) pooled += e.bytes;
        if (e.renderTarget) rtt += e.bytes;
        if (residentOnly && !isResident) continue;
        if (top > 0 && rows.size() >= top) continue;
        QVariantMap row;
        row.insert("name", QString::fromStdString(e.name));
        row.insert("resource", QString::fromStdString(e.resource));
        row.insert("width", e.width);
        row.insert("height", e.height);
        row.insert("depth", e.depth);
        row.insert("slices", e.slices);
        row.insert("mipmaps", e.mipmaps);
        row.insert("msaa", e.msaa);
        row.insert("format", QString::fromStdString(e.format));
        row.insert("bytes", qulonglong(e.bytes));
        row.insert("renderTarget", e.renderTarget);
        row.insert("uav", e.uav);
        row.insert("manual", e.manual);
        row.insert("pooled", e.pooled);
        row.insert("residency", QString::fromStdString(e.residency));
        rows.append(row);
    }
    out.insert("count", int(entries.size()));
    out.insert("totalBytes", total);
    out.insert("residentBytes", resident);
    out.insert("pooledBytes", pooled);
    out.insert("renderTargetBytes", rtt);
    out.insert("entries", rows);
    return out;
}

QVariantMap AppApi::reclaimMemory()
{
    QVariantMap out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.reclaimMemory: no engine in this session"); return out; }
    jahshaka::engine::MemoryStats before, after;
    if (!engine->reclaimMemory(&before, &after)) {
        fail("app.reclaimMemory: the engine refused: " +
             QString::fromStdString(engine->lastError()));
        return out;
    }
    out.insert("before", memoryStatsToMap(before));
    out.insert("after", memoryStatsToMap(after));
    return out;
}

bool AppApi::profiling(const QVariant &on)
{
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.profiling: no engine in this session"); return false; }
    if (on.isValid() && !on.isNull()) engine->setProfiling(on.toBool());
    return engine->profiling();
}

QVariantMap AppApi::threading()
{
    // Needs::Engine, same reasoning as renderStats/engineObjects: every field
    // comes from the boundary, and an empty map would read like "not threaded"
    // rather than like "no engine in this session" — which is precisely the
    // confusion this verb exists to prevent.
    QVariantMap out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.threading: no engine in this session"); return out; }
    jahshaka::engine::EngineThreading t;
    if (!engine->threading(t)) {
        fail("app.threading: the engine could not report its threading state");
        return out;
    }
    out.insert("multithreadedShaderCompilation", t.multithreadedShaderCompilation);
    out.insert("shaderThreadingMode", t.shaderThreadingMode);
    QVariantMap scenes;
    for (const auto &s : t.sceneWorkerThreads)
        scenes.insert(QString::fromStdString(s.first), s.second);
    out.insert("sceneWorkerThreads", scenes);
    out.insert("hlmsThreads", t.hlmsThreads);
    return out;
}

QVariantMap AppApi::textureStreaming()
{
    // Needs::Engine, same reasoning as threading(): every field comes off the
    // boundary and zeros would read like "nothing is loading" rather than like
    // "there is no engine in this session".
    QVariantMap out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.textureStreaming: no engine in this session"); return out; }
    out.insert("multiLoadThreads", engine->textureMultiLoadThreads());
    out.insert("doneStreaming", engine->texturesDoneStreaming());
    out.insert("loadRequests", QVariant::fromValue(qulonglong(engine->textureLoadRequests())));
    out.insert("metadataCacheEntries", engine->textureMetadataCacheEntries());
    out.insert("channelCacheEntries", engine->textureChannelCacheEntries());
    out.insert("waitTimeouts", engine->textureWaitTimeouts());
    out.insert("waitWorstMs", engine->textureWaitWorstMs());
    out.insert("waitBudgetMs", engine->textureWaitBudgetMs());
    return out;
}

QVariantMap AppApi::waitForTextures()
{
    QVariantMap out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail("app.waitForTextures: no engine in this session"); return out; }
    out.insert("waitedMs", engine->waitForTextureLoads());
    out.insert("loadRequests", QVariant::fromValue(qulonglong(engine->textureLoadRequests())));
    out.insert("doneStreaming", engine->texturesDoneStreaming());
    return out;
}

QVariantMap AppApi::theme()
{
    QVariantMap out;
    out["id"] = ThemeManager::classicActive() ? ThemeManager::classicId()
                                              : ThemeManager::qlementineDarkId();
    out["classic"] = ThemeManager::classicActive();
    if (QStyle *style = QApplication::style())
        out["style"] = QString::fromLatin1(style->metaObject()->className());
    const QFont font = QApplication::font();
    out["font"] = QVariantMap{ { "family", font.family() },
                               { "pointSize", font.pointSizeF() },
                               { "pixelSize", font.pixelSize() } };
    return out;
}

namespace {

// "MainWindow/centralWidget/AssetView/QLabel": objectNames where they exist,
// class names where they do not — enough to find the widget in the source.
QString widgetPath(const QWidget *w)
{
    QStringList parts;
    for (const QWidget *p = w; p; p = p->parentWidget()) {
        const QString name = p->objectName();
        parts.prepend(name.isEmpty() ? QString::fromLatin1(p->metaObject()->className()) : name);
        if (p->isWindow()) break;
    }
    return parts.join(QLatin1Char('/'));
}

QString windowName(const QWidget *w)
{
    const QWidget *win = w->window();
    if (!win) return QString();
    return win->objectName().isEmpty() ? QString::fromLatin1(win->metaObject()->className())
                                       : win->objectName();
}

} // namespace

QVariantMap AppApi::styleSheets(const QVariantMap &options)
{
    const bool visibleOnly = options.value("visibleOnly").toBool();
    const QString onlyWindow = options.value("window").toString();
    const bool full = options.value("full").toBool();

    QVariantList sheets;
    int widgets = 0, styled = 0, themeOwned = 0, raw = 0;
    const QWidgetList all = QApplication::allWidgets();
    for (QWidget *w : all) {
        if (visibleOnly && !w->isVisible()) continue;
        if (!onlyWindow.isEmpty() && windowName(w) != onlyWindow) continue;
        ++widgets;
        const QString sheet = w->styleSheet();
        if (sheet.trimmed().isEmpty()) continue;
        ++styled;
        const bool ours = ThemeManager::isThemeSheet(sheet);
        ours ? ++themeOwned : ++raw;
        sheets.append(QVariantMap{
            { "path", widgetPath(w) },
            { "class", QString::fromLatin1(w->metaObject()->className()) },
            { "name", w->objectName() },
            { "window", windowName(w) },
            { "visible", w->isVisible() },
            { "owner", ours ? QStringLiteral("theme") : QStringLiteral("raw") },
            { "sheet", full ? sheet : sheet.simplified().left(160) } });
    }
    QVariantMap out;
    out["theme"] = ThemeManager::classicActive() ? ThemeManager::classicId()
                                                 : ThemeManager::qlementineDarkId();
    out["classic"] = ThemeManager::classicActive();
    out["widgets"] = widgets;
    out["styled"] = styled;
    out["themeOwned"] = themeOwned;
    out["raw"] = raw;
    out["sheets"] = sheets;
    return out;
}

QVariantList AppApi::dialogs()
{
    QVariantList out;
    if (!host.mainWindow) {
        fail("app.dialogs: this verb needs the editor window");
        return out;
    }
    for (const QString &name : host.mainWindow->dialogNames()) {
        // closeDialog/openDialog own the bookkeeping; "open" is simply whether
        // the name's widget is on screen now.
        out.append(QVariantMap{ { "name", name },
                                { "open", host.mainWindow->isDialogOpen(name) } });
    }
    return out;
}

QVariantMap AppApi::dialog(const QString &name, bool open)
{
    QVariantMap out;
    if (!host.mainWindow) {
        fail("app.dialog: this verb needs the editor window");
        return out;
    }
    if (!host.mainWindow->dialogNames().contains(name)) {
        fail(QStringLiteral("app.dialog: no dialog named '%1' (app.dialogs() lists them)").arg(name));
        return out;
    }
    out["name"] = name;
    if (!open) {
        host.mainWindow->closeDialog(name);
        out["open"] = false;
        return out;
    }
    QWidget *w = host.mainWindow->openDialog(name);
    if (!w) {
        fail(QStringLiteral("app.dialog: '%1' could not be opened in this session").arg(name));
        return out;
    }
    out["open"] = w->isVisible();
    out["window"] = windowName(w);
    out["title"] = w->windowTitle();
    const QRect g = w->frameGeometry();
    out["x"] = g.x();
    out["y"] = g.y();
    out["width"] = g.width();
    out["height"] = g.height();
    return out;
}
