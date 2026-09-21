/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <algorithm>

#include "app/notices.h"
#include "services/services.h"
#include "services/projectservice.h"
#include "scripting/modules/appapi.h"
#include "scripting/modules/moduleshared.h"

#include "shell/mainwindow.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/thememanager.h"
#include "ui/pages/projectmanager.h"
#include "scripting/apiregistry.h"
#include "scripting/scriptengine.h"
#include "services/engineerrorpump.h"
#include "services/loadtimeline.h"
#include "services/mainthreadheartbeat.h"
#include "irisgl/import/parsecensus.h"
#include "services/mainthreadwatchdog.h"
#include "bridge/enginehost.h"
#include "viewport/enginerenderdriver.h"
#include "viewport/ieditorviewport.h"
#include "services/framepacing.h"
#include "services/framemonitor.h"
#include "services/scenestats.h"
#include "services/sceneeditservice.h"
#include "irisgl/document/scenegraph/scene.h"
#include "data/settingsmanager.h"
#include "scripting/mcp/mcplog.h"
#include "services/jahlog.h"
#include "services/apppaths.h"
#include "services/assetstorepaths.h"
#include "app/firstrun.h"
#include "services/libraryreset.h"
#include "data/constants.h"
#include "services/ogresamples.h"
#include "irisgl/core/irisutils.h"
#include <QApplication>
#include <QDir>
#include <QProcess>
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
        { "docks", "app.docks() -> [{name, title, visible, shown, tabbed, current, floating, x, y, "
                   "width, height, minWidth, area}]",
          "THE EDITOR'S PANELS, MEASURED: one entry per dock of the editor page (Hierarchy, "
          "Properties, Presets, Assets, Timeline, Console). `visible` is whether the dock is on "
          "screen RIGHT NOW — false for every one of them while another space is showing, because "
          "they are children of the editor page — and `shown` is the dock's own state, which is "
          "what the editor will show when its page comes back. `tabbed` and `current` are the "
          "bottom area's story (lane SPACE-2): the three panels down there share ONE tab bar, so "
          "an open panel can still sit behind another tab — `tabbed` says it shares a bar, "
          "`current` that it is the tab in front; a panel the user closed is `shown: false` and "
          "has no tab at all. `width` matters as much as either: a "
          "restored layout can bring a panel back at a degenerate width (a 20 px left column "
          "showing nothing but icons), which from the user's chair is indistinguishable from a "
          "panel that is gone, and `minWidth` is the width below which that is what has "
          "happened. The verb exists because \"the editor came back with its panels\" was "
          "otherwise only checkable by looking at the window (lane SPACE-1). `x`/`y` are the "
          "dock's top-left corner in the WINDOW's coordinates, for a rig that has to put a "
          "real pointer on a real panel.",
          Needs::Window },
        { "openTimings", "app.openTimings() -> [{stage, ms, items?, label?}]",
          "The millisecond ledger of the most recent scene open (services/loadtimeline.h): one entry per stage, "
          "the first entry being {stage:'total', ms, label}, plus 'counter:*' entries for the work that "
          "accumulates inside the stages (assimp parses, database sweeps, the engine push). Empty before "
          "the first open of the session.",
          Needs::Document },
        { "openStats", "app.openStats({reset:false}) -> {uiThreadParses, uiThreadParseMs, "
          "uiThreadResourceParses, uiThreadResourceParseMs, workerParses, workerParseMs, "
          "lastUiThreadParse, bakeHits, bakeMisses, sliceBoundaries, sliceBoundaryFrames}",
          "Model PARSES since the last reset, split by the thread that paid for them "
          "(irisgl/import/parsecensus.h), and the bake reads beside them. A project open must "
          "never parse a model on the UI thread — assimp on a 6 MB mesh is a second of frozen "
          "window — so 'uiThreadParses' is the number open.responsive asserts is ZERO over the "
          "open of every shipped sample. 'bakeMisses' says why a parse was needed at all (no "
          "bake for that content yet), and 'lastUiThreadParse' names the file when the count is "
          "not zero. Pass {reset:true} to zero the counters AFTER reading them, which is how a "
          "caller measures ONE open. 'bakeMisses' counts LOOKUPS, not models — one bake-less "
          "model is asked for twice on a cold open (the prewarm worker's plan item, then the "
          "reader's own) — so read it as how often the open had to fall back to a parse. "
          "'uiThreadResourceParses' counts the built-in primitives (':/...') separately: a few "
          "kilobytes compiled into the binary, which no worker can hoist because every caller "
          "asks for them by name. They are parsed once per process only because the shell PINS "
          "them (iris::Mesh::pinLoadPaths); the load cache itself holds weak references, so "
          "before the pin every open after a close re-parsed them here. Process-wide and always "
          "on: the cost is one clock read per parse. "
          "'sliceBoundaries' and 'sliceBoundaryFrames' are the THREADED open's own drive "
          "(services/sceneopenrunner.h): the install runs one slice per event-loop turn and "
          "renders a frame at every boundary between two of them, because the app's 16 ms render "
          "tick loses to a chain of posted events and an open driven by a polling script or a "
          "busy panel would otherwise install a whole world with no frame at all — which leaves "
          "the renderer's per-frame recycling un-run for the length of the install. "
          "'sliceBoundaries' counts the crossings, 'sliceBoundaryFrames' how many of them really "
          "drew (a session with no viewport makes the engine's bare resource advance instead). "
          "Both are monotonic over the window's life, never reset by {reset:true} — only "
          "differences mean anything.",
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
        { "watchdogStats", "app.watchdogStats() -> {supported, running, enabled, stallMs, minStallMs, defaultStallMs, reports, lastStallMs, cooldownMs, sinceLastReportMs}",
          "The main-thread watchdog (services/mainthreadwatchdog.h): a thread of our own that polls the "
          "heartbeat's last-tick atomic and, when the UI thread has not ticked for stallMs, makes THAT thread "
          "print its own backtrace (a watchdog thread calling backtrace() would photograph itself). 'reports' "
          "counts the stalls this session reported — at most one per stall, capped and cooled down. THE COOLDOWN IS "
          "REPORTED, because it is otherwise invisible: 'cooldownMs' is the rate limit and 'sinceLastReportMs' "
          "how long ago the last report was (-1 = none yet), so a caller that stalls the thread WITHIN the "
          "cooldown can tell a dropped report from a missed one — which is exactly what made app.watchdog_stall "
          "flake on a cold cache, where a boot stall from shader compilation swallowed the suite's own. A "
          "DEVELOPMENT-BUILD feature: 'supported' is false in a release build, and a dev build can still turn "
          "it off with the watchdog_enabled preference or --watchdog=off. 'stallMs' is the LIVE threshold: "
          "app.watchdog(stallMs) and --watchdog-stall=N move it, and 'minStallMs'/'defaultStallMs' are its "
          "floor and the shipped value.",
          Needs::Window },
        { "watchdog", "app.watchdog(stallMs) -> {supported, running, enabled, stallMs, minStallMs, defaultStallMs, reports, lastStallMs, cooldownMs, sinceLastReportMs}",
          "Sets the main-thread watchdog's STALL THRESHOLD for the rest of the session and returns the "
          "stats it would have returned anyway (app.watchdogStats). The threshold shipped hardcoded at "
          "2000 ms — right for 'the user noticed a freeze', wrong for a diagnosis: a 1,439 ms block in the "
          "archive export produced no backtrace because it was under the fence. Lower it around the step "
          "you are measuring and put it back afterwards. The floor is 200 ms, the watchdog's own poll "
          "interval, and a smaller value is REFUSED rather than clamped (the returned stallMs would "
          "otherwise be a number nobody asked for); 'minStallMs' and 'defaultStallMs' are reported so a "
          "caller need not hardcode either. The change takes effect within one poll, with no restart. "
          "WHAT IT DOES NOT CHANGE is the rate limit: the backtrace is taken AT MOST ONCE PER STALL, and "
          "after a report the watchdog drops everything for cooldownMs (5000) with a cap of 20 reports per "
          "session — so a short stall inside the cooldown still produces nothing, and 'sinceLastReportMs' "
          "is how a caller tells that from a stall the watchdog missed. `--watchdog-stall=N` does the same "
          "thing for a whole launch. A DEVELOPMENT-BUILD feature: it fails in a release build.",
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
                         "compiledThisRun, loadedThisRun, expectedShaders, lastSaved, "
                         "passCacheEntries, passCacheCapacity, renderableCacheEntries, "
                         "renderableCacheCapacity}",
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
          "itself is enabled. "
          "passCacheEntries and renderableCacheEntries are the LIVE SIZES of the two caches "
          "Ogre's 32-bit shader hash addresses — the pass property sets and the material/mesh "
          "property sets this process has produced so far — reported for the fullest Hlms, with "
          "the capacity each field can address beside them. Neither cache is ever evicted, so "
          "these only rise; a session whose passCacheEntries climbs without settling has a pass "
          "property carrying a unique name (a render target's, a probe's), which is exactly how "
          "the 2026-09-14 editor crash happened while the field was eight bits wide.",
          Needs::Engine },
        { "clearShaderCache", "app.clearShaderCache() -> bool",
          "Deletes every cached shader artifact. The running session is unaffected (its shaders are "
          "already in memory); the NEXT launch is cold. Our r.InvalidateCachedShaders — the same "
          "thing --clear-shader-cache does before the engine starts.",
          Needs::Engine },
        { "saveShaderCache", "app.saveShaderCache() -> bool",
          "Serializes the shader cache now instead of waiting for the burst-settle watchdog or a "
          "clean quit, and hands the FILE write to the engine's writer thread — the fsync that "
          "makes it durable waits behind whatever else the disk is flushing, and no UI thread may "
          "wait for that. True means 'serialized and handed over'; app.flushShaderCache() waits "
          "for the bytes to land. A no-op returning true when nothing new has been compiled. "
          "Mostly for tests: the app saves on its own.",
          Needs::Engine },
        { "flushShaderCache", "app.flushShaderCache(budgetMs?) -> bool",
          "Waits for the write app.saveShaderCache() handed off (default 20 s). True when the "
          "writer is idle — nothing in flight, or it finished; false when the budget ran out and "
          "the write is still going. For a test that wants to read the files back; the app itself "
          "waits at quit.",
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
        { "frameStats", "app.frameStats({reset}) -> {running, intervalMs, ticks, rendered, drawing, fpsDrawn, "
                        "slowFramesLastMinute, workMs, worstMs, slowFrames, enabledViews}",
          "What the ONE render loop (EngineRenderDriver) has been doing. `ticks` counts timer fires and "
          "`rendered` the ticks that actually called the engine, both cumulative; the ticks that had no "
          "enabled View to draw — sitting on a page with no viewport — are `ticks - rendered`, and the "
          "counter that used to report them (`skipped`) is GONE (owner review 2026-09-18): it was a "
          "lifetime total climbing at ~62 a second, shown on the F3 readout where it read as dropped "
          "frames. The LIVE answer to the same question is `drawing`, the state of the last counted "
          "tick. `fpsDrawn` is frames ACTUALLY DRAWN in the last second — a rolling window, and it "
          "counts frames drawn outside this loop too (a script's editor.frame, the VR pump), because "
          "what it answers is whether the picture is moving. `slowFramesLastMinute` is the hitch count "
          "over a rolling MINUTE, the number the readout shows; `slowFrames` is still the cumulative "
          "one for session logs. `enabledViews` "
          "is the engine's live answer to the same question the loop asks each tick. Note the scripted "
          "stepping verb editor.frame(n) bypasses the driver entirely, so it moves none of these. "
          "`workMs` is THE HONEST PERFORMANCE NUMBER on this architecture and the reason to prefer it "
          "over any FPS reading: the loop is a TIMER (paced from the display since the perf wave — see "
          "app.pacing), so a healthy editor reports whatever that timer allows whatever the scene "
          "costs, and only starts dropping once the budget is already blown. workMs "
          "is how long the frame's work actually took, averaged over the last ~60 rendered ticks; "
          "`worstMs` is the worst single tick since startup and `slowFrames` counts the ticks that "
          "crossed the 100 ms hitch threshold (the ones that also log `[open-profile] slow frame`). "
          "Read app.renderStats() beside this for the renderer's own view of the same frames. `{reset:true}` reads the numbers and THEN forgets the two HIGH-WATER marks — `worstMs` and `slowFrames` — leaving the rolling averages, the tick counts and the windows alone. A running maximum answers \"has this process ever hitched\", which is not what a caller measuring ONE open or ONE create is asking: a boot's own compile frame (2 s on a cold cache) would swamp every reading taken after it for the life of the session. Read first, then forget — exactly app.openStats's rule.",
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
        { "renderStats", "app.renderStats() -> {sceneTriangles, submittedTriangles, draws, perPass:[{name, triangles, draws}], metricsRecording, fps, frameMs, lastMs, p95Ms, p99Ms, bestMs, worstMs, batches, vertices, instances, incompletePsoRequests, forwardPlusLights, forwardPlusBudget, forwardPlusOverBudget, resourceAdvances}",
          "What the RENDERER measured, straight off the engine boundary — the numbers behind the F3 "
          "stats overlay, and the read-back answer for an agent that wants to know what a frame costs "
          "(a screenshot cannot carry them; the overlay is deliberately absent from offscreen renders). "
          "TWO TRIANGLE NUMBERS, and the difference between them is the point (owner review "
          "2026-09-18). `sceneTriangles` is WHAT IS IN THE SCENE: the authored (LOD 0) triangles of "
          "every effectively-visible mesh node, counted ONCE each on the DOCUMENT — no editor helpers "
          "(the grid, light icons and their wires, the sun disc, the horizon plane, gizmos, outlines "
          "live in no document), no extra passes, no HUD, and never computed by subtracting anything "
          "from the GPU figure. A new world with the default ground reads 2,178; an Empty scene reads "
          "0; it does not move when the camera does, which is why it is the authored level and not the "
          "LOD actually drawn. `submittedTriangles` is the other question — what the renderer handed "
          "the GPU last frame, EVERY pass included (the same geometry drawn again for the SSR depth "
          "pre-pass, each shadow cascade, probe captures, one full-screen quad per post step) — and it "
          "is the number the readout used to show unlabelled as \"triangles\". `perPass` breaks that "
          "total down per compositor pass, and it is filled only while the render monitor is capturing "
          "(Ctrl+F4 / perf.start): the per-pass counters cost clock reads and listeners on every "
          "workspace, so nothing pays for them when nobody is looking, and the list is empty "
          "otherwise. "
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
          "non-zero value means somebody turned it on. "
          "`resourceAdvances` counts the times the renderer's RESOURCE bookkeeping was advanced "
          "without drawing anything (Engine::advanceResources) — the texture worker's command "
          "buffer, the staging recycle and the buffer manager's frame counter, which a frame "
          "normally turns. It is monotonic and only differences mean anything; it rises during a "
          "threaded project open, whose install slices advance it themselves so that the open never "
          "depends on a frame the event loop may never let render.",
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
        { "textureStreaming", "app.textureStreaming() -> {multiLoadThreads, doneStreaming, loadRequests, metadataCacheEntries, channelCacheEntries, waitTimeouts, waitWorstMs, waitBudgetMs, waitAdvances}",
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
          "overrides it). `waitAdvances` counts the times a drain advanced the renderer's "
          "resource bookkeeping (the staging/semaphore/delayed-block retire, and a COMMIT "
          "every OTHER advance — a bare VaoManager::_update outside a frame arms on the first "
          "call and commits on the second): the drain polls every millisecond but advances on "
          "a 16 ms cadence, so this rises about once per frame's worth of waiting and not "
          "once per poll "
          "(JAH_TEXTURE_DRAIN_ADVANCE_MS overrides the cadence for measurement; 0 = every "
          "poll, which is what it did before DRAIN-1). All of them are plain members — free "
          "to ask.",
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
        { "resetLibrary", "app.resetLibrary({restart}) -> {ok, removed: {objects, sidecars, projects, thumbnails, staging}, restarted}",
          "RESET THE LIBRARY TO A FIRST LAUNCH (owner review R10.2) — the gesture behind "
          "Preferences > World > Clear Database, and everything that button never did. It closes "
          "the open project WITHOUT SAVING, drops every catalog table and creates them again, "
          "removes the CONTENTS of the asset store (objects, sidecars, derived caches, the store's "
          "identity and its abandoned staging temps — never the store ROOT directory, which the "
          "user may have chosen), removes every project FOLDER by the location its own row records "
          "(a project filed on another drive included) plus anything left under the projects root, "
          "drops the thumbnail caches, and then runs the fresh-install bootstrap: a new store "
          "identity, and the same first-run preset seed a launch runs — which a DRIVEN session (a "
          "suite, a script, an MCP client) does not get at launch and does not get here either "
          "(`materials.seedPresets()` is that seed on demand). What it does NOT touch: the "
          "settings file (preferences are not library content), the shader cache, the logs, and "
          "anything outside the data root. `removed` counts what WAS there, measured before the "
          "first byte went. It REFUSES while an import, the preset seed or a thumbnail rebuild is "
          "running — each of them would otherwise finish into a library that no longer exists — "
          "and while the asset store is OFFLINE (an unmounted drive: wiping the catalog would "
          "orphan the whole store on it). A refusal answers an EMPTY map with the reason in "
          "app.lastError(). `restart: true` spawns this executable again with THIS run's "
          "arguments and working directory and quits once the spawn succeeded (`restarted` says "
          "whether it did); it is REFUSED in a driven session — a script, a suite, an MCP client, "
          "the rig — because the respawn would carry `--script`/`--headless` with it and each "
          "child would reset the library and spawn another one. Without `restart` the process "
          "carries on with an empty library, which is what a script wants, and the windows that "
          "were already listing rows keep their stale lists until something refreshes them. What "
          "it removes in the store is the store's OWN layout — objects, sidecars, derived caches, "
          "store.json, the staging temps and the legacy per-guid folders — and nothing else in "
          "that directory, because the store root is a path the user chose and may hold their own "
          "files; the same rule under the projects root, where only folders named like a guid are "
          "this app's. Calling it twice is a no-op with zeroes in `removed`.",
          Needs::Document },
        { "notices", "app.notices({id}) -> [{id, name, role, homepage, licence, path, file, present, vendored, textLength}]",
          "THE THIRD-PARTY NOTICES THIS BINARY OWES — every vendored component that ships inside "
          "the executable, with what it does for Jahshaka and the LICENCE TEXT read out of its own "
          "vendored tree at build time (app/notices.json is the manifest; nothing is retyped into "
          "this repository, so a licence in the binary cannot drift from the one in the tree). "
          "Called with no argument it lists them in the manifest's order without the texts, which "
          "are kilobytes each; `{id: \"assimp\"}` answers the one component AND its full `text`. "
          "`role` is what the component does for us in one line, `licence` its short name (the "
          "TEXT is the authority), `path` the vendored directory it was read from and `present` "
          "whether THIS build carries it — computed when the build was configured, from whether "
          "the component's notice was actually there and (for the crash reporter) from "
          "DISABLE_BREAKPAD, so a component declared but not compiled in says so instead of "
          "disappearing. `vendored` is false for the three whose source is NOT in this tree — Qt, "
          "which is linked dynamically, and the Vulkan loader and MoltenVK, which the macOS "
          "bundle redistributes: their licence texts live in app/notices/ with their provenance "
          "recorded, which is the one exception to reading a notice off the code it covers. This is the same data the "
          "About dialog's Third-party notices page shows, and the coverage of it is a test: "
          "source.notices_coverage fails on a vendored directory the manifest does not claim.",
          Needs::Document },
        { "mcpLogging", "app.mcpLogging({session, source}) -> {session, source, errorLog, sessionLog, sessionId}",
          "What the MCP surface writes down about itself, and where. The ERROR log is always on "
          "and holds failures only — a tool call that came back an error, a run_script's message "
          "and failing line, a timeout, a refused request — never the script TEXT and never an "
          "argument's value, though a JavaScript error message quotes identifiers and carries "
          "whatever the script threw. It is bounded (1 MiB plus one previous generation). "
          "`session` turns "
          "on the opt-in RESEARCH record: one JSON line per tool call naming the tool, the "
          "argument keys with their sizes, the registry verbs a script actually called, the "
          "duration and the outcome. `source` additionally records the script text, and is "
          "meaningless without `session`. Both are OFF by default, both persist, and nothing is "
          "ever sent anywhere — the files stay under the data root's logs/ folder. Called with no "
          "arguments it only reports.",
          Needs::Document },
        { "lastError", "app.lastError() -> string | null",
          "Why the last verb answered falsy. Verbs REFUSE by returning their documented falsy "
          "value (false / 0 / null) rather than throwing — an exception would abort the whole "
          "script over an answer it asked for — and the reason is recorded here. Thrown "
          "precondition errors land here too. Null when nothing has failed yet; never cleared, "
          "so read it right after the call you are diagnosing.",
          Needs::Document },
        { "scriptPolicy", "app.scriptPolicy(mode='') -> {mode, applies, running, runPolicy}",
          "LIVE SCRIPT FEEDBACK — whether the viewport keeps drawing while a script runs. The "
          "JavaScript runs on its own thread and every verb hops to the UI thread, so between two "
          "verbs the app is free: in 'live' the render loop ticks and you watch the script build "
          "the scene (and the app answers, and Stop works); in 'off' the loop skips its ticks for "
          "the whole run, so the picture holds still and nothing advances between verbs — which is "
          "what a deterministic script wants and what every test uses. `editor.frame(n, dt)` "
          "renders its n frames either way; it is a verb. The setting applies to the CONSOLE and "
          "to MCP run_script ('applies'); --script and --headless are always 'off'. Called with no "
          "argument it only reports. It persists (Preferences > Scripting). While a run is in "
          "flight — under EITHER policy — the editor is NON-EDITABLE BUT FULLY NAVIGABLE: hand "
          "edits are refused for the run's duration and the camera, panels, tabs and selection go "
          "on working (editor.editGate reports it). `runPolicy` is the policy of the run asking — which is what the "
          "setting gave THIS run, and is 'off' for a --script run unless it was started with "
          "--script-live.",
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
        { "dialog", "app.dialog(name, openOrOptions=true) -> {name, open, window, title, x, y, width, height}",
          "Opens (or, with false, closes) one of the app's dialogs by name — see app.dialogs. "
          "INSPECTION ONLY for most of them: shown with show(), never exec() (a verb cannot wait "
          "inside exec()), so there is no result to consume — a dialog whose production caller "
          "reads its result after exec() (newProject, renameProject, getName) shows an accept "
          "button that does NOTHING here. A dialog that sets its own window modality (Preferences "
          "is application-modal) is still modal. For the theme walk (a dialog built on demand "
          "exists only while it is open) and for rigs that photograph the UI. Returns {} for an "
          "unknown name. "
          "THE SECOND ARGUMENT may be an OBJECT instead of a bool, carrying {open} plus that "
          "dialog's own keys; only 'importSettings' has any: {guid} opens the import-settings "
          "dialog on a library asset, pre-filled from assets.importSettings; {settings} writes "
          "the record's fields into it; {accept} presses its OK button, which in that mode runs "
          "assets.reimport — the very path the user's click takes. The answer then also carries "
          "`settings` (the record the dialog holds) and `accepted`.",
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

QVariantMap AppApi::openStats(const QVariantMap &options)
{
    const iris::ParseCensus::Counts parses = iris::ParseCensus::snapshot();
    QVariantMap out;
    out.insert(QStringLiteral("uiThreadParses"), parses.mainThreadParses);
    out.insert(QStringLiteral("uiThreadParseMs"), parses.mainThreadMs);
    out.insert(QStringLiteral("uiThreadResourceParses"), parses.mainThreadResourceParses);
    out.insert(QStringLiteral("uiThreadResourceParseMs"), parses.mainThreadResourceMs);
    out.insert(QStringLiteral("workerParses"), parses.workerParses);
    out.insert(QStringLiteral("workerParseMs"), parses.workerMs);
    out.insert(QStringLiteral("lastUiThreadParse"), parses.lastMainThreadPath);
    out.insert(QStringLiteral("bakeHits"), parses.bakeHits);
    out.insert(QStringLiteral("bakeMisses"), parses.bakeMisses);
    // THE OPEN'S OWN DRIVE (lane OPEN-FRAMES-1). NOT reset with the parse
    // census: these count the window's life, and a caller measuring one open
    // subtracts. A session with no window reports zeros.
    out.insert(QStringLiteral("sliceBoundaries"),
               host.mainWindow ? host.mainWindow->openSliceBoundaries() : 0u);
    out.insert(QStringLiteral("sliceBoundaryFrames"),
               host.mainWindow ? host.mainWindow->openSliceBoundaryFrames() : 0u);
    // Read FIRST, then zero: a caller measuring one open wants the numbers of
    // the window it just closed, not an empty map.
    if (options.value(QStringLiteral("reset")).toBool()) iris::ParseCensus::reset();
    return out;
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

QVariantMap AppApi::watchdog(int stallMs)
{
    if (!MainThreadWatchdog::isSupported()) {
        fail("app.watchdog: development builds only");
        return MainThreadWatchdog::stats();
    }
    if (!MainThreadWatchdog::setStallMs(stallMs))
        fail(QStringLiteral("app.watchdog: the stall threshold must be at least %1 ms "
                            "(the watchdog's own poll interval)")
                 .arg(MainThreadWatchdog::kMinStallMs));
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
    m["passCacheEntries"]        = s.passCacheEntries;
    m["passCacheCapacity"]       = s.passCacheCapacity;
    m["renderableCacheEntries"]  = s.renderableCacheEntries;
    m["renderableCacheCapacity"] = s.renderableCacheCapacity;
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

bool AppApi::flushShaderCache(int budgetMs)
{
    auto engine = EngineHost::instance().engine();
    if (!engine) return fail("app.flushShaderCache: the engine is not running");
    return engine->flushShaderCache(unsigned(qMax(0, budgetMs)));
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
    // Deferred: let the calling script (and its undo macro) finish first. It
    // has to go through the host's afterRun hook — a plain queued call is
    // delivered BETWEEN TWO VERBS now that the script runs off the UI thread,
    // which would close the window (and the engine, and this module) underneath
    // the run that asked for it.
    auto close = [w = host.mainWindow]() { w->close(); };
    if (host.afterRun) host.afterRun(close);
    else QMetaObject::invokeMethod(host.mainWindow, close, Qt::QueuedConnection);
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
    // never claim a switch the window didn't make. AND SAY WHY (SMOKE-FIX-1):
    // the window records the reason it bounced, so a script's refusal carries
    // the same sentence the user's toast does instead of a bare "refused".
    if (host.mainWindow->getWindowSpace() != space) {
        const QString why = host.mainWindow->lastSpaceRefusal();
        return fail(why.isEmpty()
                        ? QStringLiteral("app.space: the window refused to switch to '%1'").arg(s)
                        : QStringLiteral("app.space: the window refused to switch to '%1' — %2")
                              .arg(s, why));
    }
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

QVariantList AppApi::notices(const QVariant &which)
{
    // THE MANIFEST IS THE ANSWER (app/notices.h): this verb adds no list of its
    // own, no ordering of its own and no text of its own — it serves what the
    // build read out of the vendored trees. `id` selects one component and is
    // the only form that carries a `text`, because ten licences are ~100 KB of
    // JSON and a list is a list.
    const QVariantMap options = scriptmod::normalizeJs(which).toMap();
    const QString id = options.value(QStringLiteral("id")).toString().trimmed();
    if (!options.isEmpty()) {
        static const QStringList known = { "id" };
        for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
            if (known.contains(it.key())) continue;
            fail(QStringLiteral("app.notices: unknown key '%1' (known: id)").arg(it.key()));
            return QVariantList();
        }
    }
    QVariantList out;
    bool found = false;
    for (const notices::Entry &e : notices::entries()) {
        if (!id.isEmpty() && e.id != id) continue;
        found = true;
        const QString body = notices::text(e.id);
        QVariantMap row;
        row["id"] = e.id;
        row["name"] = e.name;
        row["role"] = e.role;
        row["homepage"] = e.homepage;
        row["licence"] = e.licence;
        row["path"] = e.path;
        row["file"] = e.file;
        row["present"] = e.present;
        // IS ITS SOURCE IN THIS TREE? False for the three components that are
        // not vendored (Qt, the Vulkan loader, MoltenVK) and whose notices
        // therefore live in app/notices/ with their provenance.
        row["vendored"] = e.vendored;
        row["textLength"] = body.size();
        if (!id.isEmpty()) row["text"] = body;
        out.append(row);
    }
    if (!id.isEmpty() && !found) {
        QStringList ids;
        for (const notices::Entry &e : notices::entries()) ids << e.id;
        fail(QStringLiteral("app.notices: unknown component '%1' (%2)")
                 .arg(id, ids.join(QStringLiteral(", "))));
        return QVariantList();
    }
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

// RESET THE LIBRARY (owner review R10.2). The verb is the THIN half on
// purpose: it closes the open project through the shell and — when asked —
// restarts the process, because those are the two things a service may not do.
// Everything that touches rows and bytes is services/libraryreset.h, which is
// what lets the headless suite drive the whole of it and what lets the
// Preferences button be four lines that call this verb.
QVariantMap AppApi::resetLibrary(const QVariantMap &options)
{
    QVariantMap out;
    if (!host.db) { fail("app.resetLibrary: there is no library in this session"); return out; }

    const bool restart = options.value(QStringLiteral("restart"), false).toBool();
    if (restart && !host.mainWindow) {
        fail("app.resetLibrary: {restart: true} needs the application (this session has no window "
             "to bring back)");
        return out;
    }
    // A DRIVEN SESSION IS NEVER RESTARTED, and this one is a safety rule
    // rather than a convenience. The restart re-runs THIS process's arguments
    // — `--script x.js` and `--headless` included — so a script that asks for
    // it spawns a child running the same script, which resets the library and
    // spawns another: an unbounded chain of processes, each one wiping what
    // the last one made. (The earlier guard for this was `!host.mainWindow`,
    // and its premise was simply false: a --headless run builds a MainWindow
    // too — main.cpp does, it merely never shows it.) Stripping the one-shot
    // flags would be a second answer to "what is this process for"; the
    // refusal is the rule, and `app/firstrun.h` is the one predicate that
    // knows (a data root forced, --script/--headless, --dump-api-docs,
    // --mcp-port, the selftest or an offscreen QPA).
    if (restart && FirstRun::isDrivenSession()) {
        refuse("app.resetLibrary: a driven session — a script, a suite, an MCP client — is not "
               "restarted; reset without restart and let whoever started this process start it "
               "again");
        return out;
    }

    // THE REFUSAL, TAKEN HERE AS WELL AS IN THE SERVICE, so the caller is told
    // before anything closes: a library being written to — or one whose store
    // is on a drive that is not there — is not a library to delete.
    const QString why = libraryreset::refusalReason();
    if (!why.isEmpty()) {
        refuse(QStringLiteral("app.resetLibrary: %1").arg(why));
        return out;
    }

    // THE OPEN PROJECT GOES FIRST, AND IT IS DISCARDED. Every row it is made
    // of is about to be dropped, so saving it would write a scene into a
    // catalog that is on its way out. The undo macro is ended around the close
    // exactly as project.close does it — the history names a document that
    // will not exist.
    ProjectService *projects = host.services ? host.services->project : nullptr;
    if (projects && projects->isSceneOpen() && host.mainWindow) {
        host.endRunUndoMacro();
        host.mainWindow->closeProject();
        host.beginRunUndoMacro();
    }

    const QString projectsRoot =
        projects ? projects->projectsRoot()
                 : AppPaths::projectsRoot(SettingsManager::getDefaultManager()
                                              ->getValue("default_directory", QString()).toString(),
                                          Constants::PROJECT_FOLDER);
    const auto folderFor = [projects](const QString &guid) -> QString {
        return projects ? projects->projectFolderFor(guid) : QString();
    };

    // THE SEED IS THE CHILD'S JOB WHEN WE ARE RESTARTING (the service's
    // `seedPresets`): started here it would run in a process that is already
    // on its way out, and two seeders over one store is how a
    // content-addressed library still gets two rows for one picture.
    const libraryreset::Result result = libraryreset::reset(
        host.db, SettingsManager::getDefaultManager(), projectsRoot, folderFor,
        /*seedPresets*/ !restart);

    out.insert(QStringLiteral("ok"), result.ok);
    out.insert(QStringLiteral("removed"), result.removed.toMap());
    out.insert(QStringLiteral("restarted"), false);
    if (!result.ok) {
        // A PARTIAL RESET IS STILL REPORTED: the catalog may already be empty,
        // and a caller that got nothing back could not tell what happened.
        refuse(QStringLiteral("app.resetLibrary: %1").arg(result.error));
        return out;
    }

    if (!restart) return out;

    // THE RESTART. The old button quit and THEN spawned `arguments()[0]` —
    // whatever string the shell used to launch us, resolved against the NEW
    // process's working directory: it comes back when the app was started by a
    // path that still resolves from there (the owner's `./Jahshaka` does) and
    // silently does not when it was not. This spawns applicationFilePath() (an
    // absolute path, always) with this run's arguments and this run's working
    // directory, and quits only once the spawn reported a pid — so the answer
    // is the same wherever the app was launched from, and `restarted` is a
    // FACT rather than a hope.
    const QStringList args = QCoreApplication::arguments().mid(1);
    qint64 pid = 0;
    const bool spawned = QProcess::startDetached(QCoreApplication::applicationFilePath(), args,
                                                 QDir::currentPath(), &pid);
    out[QStringLiteral("restarted")] = spawned;
    if (!spawned) {
        refuse("app.resetLibrary: the library was reset but this executable could not be started "
               "again — close and reopen Jahshaka");
        return out;
    }
    // Deferred through the host's afterRun hook for the same reason app.quit()
    // is: a plain queued close is delivered BETWEEN TWO VERBS now that a script
    // runs off the UI thread, which would close the window underneath the run
    // that asked for it.
    auto close = [w = host.mainWindow]() { w->close(); };
    if (host.afterRun) host.afterRun(close);
    else QMetaObject::invokeMethod(host.mainWindow, close, Qt::QueuedConnection);
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

QVariantList AppApi::docks()
{
    if (!host.mainWindow) {
        fail("app.docks: this verb needs the editor window (a --script/--headless run has no "
             "pages)");
        return {};
    }
    return host.mainWindow->dockReport();
}

QVariantMap AppApi::mcpLogging(const QVariantMap &options)
{
    McpLog &log = McpLog::instance();
    // A write only where the caller asked for one: passing {source: true}
    // alone must not silently turn the session recording on as well.
    if (options.contains(QStringLiteral("session")))
        log.setSessionRecording(options.value(QStringLiteral("session")).toBool());
    if (options.contains(QStringLiteral("source")))
        log.setRecordScriptSource(options.value(QStringLiteral("source")).toBool());

    QVariantMap out;
    out["session"] = log.sessionRecording();
    out["source"] = log.recordScriptSource();
    out["errorLog"] = log.errorLogPath();
    const QString sessionLog = log.sessionLogPath();
    out["sessionLog"] = sessionLog.isEmpty() ? QVariant() : QVariant(sessionLog);
    out["sessionId"] = log.sessionId().isEmpty() ? QVariant() : QVariant(log.sessionId());
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

QVariantMap AppApi::scriptPolicy(const QString &mode)
{
    ScriptEngine *engine = host.mainWindow ? host.mainWindow->scripting() : nullptr;
    if (!engine) {
        refuse(QStringLiteral("app.scriptPolicy: this session has no script engine"));
        return {};
    }
    if (!mode.isEmpty()) {
        bool ok = false;
        const ScriptRunPolicy wanted = ScriptEngine::policyFromName(mode, ok);
        if (!ok) {
            fail(QStringLiteral("app.scriptPolicy: '%1' is not a mode — use 'live' or 'off'").arg(mode));
            return {};
        }
        engine->setInteractivePolicy(wanted);
        // Persisted like every other preference, so the next session opens the
        // way the user left it.
        if (SettingsManager *settings = SettingsManager::getDefaultManager())
            settings->setValue(QStringLiteral("script_feedback_live"),
                               wanted == ScriptRunPolicy::Live);
    }
    QVariantMap out;
    out["mode"] = ScriptEngine::policyName(engine->interactivePolicy());
    out["applies"] = QVariantList{ QStringLiteral("console"), QStringLiteral("mcp") };
    out["running"] = engine->isRunning();
    // THE RUN YOU ARE IN, which is not the same thing as the setting: a script
    // reading this is always inside a run, and its policy was fixed when the
    // run started (the console/MCP take `mode`, --script takes 'off' unless
    // --script-live said otherwise).
    out["runPolicy"] = ScriptEngine::policyName(engine->currentRunPolicy());
    return out;
}

QVariantMap AppApi::frameStats(const QVariantMap &options)
{
    // No engine guard, same reasoning as engineErrors: "the loop is not running"
    // is one of the answers this verb exists to give, so it must be readable
    // when there is no engine at all.
    QVariantMap out;
    EngineRenderDriver *driver = EngineHost::instance().driver();
    // READ FIRST, THEN FORGET (`{reset:true}`, lane OPEN-COVER-2b): a caller
    // measuring one open wants the numbers of the window it just closed, and
    // the same call arms the next window — exactly app.openStats's rule.
    const bool reset = options.value(QStringLiteral("reset")).toBool();
    const EngineRenderDriver::Stats s = driver ? driver->stats() : EngineRenderDriver::Stats{};
    out.insert("running", driver ? driver->isRunning() : false);
    out.insert("intervalMs", driver ? driver->intervalMs() : 0);
    out.insert("ticks", QVariant::fromValue(s.ticks));
    out.insert("rendered", QVariant::fromValue(s.rendered));
    // The live state and the two ROLLING numbers that replaced the lifetime
    // `skipped` counter (owner review 2026-09-18, answer Q3).
    out.insert("drawing", s.drawing);
    out.insert("fpsDrawn", s.fpsDrawn);
    out.insert("slowFramesLastMinute", s.slowFramesLastMinute);
    out.insert("workMs", s.workMs);
    out.insert("worstMs", s.worstMs);
    out.insert("slowFrames", QVariant::fromValue(s.slowFrames));
    auto engine = EngineHost::instance().engine();
    out.insert("enabledViews", engine ? engine->hasEnabledViews() : false);
    if (reset && driver) driver->resetWorst();
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
    // THE SCENE'S OWN TRIANGLES — the DOCUMENT's answer to "what is in my
    // scene", walked here and not derived from anything below it
    // (services/scenestats.h says exactly what it counts and why it is the
    // authored LOD level). No project open = an honest 0.
    const iris::ScenePtr doc = (host.services && host.services->sceneEdit)
                                   ? host.services->sceneEdit->scene()
                                   : iris::ScenePtr();
    out.insert("sceneTriangles",
               QVariant::fromValue(qulonglong(scenestats::sceneGeometry(doc).triangles)));
    out.insert("draws", QVariant::fromValue(qulonglong(s.draws)));
    out.insert("batches", QVariant::fromValue(qulonglong(s.batches)));
    // …and what the RENDERER submitted, across every pass. Renamed from
    // `triangles`, which is what the F3 row used to call it and what the owner
    // read as his scene's content (owner review 2026-09-18, R4a).
    out.insert("submittedTriangles", QVariant::fromValue(qulonglong(s.triangles)));
    // The per-pass breakdown of that total, from the render monitor's own pass
    // rows. Empty unless a capture is recording — see the verb's doc.
    out.insert("perPass", FrameMonitor::instance().lastFramePasses());
    out.insert("vertices", QVariant::fromValue(qulonglong(s.vertices)));
    out.insert("instances", QVariant::fromValue(qulonglong(s.instances)));
    out.insert("incompletePsoRequests", s.incompletePsoRequests);
    // The Forward+ census. `forwardPlusOverBudget` == 0 PROVES no light was
    // dropped from any cell; non-zero says a full cell would have dropped that
    // many. See RenderStats for why an exact drop count needs an Ogre patch.
    out.insert("forwardPlusLights", s.forwardPlusLights);
    out.insert("forwardPlusBudget", s.forwardPlusBudget);
    out.insert("forwardPlusOverBudget", s.forwardPlusOverBudget);
    // How many times the renderer's resource bookkeeping was advanced WITHOUT
    // a frame (Engine::advanceResources). Monotonic; only differences mean
    // anything. See the verb's doc.
    out.insert("resourceAdvances", QVariant::fromValue(qulonglong(s.resourceAdvances)));
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
    out.insert("waitAdvances", QVariant::fromValue(qulonglong(engine->textureWaitAdvances())));
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

QVariantMap AppApi::dialog(const QString &name, const QVariant &openOrOptions)
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

    // A BOOL is the old shape and still the common one; an OBJECT carries that
    // dialog's own keys (and `open`, should a caller want to close one by
    // object). Nothing else is accepted — a number or a string here is a
    // mistake, not an opinion about opening.
    bool open = true;
    QVariantMap options;
    const QVariant given = scriptmod::normalizeJs(openOrOptions);
    if (given.isValid() && given.typeId() == QMetaType::Bool) {
        open = given.toBool();
    } else if (given.typeId() == QMetaType::QVariantMap) {
        options = given.toMap();
        open = options.value(QStringLiteral("open"), true).toBool();
        options.remove(QStringLiteral("open"));
    } else if (given.isValid()) {
        fail(QStringLiteral("app.dialog: the second argument is true/false or an options object"));
        return out;
    }

    out["name"] = name;
    if (!open) {
        host.mainWindow->closeDialog(name);
        out["open"] = false;
        return out;
    }
    QVariantMap extra;
    QWidget *w = host.mainWindow->openDialog(name, options, &extra);
    // THE DIALOG'S OWN MESSAGE FIRST, whether or not it opened: a refusal with
    // a reason ("this asset has no import record") is worth more than "could
    // not be opened in this session".
    const QString dialogError = extra.take(QStringLiteral("error")).toString();
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) out[it.key()] = it.value();
    if (!dialogError.isEmpty()) {
        fail(QStringLiteral("app.dialog('%1'): %2").arg(name, dialogError));
        return out;
    }
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
