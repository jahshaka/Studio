# Scripting engine tests (SCRIPTING_SPEC.md phase 0+).
#
# scripting.engine — unit test of the QJSEngine host: evaluation, error line
# numbers, console capture, undo-macro wrapping, registry metadata validation.
# Links ONLY the scripting core (no MainWindow, no engine): the core is designed
# to have zero Studio dependencies (ScriptHost carries std::function probes).
# Runs offscreen.
#
# The end-to-end --script suites (full app binary, engine up) live below as
# scripting.e2e.* — added in phase 1.
find_package(Qt6 REQUIRED COMPONENTS Widgets Qml)
add_executable(test_script_engine test_script_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/apimodule.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/apiregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/scriptengine.cpp
    # the one flag ScriptEngine::stop sets so a stopped script's thumbnail
    # sweep stops too (THUMBS-1 F1) — a TU with no dependencies of its own
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailstop.cpp
    # The worker thread + the verb bridge: the script engine IS these three
    # objects since it moved off the UI thread (SCRIPTING_LIVE_SPEC), and the
    # unit test drives its fake module through the same dispatch the app uses.
    ${CMAKE_SOURCE_DIR}/src/scripting/scriptworker.cpp
    # The session log's script-run record (SESSION_LOG_SPEC §5) is emitted from
    # ScriptEngine::evaluate, the one entry point the console dock, --script and
    # MCP run_script all share. JahLog is a LEAF service — Qt plus iris::Logger
    # and nothing else — so the "core has zero Studio dependencies" property
    # above still holds in the way that matters: no MainWindow, no engine, no
    # document, no database.
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    # UndoService is the app's ONE undo sink, and since the run macro became
    # lazy (opened by the first command that lands, not by the run) the sink IS
    # the behaviour under test — the fake module pushes through it exactly as
    # the editor's verbs do. Another leaf: QUndoStack plus a header-only
    # StudioCommand base, no MainWindow, no engine, no document.
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp)
set_target_properties(test_script_engine PROPERTIES AUTOMOC ON)
target_include_directories(test_script_engine PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/scripting
    ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_script_engine PRIVATE IrisGL Qt6::Widgets Qt6::Qml)
add_test(NAME scripting.engine COMMAND test_script_engine
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(scripting.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "scripting")

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
# The same script, twice, with the only difference being --script-live: the
# live half proves the render loop DRAWS between the script's verbs (which the
# UI-thread engine could not do at all), the off half proves it draws NOTHING
# between them — the property every other e2e script here is written against.
# They are separate ctest entries rather than one, because "what a run's policy
# does to the driver" is decided when the run starts and cannot be switched
# from inside it.
foreach(_variant live off)
    set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-live_feedback_${_variant})
    file(MAKE_DIRECTORY ${_home})
    file(MAKE_DIRECTORY ${_home}/run)
    if(_variant STREQUAL live)
        set(_flag --script-live)
        set(_name scripting.live)
    else()
        set(_flag)
        set(_name scripting.live_off)
    endif()
    jah_gpu_row(${_name} CLASS app TIER low
             COMMAND ${JAH_FRESH_HOME} --warm ${_home} --
                     $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_live_feedback.js ${_flag}
             WORKING_DIRECTORY ${_home}/run)
    set_tests_properties(${_name} PROPERTIES
        # JAHSHAKA_TEST_TIER=low (TEST-TIER-1): no claim here needs the shipped picture — the same verdict and ok count at Low (spikes/test-tier-1/solo-okcount.txt)
        ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka;JAHSHAKA_TEST_TIER=low"
        LABELS "scripting"
        TIMEOUT 180)
endforeach()

jah_pool_arm(editor build_scene ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_build_scene.js 8)
# EDITOR_MULTISELECT_SPEC §5: the selection SET's semantics (primary, Ctrl
# toggle with topmost promotion, the owner's Shift range, the World root rule)
# and its EDITS (D5-reduced delete/duplicate/copy/paste, placement, fresh
# guids). Document verbs only -> --headless: no engine, no display.

# MOVEMENT AS A VERB (SPECS/REALTIME_REFLECTIONS_SPEC.md §3.3.5): the
# `mobility` reflected row and node.mobility's reading of it — every driver with
# its own reason, rule 2's inheritance, a driver openly beating a user setting,
# ONE undo step per write, the setting surviving a save and reopen, and the two
# claims the design rests on: an editor move neither promotes an object nor
# clears the user's setting (a promotion costs a from-scratch GI rebuild).
# It also opens a scene written BEFORE mobility, through the same
# writer/reader pair the file uses (node.serialize/deserialize). Document verbs
# only -> --headless.
jah_pool_arm(doc mobility ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_mobility.js 1 HEADLESS)

# THE SCALE-RATIO LOCK (SCALE-LOCK-1, the owner's "a lock for the
# transformations"): node.setScaleLock / node.scaleLock, the reflected
# `scaleLock` row, node.transform's one-channel rule and its `scaleUniform`
# override, the three degenerate cases (a zero channel has no ratio, a negative
# ratio mirrors, the same value is a no-op), the flag travelling with a
# duplicate, and the save/close/open round trip through the real writer and
# reader. The panel and gizmo halves are ui.panel_undo and gizmo.scale_lock.
# Document verbs only -> --headless.
jah_pool_arm(doc scale_lock ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_scale_lock.js 1 HEADLESS)

# THE PARTS OF A GROUPED NODE (owner review R14, COMPONENTS-1): node.components
# — the flattened descendants an imported model's outliner row deliberately
# hides, in document order, with their depth and their own visibility, lock and
# selection state. The panel half is ui.components. Document verbs only ->
# --headless.
jah_pool_arm(doc components ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_components.js 1 HEADLESS)

jah_pool_arm(editor undo_macro ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_undo_macro.js 7)
# SCENEGRAPH_SPEC §3 step 4: the v2 scene format's fragment pair
# (node.serialize/node.deserialize) and the persisted SCENE_STATIC override,
# driven through the verbs against the real database and a real save/reopen.
jah_pool_arm(editor fragments ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_fragments.js 3)
# PBR material persistence: container applies (imported-model roots), saved
# material assets by guid, and the full save/close/open round-trip of every
# PBR property (the owner-reported "materials lost on reopen" data loss).
jah_pool_arm(shading pbr_materials ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_pbr_materials.js 9)

# THE LIVE HOVER PREVIEW, for every material source (MATERIAL-PREVIEW-1). The
# owner found it working for a material made from an image and dead for the
# tray and the Materials module; it was one QVariant static-type mismatch, and
# nothing covered it. Drives material.preview/endPreview for four sources and
# asserts the whole contract: restore on move, restore on end, no undo step, no
# dirty flag, a save during a preview writes the ORIGINAL, a locked node
# refuses, an unresolvable payload refuses, and a preset apply reuses its one
# project row instead of minting a new one per apply.
jah_pool_arm(shading material_preview ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_material_preview.js 9)
# GF1: the DEFAULT GROUND in pixels — matte at a grazing angle, and no edge from
# a high oblique view (the two things the owner reported while testing push #18).
# The document half of the floor is scripting.e2e.default_floor, which is
# headless; this one needs a camera.
jah_pool_arm(world_sky default_ground ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_default_ground.js 36)
# ATOM S3-DRAW: world.atomStatus() on the default scene (the floor through the id
# pass and the decode, its horizon plane on PBS as a backdrop) and world.setAtomDraw,
# the split's measuring door — the editor's shot agrees with the split on and off.
jah_pool_arm(atom atom_draw ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_atom_draw.js 4)
# CUBE-SHADE-1: the two things the owner saw on a cube (review R8) — a flat
# face shaded as two triangles (fork 8f09c0cd4 (was 0083), the diffuse cone basis) and the
# dark climb up its sides (the AO radius, now a contact scale). Needs a camera
# and the whole post chain, so it is an engine-up e2e and not a headless one.
# a one-script row's old 180 s was not enough for it: four 2 m cubes at Epic mean
# four GI re-solves (one per turn of the first cube) and seven settled shots.
# 71 s solo on the rig; 400 s is the gate-load headroom.
jah_pool_arm(shading_live cube_shade ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_cube_shade.js 49)
# LATTICE-1: the picture does not know where the voxel lattice sits. Five arms,
# each one standing the whole Photon chain somewhere else (up to eight cascade-0
# cells off the camera) while the scene and the camera never move, and each one
# checking that it really did — a cascade that re-centred on the way back
# measures nothing and is skipped by name. Engine-up, four GI chain builds per
# arm; ~110 s solo on the rig, 400 s is the gate-load headroom.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-lattice_placement)
file(MAKE_DIRECTORY ${_home})
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(scripting.e2e.lattice_placement CLASS app
         COMMAND ${JAH_FRESH_HOME} --warm ${_home} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_lattice_placement.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.lattice_placement PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 400)
# HLMS_ADOPTION_SPEC P1 (§3.5): the five cheap PBS knobs — clear coat + coat
# roughness, the curated BRDF picker, receive-shadows, emissive-as-lightmap —
# plus the generic enum row they forced (alphaMode rides it now too). Asserts
# the DEFAULTS are the renderer's own, which is the byte-identical-pixels
# argument expressed as a test.
jah_pool_arm(shading pbs_knobs ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_pbs_knobs.js 7)
# HLMS_ADOPTION_SPEC P4a (§6.5): the UNLIT shading model — the renderer's other
# material family, reached through the same declared-row/material.set path
# everything else uses. Asserts it reaches PIXELS (an unlit surface renders its
# authored colour, a lit one does not), that the switch is one undo step, and
# that every value the family cannot use survives the round trip.
jah_pool_arm(shading shading_model ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_shading_model.js 15)
# MATERIAL_GAPS_SPEC GAP 1: the per-material SPECULAR / FRESNEL WORKFLOW. The
# I-1 fence lives here — metalness and F0 are the SAME float in the renderer's
# datablock and our engine is built with NDEBUG, so nothing but a dumpDatablock
# assertion notices a push that writes both. Also asserts Metallic is still the
# default (the renderer's own default is Specular; ours is what every existing
# material has), the enum accepts its vocabulary BY NAME, and the workflow
# reaches pixels.
jah_pool_arm(shading workflows ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_workflows.js 12)
# MATERIAL_GAPS_SPEC GAP 1 ACCEPTANCE (§2.7): Ogre's own PbsMaterials 8x8
# roughness x fresnel grid, rebuilt FROM OUR DOCUMENT MODEL through the verbs.
# Deterministic by construction — one directional light, no sky, GI off, raw
# (post-chain-free) readbacks and a FIXED camera — and asserted as monotonic
# relations inside one frame plus a metallic-workflow CONTROL, never as absolute
# colours.
jah_pool_arm(shading workflow_grid ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_workflow_grid.js 4)

# MATERIALS_EVALUATOR_SPEC: graph verbs -> bakeInfo/bake -> BakedMaps PNGs ->
# toMaterial/save/regenerate; baked maps land inside the project folder (and
# so ride the recursive .jaf zip).
jah_pool_arm(shading bake_pipeline ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_bake_pipeline.js 5)
# HLMS_ADOPTION P5: a GENERATED SHADER PIECE end to end — the emitter's verdict
# through graph.emitInfo, the content-addressed cache file, the piece riding a
# real scene material to the renderer, and the save/close/open round trip that
# proves re-emission is deterministic.
jah_pool_arm(shading custom_piece ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_custom_piece.js 4)
# EDITOR_SHORTCUTS_SPEC §5: the verbs behind the Unreal-style editor controls.
jah_pool_arm(editor_view editor_controls ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_editor_controls.js 30)
# scripting.e2e.editor_controls.timing — the per-selection 11 ms frame bar, nightly (lane
# D6B-GATE-SHAPE; audit §5 F17 FIX-TIME): a generated copy of the script with the bar armed
# (jah_timing_script), its own home, quiet box, the GPU-timing lock.
set(_ect_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-editor_controls-timing)
file(MAKE_DIRECTORY ${_ect_home}/run)
jah_timing_script(${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_editor_controls.js
                  ${CMAKE_CURRENT_BINARY_DIR}/e2e_editor_controls.timing.js)
jah_gpu_exclusive_test(NAME scripting.e2e.editor_controls.timing CLASS app RUN_TIMEOUT 180
    COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_editor_controls.timing.js
    WORKING_DIRECTORY ${_ect_home}/run)
set_tests_properties(scripting.e2e.editor_controls.timing PROPERTIES
    ENVIRONMENT "HOME=${_ect_home};JAHSHAKA_DATA_ROOT=${_ect_home}/.local/share/Jahshaka"
    LABELS "scripting;nightly;quiet-box")
# smoke S15: every rotation ring clickable from every camera, asked through
# editor.gizmoHitTest in the REAL viewport (gizmo.ring_pick covers the geometry
# document-side).
jah_pool_arm(editor_view gizmo_rings ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_gizmo_rings.js 13)
# DESKTOP_SLIDER_SPEC: desktop.* view-mode + filmstrip verbs (window verbs).
jah_pool_arm(assets_import desktops ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_desktops.js 40)
# PLAY-FLY-1: the play-in-place FLY, driven by a held key through the viewport's
# own key path (editor.key) — the camera must move monotonically along the key's
# axis over 30 frames. Engine up: play runs in the editor viewport.
jah_pool_arm(player play_fly ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_play_fly.js 4)
# STATS_OVERLAY_SPEC phase 1 — the DATA half of the engine-drawn stats overlay,
# landed and tested before a pixel of it exists (the API-first rule). Pins the
# shape of app.renderStats(), the LAZY geometry counters, the invariants between
# the timing fields, and the driver's new work-per-tick numbers — the honest
# performance figure this architecture has, which the render loop used to
# measure and throw away.
jah_pool_arm(editor_view render_stats ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_render_stats.js 8)
# PLANAR_REFLECTIONS_SPEC §8: the reflector flag, the budget/resolution/shadow
# settings, the World Mode row driving them, and the save/reopen round trip.
jah_pool_arm(world_sky planar ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_planar.js 12)
# LIGHTING CHANNELS (light masks): node.setLightMask / node.lightMask, the
# reflected `lightMask` row and its signed spelling, and the save/reopen round
# trip in both directions (a masked node keeps its channels; an unmasked one
# comes back on ALL of them, which is what an absent key has to mean). The pixel
# half — that the channels actually filter light, and that shadows do not follow
# them — is the lights.masks engine suite.
jah_pool_arm(world_light light_masks ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_light_masks.js 15)
# REFLECTIONS_ADOPTION_SPEC §3: world.giStatus() — the ACHIEVED reading of
# global illumination (probe count, and whether the probe/VCT bindings are live
# on the PBR shader). The API-first half of the PCC gate; the pixel half is the
# gi.pcc_mirror engine suite. Engine UP, not --headless: the verb's entire value
# is that it measures the renderer rather than echoing the document.
jah_pool_arm(gi_verbs gi_status ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_gi_status.js 20)
# gi.gather_default (PHOTON-GATHER-1d): the screen-probe gather is the diffuse BY
# TIER — Low off, Medium 36 rays, High 64, Epic a probe per 8 px — through the
# app's own path, and at High the default ground's picture is the gather's (the
# world.gi({gather:false}) A/B moves it). Its own ctest name in the gi family
# (the brief's), the scripting harness's home and data root.
jah_pool_arm(gi_verbs gather_default ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_gather_default.js 17)
# THE SURPRISE MOVER (REALTIME_REFLECTIONS_SPEC §3.3.3, owner decision O3): an
# object nobody marked as moving starts moving during PLAY. It moves smoothly,
# is treated as movable from that frame, leaves its old bounce light behind
# until play stops, and the author is warned ONCE by name — and, the claim that
# matters, world.giStatus().rebuilds does not move while it happens (option B
# was a half-second freeze at that exact moment). Engine UP, not --headless:
# editor.mirrorStats() and world.giStatus() are measurements of the renderer.
jah_pool_arm(gi_movers mobility_play ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_mobility_play.js 11)

# WHAT THE MIRROR LOOKS AT, THROUGH THE REAL APP
# (SPECS/DIRTY_SET_MIRROR_SPEC.md, owner option A). The C++ suites prove the two
# halves in isolation — document.dirty_list that the change list behaves,
# mirror.dirty_equals_full that what it carries is ENOUGH. This one proves it in
# the shipped path: the editor viewport's own mirror, driven by the same verbs a
# user's actions go through. A still scene visits ZERO objects; one move is a
# list of one; hiding a group is its subtree (the F6 contract); a material edit
# reaches the object that draws it without that object having changed; and the
# always-on verifier finds NOTHING behind any of it.
jah_pool_arm(gi_movers dirty_mirror ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_dirty_mirror.js 9)

# A MOVABLE LAMP THAT STOPS MOVING GETS ITS BOUNCES BACK (round-2 review F1 of
# lane ENGINE-4). The engine's light-only injection is deliberately cheap while
# something moves (one bounce, coarse march) and must be the full count on the
# frame the motion ends — and for a MOVABLE lamp the mirror is the only thing
# that can say when that is, because this path arms no settle at all. Engine UP:
# the assertion is a picture.
jah_pool_arm(gi_movers movable_lamp_rest ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_movable_lamp_rest.js 13)

# THE LAMP-MAP CACHE UNDER REFLECTION PROBES, ON A COLD SHADER CACHE
# (ENGINE_CACHE_POLICY_SPEC P2/P4, fork 6130df9d1 (was 0025)). The home is WIPED before
# the run — that is the point of the case: the defect it guards only shows while
# shaders are being generated and compiled for the first time, and it took the
# process down through the shader-cache save when it did. The script asserts the
# engine's own self-check (world.shadowStatus().shaderLightMismatches).
set(_scHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-shadow_cache_probes)
jah_fresh_home_fixture(scripting.e2e.shadow_cache_probes.fresh_home shadowCacheProbesFreshHome "${_scHome}" LABELS "scripting")
jah_gpu_row(scripting.e2e.shadow_cache_probes CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_shadow_cache_probes.js
         WORKING_DIRECTORY ${_scHome}/run)
set_tests_properties(scripting.e2e.shadow_cache_probes PROPERTIES
    ENVIRONMENT "HOME=${_scHome};JAHSHAKA_DATA_ROOT=${_scHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED shadowCacheProbesFreshHome
    LABELS "scripting" TIMEOUT 240)
# THE LIT VOLUME (REFLECTIONS_ADOPTION_SPEC P1a/P1d, re-scoped by owner decision
# D8 2026-09-13): the AUTOMATIC fit and the volumes world.giStatus() reports —
# the only surface left, now that the bounds rows, the Fit button and
# world.fitGiBounds are deleted — plus the per-node exclude flag, the three
# retired keys refused by name, and world.refreshGi. Engine UP for the same
# reason as gi_status: the interesting numbers are the renderer's.
jah_pool_arm(gi_verbs gi_bounds ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_gi_bounds.js 7)
# gi_verbs.probe_nan_view — PROBE-NAN-1 (D4-PHOTON-TIERS): the Probes view under a lamp of
# intensity 20 at 0.3 m is lit and no darker than at 2 (it drew every probe black; gi.probe_nan
# holds the field's own half).
jah_pool_arm(gi_verbs probe_nan_view ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_probe_nan_view.js 20)
# gi_verbs.contact_band — SSAO-DOUBLE-1: no second occlusion over the GI. A cube on the default and
# on a white floor at Epic and High: the contact band within 2 codes of the mid-face control (was
# -11 / -13 under the SSAO multiply); with Photon off the pinned SSAO still darkens it (> 5 codes).
jah_pool_arm(gi_verbs contact_band ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_contact_band.js 95)
# gi_verbs.ssr_off_ray_tier — D4-PHOTON-TIERS fix round (F2/F3): a document's SSR OFF is honoured
# at a ray tier (no trace runs), and the row offers exactly "Off" and a disabled "Traced" there.
jah_pool_arm(gi_verbs ssr_off_ray_tier ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_ssr_off_ray_tier.js 15)
# gi_verbs.offscreen_contract — D4-PHOTON-TIERS §2.2: the declared offscreen picture table
# (world.tierTable().offscreen) held against the renderer on Showroom 2 at High — the scene-grade
# screenshot runs the tier's gather and traced reflections, the plain grade (declared lower)
# runs neither. A pixel claim, in the Epic pool.
set(JAH_OFFSCREEN_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
configure_file(scripts/e2e_offscreen_contract.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_offscreen_contract.js @ONLY)
jah_pool_arm(gi_verbs offscreen_contract ${CMAKE_CURRENT_BINARY_DIR}/e2e_offscreen_contract.js 60)
# gi_verbs.pcc_budget — PCC-BUDGET-1: the probe grid under the tier's WHOLE-grid VRAM budget on
# Showroom 2 with the rays off (Epic keeps the measured 32, High cuts to its budget's 19), the
# total asserted by the renderer's arithmetic and by the texture manager.
set(JAH_PCC_BUDGET_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
configure_file(scripts/e2e_pcc_budget.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_pcc_budget.js @ONLY)
jah_pool_arm(gi_verbs pcc_budget ${CMAKE_CURRENT_BINARY_DIR}/e2e_pcc_budget.js 60)
# gi_verbs.planar_records — PLANAR-RECORDS-1: the hit decode's list on the Mirror Room at Epic
# and on a planar mirror floor under 30 movers — no record dropped, the counts printed.
set(JAH_PLANAR_RECORDS_MIRROR "${CMAKE_SOURCE_DIR}/scenes/Mirror Room.zip")
configure_file(scripts/e2e_planar_records.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_planar_records.js @ONLY)
jah_pool_arm(gi_verbs planar_records ${CMAKE_CURRENT_BINARY_DIR}/e2e_planar_records.js 40)
# ENGINE_CACHE_POLICY_SPEC P10: a page return (editor -> assets -> editor, and
# editor -> player -> editor) never re-pushes or rebuilds GI — giPushes,
# giStatus().rebuilds and the whole giStatus reading are unchanged, and the
# editor's passes still bind its own arms. Engine + real pages -> needs a display.
jah_pool_arm(gi_verbs page_gi ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_page_gi.js 9)
# GI_UNIFIED_SPEC.md §4 P1: the DDGI verb surface — world.gi's {ddgi} (the
# intensity dial is gone and refused) and the four ifd* readings on world.giStatus(). Engine UP for
# the same reason as the two above: "asked for a field" and "the shader is
# sampling one" are different readings, and only the renderer knows the second.
# The pixel half is the gi.ddgi engine suite.
jah_pool_arm(gi_verbs gi_ddgi ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_gi_ddgi.js 5)
# GATHER-1a: the SCREEN-PROBE GATHER's row — `world.gi({gather})`, its undo step
# and `world.giStatus().gather`. Engine UP for the same reason as gi_ddgi: "the
# project asked for a gather" and "this machine is running one" are different
# readings and only the renderer knows the second. The pixel half is the
# gi.gather engine suite.
jah_pool_arm(gi_verbs gather_row ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_gather_row.js 1)
# PHOTON-M3 (fork ae2ed529f+155a56bf8 (was 0080)): the voxel TOTAL volume is a float that holds the
# bounce's fixed point — the direct term bounded by the ceiling, the bounced
# total above it in a real set of voxels. Read through world.giVoxelStats: a
# clipped voxel draws a merely dimmer picture, so only the volume can say it.
jah_pool_arm(gi_verbs gi_voxel_store ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_gi_voxel_store.js 5)
# PHOTON_SPEC §7 E2 (9): the irradiance field rides CASCADE 0 at EVERY TIER.
# Engine UP for the same reason as gi_ddgi — "the tier says on" and "the shader
# is sampling a field whose volume is cascade 0's box" are different readings —
# and in the scripting tier rather than as an engine suite because the tier
# table is the Studio registry's and a second copy of it is the defect this
# lane deleted five of.
jah_pool_arm(gi_verbs field_every_tier ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_field_every_tier.js 20)
# PARTICLES_FX2_SPEC §11 phase 2: the emitter verbs. scene.addParticles with a
# recipe, every scalar row through the shared node.setProperty, the two over-life
# ramps (which no Property kind can carry), the scene clock, the save round trip
# for keys that are all optional in the reader — and pixels, because a perfect
# document proves nothing about what the engine draws.
jah_pool_arm(shading_live particles ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_particles.js 11)   # shading_live, an Epic pool (TEST-TIER-1): pixel probes of the plume, framed by the viewport's aspect — red at the Low test tier (9/25 after editor_view's window-changing arms, 11 alone)
# DEEP_AUDIT_2026_09 List B #7: the anim.* verbs — the keyframe/timeline domain,
# which had no verbs and therefore no test at all. Animations on a node, the
# active one, tracks per property, keys (write/overwrite/read/remove), the
# document evaluation (seek poses the node between the keys), looping, and the
# save/close/open round trip. Every verb is a document verb -> --headless.
# Riders lane R4: app.memoryStats / app.reclaimMemory (app.profiling went with
# chain::PassProfiler, lane MON-P1a). The
# NULL render system has a VaoManager and scene managers, and the document's
# nodes live in the engine's staging manager, so the verbs are real headless
# (shape + counts; magnitudes are the box's, never pinned) -> --headless.
jah_pool_arm(doc memory ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_memory.js 2 HEADLESS FIRST) # FIRST: its burst reads the engine node pools' slots, which carry the process's history (a hole left by an earlier arm is refilled, not counted)

jah_pool_arm(doc anim ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_anim.js 1 HEADLESS)

# The viewport's presentation state machine (editor.viewportState) — what
# decides whether the editor viewport shows engine frames or its loading cover.
# Gates the desktop-bleed fix, 2026-09-03.
#
# RENAMED from viewport_cover when the Qt cover widget was deleted (owner
# decision D2, SPECS/STATS_OVERLAY_SPEC.md §6): this suite never tested the
# widget, it tests the STATE MACHINE that drives it, which is exactly why its
# assertions did not change when the cover moved into the engine. That makes it
# the regression gate for the port.
jah_pool_arm(editor_view presentation_state ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_presentation_state.js 15)
# The IBL-cubemap-swap crash guard (2026-09-03): a sky re-bake frees and
# re-allocates the environment cubemap — usually AT THE SAME ADDRESS — while
# metallic datablocks, planar reflection passes and offscreen post-fx shots are
# all binding descriptor sets built from that pointer. The script's own header
# says why each ingredient is there; surviving it is the assertion.
# VISUAL_PARITY re-audit F5: sun/sky-light coupling — world.sunLight links a
# directional light to the realistic sky's sun, the light follows the dials,
# unlinking gives manual control back, and the link is a document field.
jah_pool_arm(world_light sun_light ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_sun_light.js 45)
# THE 2D CLOUD LAYER (CLOUDS-2D-1): the world.clouds block (defaults, clamps,
# refusals, undo, save/reopen), coverage 0 byte-identical to no layer, the
# environment SH moving with the coverage, the ground shadow against the sun's
# measured direct term, the scroll and its capture cadence, an image sky
# ignoring the block, and the export bake.
jah_pool_arm(world_sky clouds_2d ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_clouds_2d.js 42)
# THE EXPONENTIAL HEIGHT FOG (SKY-DEFAULTS-1): the world.heightFog block
# (the Basic template's defaults in Unreal's units, clamps, refusals, one undo
# step, save/reopen), the start distance leaving a near surface byte-identical,
# density and base height moving the far surface and the sky. The default look
# it gives a new scene is scripting.e2e.new_scene's.
jah_pool_arm(world_sky height_fog ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_height_fog.js 16)
# HARD SUN CONTACT SHADOWS (PHOTON-RAYS-1): world.sunContact's contract — off by
# default, validation, one undo step, the file round trip, and what the renderer
# did (gi.sun_contact is the picture).
jah_pool_arm(world_light sun_contact ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_sun_contact.js 16)
# A WORLD ROW SHOWS WHAT A VERB WROTE (SMALL-FIXES-3): every World section that
# shows a sceneprops key (or a World Mode registry row) re-reads it on a
# changed external write — one verb, then the row read with no gesture between.
jah_pool_arm(world_sky world_rows_follow ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_world_rows_follow.js 3)
# SUN_AND_LIGHT_DEFAULTS_SPEC Q1b/Q1c: THE SCENE-ERROR AREA. The general
# facility (raise / never repeat / dismiss / clear / scan) and its two first
# customers — two directional lights fighting to be the sun, and a light whose
# shadows are off standing over something it lights straight through. Plus the
# case that is deliberately NOT an issue: a scene with no directional light.
jah_pool_arm(editor_view scene_issues ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_scene_issues.js 11)
# TEXTURE-FAIL-ISSUE-1 (SMALL-FIXES-3): a material's texture file deleted on
# disk under a running editor is a `texture.missing` scene issue — raised
# within two scans, one per object, cleared when the file returns. A harness,
# not a --script run: the file has to vanish from OUTSIDE the app (no verb
# deletes files), so the app runs under MCP and this process removes and
# restores it (tests/support/mcpharness.h, the ui.shot_aspect shape).
add_executable(test_texture_missing test_texture_missing.cpp)
set_target_properties(test_texture_missing PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_texture_missing PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
target_include_directories(test_texture_missing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_texture_missing PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
set(_texmiss_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-texture_missing)
file(MAKE_DIRECTORY ${_texmiss_home}/run)
jah_gpu_row(scripting.e2e.texture_missing CLASS app COMMAND test_texture_missing
         WORKING_DIRECTORY ${_texmiss_home}/run)
set_tests_properties(scripting.e2e.texture_missing PROPERTIES
    ENVIRONMENT "HOME=${_texmiss_home};JAHSHAKA_DATA_ROOT=${_texmiss_home}/.local/share/Jahshaka"
    TIMEOUT 300 LABELS "scripting")
jah_tsan_blocked(scripting.e2e.texture_missing)   # spawns the app
jah_lsan_blocked(scripting.e2e.texture_missing)   # spawns the app
# THE PROJECT'S VR SETTINGS (lane VR-WORLD-1, owner request 2026-09-18) — the
# World panel's VR section as a verb: `world.vr` reads and writes six document
# fields (the fly speed in m/s, the fly direction, the turn and its step or
# rate, the dominant hand), refuses what is not a setting, clamps what is merely
# out of range, records ONE undo step per call and survives save/close/open
# through the real writer and reader. It also pins THE SESSION RULE from the
# document's side: `vr.locomotion` overrides these for one session and writes
# nothing here. The rig actually moving is `vr.input_session`; the panel is
# `ui.vr_panel`.
jah_pool_arm(world_sky world_vr ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_world_vr.js 10)   # not vr_noruntime: its session override has no release without a session, and the VR arms read the defaults
# THE SCREEN-SPACE MARCH'S PHASE RULE (SSR-RINGS-1) — the document half of the
# knob the owner picks from: three spellings, the refusals, one undo step per
# call, save/close/open, and the two properties that make it safe to ship
# unchosen (the default is the shipped march, and no World Mode touches it).
# The pixels are `ssr.rings`.
jah_pool_arm(gi_movers ssr_march ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_ssr_march.js 6)
# A DRAGGED STILL ON THE MOVER CHANNEL (MOVER-1) — the document half: the key,
# its refusals, one undo step per call, save/close/open, the two giStatus
# readings, and the two properties that make it safe to ship unchosen (the
# default is today's behaviour and no World Mode touches it). The counters and
# the pixels are gi.drag_mover.
jah_pool_arm(gi_movers drag_mover ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_drag_mover.js 7)
# vr.* WITH NO RUNTIME (SPECS/VR_SPEC.md §6) — the cheap case, and the one that
# runs on every box in every gate: this app is launched WITHOUT --vr, so no
# OpenXR loader is opened at all, and every verb must still answer with a
# refusal in words instead of throwing, hanging or crashing. The session half
# lives in tests/vr (vr.verbs_session), where the simulated runtime is.
jah_pool_arm(vr_noruntime vr_verbs ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_vr_verbs.js 3)
# EVERY VR GESTURE, WITH NO HEADSET (SPECS/VR_INPUT_SPEC.md stage 1 §10) — the
# ray, the document pick, select/toggle/deselect, the near and far rigid grabs,
# the stick along the ray, snapping under the menu button, one undo macro per
# gesture, a cancelled gesture pushing nothing, the dominant-hand swap and the
# locomotion refusals. It runs on the injection route (`vr.inject` writes the
# hands into the ENGINE's own store, `vr.step()` runs the frame), which is what
# makes all of it gateable on a box with no runtime at all; the arithmetic under
# it is `vr.grab_maths` and the rig half is `vr.input_session`.
jah_pool_arm(vr_noruntime vr_input_headless ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_vr_input_headless.js 5)
jah_pool_arm(vr_noruntime vr_hands ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_vr_hands_headless.js 1)
# BARE HANDS, WITH NO HEADSET AND NO HANDS (VR_INPUT_SPEC §7, phase 4b stage
# 3) — the same injection route, and the rules only a HAND can exercise: the
# profile is per hand, a grab follows the PINCH POINT rather than the palm
# (asserted by moving the two independently, which no controller can do),
# arm's reach is measured from the hand instead of along the aim ray, the
# 26-joint skeleton round-trips through `vr.inject`/`vr.handJoints`, and a
# hand RE-BOUND mid-gesture (a wearer picking a controller up) cancels that
# gesture and records nothing. The pinch hysteresis is `vr.grab_maths`, the
# DRAWING is `mirror.vr_proxies`, and the bindings parsing against a real
# runtime is `vr.input_session`.
# THE EDITOR'S GIZMO IN THE WEARER'S HANDS (VR_INPUT_SPEC §5.2, phase 4b stage
# 2) — the same injection route, and the same claim: the gizmo a controller
# drags is the gizmo the mouse drags. A press on a handle drags it and leaves
# the selection alone, a press off it selects; `menu` held snaps; a SHORT press
# of `menu` cycles the mode through editor.setGizmoMode; every drag is ONE undo
# entry and a cancelled one is none. The pick geometry under it is
# gizmo.ring_pick's and gizmo.plane_handles' ray arms, and the size rule is
# gizmo.screen_size's — all three document-only.
jah_pool_arm(vr_noruntime vr_gizmo ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_vr_gizmo.js 1)
# THE TELEPORT ARC, WITH NO HEADSET (VR_INPUT_SPEC §6 row L4; the owner's answer
# 8) — the same injection route, and the whole gesture: the dominant stick
# forward arms a thrown arc, the document's picker decides what it lands on, a
# wall and a locked object are refused in words, a raised platform is a floor,
# `menu` and a back-flick cancel, a held object owns that stick, a focus loss
# puts the arc away, and every piece of the curve is a line node in the world.
# The arithmetic under it (the parabola, the plane solve, the 45 degree slope
# rule, the rig that arrives level at the wearer's own height) is
# `vr.grab_maths`; the wearer ACTUALLY moving needs a rig and is
# `vr.input_session` on Monado.
jah_pool_arm(vr_noruntime vr_teleport ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_vr_teleport.js 1)
# THE PLAYER'S VR MODE WITH NO RUNTIME (SPECS/VR_SPEC.md §4.5, phase 3) — the
# other everywhere case, and the owner's rule in executable form: without a
# headset the tool is today's tool. `player.play({vr:true})` must refuse and
# leave the scene STOPPED, the toggle must refuse without switching the page
# (switchSpace(PLAYER) starts the scene), and the plain player must be exactly
# what it has always been. The session half is `vr.player_session`.
jah_pool_arm(vr_noruntime player_vr ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_player_vr.js 1)
jah_pool_arm(world_sky sky_ibl_churn ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_sky_ibl_churn.js 20)
# THE SKY'S RESOURCE NAMES (lane ENGINE-SMALL-C, SKYARRAY-LEAK-1): sixty sky
# changes must not burn sixty texture NAMES, because a dead name in the sky
# material's texture unit resolves against the resource groups and leaves the
# layer blank ("Cannot locate resource skyarray_NN"). Reads the live names
# through app.textureMemory(); the file header carries the mechanism.
jah_pool_arm(world_sky sky_name_recycle ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_sky_name_recycle.js 8)
# THE ENVIRONMENT LANDS AS ONE SET (PHOTON-SKY-TRANSIENT-1): the first frame
# after a sky power change, a sun move, a sky type change either way and the
# cloud layer on/off equals the frame after it within 2/255 — the change frame
# used to sample a reflection cube nothing had written, with the SH one host
# push behind it. Engine UP (a picture); the home is WIPED first, so no cached
# shader or face stands between a change and its first frame. ~10 s solo.
set(_sstHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-sky_swap_transient)
jah_fresh_home_fixture(scripting.e2e.sky_swap_transient.fresh_home skySwapTransientFreshHome "${_sstHome}" LABELS "scripting")
jah_gpu_row(scripting.e2e.sky_swap_transient CLASS app
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
configure_file(scripts/e2e_full_surface.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_full_surface.js @ONLY)
jah_pool_arm(editor_view full_surface ${CMAKE_CURRENT_BINARY_DIR}/e2e_full_surface.js 6)   # editor_view, an Epic pool (TEST-TIER-1): asserts a new scene's Epic rows (a 2048 shadow atlas) — red at the Low test tier

# Asset drawers (ASSET_DRAWERS_SPEC.md §4/§5): drawer verbs round-trip +
# importFile(png/wav) creating first-class AssetsView rows. Every verb is a
# document verb, so this runs --headless (offscreen platform, no engine) —
# fixture paths are configured in.
configure_file(scripts/e2e_drawers.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_drawers.js @ONLY)
configure_file(scripts/e2e_metadata.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_metadata.js @ONLY)
jah_pool_arm(doc drawers ${CMAKE_CURRENT_BINARY_DIR}/e2e_drawers.js 1 HEADLESS)

# Rich asset metadata (ASSET_DRAWERS_SPEC addendum): import-time "metadata"
# blocks + the assets.metadata verb (with its lazy backfill). Image/audio/mesh
# imports here are all document-side, so this runs --headless too.
# Library asset deletion (the 2026-09-03 silent-no-op defect): assets.remove
# must report the database's real answer, and the row must actually go.
# Document-only, so --headless; fixture paths configured in.
configure_file(scripts/e2e_asset_delete.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_asset_delete.js @ONLY)
jah_pool_arm(doc_assets asset_delete ${CMAKE_CURRENT_BINARY_DIR}/e2e_asset_delete.js 1 HEADLESS)

# AI_SURFACE_AUDIT F1: scene.addMesh was a DATA-LOSS verb (disk path written
# where the reader expects a guid) with zero tests. It now refuses with the
# correct flow in the message, and the correct flow's save/close/reopen round
# trip — the test that never existed — is asserted here. Document verbs only.
configure_file(scripts/e2e_mesh_roundtrip.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_mesh_roundtrip.js @ONLY)
jah_pool_arm(doc_assets mesh_roundtrip ${CMAKE_CURRENT_BINARY_DIR}/e2e_mesh_roundtrip.js 1 HEADLESS)

# AI_SURFACE_PROGRAM_SPEC lane D #7 + #12: assets.importAndPlace — the correct
# import in ONE call — and, where #12 used to gate scene.addViewer, the REMOVAL
# of that verb and the reader's tolerance for an old file that still carries a
# `viewer` node (AVATAR_LOCOMOTION_SPEC Stage 0). The round trip scene.addMesh never
# had (place -> save -> close -> reopen -> the geometry identity still
# resolves) is the reason this suite exists. Document verbs only -> --headless;
# fixture paths configured in.
configure_file(scripts/e2e_world_reach.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_world_reach.js @ONLY)
jah_pool_arm(doc world_reach ${CMAKE_CURRENT_BINARY_DIR}/e2e_world_reach.js 1 HEADLESS)

# node.exportArchive (STUDIO-CRUD-1 item 9): the verb behind the outliner's
# Export Object / Export Particle System. Document verbs only -> --headless.
configure_file(scripts/e2e_node_export.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_node_export.js @ONLY)
jah_pool_arm(doc_assets node_export ${CMAKE_CURRENT_BINARY_DIR}/e2e_node_export.js 1 HEADLESS)

# AI_SURFACE_PROGRAM_SPEC lane D #14: node.physics / node.physicsInfo — the last
# UI-with-no-verb domain. Every scalar and both enums round-trip, every refusal
# is loud, the values survive a reopen, AND the settings reach Bullet (two
# identical spheres, one made dynamic and one static by the verb, simulated in
# the same world — one falls, one does not). ENGINE UP, not --headless: the
# physics stepper lives in the engine viewport's syncFrame and the headless
# stand-in's startPhysicsSimulation is an empty override.
jah_pool_arm(editor physics ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_physics.js 7)

# AI_SURFACE_AUDIT F7/F8/F9 — the three SILENT SUCCESSES. A verb that accepts a
# value it does not understand, changes nothing and answers true is the worst
# defect class on an agent surface. Document verbs only.
configure_file(scripts/e2e_ai_surface.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_ai_surface.js @ONLY)
jah_pool_arm(doc ai_surface ${CMAKE_CURRENT_BINARY_DIR}/e2e_ai_surface.js 1 HEADLESS)

# AI_SURFACE_PROGRAM_SPEC lane B #8 (verb half): scene.nodes({subtree, depth,
# include}). describe_scene is only the byte-carrying view of these options, so
# the bounding (childCount/truncated) and the enrichment blocks are gated here,
# on the verb. Document verbs only -> --headless.
jah_pool_arm(doc scene_query ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_scene_query.js 1 HEADLESS)

# AI_SURFACE_PROGRAM_SPEC lane 0 + lane A — DISCOVERY. app.apiProblems() runs
# ApiRegistry::validate() over the real module set (the unit test only ever saw
# its own fake module), and the lane A verbs that stop an agent guessing key
# names: node.properties, material.properties, the world noun verbs (the set*
# aliases are gone), the Ground primitive and addPrimitive {count}. Document verbs
# only -> --headless.
jah_pool_arm(doc introspection ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_introspection.js 1 HEADLESS)

# Web export (WEB_EXPORT_AUDIT): project.exportWeb through the real binary.
# Document-only, so --headless.
jah_pool_arm(doc_assets export_web ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_export_web.js 1 HEADLESS)

# Unified export front half (ASSET_PIPELINE_SPEC phase 5): assets.exportRaw +
# project.exportManifest through the real binary. Document-only, --headless;
# fixture paths configured in.
configure_file(scripts/e2e_export_raw.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_export_raw.js @ONLY)
jah_pool_arm(doc_assets export_raw ${CMAKE_CURRENT_BINARY_DIR}/e2e_export_raw.js 1 HEADLESS)

# Light asset bindings (LIGHTS_COMPLETION_SPEC): the .ies library type
# (import/validate/metadata), node.setLightProfile / node.setLightTexture, the
# renderer limitations reported honestly by the `applies` flags, and the D4
# boundary (a texture bound to a light is a DEPENDENCY — no companion
# material). Every verb is a document verb, so --headless.
configure_file(scripts/e2e_light_bindings.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_light_bindings.js @ONLY)
jah_pool_arm(doc light_bindings ${CMAKE_CURRENT_BINARY_DIR}/e2e_light_bindings.js 1 HEADLESS)

jah_pool_arm(doc metadata ${CMAKE_CURRENT_BINARY_DIR}/e2e_metadata.js 1 HEADLESS)

# Video assets (ASSET_MEDIA_SPEC §1): importFile(mp4) → Video row + frame-grab
# thumbnail + probed metadata block. Qt Multimedia's ffmpeg backend decodes
# with no display (verified under QT_QPA_PLATFORM=offscreen), so this runs
# --headless like the drawers/metadata suites.
configure_file(scripts/e2e_video.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_video.js @ONLY)
jah_pool_arm(doc_assets video ${CMAKE_CURRENT_BINARY_DIR}/e2e_video.js 2 HEADLESS)

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
configure_file(scripts/e2e_store_root.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_store_root.js @ONLY)
# The home is WIPED before every run: a crashed run cannot reach the script's
# own exit reset, and would leave a moved store + a stale assets/storeRoot here.
jah_fresh_home_fixture(scripting.e2e.store_root.fresh_home storeRootFreshHome "${_home}" ARGS --mkdir "${STORE_EMPTY_TARGET}" LABELS "scripting")
add_test(NAME scripting.e2e.store_root
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_store_root.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.store_root PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED storeRootFreshHome
    LABELS "scripting"
    TIMEOUT 120)

# The shipped sample scenes are gated by samples.cleanstart.* — one row per
# sample, each on a VIRGIN home. scripting.e2e.samples used to re-import five of
# them into one shared home to probe their colours; its probes, its material
# assertions and its Particles save round trip live in
# tests/samples/scripts/cleanstart.js.in since 2026-09-09 (TEST_GATE_AUDIT.md §2).

# The ONE import pipeline (phase 3): determinism record + checkConsistency +
# identical object sets on double import, in the real binary.
configure_file(scripts/e2e_import_pipeline.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_pipeline.js @ONLY)
jah_pool_arm(doc_assets import_pipeline ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_pipeline.js 1 HEADLESS)

# IMAGE_PLANE_SPEC option A: scene.addImagePlane — aspect-correct plane
# (long side 1 m), camera-facing at creation, image as baseColorMap
# (roughness 1 / metallic 0, alpha images blend), dependency row. Engine up
# (no --headless): the camera-facing assertion reads editor.camera(), an
# engine verb. Fixture paths configured in.
configure_file(scripts/e2e_image_plane.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_plane.js @ONLY)
jah_pool_arm(editor image_plane ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_plane.js 1)

# DECALS_SPEC: scene.addDecal — the projected-texture object, its box and
# material values, the image bound as a project BINDING (dependency row, no
# companion material), property round-trip, duplicate, undo and save/reopen.
# Document verbs only -> --headless; fixture paths configured in.
configure_file(scripts/e2e_decals.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_decals.js @ONLY)
jah_pool_arm(doc_assets decals ${CMAKE_CURRENT_BINARY_DIR}/e2e_decals.js 1 HEADLESS)

# IMAGE_PLANE_SPEC option B1: the companion image material — automatic on a
# DIRECT image add-to-project (owner call), never for dependency textures
# riding an object's closure, plus the manual materials.createFromImage verb;
# the material applies to a mesh and survives a reopen. Document verbs only
# -> --headless; fixture paths configured in.
configure_file(scripts/e2e_image_material.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_material.js @ONLY)
jah_pool_arm(doc_assets image_material ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_material.js 1 HEADLESS)

# MATERIAL_UV_NODES_SPEC phase 3: the UV input end to end — the fold (a
# uniform tiling rides the material, no map is baked, the source binds at full
# resolution), the resample route when two textures tile differently, and the
# two-number spellings material.set accepts. Document verbs only -> --headless;
# the pixel proof that the material's transform tiles/offsets/rotates the
# render belongs to tests/engine (pbr_texture_scale_tiles_uvs).
configure_file(scripts/e2e_uv_tiling.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_uv_tiling.js @ONLY)
jah_pool_arm(doc_assets uv_tiling ${CMAKE_CURRENT_BINARY_DIR}/e2e_uv_tiling.js 1 HEADLESS)

# IMAGE_PLANE_SPEC §6: pinned library assets hydrate on project REOPEN —
# add-to-project → close → reopen → material.apply by the pinned guid onto a
# cube → document material asserted. Document verbs only -> --headless;
# fixture paths configured in.
configure_file(scripts/e2e_pinned_reopen.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_pinned_reopen.js @ONLY)
jah_pool_arm(doc_assets pinned_reopen ${CMAKE_CURRENT_BINARY_DIR}/e2e_pinned_reopen.js 1 HEADLESS)

# Plan item 15c: SHIPPED ASSETS THROUGH THE CAS. The default ground's tile, the
# default particle image, a material preset's maps and a sky preset's faces are
# pinned library textures saved by guid (one row per library), and nothing
# copies a shipped file into a project folder or the working directory. The
# wrapper runs the document half (--headless), the rigged-model arm (a placed
# rig keeps its clips after its source file is gone — the one real consumer the
# deleted by-name lookup had) and then sweeps the disk.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-shipped_assets)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.shipped_assets
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/shipped_assets.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_shipped_assets.js
                 ${CMAKE_SOURCE_DIR}/tests/avatar/fixtures/rig2.glb
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.shipped_assets PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 120)

# scripting.e2e.perf_capture_frames — THE CAPTURE WINDOW IS COUNTED IN FRAMES
# (PERF-CAPTURE-SCRIPT-1, lane FENCE-1). Two halves like perf.capture_bundle:
# the app records, the shell half reads frames.jsonl and machine.json, because
# "exactly 60 records" is a line count and not a counter the app reports about
# itself. JAHSHAKA_PERF_ROOT: `out` is confined to the capture root, so the
# suite says where its own root is.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-perf_capture_frames)
file(MAKE_DIRECTORY ${_home}/run)
set(JAH_TEST_BUNDLE ${_home}/perf/bundle)
configure_file(scripts/e2e_perf_capture_frames.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_perf_capture_frames.js @ONLY)
jah_gpu_row(scripting.e2e.perf_capture_frames CLASS app
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/perf_capture_frames.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_BINARY_DIR}/e2e_perf_capture_frames.js
                 ${JAH_TEST_BUNDLE}
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.perf_capture_frames PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka;JAHSHAKA_PERF_ROOT=${_home}/perf"
    LABELS "scripting"
    TIMEOUT 180)

# Post-release fix (2026-08-31): an imported textured GLB RENDERS — texture
# properties resolve to store files (no raw guid reaches Texture2D::load),
# the Mesh member carries the model bytes (scene placement has geometry),
# and the preview pixel shows the bound map. Engine up (no --headless).
configure_file(scripts/e2e_import_render.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_render.js @ONLY)
jah_pool_arm(assets_import import_render ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_render.js 9)

# assets_import.node_archive_roundtrip — ARCHIVE-ROUNDTRIP (D7): node.exportArchive's file is
# self-contained — the source model, its members and its project removed and the store
# collected, the file re-imports as one object tile that renders its own map. Engine up.
configure_file(scripts/e2e_node_archive_roundtrip.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_node_archive_roundtrip.js @ONLY)
jah_pool_arm(assets_import node_archive_roundtrip ${CMAKE_CURRENT_BINARY_DIR}/e2e_node_archive_roundtrip.js 12)

# assets_import.bundle_export_{texture,material,pack} — JAF-EXPORTS-1: the Assets tray's exports
# are the share file (assets.exportBundle, one entry or a pack); each kind's file re-imports into
# an emptied library with its members, and a cube wearing the landed material samples the
# fixture's own texel. One template, configured per kind. Engine up.
foreach(BUNDLE_KIND texture material pack)
    configure_file(scripts/e2e_bundle_export.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_bundle_export_${BUNDLE_KIND}.js @ONLY)
    jah_pool_arm(assets_import bundle_export_${BUNDLE_KIND} ${CMAKE_CURRENT_BINARY_DIR}/e2e_bundle_export_${BUNDLE_KIND}.js 2)
endforeach()

# scripting.e2e.assets_page — the owner's Assets-module smoke round (S4-S7,
# 2026-09-11): the import selects its tile, the preview is the size the editor
# places (fit-to-size on both paths), the stored thumbnail is TEXTURED and
# FRAMED (it used to be a render of the live import fragment, whose textures
# live in the deleted staging dir), and the preview flies. Engine up (no
# --headless): the thumbnail is a render and the preview is a scene.
configure_file(scripts/e2e_assets_page.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_assets_page.js @ONLY)
jah_pool_arm(assets_import assets_page ${CMAKE_CURRENT_BINARY_DIR}/e2e_assets_page.js 14)

# scripting.e2e.assets_scope — ASSETS-SCOPE-1 (owner 2026-09-26): the Assets
# module lists ONLY what was imported into the library or made there
# explicitly; every row minted while editing a project (a material created in
# the editor, its textures, an image's companion, a preset's project copy) is
# that project's own (services/assethome.h). Document verbs only -> --headless.
configure_file(scripts/e2e_assets_scope.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_assets_scope.js @ONLY)
# The home is WIPED before every run: which ROW a picture answers depends on
# which rows already hold those bytes (the by-content dedup), so the cases are
# only meaningful on a library that has never seen them.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-assets_scope)
file(MAKE_DIRECTORY ${_home}/run)
jah_fresh_home_fixture(scripting.e2e.assets_scope.fresh_home assetsScopeFreshHome "${_home}" LABELS "scripting")
add_test(NAME scripting.e2e.assets_scope
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_assets_scope.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.assets_scope PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED assetsScopeFreshHome
    LABELS "scripting"
    TIMEOUT 120)

# scripting.e2e.thumbnails_order — THE OWNER'S ORDER (THUMBS-1, 2026-09-18).
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
configure_file(scripts/e2e_thumbnails_order.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_thumbnails_order.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-thumbnails_order)
# A VIRGIN LIBRARY: the run counts what a sweep finds and which rows a tray
# import added, and a home carried over from the last run answers both with
# yesterday's assets.
jah_fresh_home_fixture(scripting.e2e.thumbnails_order.fresh_home thumbnailsOrderHome "${_home}" LABELS "scripting")
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(scripting.e2e.thumbnails_order CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_thumbnails_order.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.thumbnails_order PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED thumbnailsOrderHome
    LABELS "scripting"
    TIMEOUT 180)

# scripting.e2e.import_dialog — THE IMPORT DIALOG, DRIVEN
# (SPECS/IMPORT_DIALOG_SPEC.md §8/§9). ui.import_dialog pins the record the
# widgets produce and scripting.e2e.import_settings pins the verbs; this pins
# the JOIN — the dialog's own OK button, on a real library asset, re-bakes it,
# the open scene follows at scale 1, and the record it writes is byte-identical
# to the one the verb writes. Needs the editor window (app.dialog) -> no
# --headless.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(scripts/e2e_import_dialog.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_dialog.js @ONLY)
jah_pool_arm(assets_import import_dialog ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_dialog.js 2)

# VISUAL_PARITY_SPEC item 5: a saved shader GRAPH gets a real thumbnail —
# createGraph -> colour wired into Base Color -> save -> refreshThumbnail
# renders the evaluated PbrMaterial on the preview sphere, and the stored PNG's
# centre pixel is the graph's colour (assets.thumbnail reads it back). Engine up
# (no --headless): the render is the point.
jah_pool_arm(shading_live shader_thumbnail ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_shader_thumbnail.js 3)

# Avatar module, Part 0 (AVATAR_MODULE_SPEC S8): the Avatar page's surface
# driven from outside the app — space switch, loadPreview off disk (no project,
# no library, no database), clip display names, the pose through avatar.bones(),
# and avatar.snapshot proving all the toggle combinations at the pixel level.
# NOT --headless: the snapshot needs the engine.
configure_file(scripts/e2e_avatar.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_avatar.js @ONLY)
jah_pool_arm(avatar_anim avatar ${CMAKE_CURRENT_BINARY_DIR}/e2e_avatar.js 12)

# GPU_SKINNING_SPEC T10: a rigged character through the ONE import pipeline into
# a real project, reporting its rig through node.skinningMode / node.boneNames,
# with the engine viewport up. NOT --headless: the point is the production
# mirror and the production skinning path.
configure_file(scripts/e2e_skinning.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_skinning.js @ONLY)
jah_pool_arm(avatar_anim skinning ${CMAKE_CURRENT_BINARY_DIR}/e2e_skinning.js 13)

# ENGINEERING_DEBT_SPEC A4.2 — the ONE fixed-step clock's determinism proof: a
# 120-frame physics drop (six tumbling rigid bodies), a keyframe track and a
# skinned clip, run as 120 x 1/60, 60 x 1/30, 240 x 1/120 and 60+pause+60
# frames, must leave the document byte-identical; the Simulate path lands the
# bodies where play did; a pause holds the clock; an over-bound dt is refused.
# Renders (physics steps in the engine viewport's syncFrame) -> needs a display.
configure_file(scripts/e2e_fixed_clock.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_fixed_clock.js @ONLY)
jah_pool_arm(player fixed_clock ${CMAKE_CURRENT_BINARY_DIR}/e2e_fixed_clock.js 19)

# World Modes (POST_CHAIN_SPEC.md §9): the scalability registry, tier
# write-through, pins that survive mode switches, and the save/open round-trip.
# Every verb is a document verb -> --headless.
jah_pool_arm(doc world_modes ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_world_modes.js 1 HEADLESS)

# world.tierTable(): WHAT A TIER PHYSICALLY IS — the chain, the VR column and the
# ATOM column (the tier's geometric tolerance in samples, A4 §3). Its own suite
# since ATOM-RESUMES-1: it reads the RENDERER's tables (worldmodes::kPhotonTable
# + the engine's giQualityFacts) where world_modes reads the registry, and the
# block was moved out of that script whole rather than copied. Own home, like
# every suite in this family.
jah_pool_arm(doc tier_table_atom ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_tier_table_atom.js 1 HEADLESS)

# THE LOOKS STACK (POST_LOOKS_SPEC.md §4.1): the catalogue, the six verbs, the
# one-instance-per-look rule, clamping, undo and the save/open round trip. The
# pixel half of the program is the looks.engine suite; these are all document
# verbs -> --headless.
jah_pool_arm(doc looks ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_looks.js 1 HEADLESS)

# PHOTON (GI_UNIFIED_SPEC.md §2 / P2): the one GI switch and its quality tier —
# the new-scene default (Epic), the tier table seen through the verbs, an
# Advanced pin surviving a tier switch, world.photon round-tripping through
# save/open, and the registry saying which dial owns which row. Document verbs
# only -> --headless; the pixel half is gi.modes/gi.ddgi and the resolution half
# is the gi.tiers suite.
jah_pool_arm(doc photon ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_photon.js 1 HEADLESS)

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
jah_gpu_row(scripting.e2e.ray_row_norays CLASS app
         COMMAND ${JAH_FRESH_HOME} --warm ${_home} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_ray_row.js
         WORKING_DIRECTORY ${_home}/run)
# A ROW OF ITS OWN (the process latch): JAHSHAKA_NO_RAY_QUERY is read when the device is made.
# The as-this-machine-is half is the gi_movers.ray_row arm.
set_tests_properties(scripting.e2e.ray_row_norays PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka;JAHSHAKA_NO_RAY_QUERY=1"
    LABELS "scripting"
    TIMEOUT 240)
jah_pool_arm(gi_movers ray_row ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_ray_row.js 14)

# OUTLINER FOLDERS (SCENEGRAPH_SPEC.md §6b): scene.folders/createFolder/
# renameFolder/removeFolder + node.setFolder/folder. The two LAWS are the point
# of the suite — filing a node never reparents it, removing a folder never
# deletes a node — plus one undo step per gesture and the save/reopen round trip
# through the EDITOR section. Document verbs only -> --headless.
jah_pool_arm(doc_assets folders ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_folders.js 1 HEADLESS)

# The leak lane (scripts/sanitize.sh lsan): the QJSEngine host only. The
# scripting.e2e.* suites run the real binary with the engine up and stay out.
jah_lsan_lane(scripting.engine)

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
configure_file(scripts/e2e_space_switch.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_space_switch.js @ONLY)
# OUT OF THE GPU-TIMING LOCK (lane D6B-GATE-SHAPE; audit §4 R4, FIX-LOCK): it measures no time
# (its script's only Date.now names projects).
jah_pool_arm(player space_switch ${CMAKE_CURRENT_BINARY_DIR}/e2e_space_switch.js 34)

# The verb surface's SCENE_STATIC contract + scene.raycast — the 2026-09-05
# scripting-audit regression net (F1/F2/F4/F7/F12; F3's undo half waits for
# undo v1.5). Document verbs only -> --headless.
jah_pool_arm(doc static_semantics ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_static_semantics.js 1 HEADLESS)

# AVATAR_LOCOMOTION_SPEC Stage 3 through the VERBS and the REAL play path:
# possession, camera-relative input, the follow arm, editor.pause, and the two
# claims the document-level `avatar.possession` suite cannot make — the actual
# `editor.stop(); editor.stop();` (a HeadlessEditorViewport's play is a no-op,
# so this needs the engine viewport) and scene.playMode surviving a save and
# reopen. NOT --headless, deliberately.
configure_file(scripts/e2e_possession.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_possession.js @ONLY)
jah_pool_arm(player possession ${CMAKE_CURRENT_BINARY_DIR}/e2e_possession.js 15)

# PLAY-SELECT-1 (owner R13): EDITING WHILE THE SCENE PLAYS, through the verbs —
# who owns a click during a run (editor.playInputOwner), the eject latch
# (editor.playEject, refused outside play), the editor's selection and gizmo
# verbs working mid-run, a hand-placed rigid body carrying on from where it was
# put instead of being overwritten by the solver, and Stop throwing the run's
# edits away while KEEPING the selection. The possession arm needs a real
# character, hence the fixture and the configure_file. The other half of the
# lane — a real left click and a real gizmo drag on the viewport — is
# app.play_select, which needs synthesised X input.
#
# NOT --headless: play-in-place, the physics stepper and the input routing all
# live in the engine viewport.
configure_file(scripts/e2e_play_select.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_play_select.js @ONLY)
jah_pool_arm(editor_view play_select ${CMAKE_CURRENT_BINARY_DIR}/e2e_play_select.js 6)   # editor_view, an Epic pool (TEST-TIER-1): a click on a pixel of the framed ground — red at the Low test tier

# AVATAR_LOCOMOTION_SPEC Stage 4 through the VERBS and a REAL scene file: the
# four state-machine verbs, the shipped default asset generated for an imported
# character with zero authoring, its degradation when only one role binds
# (rig2.glb ships "Idle" and the junk name "mixamo.com" — the Mixamo reality
# §7.4 is written for), the closed vocabulary's refusal, and gate S6's save +
# close + open, which is the only thing that exercises SceneWriter's and
# SceneReader's locomotion hunks.
configure_file(scripts/e2e_locomotion.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_locomotion.js @ONLY)
jah_pool_arm(doc locomotion ${CMAKE_CURRENT_BINARY_DIR}/e2e_locomotion.js 1 HEADLESS)

# The DEFAULT AVATAR fixture's contract: beta_mannequin.dae (generated by
# tests/avatar from the shipped Skeletal Animation sample — Mixamo Beta, full
# bare-named Mixamo skeleton, baked clip STRIPPED) imports, spawns, resolves
# its skeleton (the positive complement of sockets.e2e's rig2 fail-soft), kept
# its cm unit scale, and is NEUTRAL — zero clips, zero roles, empty machine.
# Document verbs only -> --headless. The path is set here explicitly (not via
# the tests/avatar cache var) because tests/avatar configures AFTER this dir.
set(JAHSHAKA_MANNEQUIN_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_mannequin.dae")
configure_file(scripts/e2e_mannequin.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_mannequin.js @ONLY)
jah_pool_arm(doc mannequin ${CMAKE_CURRENT_BINARY_DIR}/e2e_mannequin.js 1 HEADLESS)

# The Avatar module AS VERBS (verb-coverage audit F15) plus avatar.loadClip
# (F5): spawn -> possess -> setMovement clamping -> setLocomotionAsset's six
# refusal classes -> locomotionState -> loadClip, and the save + reopen that
# proves a loaded clip is a real project asset rather than a path on the
# author's disk. Both fixtures are generated by tests/avatar (which configures
# AFTER this dir, so the paths are spelled out here exactly as the mannequin
# suite's is).
set(JAHSHAKA_BETA_WALK_DAE "${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_walk.dae")
configure_file(scripts/e2e_avatar_verbs.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_avatar_verbs.js @ONLY)
jah_pool_arm(doc avatar_verbs ${CMAKE_CURRENT_BINARY_DIR}/e2e_avatar_verbs.js 2 HEADLESS)

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
set_tests_properties(scripting.e2e.one_asset PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED one_asset_home
    LABELS "scripting"
    TIMEOUT 120)

# scripting.e2e.default_floor — THE DEFAULT FLOOR (owner 2026-09-12, lane
# L13): a new scene stands on the flagged floor wearing its own checker
# material; material.reset restores it in one undo step and clears a
# user-applied library material (released from the project when nothing else
# uses it); a node with no default answers false; the flag survives reopen.
# Document verbs only -> --headless.
configure_file(scripts/e2e_default_floor.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_default_floor.js @ONLY)
jah_pool_arm(doc default_floor ${CMAKE_CURRENT_BINARY_DIR}/e2e_default_floor.js 1 HEADLESS)

# scripting.e2e.tray_panel — the asset tray shows EVERY ASSET A SCENE USES,
# once (owner 2026-09-12, lane L13): the tray PANEL (editor.trayAssets reads
# the widget) against assets.list({tray:true}) after verb pins and uses, and
# each shipped sample's tray against the scene's asset closure read off the
# document. Engine-up — the tray is a widget — so it needs the rig's display.
# A FRESH home per run: it asserts exact tile sets.
configure_file(scripts/e2e_tray_panel.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_tray_panel.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-tray_panel)
jah_fresh_home_fixture(scripting.e2e.tray_panel.fresh_home tray_panel_home "${_home}" LABELS "scripting")
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(scripting.e2e.tray_panel CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_tray_panel.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.tray_panel PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED tray_panel_home
    LABELS "scripting"
    TIMEOUT 300)

# gi.one_writer — ONE WRITER PER VOXEL VOLUME (PHOTON-WRITER-1): a drag and a
# lamp moving while the camera walks, on Showroom 2, read against the engine's
# own books (world.giStatus's chainInjectionRefusals / chainInjectionsPeakFrame).
# A FRESH home per run (the sample is imported).
set(JAH_ONE_WRITER_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
configure_file(scripts/gi_one_writer.js.in ${CMAKE_CURRENT_BINARY_DIR}/gi_one_writer.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-one_writer)
jah_fresh_home_fixture(gi.one_writer.fresh_home one_writer_home "${_home}" LABELS "scripting")
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(gi.one_writer CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/gi_one_writer.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(gi.one_writer PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED one_writer_home
    LABELS "scripting"
    TIMEOUT 300)

# gi.pcc_mirror.ray_tier — AT A RAY TIER THE PROBE GRID IS NOT BUILT (PHOTON-F12-PCC), on
# Showroom 2 at Epic: no grid with rays on, the grid with rays off, gone again with rays back
# on, and the roughness-0 sphere's reflection A/B against the grid's answer. The engine's books
# for the same rule are gi.pcc_mirror's arm (i). A FRESH home per run (the sample is imported).
set(JAH_PCC_RAY_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-pcc_ray_tier)
set(JAH_PCC_RAY_OUT "${_home}/run/")
configure_file(scripts/gi_pcc_mirror_ray_tier.js.in ${CMAKE_CURRENT_BINARY_DIR}/gi_pcc_mirror_ray_tier.js @ONLY)
jah_fresh_home_fixture(gi.pcc_mirror.ray_tier.fresh_home pcc_ray_tier_home "${_home}" LABELS "scripting")
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(gi.pcc_mirror.ray_tier CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/gi_pcc_mirror_ray_tier.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(gi.pcc_mirror.ray_tier PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED pcc_ray_tier_home
    LABELS "scripting"
    TIMEOUT 420)

# gi.chain_converge_scenes — ONE AT-REST SWEEP IS THE CHAIN'S FIXED POINT on the
# default scene and Showroom 2 at High (PHOTON-WRITER-1 SWEEPS-3; the room half is
# gi.chain_converge): from a perturbed history a second sweep changes no byte of
# any cascade's light (world.giVoxelStats's lightDigest). Fresh home per run.
# NIGHTLY (lane D6B-GATE-SHAPE; audit §3c, verdict NIGHTLY): 80 s for the claim gi.chain_converge
# (9 s) holds at every push; an engine change still selects it in its own scoped gate.
set(JAH_CONVERGE_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
configure_file(scripts/gi_chain_converge_scenes.js.in ${CMAKE_CURRENT_BINARY_DIR}/gi_chain_converge_scenes.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-chain_converge_scenes)
jah_fresh_home_fixture(gi.chain_converge_scenes.fresh_home chain_converge_scenes_home "${_home}" LABELS "scripting;nightly")
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(gi.chain_converge_scenes CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/gi_chain_converge_scenes.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(gi.chain_converge_scenes PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED chain_converge_scenes_home
    LABELS "scripting;nightly"
    TIMEOUT 600)

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
configure_file(scripts/e2e_animation_assets.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_animation_assets.js @ONLY)
# A FRESH home per run for BOTH halves (the mcp.e2e.fresh_home pattern): these
# two assert on absolute row counts — "one animation asset", "an EMPTY library"
# — and a home that persists across gates accumulates a second copy of
# everything, so the suite would go red on nothing.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-animation_assets)
jah_fresh_home_fixture(scripting.e2e.animation_assets.fresh_home animationAssetsHomeA "${_home}" ARGS --reset "${CMAKE_CURRENT_BINARY_DIR}/animation_assets_export" LABELS "scripting")
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.animation_assets
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_animation_assets.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.animation_assets PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED animationAssetsHomeA
    FIXTURES_SETUP animation_assets_archive
    LABELS "scripting"
    TIMEOUT 120)

configure_file(scripts/e2e_animation_assets_import.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_animation_assets_import.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-animation_assets_import)
jah_fresh_home_fixture(scripting.e2e.animation_assets_import.fresh_home animationAssetsHomeB "${_home}" LABELS "scripting")
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.animation_assets_import
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_animation_assets_import.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.animation_assets_import PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED "animation_assets_archive;animationAssetsHomeB"
    LABELS "scripting"
    TIMEOUT 120)

# The PLAYER space's verbs (verb-coverage audit F1): player.play/stop/playing/
# restart/state driving the real EnginePlayerView through PlayerService, and
# player.screenshot rendering the player's OWN engine Scene through the
# document's camera. NOT --headless: a headless run has no player backend at
# all, which is the one thing player.state().available exists to report.
jah_pool_arm(player player_verbs ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_player_verbs.js 9 FIRST)   # FIRST: it asserts the Player page was never shown in the process

# The graph READ/EDIT verbs (verb-coverage audit F2): graph.connections,
# graph.nodeInfo, graph.removeNode and graph.disconnect over a script graph.
# Document verbs only -> --headless.
jah_pool_arm(doc graph_edit ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_graph_edit.js 1 HEADLESS)

# scripting.e2e.material_refusal — LEGACY-CONVERT-CRUD fix round: a material
# that cannot be opened must change NOTHING. A loader that takes the new
# material's identity before it has its graph leaves the app editing the
# previous graph while saving to the refused material's row — one material
# written over another. See the script's header.
configure_file(scripts/e2e_material_refusal.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_refusal.js @ONLY)
jah_pool_arm(doc_assets material_refusal ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_refusal.js 1 HEADLESS)

# scripting.e2e.import_settings — the import decision end to end
# (SPECS/IMPORT_DIALOG_SPEC.md §9). The unit suite (importer.extent) pins the
# arithmetic; this pins what the user does — import with settings, place it and
# find the size in the GEOMETRY with the node at scale 1, then reimport with new
# settings and watch the placed node follow while its transform does not — plus
# the retirement of assets.setFit. Document verbs only -> --headless.
# It replaces scripting.e2e.fitsize.
configure_file(scripts/e2e_import_settings.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_settings.js @ONLY)
jah_pool_arm(doc_assets import_settings ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_settings.js 1 HEADLESS)

jah_no_display(
    scripting.e2e.store_root
    scripting.e2e.shipped_assets)

# scripting.e2e.glb_textures — the owner's "the glb imports still don't work,
# they import without the textures" (2026-09-09), as the round trip it really
# is: import + place, read the material's maps, save/close/REOPEN, read them
# again. The textures were never lost at import; the SAVE stored the .glb
# Object's guid in every map slot (one stored object backs both the Object and
# the member Texture, and the writer's tie-break did not say which), and the
# reader resolved it straight back to the model file. Compared by store file
# NAME — in the CAS that name is the content's sha256. Document verbs only ->
# --headless.
configure_file(scripts/e2e_glb_textures.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_glb_textures.js @ONLY)
jah_pool_arm(doc_assets glb_textures ${CMAKE_CURRENT_BINARY_DIR}/e2e_glb_textures.js 1 HEADLESS)

# AVATAR_RIG_PERF_SPEC §3.5: the rig-cost read surface, in the real binary with
# the ENGINE UP — `available` is half of what the suite checks, and a headless
# session reports false by design.
jah_pool_arm(avatar_anim rig_stats ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_rig_stats.js 3)

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
configure_file(scripts/e2e_avatar_assets.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_avatar_assets.js @ONLY)
jah_pool_arm(doc_assets avatar_assets ${CMAKE_CURRENT_BINARY_DIR}/e2e_avatar_assets.js 3 HEADLESS)

# --- multi-selection (EDITOR_MULTISELECT_SPEC §5) ---------------------------
# Both are document-only, so they run --headless (offscreen platform, NULL
# render system, DISPLAY unset) — the selection set, its verbs and its edits
# never touch a pixel.
# node.rename (plan item 15): the sibling-unique copy rule and ONE recorded undo
# step per rename; the undo itself is asserted in mcp.e2e (a --script run's
# macro never closes). Document verbs only -> --headless.
jah_pool_arm(doc node_rename ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_node_rename.js 1 HEADLESS)

foreach(_ms multiselect multiselect_edit)
    jah_pool_arm(doc ${_ms} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_${_ms}.js 5 HEADLESS)
endforeach()

# --- refusals (hygiene lane, 2026-09-09) ------------------------------------
# A verb whose documented answer is "no" RETURNS it (false / 0 / null) instead
# of throwing: an exception aborts the caller's whole run over a question it
# asked on purpose. node.info on a deleted node, editor.selection with nothing
# selected, editor.copy on an empty selection — plus the other half, that real
# misuse still throws. Document verbs only -> --headless, no display.
jah_pool_arm(doc refusals ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_refusals.js 1 HEADLESS)

# SCENE-SCALE CONVENTION (owner 2026-09-08), verb half: scene.bounds — the world
# AABB of a scene's geometry — and editor.camera's lens, which stopped requiring
# a rendering engine in the same lane (the saved fov is document state; gating
# it on a GPU is why the samples' 70 and 75 degree lenses went unnoticed for a
# week). samples.cleanstart measures the shipped archives THROUGH these two; this
# gates them against primitives of known size. Document verbs only -> --headless.
jah_pool_arm(doc scene_bounds ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_scene_bounds.js 1 HEADLESS)

# scripting.e2e.ogre_samples — SPECS/OGRE_SAMPLES_TAB_SPEC.md §6, the API-first
# half of the Ogre-samples tab: app.ogreSamples() (the curated inventory the
# tab's tiles are built from) and app.launchOgreSample() (the Launch Original
# button's verb).
#
# --headless AND jah_no_display, deliberately. THE REFUSAL IS THE TEST: Ogre's
# own sample binaries are opt-in at engine-build time (OGRE_SAMPLES=1
# ./irisgl/scripts/build-ogre.sh) and exist in no CI tree and on no macOS box,
# so this suite proves the verbs answer honestly and spawn NOTHING when they
# are absent — never that a sample runs. A gate that launched a second Vulkan
# application would be a gate that fights the machine it runs on.
jah_pool_arm(doc ogre_samples ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_ogre_samples.js 1 HEADLESS)

# scripting.e2e.ogre_ports — THE PORTS (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3): the
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
configure_file(scripts/e2e_ogre_ports.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_ogre_ports.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-ogre_ports)
jah_fresh_home_fixture(scripting.freshhome.ogre_ports scripting_home_ogre_ports "${_home}" LABELS "scripting")
jah_gpu_row(scripting.e2e.ogre_ports CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_ogre_ports.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.ogre_ports PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED scripting_home_ogre_ports
    LABELS "scripting"
    TIMEOUT 420)

# MATERIAL_GAPS_SPEC GAP 2 (P6 + P7): DETAIL LAYERS end to end, and the §3.7
# acceptance port — Ogre's own `Marble` material (detail_map0 MRAMOR6X6.jpg at
# offset/scale 0 0 5 5, roughness_map MRAMOR-bump.jpg) rebuilt from OUR document
# model through the verbs. Both images come in through the ordinary import
# pipeline from the ogre-next submodule, which is half of what the port proves.
# Asserts the one-setter-table rule (material.set and material.setDetail write
# the same rows), the thirteen-mode blend vocabulary BY NAME, that the per-layer
# scale is the detail-tiling knob, and — the byte-identical argument as a test —
# that an unauthored layer at its defaults changes nothing.
configure_file(scripts/e2e_detail_layers.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_detail_layers.js @ONLY)
jah_pool_arm(shading detail_layers ${CMAKE_CURRENT_BINARY_DIR}/e2e_detail_layers.js 6)

# MATERIAL_GAPS_SPEC ADDENDUM A-1: LIVE TEXTURES — pixels a producer writes at
# runtime, catalogued as a session-only asset kind so material.set can resolve
# them by guid like any other texture. Document verbs only -> --headless. The
# load-bearing assertion is the WRITER one: a saved scene must not carry a live
# guid, because nothing could ever resolve it again.
jah_pool_arm(doc_assets live_textures ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_live_textures.js 1 HEADLESS)

# The PIXEL half of A-1: a live texture bound as a base colour map is sampled
# by the renderer, a write changes what is drawn with no re-bind, and a run of
# writes creates no new engine texture. Engine UP (no --headless): the whole
# point is what the renderer does with the generation counter.
jah_pool_arm(shading_live live_texture_pixels ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_live_texture_pixels.js 8)

# RENDER_PIPELINE_AUDIT 3.2 (smoke lane L12): a DUPLICATE RENDERS LIKE ITS
# ORIGINAL. Every duplicate path (node.duplicate, Ctrl+D / Alt+drag through
# editor.duplicateSelection, the outliner menu) handed the copy a blank base
# Material and the mirror drew it grey. Pixel probes on the original, a
# node.duplicate copy and a duplicateSelection copy, then a repaint of the copy
# that must leave the original red (a copy, not a shared material).
jah_pool_arm(shading_live duplicate_material ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_duplicate_material.js 2)
# RENDER_PIPELINE_AUDIT 1.1/1.2 (smoke lane L12): EFFECTIVE VISIBILITY at the
# Epic tier, full stack. Hiding a model's ROOT takes every part out of the lit
# volume and out of the clicks (its parts stayed voxelised before), and showing
# it again brings back every part EXCEPT one the user hid itself (the engine's
# setVisible cascade re-drew it). Read through world.giStatus, scene.raycast and
# the visibility report. Engine up: the lit volume is the renderer's answer.
jah_pool_arm(editor_view hierarchy_visibility ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_hierarchy_visibility.js 6)

# ASSET_MEDIA_SPEC §3 Option A + A-1: a VIDEO decoded into a live texture —
# every decoded frame is a write, every write is a generation bump. Qt
# Multimedia's ffmpeg backend decodes with no display (the same fact
# scripting.e2e.video relies on) -> --headless. Fixture: tests/scripting/
# fixtures/tiny.mp4 (64x64, 16 frames at 8 fps).
configure_file(scripts/e2e_video_texture.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_video_texture.js @ONLY)
jah_pool_arm(doc_assets video_texture ${CMAKE_CURRENT_BINARY_DIR}/e2e_video_texture.js 2 HEADLESS)

# MATERIAL_GAPS_SPEC ADDENDUM A-5: the PER-MATERIAL REFLECTION CUBEMAP. Two
# spheres, one override: the hero's reflection moves and the control's does
# not. Engine UP — the row is only interesting because of what the renderer
# binds, and the document cannot answer that.
configure_file(scripts/e2e_reflection_map.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_reflection_map.js @ONLY)
# OUT OF THE GPU-TIMING LOCK (lane D6B-GATE-SHAPE; audit §4 R4, FIX-LOCK): it measures no time
# (its flake was cold-cache uninitialised VRAM — a defect class, not time).
jah_pool_arm(shading_live reflection_map ${CMAKE_CURRENT_BINARY_DIR}/e2e_reflection_map.js 12)

# ---------------------------------------------------------------------------
# DISPLAY-FREE, AND PROVEN (TEST_GATE_AUDIT.md §4.5). Every suite named here
# claims — in its own comment, or by running --headless, or by building nothing
# but a document — that it needs no X server. The claim was never enforced, so
# it drifted: a suite that quietly opened a display still passed on a developer
# box and would have failed the first CI runner without one, and every
# unnecessary X connection is one more thing four parallel jobs contend over.
# jah_no_display runs these with DISPLAY and WAYLAND_DISPLAY UNSET, which turns
# the claim into a gate. Verified 2026-09-09: each of these passes displayless
# with the same assertion count it has with a display.
jah_no_display(scripting.e2e.animation_assets scripting.e2e.animation_assets_import scripting.engine)
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
jah_pool_arm(doc_assets clipboard ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_clipboard.js 1 HEADLESS)
foreach(_clip clipboard_xproject clipboard_assets)
    jah_pool_arm(doc_assets ${_clip} ${CMAKE_CURRENT_BINARY_DIR}/e2e_${_clip}.js 5 HEADLESS)
endforeach()

# THE DUAL IMAGE ON A CURVED MIRROR (lane SSR-2; the owner's push #35 smoke).
# The script's own header says why this lives here and not in tests/ssr: the
# defect needs a parallax-corrected PROBE GRID whose answer disagrees with the
# screen's, and every fixture in the engine suite falls back to the same
# analytic sky the trace is looking at. The Mirror Room sample is the scene the
# owner photographed. Engine-up and on-screen, like the grades suite beside it.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(scripts/e2e_ssr_mirror.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_ssr_mirror.js @ONLY)
set(_ssrhome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-ssr_mirror)
file(MAKE_DIRECTORY ${_ssrhome}/run)
jah_gpu_row(scripting.e2e.ssr_mirror CLASS app
         COMMAND ${JAH_FRESH_HOME} --warm ${_ssrhome} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_ssr_mirror.js
         WORKING_DIRECTORY ${_ssrhome}/run)
# THE SCREEN-ONLY CONTRACT, AND IT STAYS THAT (lead ruling, PHOTON R5 round B;
# the same ruling as tests/ssr/CMakeLists.txt). This suite is lane SSR-2's: it
# measures that the SCREEN does not overrule the probe across the Mirror Room
# sphere's limb. With the ray-query tier live that limb is answered by a TRACED
# reflection, which is the correct picture and not the one this suite is about,
# so it runs in the picture it was written for and none of its bars moves. The
# combined picture is `gi.rt_reflect` / `gi.rt_reflect_norays`.
set_tests_properties(scripting.e2e.ssr_mirror PROPERTIES
    ENVIRONMENT "HOME=${_ssrhome};JAHSHAKA_DATA_ROOT=${_ssrhome}/.local/share/Jahshaka;JAHSHAKA_NO_RAY_QUERY=1"
    LABELS "scripting"
    TIMEOUT 540)

# WHAT A SCREENSHOT IS A PICTURE OF (SS1, owner 2026-09-13 — "match the
# screenshot to the scene properly"). The four ScreenshotGrade modes through
# editor.screenshot: the plain readback the whole pixel corpus asserts does not
# move; a world with HDR off photographs ungraded; the World's exposure reaches
# both the scene grade and the thumbnail grade; the chain (SSR above all) is in
# the user's picture; and a MOVABLE object is in it.
#
# ENGINE-UP AND ON-SCREEN, deliberately not --headless: the scene grade's
# exposure is read off the VIEWPORT's converged luminance history, so the suite
# is only meaningful with a real viewport rendering real frames. Four settle()
# runs of 240 frames at Epic is what the timeout is for.
jah_pool_arm(world_light screenshot_grades ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_screenshot_grades.js 61)

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
configure_file(scripts/e2e_atom_lods.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_atom_lods.js @ONLY)
jah_pool_arm(atom atom_lods ${CMAKE_CURRENT_BINARY_DIR}/e2e_atom_lods.js 26)

# ---------------------------------------------------------------------------
# SURFACE CARDS THROUGH THE VERBS (SURFACE-CACHE phase 1). The companion of
# meshbake.cards, which tests the GENERATOR with no app around it: this one
# takes the door a script and the import dialog take — the real import
# pipeline, the real CAS, the real bake store — so assets.meshCards reports
# what a bake on disk holds rather than what a generator was asked for. Same
# fixture as atom_lods (the shipped gizmo sphere: static, convex, and big
# enough to carry a chain, which is what makes the card's LOD level mean
# something).
configure_file(scripts/e2e_surface_cards.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_surface_cards.js @ONLY)
jah_pool_arm(gi_movers surface_cards ${CMAKE_CURRENT_BINARY_DIR}/e2e_surface_cards.js 2)

# ---------------------------------------------------------------------------
# THE SMOKE OF PUSH #50, AS TWO SUITES (lane SMOKE-FIX-1, 2026-09-18).
#
# Both scripts carry their own reasoning; in one line each:
#   sample_open      — a sample-browser open lands in the EDITOR, through the
#                      verb the browser's tiles call (the uninitialised
#                      play-mode flag).
#   player_no_editor — the Player over a process whose editor page was never
#                      shown DRAWS, instead of enabling a View with no scene and
#                      leaving the previous page's pixels on the screen.
# Both are engine-up (they assert frames), so neither is --headless.
jah_pool_arm(assets_import sample_open ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_sample_open.js 24)
# 300 like the other open-and-render suites: it imports an archive and opens it.

configure_file(scripts/e2e_player_no_editor.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_player_no_editor.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-player_no_editor)
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(scripting.e2e.player_no_editor CLASS app TIER low
         COMMAND ${JAH_FRESH_HOME} --warm ${_home} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_player_no_editor.js
         WORKING_DIRECTORY ${_home}/run)
# JAHSHAKA_TEST_NO_EDITOR_BOOT: the PERSON's boot shape (window up, Desktop
# page, editor page never shown) instead of the script harness's, which shows
# the editor page before the script's first line. Without it the editor's
# engine scene already exists and this suite proves nothing — see the script.
set_tests_properties(scripting.e2e.player_no_editor PROPERTIES
    # JAHSHAKA_TEST_TIER=low (TEST-TIER-1): no claim here needs the shipped picture — the same verdict and ok count at Low (spikes/test-tier-1/solo-okcount.txt)
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka;JAHSHAKA_TEST_NO_EDITOR_BOOT=1;JAHSHAKA_TEST_TIER=low"
    LABELS "scripting"
    TIMEOUT 300)

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
jah_gpu_row(scripting.e2e.player_bounce CLASS app
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
# the tile icons, the old-guid mapping — is defaults.primitive_table; this is
# "every name the drawer offers is a name the verb accepts", plus the retired
# four refused by name, plus the Mirror Room still opening with its teapot.
jah_pool_arm(editor primitives ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_primitives.js 8)

# scripting.e2e.add_primitive — ATOM P2's acceptance for the VERB: a primitive is a
# baked library asset, so the node it adds reports a real LOD chain in
# `app.renderStats().perObject` (a renderer reading, so the chain is proven all the
# way through the mirror and the upload), and a bias of 0 pins every object at its
# finest level. Needs the engine: no --headless.
jah_pool_arm(editor add_primitive ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_add_primitive.js 2)

# scripting.e2e.material_tabs — MATERIALS_TABS_SPEC §5: SEVERAL MATERIALS OPEN
# AT ONCE. It needs the PAGE (the five tab verbs are the page's own entry
# points), so no --headless; its own home, wiped before the run, because the
# tab set it leaves behind is what the relaunch half reads.
set(_mtHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material_tabs)
jah_fresh_home_fixture(scripting.e2e.material_tabs.fresh_home materialTabsHome "${_mtHome}" LABELS "scripting")
file(MAKE_DIRECTORY ${_mtHome}/run)
jah_gpu_row(scripting.e2e.material_tabs CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_material_tabs.js
         WORKING_DIRECTORY ${_mtHome}/run)
set_tests_properties(scripting.e2e.material_tabs PROPERTIES
    ENVIRONMENT "HOME=${_mtHome};JAHSHAKA_DATA_ROOT=${_mtHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED materialTabsHome
    FIXTURES_SETUP materialTabsSession
    LABELS "scripting"
    TIMEOUT 300)

# ...and a SECOND process on the same home: the tab set is persisted in the
# app's settings, and only a relaunch can prove it is read back.
jah_gpu_row(scripting.e2e.material_tabs.relaunch CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_material_tabs_relaunch.js
         WORKING_DIRECTORY ${_mtHome}/run)
set_tests_properties(scripting.e2e.material_tabs.relaunch PROPERTIES
    ENVIRONMENT "HOME=${_mtHome};JAHSHAKA_DATA_ROOT=${_mtHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED "materialTabsHome;materialTabsSession"
    LABELS "scripting"
    TIMEOUT 120)

# scripting.e2e.material_bundle — MATERIAL_BUNDLE_SPEC phase 1, the acceptance
# flow the spec names: pick an image from disk -> ONE tile -> export -> an
# EMPTY library -> it still resolves. Two processes, because "an empty library"
# is not something a verb can fake: the first exports an archive from a normal
# session, the second runs on its OWN home and data root, wiped before it
# starts, and is given nothing but that archive.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(scripts/e2e_material_bundle.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_bundle.js @ONLY)
set(_mbHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material_bundle)
jah_fresh_home_fixture(scripting.e2e.material_bundle.fresh_home materialBundleHome "${_mbHome}" LABELS "scripting")
file(MAKE_DIRECTORY ${_mbHome}/run)
add_test(NAME scripting.e2e.material_bundle
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_bundle.js --headless
         WORKING_DIRECTORY ${_mbHome}/run)
set_tests_properties(scripting.e2e.material_bundle PROPERTIES
    ENVIRONMENT "HOME=${_mbHome};JAHSHAKA_DATA_ROOT=${_mbHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED materialBundleHome
    FIXTURES_SETUP materialBundleArchive
    LABELS "scripting"
    TIMEOUT 180)

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
set_tests_properties(scripting.e2e.material_share_import PROPERTIES
    ENVIRONMENT "HOME=${_msiHome};JAHSHAKA_DATA_ROOT=${_msiHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED "materialShareImportHome;materialShareFile"
    LABELS "scripting"
    TIMEOUT 120)

# The far side: a library that has never seen the material, the image or the
# project, and one archive.
set(ARCHIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/material_bundle.zip)
configure_file(scripts/e2e_material_bundle_import.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_bundle_import.js @ONLY)
set(_mbiHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material_bundle_import)
jah_fresh_home_fixture(scripting.e2e.material_bundle_import.fresh_home materialBundleImportHome "${_mbiHome}" LABELS "scripting")
file(MAKE_DIRECTORY ${_mbiHome}/run)
add_test(NAME scripting.e2e.material_bundle_import
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_bundle_import.js --headless
         WORKING_DIRECTORY ${_mbiHome}/run)
set_tests_properties(scripting.e2e.material_bundle_import PROPERTIES
    ENVIRONMENT "HOME=${_mbiHome};JAHSHAKA_DATA_ROOT=${_mbiHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED "materialBundleImportHome;materialBundleArchive"
    LABELS "scripting"
    TIMEOUT 120)

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
set_tests_properties(scripting.e2e.preset_apply PROPERTIES
    ENVIRONMENT "HOME=${_paHome};JAHSHAKA_DATA_ROOT=${_paHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED presetApplyHome
    LABELS "scripting"
    TIMEOUT 180)

# scripting.e2e.preset_edit — PRESET-EDIT-1: A PRESET A PROJECT HOLDS IS THE
# PROJECT'S TO EDIT, and the FIRST EDIT is what makes the copy (the owner's
# rule, 2026-09-21). The copy keeps the preset's NAME, the project's pin moves
# onto it, the meshes follow, the library master and every other project are
# untouched — and with no project open the master is still locked, which is
# the only lock left. Document verbs only -> --headless. A FRESH HOME, because
# the arms count library rows.
set(_peHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-preset_edit)
jah_fresh_home_fixture(scripting.e2e.preset_edit.fresh_home presetEditHome "${_peHome}" LABELS "scripting")
file(MAKE_DIRECTORY ${_peHome}/run)
add_test(NAME scripting.e2e.preset_edit
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_preset_edit.js --headless
         WORKING_DIRECTORY ${_peHome}/run)
set_tests_properties(scripting.e2e.preset_edit PROPERTIES
    ENVIRONMENT "HOME=${_peHome};JAHSHAKA_DATA_ROOT=${_peHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED presetEditHome
    LABELS "scripting"
    TIMEOUT 180)

# scripting.e2e.preset_seed_boot — THE FIRST-RUN SEED ON THE ROUTE A PERSON
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
configure_file(scripts/e2e_preset_seed_user.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_preset_seed_user.js @ONLY)
configure_file(scripts/e2e_preset_seed_boot.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_preset_seed_boot.js @ONLY)
set(_psHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-preset_seed_boot)
jah_fresh_home_fixture(scripting.e2e.preset_seed_boot.fresh_home presetSeedBootHome "${_psHome}" LABELS "scripting")
file(MAKE_DIRECTORY ${_psHome}/run)
add_test(NAME scripting.e2e.preset_seed_boot.user_first
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_preset_seed_user.js --headless
         WORKING_DIRECTORY ${_psHome}/run)
set_tests_properties(scripting.e2e.preset_seed_boot.user_first PROPERTIES
    ENVIRONMENT "HOME=${_psHome};JAHSHAKA_DATA_ROOT=${_psHome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED presetSeedBootHome
    FIXTURES_SETUP presetSeedBootUser
    LABELS "scripting"
    TIMEOUT 180)
jah_no_display(scripting.e2e.preset_seed_boot.user_first)
jah_gpu_row(scripting.e2e.preset_seed_boot CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_preset_seed_boot.js
         WORKING_DIRECTORY ${_psHome}/run)
set_tests_properties(scripting.e2e.preset_seed_boot PROPERTIES
    ENVIRONMENT "HOME=${_psHome};JAHSHAKA_DATA_ROOT=${_psHome}/.local/share/Jahshaka;JAHSHAKA_SEED_PRESETS=1"
    FIXTURES_REQUIRED "presetSeedBootHome;presetSeedBootUser"
    LABELS "scripting"
    TIMEOUT 180)

# scripting.e2e.preset_seed_race — THE USER'S IMPORT *DURING* THAT SEED
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
# MainWindow) and JAHSHAKA_SEED_PRESETS=1, like the boot arm.
set(_prHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-preset_seed_race)
configure_file(scripts/e2e_preset_seed_race.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_preset_seed_race.js @ONLY)
jah_fresh_home_fixture(scripting.e2e.preset_seed_race.fresh_home presetSeedRaceHome "${_prHome}" LABELS "scripting")
file(MAKE_DIRECTORY ${_prHome}/run)
jah_gpu_row(scripting.e2e.preset_seed_race CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_preset_seed_race.js
         WORKING_DIRECTORY ${_prHome}/run)
set_tests_properties(scripting.e2e.preset_seed_race PROPERTIES
    ENVIRONMENT "HOME=${_prHome};JAHSHAKA_DATA_ROOT=${_prHome}/.local/share/Jahshaka;JAHSHAKA_SEED_PRESETS=1"
    FIXTURES_REQUIRED presetSeedRaceHome
    LABELS "scripting"
    TIMEOUT 300)

# <pool>.tier — THE POOL'S TIER IS THE PROCESS'S (lane TEST-TIER-1, services/testtier.h): in a
# `TIER low` pool app.testTier() is "low", a new scene AND an opened one (saved at Medium) report
# world.mode() "low", and the window booted 1280x720; in a `TIER epic` pool the same script finds
# no test tier, a new scene at the document's Epic and the reopened one at the Medium it saved.
# One arm in a pool of each tier.
jah_pool_arm(editor tier ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_pool_tier.js 3)
jah_pool_arm(gi_verbs tier ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_pool_tier.js 3)
# THE SPLIT BY TIER (TEST-TIER-1, measured arm by arm, spikes/test-tier-1/okcount.md): editor,
# player, assets_import, avatar_anim, vr_noruntime, cameras run Low — the same verdict and the
# same count of `ok` lines as at Epic. editor_view is Epic: gizmo_rings (clickable handles at the
# boot window), render_stats (the Epic pre-pass), hierarchy_visibility (asserts the Epic GI) and
# the two arms moved into it (full_surface, play_select) are red at Low; particles, red at Low
# and short of its line after editor_view's window-changing arms, moved to shading_live.

# ---- THE POOLS (lane SUITE-POOL-1; tests/CMakeLists.txt, jah_pool_arm/jah_add_pool) --------
# One row, one process per family; every arm is declared beside the comment that says what it
# claims (above, and in tests/app, tests/shadergraph and tests/atom, which configure before this
# directory). The split of a family in two (gi_verbs/gi_movers, editor/editor_view,
# shading/shading_live, world_sky/world_light, doc/doc_assets) keeps every pool well under the
# -j4 critical path (~200 s).
# editor.material_drop — MATERIAL-DROP-1 + TRAY-DUPLICATE-1 (the owner, 2026-09-28): a LIBRARY
# drop is a fresh, pristine project copy ("Wood PBR", "Wood PBR 2", …), a PROJECT-tray drop
# assigns, assets.duplicate copies the project's own version by the same naming, and the
# viewport's real drag does the same. Engine up (the gesture), the Low tier.
configure_file(scripts/e2e_material_drop.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_drop.js @ONLY)
jah_pool_arm(editor material_drop ${CMAKE_CURRENT_BINARY_DIR}/e2e_material_drop.js 12)

jah_add_pool(doc HEADLESS LABELS scripting)
jah_add_pool(doc_assets HEADLESS LABELS scripting)
jah_add_pool(gi_verbs TIER epic LABELS scripting)
jah_add_pool(gi_movers TIER epic LABELS scripting)
jah_add_pool(editor TIER low LABELS scripting)
jah_add_pool(editor_view TIER epic LABELS scripting)
jah_add_pool(shading TIER epic LABELS scripting)
jah_add_pool(shading_live TIER epic LABELS scripting)
jah_add_pool(world_sky TIER epic LABELS scripting)
jah_add_pool(world_light TIER epic LABELS scripting)
jah_add_pool(player TIER low LABELS scripting)
jah_add_pool(assets_import TIER low LABELS scripting)
jah_add_pool(vr_noruntime TIER low LABELS scripting
    BASELINE ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pool_baseline_vr_noruntime.js)   # injected hands + gizmo mode, after every arm
jah_add_pool(avatar_anim TIER low LABELS scripting)
jah_add_pool(atom TIER epic LABELS scripting)

