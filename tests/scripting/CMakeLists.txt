# Scripting engine tests (SCRIPTING_SPEC.md phase 0+).
#
# scripting.engine (archived: archive/tests/scripting) — unit test of the QJSEngine host: evaluation, error line
# numbers, console capture, undo-macro wrapping, registry metadata validation.
# Links ONLY the scripting core (no MainWindow, no engine): the core is designed
# to have zero Studio dependencies (ScriptHost carries std::function probes).
# Runs offscreen.
#
# The end-to-end --script suites (full app binary, engine up) live below as
# scripting.e2e.* — added in phase 1.
find_package(Qt6 REQUIRED COMPONENTS Widgets Qml)

# ---- end-to-end: the real app binary running --script (phase 1) ----
#
# HOME is redirected to a per-test scratch dir so the runs get a FRESH
# JahLibrary.db and projects folder and can never touch the user's live
# library. XAUTHORITY/DISPLAY are inherited from the session (the engine needs
# a reachable X display; XWayland on :0 in CI-like runs, lavapipe covers no-GPU).
#
# FRESH EVERY RUN, NOT ONLY THE FIRST (lane D6B-GATE-SHAPE; audit §9 H1): the row's
# command is `${JAH_FRESH_HOME} --warm <home> -- <app> --script …`, so the home is wiped
# before every run — library, store, projects, settings — while the shader caches stay
# warm (a cold cache is ~4 s a boot and its own measurement). The homes used to
# accumulate: 61 projects and a 220 MB store in e2e-home-full_surface, 1.1 GB in
# e2e-home-export_web, measured on the main tree.
# (add_script_e2e, the one-row-per-script helper, went with its last caller: every e2e script
# is a pool ARM since SUITE-POOL-1, and a row of its own is a hand-written add_test with its
# reason — TEST-TIER-1 deleted the dead function.)

# ---- THE RUN POLICY (SCRIPTING_LIVE_SPEC §3.1) --------------------------
#
# scripting.live — the script run with --script-live: the render loop DRAWS between the script's verbs (which the
# UI-thread engine could not do at all). Its "off" twin (the same script without the flag: it draws NOTHING between
# them) was archived by TEST-CUT-1 (archive/tests/scripting/).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-live_feedback_live)
file(MAKE_DIRECTORY ${_home})
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(scripting.live CLASS app TIER low NEEDS NONE TEST_WINDOW
         COMMAND ${JAH_FRESH_HOME} --warm ${_home} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_live_feedback.js --script-live
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.live PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 180)
jah_ship_row(scripting.live)   # THE SHIP SET (TEST-CUT-1)

# host.sync_getters — THE DOOR, COUNTED (ENGINE-UPGRADE-U3 stage 2): 600 editor frames — 300 driver ticks (--script-live,
# the owner's mode: one after each script frame) and 300 lockstep frames, an object moving and selected — make ZERO
# synchronous reads of the engine (engine.syncGetters: every getter that stays counts the host's
# calls; what the editor reads per frame is the published snapshot, what it set it keeps). A count, in frames.
set(_hsgHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-host_sync_getters)
file(MAKE_DIRECTORY ${_hsgHome}/run)
jah_gpu_row(host.sync_getters SHIP CLASS app TIER low NEEDS NONE FRAMES 630
         NEEDS_WHY "a count of the host's synchronous engine entries, the capture inside the window is a subject, its picture no claim"
         COMMAND ${JAH_FRESH_HOME} --warm ${_hsgHome} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/host_sync_getters.js --script-live
         WORKING_DIRECTORY ${_hsgHome}/run)
set_tests_properties(host.sync_getters PROPERTIES
    ENVIRONMENT "HOME=${_hsgHome};JAHSHAKA_DATA_ROOT=${_hsgHome}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 120)

# host.error_door (archived: archive/tests/host) — THE HOST'S ERROR DOOR NEVER TOUCHES THE FRAME'S SINK (ENGINE-UPGRADE-U3 decision 44, D1): 300
# FREE-RUNNING driver frames (JAH_FREE_RUNNING=1) with every scene message refused (engine.arm messages.refuse) and
# one posted per frame, the error pump draining every tick; every refusal reaches it through the snapshot. A count,
# in frames (the race it guards is TSan's to see: the 47 arm).
set(_hedHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-host_error_door)
file(MAKE_DIRECTORY ${_hedHome}/run)
jah_tsan_blocked(host.sync_getters )   # TSAN-GCC15-THREAD-REGISTRY under GCC and clang alike (U3 decision 49): the D1 proof is engine.error_door

# editor.drag_latency — A DRAG IN THE EDITOR IS ON SCREEN THE NEXT FRAME (ENGINE-UPGRADE-U3 stage 5's acceptance,
# decision 50): FREE-RUNNING (JAH_FREE_RUNNING=1), a translate drag through the real viewport path (editor.pointer on
# the gizmo's X arrow, 60 moves, the driver ticking between them): every move written by the gizmo, every tick's record
# drawn by the next frame the render thread began (none merged into the next tick's), zero synchronous engine reads.
set(_edlHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-editor_drag_latency)
file(MAKE_DIRECTORY ${_edlHome}/run)
jah_gpu_row(editor.drag_latency SHIP CLASS app TIER low NEEDS NONE FRAMES 260
         COMMAND ${JAH_FRESH_HOME} --warm ${_edlHome} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/editor_drag_latency.js --script-live
         WORKING_DIRECTORY ${_edlHome}/run)
set_tests_properties(editor.drag_latency PROPERTIES
    ENVIRONMENT "HOME=${_edlHome};JAHSHAKA_DATA_ROOT=${_edlHome}/.local/share/Jahshaka;JAH_FREE_RUNNING=1"
    LABELS "scripting"
    TIMEOUT 120)

jah_pool_arm(editor build_scene ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_build_scene.js 8)
# EDITOR_MULTISELECT_SPEC §5: the selection SET's semantics (primary, Ctrl
# toggle with topmost promotion, the owner's Shift range, the World root rule)
# and its EDITS (D5-reduced delete/duplicate/copy/paste, placement, fresh
# guids). Document verbs only -> --headless: no engine, no display.

jah_pool_arm(editor undo_macro ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_undo_macro.js 7)
# SCENEGRAPH_SPEC §3 step 4: the v2 scene format's fragment pair
# (node.serialize/node.deserialize) and the persisted SCENE_STATIC override,
# driven through the verbs against the real database and a real save/reopen.
jah_pool_arm(editor fragments ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_fragments.js 3)

# material.live_roundtrip.author / .import / .restore — LIVE-PERSIST-1: ANY live graph material is a complete, persistent
# bundle. A cut-out (baked) + live-emissive (piece) graph material survives save -> reopen with its piece cache DELETED
# -> export -> import into an EMPTY library, one process a stage with scratch HOME/XDG_CACHE_HOME; the frames agree and
# the cut-out is visible (a solid control frame); deleted and restored, the graph comes back live.
# REBUILT TO ITS LAW (TEST-CUT-1; CLAUDE.md Testing: every row at most 60 s): the law is FIVE processes — a process
# boundary is what proves the bundle carries what a session held in memory — and each stage's frames are already a
# settle predicate (frame(): 30-frame steps until two probe readings agree), so nothing is cut: the one row (85 s,
# 2,342 frames) runs as a FIXTURE CHAIN of three, in its old order on its old data — author (author + reopen and its
# export) -> import (into an empty library, the four frames compared) -> restore (drop, the catalog put back, restore).
# Measured once each (2026-10-20, solo, the card lock held): author 39.3 s / 1,135 frames, import 21.2 s / 1,084,
# restore 12.5 s / 92. TIMEOUT max(6 x the solo run, the 120 s floor), rounded up.
foreach(_row author import restore)
    jah_gpu_row(material.live_roundtrip.${_row} SHIP CLASS app TIER epic NEEDS photon
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/material_live_roundtrip.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                     ${CMAKE_CURRENT_SOURCE_DIR}/scripts/material_live_roundtrip.js.in
                     ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cutout_checker.png
                     ${CMAKE_CURRENT_BINARY_DIR}/material-live-roundtrip ${_row}
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()
set_tests_properties(material.live_roundtrip.author PROPERTIES LABELS "scripting" TIMEOUT 240
    FIXTURES_SETUP material_live_roundtrip_author)
set_tests_properties(material.live_roundtrip.import PROPERTIES LABELS "scripting" TIMEOUT 130
    FIXTURES_SETUP material_live_roundtrip_import
    FIXTURES_REQUIRED material_live_roundtrip_author DEPENDS material.live_roundtrip.author)
set_tests_properties(material.live_roundtrip.restore PROPERTIES LABELS "scripting" TIMEOUT 120
    FIXTURES_REQUIRED "material_live_roundtrip_author;material_live_roundtrip_import"
    DEPENDS "material.live_roundtrip.author;material.live_roundtrip.import")
# scripting.e2e.editor_controls.timing — the per-selection 11 ms frame bar, stage-close (lane
# D6B-GATE-SHAPE; audit §5 F17 FIX-TIME): a generated copy of the script with the bar armed
# (jah_timing_script), its own home WIPED IN THE ROW (fresh_home.sh --warm, the shared shader cache linked in: the
# home survived every run before TOOLING-7 — source.testing_rules R12), quiet box, the GPU-timing lock.
set(_ect_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-editor_controls-timing)
file(MAKE_DIRECTORY ${_ect_home}/run)
jah_timing_script(${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_editor_controls.js
                  ${CMAKE_CURRENT_BINARY_DIR}/e2e_editor_controls.timing.js)
jah_gpu_exclusive_test(NAME scripting.e2e.editor_controls.timing SHIP CLASS app TIER document   RUN_TIMEOUT 180
    COMMAND sh "${JAH_FRESH_HOME}" --warm "${_ect_home}" --
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_editor_controls.timing.js
    WORKING_DIRECTORY ${_ect_home}/run)
set_tests_properties(scripting.e2e.editor_controls.timing PROPERTIES
    ENVIRONMENT "HOME=${_ect_home};JAHSHAKA_DATA_ROOT=${_ect_home}/.local/share/Jahshaka"
    LABELS "scripting;measure")

# gi_verbs.offscreen_contract — D4-PHOTON-TIERS §2.2: the declared offscreen picture table
# (world.tierTable().offscreen) held against the renderer on Showroom 2 at High — the scene-grade
# screenshot runs the tier's gather and traced reflections, the plain grade (declared lower)
# runs neither. A pixel claim, in the Epic pool.
set(JAH_OFFSCREEN_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
# gi_verbs.pcc_budget — PCC-BUDGET-1: the probe grid under the tier's WHOLE-grid VRAM budget on
# Showroom 2 with the rays off (Epic keeps the measured 32, High cuts to its budget's 19), the
# total asserted by the renderer's arithmetic and by the texture manager.
set(JAH_PCC_BUDGET_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
# gi_verbs.planar_records — PLANAR-RECORDS-1: the hit decode's list on the Mirror Room at Epic
# and on a planar mirror floor under 30 movers — no record dropped, the counts printed.
set(JAH_PLANAR_RECORDS_MIRROR "${CMAKE_SOURCE_DIR}/scenes/Mirror Room.zip")

set(_texmiss_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-texture_missing)
file(MAKE_DIRECTORY ${_texmiss_home}/run)
set(_arriving_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-arriving_line)
file(MAKE_DIRECTORY ${_arriving_home}/run)
# scripting.e2e.pump_defer (archived: archive/tests/scripting) — a UI-thread operation's pump delivers no other work
# (STUDIO-D1 fix round; services/uithreadwait.h). Arm 1, in-process: an MCP
# request landing in a real operation's pump with no MCP dispatch running is
# deferred, then answered. Arm 2, the app: two MCP clients, B asking while A's
# imports run is answered after A, and A is never held by a nested
# waitForScriptIdle (its 30 s ceiling).
# Arm 1 (DBUILD-FIX-1) runs the REAL McpServer in-process inside a REAL UiThreadWait operation (the
# deferral door no MCP client can reach: mBusy orders two clients whatever the pump does), so the
# server and its log are linked here; its tools are stubbed in the test (the request is a ping).
find_package(Qt6 REQUIRED COMPONENTS HttpServer Concurrent Widgets)
set(_pumpdefer_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-pump_defer)
file(MAKE_DIRECTORY ${_pumpdefer_home}/run)
# THE ENVIRONMENT LANDS AS ONE SET (PHOTON-SKY-TRANSIENT-1): the first frame
# after a sky power change, a sun move, a sky type change either way and the
# cloud layer on/off equals the frame after it within 2/255 — the change frame
# used to sample a reflection cube nothing had written, with the SH one host
# push behind it. Engine UP (a picture); the home is WIPED first, so no cached
# shader or face stands between a change and its first frame. ~10 s solo.
# budget 70 s: the measurement (TESTING-SPEED-B2, the lead 2026-10-12); the cold path is the subject — the boot lane lowers it.
set(_sstHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-sky_swap_transient)
jah_fresh_home_fixture(scripting.e2e.sky_swap_transient.fresh_home skySwapTransientFreshHome "${_sstHome}" LABELS "scripting")
jah_gpu_row(scripting.e2e.sky_swap_transient SHIP CLASS app TIER epic NEEDS photon
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_sky_swap_transient.js
         WORKING_DIRECTORY ${_sstHome}/run)
set_tests_properties(scripting.e2e.sky_swap_transient PROPERTIES
    ENVIRONMENT "HOME=${_sstHome};JAHSHAKA_DATA_ROOT=${_sstHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED skySwapTransientFreshHome
    LABELS "scripting" TIMEOUT 240)

# phase 2: full surface (assets/materials/graph/world). The script needs real
# repo paths (a mesh to import, a shipped .effect) — configured in.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})

# AI_SURFACE_PROGRAM_SPEC lane D #14: node.physics / node.physicsInfo — the last
# UI-with-no-verb domain. Every scalar and both enums round-trip, every refusal
# is loud, the values survive a reopen, AND the settings reach Bullet (two
# identical spheres, one made dynamic and one static by the verb, simulated in
# the same world — one falls, one does not). ENGINE UP, not --headless: the
# physics stepper lives in the engine viewport's syncFrame and the headless
# stand-in's startPhysicsSimulation is an empty override.
jah_pool_arm(editor physics ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_physics.js 7)

# Video assets (ASSET_MEDIA_SPEC §1): importFile(mp4) → Video row + frame-grab
# thumbnail + probed metadata block. Qt Multimedia's ffmpeg backend decodes
# with no display (verified under QT_QPA_PLATFORM=offscreen), so this runs
# --headless like the drawers/metadata suites.
configure_file(scripts/e2e_video.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_video.js @ONLY)

# Relocatable store root (ASSET_PIPELINE_SPEC §3.1.1/§3.1.2 phase 1):
# storeRoot/setStoreRoot/storeStatus round-trip — Move Store copy-verify-flip
# with the old tree retained, offline mode (catalog usable, byte actions fail
# cleanly), Use Existing sanity check + force, reset to default. Document
# verbs only -> --headless. The script resets assets/storeRoot on exit
# (jahsettings.ini beside the binary is shared across this build dir's
# suites).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-store_root)
set(STORE_MOVE_TARGET ${_home}/store-moved)
set(STORE_EMPTY_TARGET ${_home}/store-empty)
set(STAMP e2e)

# IMAGE_PLANE_SPEC option A: scene.addImagePlane — aspect-correct plane
# (long side 1 m), camera-facing at creation, image as baseColorMap
# (roughness 1 / metallic 0, alpha images blend), dependency row. Engine up
# (no --headless): the camera-facing assertion reads editor.camera(), an
# engine verb. Fixture paths configured in.
configure_file(scripts/e2e_image_plane.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_plane.js @ONLY)
jah_pool_arm(editor image_plane ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_plane.js 1)

# Plan item 15c: SHIPPED ASSETS THROUGH THE CAS. The default ground's tile, the
# default particle image, a material preset's maps and a sky preset's faces are
# pinned library textures saved by guid (one row per library), and nothing
# copies a shipped file into a project folder or the working directory. The
# wrapper runs the document half (--headless), the rigged-model arm (a placed
# rig keeps its clips after its source file is gone — the one real consumer the
# deleted by-name lookup had) and then sweeps the disk.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-shipped_assets)
file(MAKE_DIRECTORY ${_home}/run)

# scripting.e2e.clip_ref — a skeletal clip is referenced by its ASSET GUID
# (CLIP-REF-1): an avatar with embedded clips + a loaded clip and a placed model
# with its own clip keep every clip, with zero issues, across save -> switch ->
# reopen (twice) with the projects outside the data root, and again from a
# COPY of the data root opened with --data-root. Two --headless processes.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-clip_ref)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.clip_ref
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/clip_ref.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_SOURCE_DIR}/tests/avatar/fixtures/rig2.glb
                 ${CMAKE_SOURCE_DIR}/tests/avatar/fixtures/rig2_walk_anim.glb
                 ${CMAKE_SOURCE_DIR}/tests/skeletal/fixtures/mixamo_tpose.fbx
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_clip_ref.js
         WORKING_DIRECTORY ${_home}/run)
jah_ship_row(scripting.e2e.clip_ref)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(scripting.e2e.clip_ref PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 300)   # two processes: 98 s measured on a cold shader cache, 8 s warm

# scripting.e2e.tex_ref — a texture is referenced by its ASSET GUID (TEX-REF-1):
# a cube's maps bound from a file and by guid, an imported textured model and an
# emitter's image keep their assets, with zero issues, across save -> switch ->
# reopen (twice) with the projects outside the data root; again from a COPY of the
# data root (--data-root); and a THIRD copy with one map's object deleted raises
# texture.missing and keeps writing the same guid. Three --headless processes.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-tex_ref)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.tex_ref
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tex_ref.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/implane_wide.png
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/implane_tall.png
                 ${CMAKE_SOURCE_DIR}/tests/importer/fixtures/textured_pbr_quad.glb
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_tex_ref.js
         WORKING_DIRECTORY ${_home}/run)
jah_ship_row(scripting.e2e.tex_ref)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(scripting.e2e.tex_ref PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 300)   # three processes, like clip_ref's two (98 s cold, 8 s warm)

# scripting.e2e.perf_capture_frames — THE CAPTURE WINDOW IS COUNTED IN FRAMES
# (PERF-CAPTURE-SCRIPT-1, lane FENCE-1). Two halves like perf.capture_bundle (archived: archive/tests/perf):
# the app records, the shell half reads frames.jsonl and machine.json, because
# "exactly 60 records" is a line count and not a counter the app reports about
# itself. JAHSHAKA_PERF_ROOT: `out` is confined to the capture root, so the
# suite says where its own root is.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-perf_capture_frames)
file(MAKE_DIRECTORY ${_home}/run)
set(JAH_TEST_BUNDLE ${_home}/perf/bundle)
configure_file(scripts/e2e_perf_capture_frames.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_perf_capture_frames.js @ONLY)
jah_gpu_row(scripting.e2e.perf_capture_frames SHIP CLASS app TIER low NEEDS NONE NEEDS_WHY "the capture window is counted in frames, giStatus is a recorded field (no GI claim)"
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/perf_capture_frames.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_BINARY_DIR}/e2e_perf_capture_frames.js
                 ${JAH_TEST_BUNDLE}
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.perf_capture_frames PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka;JAHSHAKA_PERF_ROOT=${_home}/perf"
    LABELS "scripting"
    TIMEOUT 180)

# assets_import.bundle_export_{texture,material,pack,sky} — JAF-EXPORTS-1: the Assets tray's exports
# are the share file (assets.exportBundle, one entry or a pack); each kind's file re-imports into
# an emptied library with its members, and a cube wearing the landed material samples the
# fixture's own texel. One template, configured per kind. Engine up.
foreach(BUNDLE_KIND texture material pack sky)
    configure_file(scripts/e2e_bundle_export.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_bundle_export_${BUNDLE_KIND}.js @ONLY)
endforeach()

# The home is WIPED before every run: which ROW a picture answers depends on
# which rows already hold those bytes (the by-content dedup), so the cases are
# only meaningful on a library that has never seen them.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-assets_scope)
file(MAKE_DIRECTORY ${_home}/run)

# scripting.e2e.thumbnails_order (archived: archive/tests/scripting) — THE OWNER'S ORDER (THUMBS-1, 2026-09-18).
# A material thumbnail FIRST, a model import SECOND: the renderer named its
# offscreen View with a fixed string and a second instance could not create it,
# so every model imported after the session's first material/shader thumbnail
# stored a NULL image and said nothing ("View 'thumbs' already exists" in the
# owner's log, and a Qt null-pixmap warning where the tile should have been).
# Also pins every type's rebuild in one process (object, material, shader,
# AVATAR — which had no branch at all), the EDITOR TRAY's import (which never
# asked for a model thumbnail) and the bulk repair for rows already grey.
# Engine up (no --headless): every assertion is about a render.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-thumbnails_order)
# A VIRGIN LIBRARY: the run counts what a sweep finds and which rows a tray
# import added, and a home carried over from the last run answers both with
# yesterday's assets.
file(MAKE_DIRECTORY ${_home}/run)

# scripting.e2e.import_dialog — THE IMPORT DIALOG, DRIVEN
# (SPECS/IMPORT_DIALOG_SPEC.md §8/§9). ui.import_dialog (archived: archive/tests/ui) pins the record the
# widgets produce and scripting.e2e.import_settings pins the verbs; this pins
# the JOIN — the dialog's own OK button, on a real library asset, re-bakes it,
# the open scene follows at scale 1, and the record it writes is byte-identical
# to the one the verb writes. Needs the editor window (app.dialog) -> no
# --headless.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})

# THE LOOKS STACK (POST_LOOKS_SPEC.md §4.1): the catalogue, the six verbs, the
# one-instance-per-look rule, clamping, undo and the save/open round trip. The
# pixel half of the program is the looks.engine suite; these are all document
# verbs -> --headless.

# HARDWARE RAY TRACING IS A PROJECT ROW (owner, 2026-09-15; ledger §425):
# world.rayTracing("off"|"auto"|"on"), saved with the scene, one undo step per
# gesture, a loud refusal for anything else — and the SCENE ISSUE that is the
# whole difference between "on" and "auto" (on and auto render the identical
# picture; on TELLS YOU when the machine has no ray hardware).
#
# THE SAME SCRIPT TWICE, which is the only way to prove both answers on one GPU:
# `ray_row` as this machine is, and `ray_row_norays` under the --no-ray-query
# latch (JAHSHAKA_NO_RAY_QUERY=1), which builds the Vulkan device with none of
# the ray extensions asked for. The script branches on
# world.giStatus().rayQuery.available and asserts the matching half, so on a Mac
# (no ray queries at all) both entries take the no-rays branch and both still
# mean something. NOT --headless and NOT in jah_no_display: the issue half asks
# the DEVICE a question, and there is no device without a display.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-ray_row_norays)
file(MAKE_DIRECTORY ${_home}/run)

# DISPLAY-FREE, ENFORCED (SPECS/SCENEGRAPH_SPEC.md §3b). Every `--headless`
# script run boots the NULL render system now: no display connection, no driver,
# no window. These entries are therefore run with DISPLAY and WAYLAND_DISPLAY
# REMOVED, which is what turns "document verbs only" from a comment into a test.
# The suites NOT listed here (full_surface, samples, image_plane, import_render,
# shader_thumbnail, avatar, skinning, physics, particles, …) render pixels and
# keep their display requirement, honestly and permanently.

# The player-editor space round trip on the two scenes that exposed the
# graph-ownership defect (2026-09-05): particle systems and the planar pass
# reading nodes another mirror's bind had destroyed. Engine + window, real
# pages -> needs a display.
set(JAH_SAMPLES_DIR ${CMAKE_BINARY_DIR}/bin/scenes)

# The DEFAULT AVATAR fixture's contract: beta_mannequin.dae (generated by
# tests/avatar from the shipped Skeletal Animation sample — Mixamo Beta, full
# bare-named Mixamo skeleton, baked clip STRIPPED) imports, spawns, resolves
# its skeleton (the positive complement of sockets.e2e's rig2 fail-soft), kept
# its cm unit scale, and is NEUTRAL — zero clips, zero roles, empty machine.
# Document verbs only -> --headless. The path is set here explicitly (not via
# the tests/avatar cache var) because tests/avatar configures AFTER this dir.
set(JAHSHAKA_MANNEQUIN_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_mannequin.dae")

# The Avatar module AS VERBS (verb-coverage audit F15) plus avatar.loadClip
# (F5): spawn -> possess -> setMovement clamping -> setLocomotionAsset's six
# refusal classes -> locomotionState -> loadClip, and the save + reopen that
# proves a loaded clip is a real project asset rather than a path on the
# author's disk. Both fixtures are generated by tests/avatar (which configures
# AFTER this dir, so the paths are spelled out here exactly as the mannequin
# suite's is).
set(JAHSHAKA_BETA_WALK_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_walk.dae")

# ONE ASSET IN THE EDITOR (smoke S9, owner rule 2026-09-11): the tray collapse
# (assets.list({tray:true})), the placement of repeated adds, the outliner's
# one-row-per-asset rule (editor.outlinerRows) and the clip-reuse rule, all as
# VERBS — the same ones the tray, the viewport drop and the Avatar page call.
# Fixtures come from tests/avatar (which configures AFTER this dir, so the paths
# are spelled out exactly as the mannequin suite's are).
set(JAHSHAKA_MANNEQUIN_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_mannequin.dae")
set(JAHSHAKA_BETA_WALK_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_walk.dae")
configure_file(scripts/e2e_one_asset.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_one_asset.js @ONLY)
# A FRESH home per run: this suite asserts ABSOLUTE row counts ("one tray row",
# "one animation row"), and a home that persists across gates accumulates a
# second copy of everything.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-one_asset)
jah_fresh_home_fixture(scripting.e2e.one_asset.fresh_home one_asset_home "${_home}" LABELS "scripting")
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.one_asset
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_one_asset.js --headless
         WORKING_DIRECTORY ${_home}/run)
jah_ship_row(scripting.e2e.one_asset)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(scripting.e2e.one_asset PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED one_asset_home
    LABELS "scripting"
    TIMEOUT 120)

# scripting.e2e.tray_panel — the asset tray shows EVERY ASSET A SCENE USES,
# once (owner 2026-09-12, lane L13): the tray PANEL (editor.trayAssets reads
# the widget) against assets.list({tray:true}) after verb pins and uses, and
# each shipped sample's tray against the scene's asset closure read off the
# document. Engine-up — the tray is a widget — so it needs the rig's display.
# A FRESH home per run: it asserts exact tile sets.
configure_file(scripts/e2e_tray_panel.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_tray_panel.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-tray_panel)
jah_fresh_home_fixture(scripting.e2e.tray_panel.fresh_home tray_panel_home "${_home}" LABELS scripting)
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(scripting.e2e.tray_panel SHIP CLASS app TIER low NEEDS NONE
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_tray_panel.js
         WORKING_DIRECTORY ${_home}/run)
# TESTING-CLEANUP-2B item 2: TIMEOUT 550 = 6 x the run log's quiet median 90.4 s (46 quiet PASS runs since 2026-10-01, worst 268.9 s);
# declared 300 was below the policy, honest only with the +900 s widening (now deleted).
# STAGE-CLOSE (TESTING-SPEED-B2; SPECS/TESTING_SPEED_ROWS.md MOVE): the ship and the stage close only.
set_tests_properties(scripting.e2e.tray_panel PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED tray_panel_home
    LABELS "scripting"
    TIMEOUT 550)

# gi.one_writer — ONE WRITER PER VOXEL VOLUME (PHOTON-WRITER-1): a drag and a
# lamp moving while the camera walks, on Showroom 2, read against the engine's
# own books (world.giStatus's chainInjectionRefusals / chainInjectionsPeakFrame).
# A FRESH home per run (the sample is imported).
set(JAH_ONE_WRITER_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
configure_file(scripts/gi_one_writer.js.in ${CMAKE_CURRENT_BINARY_DIR}/gi_one_writer.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-one_writer)
jah_fresh_home_fixture(gi.one_writer.fresh_home one_writer_home "${_home}" LABELS scripting)
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(gi.one_writer SHIP CLASS app TIER epic NEEDS photon
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/gi_one_writer.js
         WORKING_DIRECTORY ${_home}/run)
# STAGE-CLOSE (TESTING-SPEED-B2; SPECS/TESTING_SPEED_ROWS.md MOVE): the ship and the stage close only.
set_tests_properties(gi.one_writer PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED one_writer_home
    LABELS "scripting"
    TIMEOUT 300)

# gi.pcc_mirror.ray_tier (archived: archive/tests/gi) — AT A RAY TIER THE PROBE GRID IS NOT BUILT (PHOTON-F12-PCC), on
# Showroom 2 at Epic: no grid with rays on, the grid with rays off, gone again with rays back
# on, and the roughness-0 sphere's reflection A/B against the grid's answer. The engine's books
# for the same rule are gi.pcc_mirror (archived: archive/tests/gi)'s arm (i). A FRESH home per run (the sample is imported).
set(JAH_PCC_RAY_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-pcc_ray_tier)
set(JAH_PCC_RAY_OUT "${_home}/run/")
file(MAKE_DIRECTORY ${_home}/run)

# gi.chain_converge_scenes (archived: archive/tests/gi) — ONE AT-REST SWEEP IS THE CHAIN'S FIXED POINT on the
# default scene and Showroom 2 at High (PHOTON-WRITER-1 SWEEPS-3; the room half is
# gi.chain_converge (archived: archive/tests/gi)): from a perturbed history a second sweep changes no byte of
# any cascade's light (world.giVoxelStats's lightDigest). Fresh home per run.
# STAGE-CLOSE (lane D6B-GATE-SHAPE; audit §3c, verdict STAGE-CLOSE): 80 s for the claim gi.chain_converge (archived: archive/tests/gi)
# (9 s) holds at every push; an engine change still selects it in its own scoped gate.
set(JAH_CONVERGE_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-chain_converge_scenes)
file(MAKE_DIRECTORY ${_home}/run)

# ANIMATION CLIPS AS A LIBRARY TYPE (ModelTypes::Animation): the import that
# used to be refused with a wrong message, the type-filtered listing, the
# avatar consumers (loadClip by asset guid, avatar.animations) and the archive.
#
# TWO tests, because PROJECT PORTABILITY cannot be proved inside one library:
# the first exports an archive into a shared directory, the second imports it
# in a FRESH HOME whose library is empty and reads the clip table back out of
# the transported bytes. ctest fixtures order them.
#
# Fixtures come from tests/avatar (which configures AFTER this dir, so the
# paths are spelled out exactly as the mannequin suite's are).
set(JAHSHAKA_MANNEQUIN_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_mannequin.dae")
set(JAHSHAKA_BETA_WALK_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_walk.dae")
set(JAHSHAKA_ANIMATION_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/animation_assets_export/clips.zip")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/animation_assets_export")
# A FRESH home per run for BOTH halves (the mcp.e2e.fresh_home pattern): these
# two assert on absolute row counts — "one animation asset", "an EMPTY library"
# — and a home that persists across gates accumulates a second copy of
# everything, so the suite would go red on nothing.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-animation_assets)
file(MAKE_DIRECTORY ${_home}/run)

set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-animation_assets_import)
file(MAKE_DIRECTORY ${_home}/run)

jah_no_display(
    scripting.e2e.clip_ref
    scripting.e2e.tex_ref)

# scripting.e2e.avatar_assets — AVATAR_ASSET_SPEC §9 gate T14.
#
# The owner's whole story in one run, as VERBS: import a rigged model, mint an
# avatar asset from it, publish a library version, add it to a project (a PIN,
# not a copy), spawn a LINKED instance, edit the PROJECT's version and watch
# the instance follow without a reopen, save + reopen the scene, and publish
# the project's version back to the library.
#
# --headless: every verb here is Needs::Document — the definition, the pins,
# the scene edits and the spawn all run on the NULL render system, so this
# whole model is provable with no GPU and no display.
#
# The fixture pair is generated by tests/avatar (which configures AFTER this
# directory, so the paths are spelled out exactly as the other avatar suites
# spell them).
set(JAHSHAKA_BETA_WALK_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_walk.dae")
set(JAHSHAKA_MANNEQUIN_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_mannequin.dae")

# --- multi-selection (EDITOR_MULTISELECT_SPEC §5) ---------------------------
# Both are document-only, so they run --headless (offscreen platform, NULL
# render system, DISPLAY unset) — the selection set, its verbs and its edits
# never touch a pixel.
# node.rename (plan item 15): the sibling-unique copy rule and ONE recorded undo
# step per rename; the undo itself is asserted in mcp.e2e (a --script run's
# macro never closes). Document verbs only -> --headless.

foreach(_ms multiselect multiselect_edit)
endforeach()

# scripting.e2e.ogre_ports (archived: archive/tests/scripting) — THE PORTS (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3): the
# archives the Ogre-Samples tab's tiles open, imported into a VIRGIN home,
# opened, rendered and measured. ogre_samples above is the verb half (the
# inventory and the launch refusal); this is the content half.
#
# ENGINE-UP and display-bound, unlike its sibling: a port that imports but does
# not render is exactly the failure this has to catch. A virgin HOME per run
# for the same reason samples.cleanstart has one — the import step must be an
# import and not a re-import.
#
# ONE PROCESS FOR ALL THREE ports: they share a base scene and a harness, so
# back-to-back imports into one library is also the cross-archive dedup case,
# at a third of the launches.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-ogre_ports)

# --- THE DEEP CLIPBOARD (SPECS/CLIPBOARD_SPEC.md P0 + P1) --------------------
#
# clipboard        the payload and the paste: one self-identifying TEXT envelope
#                  on the system clipboard, fresh guids + the Cube->Cube2 rule,
#                  cut, the editor.* aliases onto the same clipboard, and the
#                  tolerance rules (an unknown item kind is skipped with a
#                  reason; junk is refused without clobbering the payload).
# clipboard_xproject  the CLOSURE and its RESOLUTION: a textured cube copied in
#                  project A and pasted into project B pins the texture as a
#                  binding; a payload rewritten in JavaScript to carry only the
#                  content id still resolves out of this machine's store
#                  (§3.4 step 3); an unfindable oid is MISSING, the item that
#                  needs it is refused, and allowMissing opts into Unreal's
#                  paste-with-holes.
# clipboard_assets library TILES: the row + its bytes travel, and pasting into a
#                  library that does not know the guid registers it UNDER THAT
#                  GUID (identity, invariant I1) — idempotently.
#
# All three are document verbs only -> --headless, no display, no GPU.
foreach(_clip clipboard_xproject clipboard_assets)
    configure_file(scripts/e2e_${_clip}.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_${_clip}.js @ONLY)
endforeach()
# doc_assets.hold_refused — assets.holdBakes refuses in a process with no test tier (this pool has none).
foreach(_clip clipboard_xproject clipboard_assets)
endforeach()

# scripting.e2e.clipboard_budget — THE COPY UNDER THE INLINE BUDGET (STAGE-CLOSE-TRIAGE-1 rounds 7-8):
# a row's node tree is never budgeted (a 600-part model copies under a 64 KB budget, its file by
# reference, and pastes into a foreign library with the source's node count); a budget under its
# 64 KB floor refuses copy and cut with that reason (nothing deleted). A ROW of its own, not a
# doc_assets arm: the budget is a SETTING the process reads from its data root's jahsettings.ini,
# seeded per process by the shell half. Three --headless processes.
set(_cbud_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-clipboard_budget)
file(MAKE_DIRECTORY ${_cbud_home}/run)
# THE COMMAND: the one fresh-home implementation wipes the home every run (no hand-kept rm list), and
# `--headless` stands VISIBLY on the command line (gate-scope reads a row as a headless script from its
# command; every one of the script's three processes is --headless, the flag forwarded to each) — so
# a change to the copy, the floor or the paste selects this row (read 8 F1).
add_test(NAME scripting.e2e.clipboard_budget
         COMMAND sh "${JAH_FRESH_HOME}" --warm "${_cbud_home}" --
                 bash ${CMAKE_CURRENT_SOURCE_DIR}/clipboard_budget.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --headless
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_clipboard_budget.js
         WORKING_DIRECTORY ${_cbud_home}/run)
jah_ship_row(scripting.e2e.clipboard_budget)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(scripting.e2e.clipboard_budget PROPERTIES
    ENVIRONMENT "HOME=${_cbud_home}"
    LABELS "scripting"
    # TIMEOUT 120: the policy's floor (tests/CMakeLists.txt) — 6x the measured 13.35 s (n=4, 13.33-13.86 s,
    # 2026-10-11, three processes, solo on :72, the gate slot free) is 80 s, under it.
    TIMEOUT 120)
set_property(TEST scripting.e2e.clipboard_budget APPEND PROPERTY ENVIRONMENT_MODIFICATION
             "JAHSHAKA_DATA_ROOT=unset:")
jah_no_display(scripting.e2e.clipboard_budget)

# THE DUAL IMAGE ON A CURVED MIRROR (lane SSR-2; the owner's push #35 smoke).
# The script's own header says why this lives here and not in tests/ssr: the
# defect needs a parallax-corrected PROBE GRID whose answer disagrees with the
# screen's, and every fixture in the engine suite falls back to the same
# analytic sky the trace is looking at. The Mirror Room sample is the scene the
# owner photographed. Engine-up and on-screen, like the grades suite beside it.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(_ssrhome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-ssr_mirror)
file(MAKE_DIRECTORY ${_ssrhome}/run)

# ATOM STAGE 1 — DETERMINISTIC LEVEL SELECTION (SPECS/NANITE_SPEC.md §7.3).
# The import-time LOD chain, end to end: assets.meshLods reports what the bake
# built — BOTH lengths, the measured `bound` the renderer selects on and the
# simplifier's `error` beside it — editor.setLodBias walks the renderer through
# every level of it in ONE
# camera pose, the triangle count falls at each step, the DRAW COUNT does not
# move (finding B — the levels share a vertex buffer and an index pool, so they
# share a vaoName and the render queue advances numDraws), and bias 0 returns to
# the finest level exactly.
#
# ENGINE-UP AND ON-SCREEN, not --headless: the subject is what the renderer
# selects and draws. The fixture is the shipped gizmo sphere (960 triangles,
# static, measured to simplify to 480 and 240), configured in by path.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})

# 300 like the other open-and-render suites: it imports an archive and opens it.

set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-player_no_editor)
file(MAKE_DIRECTORY ${_home}/run)

# scripting.e2e.player_bounce — THE REFUSAL PATH (SMOKE-FIX-1's fix round, F3).
#
# The one suite that drives `EnginePlayerView::start` returning false and
# everything that hangs off it: the bounce back to a page that draws, the
# reason reaching `app.space()`, the play-mode undo, and the session log's
# PLAY START / PLAY STOP staying a pair. It needs the attach to fail on a box
# where it always succeeds, so it sets JAHSHAKA_PLAYER_TEST_NO_ATTACH=1 — the
# script says why at length, and so does EnginePlayerScene::attach.
configure_file(scripts/e2e_player_bounce.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_player_bounce.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-player_bounce)
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(scripting.e2e.player_bounce SHIP CLASS app TIER epic NEEDS photon
         COMMAND ${JAH_FRESH_HOME} --warm ${_home} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_player_bounce.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.player_bounce PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka;JAHSHAKA_PLAYER_TEST_NO_ATTACH=1"
    LABELS "scripting"
    TIMEOUT 300)

# scripting.e2e.new_scene — project.create(name, {template, location}), which IS
# the New Scene dialog's two controls (WORLD-MODEL-1: Basic / Empty / World) (owner review R1 / R1b, lane
# SMALL-UI-A). Engine up: the default template's sky is a RENDERER fact as much
# as a document one, and the empty world has to be built by the same route.
jah_pool_arm(editor new_scene ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_new_scene.js 16)

# scripting.e2e.primitives — the ONE primitive table through the VERB (owner
# review R6 / answer Q4). The table's own contract — the meshes, the tangents,
# the tile icons, the old-guid mapping — is defaults.primitive_table (archived: archive/tests/defaults); this is
# "every name the drawer offers is a name the verb accepts", plus the retired
# four refused by name, plus the Mirror Room still opening with its teapot.
jah_pool_arm(editor primitives ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_primitives.js 8)

# scripting.e2e.add_primitive — ATOM P2's acceptance for the VERB: a primitive is a
# baked library asset, so the node it adds reports a real LOD chain in
# `app.renderStats().perObject` (a renderer reading, so the chain is proven all the
# way through the mirror and the upload), and a bias of 0 pins every object at its
# finest level. Needs the engine: no --headless.
jah_pool_arm(editor add_primitive ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_add_primitive.js 2)

# scripting.e2e.material_tabs (archived: archive/tests/scripting) — MATERIALS_TABS_SPEC §5: SEVERAL MATERIALS OPEN
# AT ONCE. It needs the PAGE (the five tab verbs are the page's own entry
# points), so no --headless; its own home, wiped before the run, because the
# tab set it leaves behind is what the relaunch half reads.
set(_mtHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material_tabs)
file(MAKE_DIRECTORY ${_mtHome}/run)

# scripting.e2e.material_bundle (archived: archive/tests/scripting) — MATERIAL_BUNDLE_SPEC phase 1, the acceptance
# flow the spec names: pick an image from disk -> ONE tile -> export -> an
# EMPTY library -> it still resolves. Two processes, because "an empty library"
# is not something a verb can fake: the first exports an archive from a normal
# session, the second runs on its OWN home and data root, wiped before it
# starts, and is given nothing but that archive.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(_mbHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material_bundle)
file(MAKE_DIRECTORY ${_mbHome}/run)

# scripting.e2e.material_share — MATERIAL_BUNDLE_SPEC phase 2 (owner Q5): ONE
# material as a self-contained share file. Two processes for the same reason
# the bundle pair needs two: "a library that has never seen it" is not
# something a verb can fake.
set(SHARE_PATH ${CMAKE_CURRENT_BINARY_DIR}/material_share.jbundle)
configure_file(scripts/e2e_material_share.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_share.js @ONLY)
set(_msHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material_share)
jah_fresh_home_fixture(scripting.e2e.material_share.fresh_home materialShareHome "${_msHome}" ARGS --rm "${SHARE_PATH}" LABELS "scripting")
file(MAKE_DIRECTORY ${_msHome}/run)
add_test(NAME scripting.e2e.material_share
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_share.js --headless
         WORKING_DIRECTORY ${_msHome}/run)
set_tests_properties(scripting.e2e.material_share PROPERTIES
    ENVIRONMENT "HOME=${_msHome};JAHSHAKA_DATA_ROOT=${_msHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED materialShareHome
    FIXTURES_SETUP materialShareFile
    LABELS "scripting"
    TIMEOUT 180)

configure_file(scripts/e2e_material_share_import.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_share_import.js @ONLY)
set(_msiHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material_share_import)
jah_fresh_home_fixture(scripting.e2e.material_share_import.fresh_home materialShareImportHome "${_msiHome}" LABELS "scripting")
file(MAKE_DIRECTORY ${_msiHome}/run)
add_test(NAME scripting.e2e.material_share_import
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_share_import.js --headless
         WORKING_DIRECTORY ${_msiHome}/run)
jah_ship_row(scripting.e2e.material_share_import)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(scripting.e2e.material_share_import PROPERTIES
    ENVIRONMENT "HOME=${_msiHome};JAHSHAKA_DATA_ROOT=${_msiHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED "materialShareImportHome;materialShareFile"
    LABELS "scripting"
    TIMEOUT 120)

# The far side: a library that has never seen the material, the image or the
# project, and one archive.
set(ARCHIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/material_bundle.zip)
set(_mbiHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material_bundle_import)
file(MAKE_DIRECTORY ${_mbiHome}/run)

# scripting.e2e.preset_apply — MATERIAL_BUNDLE_SPEC phase 3: a preset is a
# READ-ONLY LIBRARY BUNDLE and applying one PINS it. Three applies add ZERO
# rows after the first seeds the bundle, no "Presets" folder and no
# matgen.material are minted, a write to a preset is refused by the WRITER,
# and R18's Customise mints "<Preset>-1" sharing the preset's own maps.
# Document verbs only -> --headless. A FRESH HOME, because the load-bearing
# first assertion is that NOTHING is seeded until a preset is used.
set(_paHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-preset_apply)
jah_fresh_home_fixture(scripting.e2e.preset_apply.fresh_home presetApplyHome "${_paHome}" LABELS "scripting")
file(MAKE_DIRECTORY ${_paHome}/run)
add_test(NAME scripting.e2e.preset_apply
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_preset_apply.js --headless
         WORKING_DIRECTORY ${_paHome}/run)
jah_ship_row(scripting.e2e.preset_apply)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(scripting.e2e.preset_apply PROPERTIES
    ENVIRONMENT "HOME=${_paHome};JAHSHAKA_DATA_ROOT=${_paHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED presetApplyHome
    LABELS "scripting"
    TIMEOUT 180)

# scripting.e2e.preset_edit (archived: archive/tests/scripting) — PRESET-EDIT-1: A PRESET A PROJECT HOLDS IS THE
# PROJECT'S TO EDIT, and the FIRST EDIT is what makes the copy (the owner's
# rule, 2026-09-21). The copy keeps the preset's NAME, the project's pin moves
# onto it, the meshes follow, the library master and every other project are
# untouched — and with no project open the master is still locked, which is
# the only lock left. Document verbs only -> --headless. A FRESH HOME, because
# the arms count library rows.
set(_peHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-preset_edit)
file(MAKE_DIRECTORY ${_peHome}/run)

# scripting.e2e.preset_seed_boot (archived: archive/tests/scripting) — THE FIRST-RUN SEED ON THE ROUTE A PERSON
# TAKES (SEED-STAMP-1). preset_apply above seeds through
# `materials.seedPresets()`, which mints each map inside `definitionFor` and
# stamps it there; a LAUNCH runs `MaterialPresetSeeder`, which mints the maps
# ahead of the row pass — so `definitionFor` found existing rows, answered
# `minted = false`, and the stamp never fired. Thirty-one preset maps stood in
# the tray and on the Assets page as the user's own tiles (owner review R10.2,
# measured 31 members / 0 stamped before the fix). Two arms over ONE data root:
#   1. user_first — no seed (no JAHSHAKA_SEED_PRESETS here): the user imports
#      one picture a preset also uses, so arm 2 can prove a seed stamps only
#      the rows it MINTS (V-2's other half). Document verbs -> --headless.
#   2. the launch seed itself, which needs the WINDOW that starts it (and so
#      the gate's display) with JAHSHAKA_SEED_PRESETS=1 — the env that lets a
#      driven session take the seed a person's launch runs.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(_psHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-preset_seed_boot)
file(MAKE_DIRECTORY ${_psHome}/run)

# scripting.e2e.preset_seed_race (archived: archive/tests/scripting) — THE USER'S IMPORT *DURING* THAT SEED
# (SEED-SMALL-1). The two arms above are a BEFORE and an AFTER, one importer at
# a time by construction; this one runs both at once, which is the case the
# seed's own by-content check cannot answer (it is asked once, for all
# thirty-one maps, before the first of them is imported). Every user door now
# stands the seeder down first — the Assets page, the editor tray, and the
# verbs through AssetService — so there is still exactly one row for those
# bytes and it is the user's, unstamped, with the preset resolving to it.
#
# Its OWN fresh data root: it seeds and imports, and it must not move the
# censuses the two arms above compare. A window (the seeder is started by
# MainWindow) and JAHSHAKA_SEED_PRESETS=1, like the boot arm. JAHSHAKA_SEED_HOLD=1
# makes the race DETERMINISTIC (ASYNC-SHADERS-1): the seed parks after hashing until the
# user's import stands it down — its timing was the startup gate's, and a longer gate let
# the whole seed finish inside the splash.
set(_prHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-preset_seed_race)
file(MAKE_DIRECTORY ${_prHome}/run)

# <pool>.tier — THE POOL'S DECLARATION IS THE PROCESS'S (lanes TEST-TIER-1, TEST-NEEDS-1;
# services/testtier.h): in a `TIER low NEEDS NONE` pool app.testTier() is {tier:"low", needs:[]},
# a new scene AND an opened one (saved at Medium) are put on Low with Photon off, and the window
# booted 1280x720; in a `TIER document` pool the same script finds no test tier, a new scene at
# the document's Epic and the reopened one at the Medium it saved. One arm in a pool of each.
jah_pool_arm(editor tier ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_pool_tier.js 3)
# THE SPLIT BY TIER (TEST-TIER-1, measured arm by arm, spikes/test-tier-1/okcount.md): editor,
# player, assets_import, avatar_anim, vr_noruntime, cameras run Low — the same verdict and the
# same count of `ok` lines as at Epic. editor_view is Epic: gizmo_rings (clickable handles at the
# boot window), render_stats (the Epic pre-pass), hierarchy_visibility (asserts the Epic GI) and
# the two arms moved into it (full_surface, play_select) are red at Low; particles, red at Low
# and short of its line after editor_view's window-changing arms, moved to shading_live.

# ---- THE POOLS (lane SUITE-POOL-1; tests/CMakeLists.txt, jah_pool_arm/jah_add_pool) --------
# One row, one process per family; every arm is declared beside the comment that says what it
# claims (above, and in tests/app, tests/shadergraph and tests/atom, which configure before this
# directory). THE POOL BUDGET (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §3 B1): <= 60 s of ARMS
# per pool plus its boot, at the arms' measured prices (scripts/gate-times.txt pool.X::arm). A family
# over it is SPLIT into pools of <= 60 s of arms (gi_verbs/gi_verbs_b, editor_view/_b/_c,
# shading_live/_b, world_sky/_b/_c — every arm keeps its name, its script, its assertions), and an
# arm that measures a whole scene (a Showroom 2 / Mirror Room import) or alone runs over the
# budget moves to the family's `<pool>_stage` pool, labelled stage-close (the SHIP and the stage
# close only). The suffix is an underscore, never a dot: the run log keys an arm as
# <row>::<arm after the first dot>.
# editor.material_drop — MATERIAL-DROP-1 + TRAY-DUPLICATE-1 (the owner, 2026-09-28): a LIBRARY
# drop is a fresh, pristine project copy ("Wood PBR", "Wood PBR 2", …), a PROJECT-tray drop
# assigns, assets.duplicate copies the project's own version by the same naming, and the
# viewport's real drag does the same. Engine up (the gesture), the Low tier.
configure_file(scripts/e2e_material_drop.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_drop.js @ONLY)
jah_pool_arm(editor material_drop ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_drop.js 12)

jah_add_pool(editor TIER low NEEDS NONE NEEDS_WHY "editor verbs and their screenshots at Low with Photon off, as TEST-TIER-1 measured them (no GI claim)" TEST_WINDOW LABELS scripting)
jah_ship_row(pool.editor)   # THE SHIP SET (TEST-CUT-1)

if(TEST scripting.e2e.clip_ref)
    jah_fresh_home_fixture(scripting.e2e.clip_ref.fresh_home scriptingE2eClipRefFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-clip_ref" WARM LABELS "scripting")
    set_property(TEST scripting.e2e.clip_ref APPEND PROPERTY FIXTURES_REQUIRED scriptingE2eClipRefFreshHome)
endif()
if(TEST scripting.e2e.tex_ref)
    jah_fresh_home_fixture(scripting.e2e.tex_ref.fresh_home scriptingE2eTexRefFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-tex_ref" WARM LABELS "scripting")
    set_property(TEST scripting.e2e.tex_ref APPEND PROPERTY FIXTURES_REQUIRED scriptingE2eTexRefFreshHome)
endif()
if(TEST scripting.e2e.perf_capture_frames)
    jah_fresh_home_fixture(scripting.e2e.perf_capture_frames.fresh_home scriptingE2ePerfCaptureFramesFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-perf_capture_frames" WARM LABELS "scripting")
    set_property(TEST scripting.e2e.perf_capture_frames APPEND PROPERTY FIXTURES_REQUIRED scriptingE2ePerfCaptureFramesFreshHome)
endif()

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST gi.one_writer)
    set_property(TEST gi.one_writer PROPERTY JAHSHAKA_TEST_FRAMES 1152)
endif()
if(TEST material.live_roundtrip.author)
    set_property(TEST material.live_roundtrip.author PROPERTY JAHSHAKA_TEST_FRAMES 1135)
    set_property(TEST material.live_roundtrip.import PROPERTY JAHSHAKA_TEST_FRAMES 1084)
    set_property(TEST material.live_roundtrip.restore PROPERTY JAHSHAKA_TEST_FRAMES 92)
endif()
if(TEST pool.editor)
    set_property(TEST pool.editor PROPERTY JAHSHAKA_TEST_FRAMES 1139)
endif()
if(TEST scripting.e2e.clip_ref)
    set_property(TEST scripting.e2e.clip_ref PROPERTY JAHSHAKA_TEST_FRAMES 2)
endif()
if(TEST scripting.e2e.clipboard_budget)
    set_property(TEST scripting.e2e.clipboard_budget PROPERTY JAHSHAKA_TEST_FRAMES 3)
endif()
if(TEST scripting.e2e.material_share)
    set_property(TEST scripting.e2e.material_share PROPERTY JAHSHAKA_TEST_FRAMES 1)
endif()
if(TEST scripting.e2e.material_share_import)
    set_property(TEST scripting.e2e.material_share_import PROPERTY JAHSHAKA_TEST_FRAMES 1)
endif()
if(TEST scripting.e2e.one_asset)
    set_property(TEST scripting.e2e.one_asset PROPERTY JAHSHAKA_TEST_FRAMES 1)
endif()
if(TEST scripting.e2e.perf_capture_frames)
    set_property(TEST scripting.e2e.perf_capture_frames PROPERTY JAHSHAKA_TEST_FRAMES 528)
endif()
if(TEST scripting.e2e.player_bounce)
    set_property(TEST scripting.e2e.player_bounce PROPERTY JAHSHAKA_TEST_FRAMES 508)
endif()
if(TEST scripting.e2e.preset_apply)
    set_property(TEST scripting.e2e.preset_apply PROPERTY JAHSHAKA_TEST_FRAMES 1)
endif()
if(TEST scripting.e2e.sky_swap_transient)
    set_property(TEST scripting.e2e.sky_swap_transient PROPERTY JAHSHAKA_TEST_FRAMES 1083)
endif()
if(TEST scripting.e2e.tex_ref)
    set_property(TEST scripting.e2e.tex_ref PROPERTY JAHSHAKA_TEST_FRAMES 3)
endif()
if(TEST scripting.e2e.tray_panel)
    set_property(TEST scripting.e2e.tray_panel PROPERTY JAHSHAKA_TEST_FRAMES 569)
endif()
if(TEST scripting.live)
    set_property(TEST scripting.live PROPERTY JAHSHAKA_TEST_FRAMES 606)
endif()
if(TEST scripting.e2e.editor_controls.timing)
    set_property(TEST scripting.e2e.editor_controls.timing PROPERTY JAHSHAKA_TEST_FRAMES 2581)
endif()

# scripting.e2e.atom_density_cap — ATOM-5 stage 1: THE VIEW'S DENSITY CAP IS ONE WORLD-MODE ROW (`atomDensityCap`,
# Low 4 / Medium 3 / High 2 / Epic 1): world.mode writes it, world.atomDensityCap() reads it, world.tierTable()'s world
# list carries each mode's column, an override pins it and the scene file keeps it across save -> close -> open.
# Document verbs only -> --headless (no engine, no display). The spec's own row: archived at the merge.
set(_adcHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-atom_density_cap)
file(MAKE_DIRECTORY ${_adcHome}/run)
add_test(NAME scripting.e2e.atom_density_cap
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_atom_density_cap.js --headless
         WORKING_DIRECTORY ${_adcHome}/run)
set_tests_properties(scripting.e2e.atom_density_cap PROPERTIES
    ENVIRONMENT "HOME=${_adcHome};JAHSHAKA_DATA_ROOT=${_adcHome}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 60)
jah_no_display(scripting.e2e.atom_density_cap)
