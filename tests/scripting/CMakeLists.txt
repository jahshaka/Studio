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
    ${CMAKE_SOURCE_DIR}/src/scripting/scriptengine.cpp)
set_target_properties(test_script_engine PROPERTIES AUTOMOC ON)
target_include_directories(test_script_engine PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/scripting)
target_link_libraries(test_script_engine PRIVATE Qt6::Widgets Qt6::Qml)
add_test(NAME scripting.engine COMMAND test_script_engine
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(scripting.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "scripting")

# ---- end-to-end: the real app binary running --script (phase 1) ----
#
# HOME is redirected to a per-test scratch dir so the runs get a FRESH
# JahLibrary.db and projects folder and can never touch the user's live
# library. XAUTHORITY/DISPLAY are inherited from the session (the engine needs
# a reachable X display; XWayland on :0 in CI-like runs, lavapipe covers no-GPU).
function(add_script_e2e name scriptfile)
    set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-${name})
    file(MAKE_DIRECTORY ${_home})
    file(MAKE_DIRECTORY ${_home}/run)
    add_test(NAME scripting.e2e.${name}
             COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/${scriptfile}
             WORKING_DIRECTORY ${_home}/run)
    set_tests_properties(scripting.e2e.${name} PROPERTIES
        ENVIRONMENT "HOME=${_home}"
        LABELS "scripting"
        TIMEOUT 180)
endfunction()

add_script_e2e(build_scene e2e_build_scene.js)
add_script_e2e(undo_macro e2e_undo_macro.js)
# SCENEGRAPH_SPEC §3 step 4: the v2 scene format's fragment pair
# (node.serialize/node.deserialize) and the persisted SCENE_STATIC override,
# driven through the verbs against the real database and a real save/reopen.
add_script_e2e(fragments e2e_fragments.js)
# PBR material persistence: container applies (imported-model roots), saved
# material assets by guid, and the full save/close/open round-trip of every
# PBR property (the owner-reported "materials lost on reopen" data loss).
add_script_e2e(pbr_materials e2e_pbr_materials.js)
# MATERIALS_EVALUATOR_SPEC: graph verbs -> bakeInfo/bake -> BakedMaps PNGs ->
# toMaterial/save/regenerate; baked maps land inside the project folder (and
# so ride the recursive .jaf zip).
add_script_e2e(bake_pipeline e2e_bake_pipeline.js)
# EDITOR_SHORTCUTS_SPEC §5: the verbs behind the Unreal-style editor controls.
add_script_e2e(editor_controls e2e_editor_controls.js)
# DESKTOP_SLIDER_SPEC: desktop.* view-mode + filmstrip verbs (window verbs).
add_script_e2e(desktops e2e_desktops.js)
# STATS_OVERLAY_SPEC phase 1 — the DATA half of the engine-drawn stats overlay,
# landed and tested before a pixel of it exists (the API-first rule). Pins the
# shape of app.renderStats(), the LAZY geometry counters, the invariants between
# the timing fields, and the driver's new work-per-tick numbers — the honest
# performance figure this architecture has, which the render loop used to
# measure and throw away.
add_script_e2e(render_stats e2e_render_stats.js)
# PLANAR_REFLECTIONS_SPEC §8: the reflector flag, the budget/resolution/shadow
# settings, the World Mode row driving them, and the save/reopen round trip.
add_script_e2e(planar e2e_planar.js)
# PARTICLES_FX2_SPEC §11 phase 2: the emitter verbs. scene.addParticles with a
# recipe, every scalar row through the shared node.setProperty, the two over-life
# ramps (which no Property kind can carry), the scene clock, the save round trip
# for keys that are all optional in the reader — and pixels, because a perfect
# document proves nothing about what the engine draws.
add_script_e2e(particles e2e_particles.js)
# DEEP_AUDIT_2026_09 List B #7: the anim.* verbs — the keyframe/timeline domain,
# which had no verbs and therefore no test at all. Animations on a node, the
# active one, tracks per property, keys (write/overwrite/read/remove), the
# document evaluation (seek poses the node between the keys), looping, and the
# save/close/open round trip. Every verb is a document verb -> --headless.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-anim)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.anim
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_anim.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.anim PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# The viewport's presentation state machine (editor.viewportState) — what
# decides whether the editor viewport shows engine frames or its loading cover.
# Gates the desktop-bleed fix, 2026-09-03.
#
# RENAMED from viewport_cover when the Qt cover widget was deleted (owner
# decision D2, SPECS/STATS_OVERLAY_SPEC.md §6): this suite never tested the
# widget, it tests the STATE MACHINE that drives it, which is exactly why its
# assertions did not change when the cover moved into the engine. That makes it
# the regression gate for the port.
add_script_e2e(presentation_state e2e_presentation_state.js)
# The IBL-cubemap-swap crash guard (2026-09-03): a sky re-bake frees and
# re-allocates the environment cubemap — usually AT THE SAME ADDRESS — while
# metallic datablocks, planar reflection passes and offscreen post-fx shots are
# all binding descriptor sets built from that pointer. The script's own header
# says why each ingredient is there; surviving it is the assertion.
add_script_e2e(sky_ibl_churn e2e_sky_ibl_churn.js)
set_tests_properties(scripting.e2e.sky_ibl_churn PROPERTIES TIMEOUT 300)

# phase 2: full surface (assets/materials/graph/world). The script needs real
# repo paths (a mesh to import, a shipped .effect) — configured in.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(scripts/e2e_full_surface.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_full_surface.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-full_surface)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.full_surface
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_full_surface.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.full_surface PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 240)

# Asset drawers (ASSET_DRAWERS_SPEC.md §4/§5): drawer verbs round-trip +
# importFile(png/wav) creating first-class AssetsView rows. Every verb is a
# document verb, so this runs --headless (offscreen platform, no engine) —
# fixture paths are configured in.
configure_file(scripts/e2e_drawers.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_drawers.js @ONLY)
configure_file(scripts/e2e_metadata.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_metadata.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-drawers)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.drawers
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_drawers.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.drawers PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# Rich asset metadata (ASSET_DRAWERS_SPEC addendum): import-time "metadata"
# blocks + the assets.metadata verb (with its lazy backfill). Image/audio/mesh
# imports here are all document-side, so this runs --headless too.
# Library asset deletion (the 2026-09-03 silent-no-op defect): assets.remove
# must report the database's real answer, and the row must actually go.
# Document-only, so --headless; fixture paths configured in.
configure_file(scripts/e2e_asset_delete.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_asset_delete.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-asset_delete)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.asset_delete
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_asset_delete.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.asset_delete PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# AI_SURFACE_AUDIT F1: scene.addMesh was a DATA-LOSS verb (disk path written
# where the reader expects a guid) with zero tests. It now refuses with the
# correct flow in the message, and the correct flow's save/close/reopen round
# trip — the test that never existed — is asserted here. Document verbs only.
configure_file(scripts/e2e_mesh_roundtrip.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_mesh_roundtrip.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-mesh_roundtrip)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.mesh_roundtrip
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_mesh_roundtrip.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.mesh_roundtrip PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

# AI_SURFACE_PROGRAM_SPEC lane D #7 + #12: assets.importAndPlace — the correct
# import in ONE call — and scene.addViewer. The round trip scene.addMesh never
# had (place -> save -> close -> reopen -> the geometry identity still
# resolves) is the reason this suite exists. Document verbs only -> --headless;
# fixture paths configured in.
configure_file(scripts/e2e_world_reach.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_world_reach.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-world_reach)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.world_reach
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_world_reach.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.world_reach PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

# AI_SURFACE_PROGRAM_SPEC lane D #14: node.physics / node.physicsInfo — the last
# UI-with-no-verb domain. Every scalar and both enums round-trip, every refusal
# is loud, the values survive a reopen, AND the settings reach Bullet (two
# identical spheres, one made dynamic and one static by the verb, simulated in
# the same world — one falls, one does not). ENGINE UP, not --headless: the
# physics stepper lives in the engine viewport's syncFrame and the headless
# stand-in's startPhysicsSimulation is an empty override.
add_script_e2e(physics e2e_physics.js)
set_tests_properties(scripting.e2e.physics PROPERTIES TIMEOUT 240)

# AI_SURFACE_AUDIT F7/F8/F9 — the three SILENT SUCCESSES. A verb that accepts a
# value it does not understand, changes nothing and answers true is the worst
# defect class on an agent surface. Document verbs only.
configure_file(scripts/e2e_ai_surface.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_ai_surface.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-ai_surface)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.ai_surface
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_ai_surface.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.ai_surface PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

# AI_SURFACE_PROGRAM_SPEC lane B #8 (verb half): scene.nodes({subtree, depth,
# include}). describe_scene is only the byte-carrying view of these options, so
# the bounding (childCount/truncated) and the enrichment blocks are gated here,
# on the verb. Document verbs only -> --headless.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-scene_query)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.scene_query
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_scene_query.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.scene_query PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

# AI_SURFACE_PROGRAM_SPEC lane 0 + lane A — DISCOVERY. app.apiProblems() runs
# ApiRegistry::validate() over the real module set (the unit test only ever saw
# its own fake module), and the lane A verbs that stop an agent guessing key
# names: node.properties, material.properties, the world set* aliases, the
# Ground primitive and addPrimitive {count}. Document verbs only -> --headless.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-introspection)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.introspection
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_introspection.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.introspection PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

# Web export (WEB_EXPORT_AUDIT): project.exportWeb through the real binary.
# Document-only, so --headless.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-export_web)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.export_web
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_export_web.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.export_web PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# Unified export front half (ASSET_PIPELINE_SPEC phase 5): assets.exportRaw +
# project.exportManifest through the real binary. Document-only, --headless;
# fixture paths configured in.
configure_file(scripts/e2e_export_raw.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_export_raw.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-export_raw)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.export_raw
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_export_raw.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.export_raw PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# Light asset bindings (LIGHTS_COMPLETION_SPEC): the .ies library type
# (import/validate/metadata), node.setLightProfile / node.setLightTexture, the
# renderer limitations reported honestly by the `applies` flags, and the D4
# boundary (a texture bound to a light is a DEPENDENCY — no companion
# material). Every verb is a document verb, so --headless.
configure_file(scripts/e2e_light_bindings.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_light_bindings.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-light_bindings)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.light_bindings
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_light_bindings.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.light_bindings PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-metadata)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.metadata
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_metadata.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.metadata PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# Video assets (ASSET_MEDIA_SPEC §1): importFile(mp4) → Video row + frame-grab
# thumbnail + probed metadata block. Qt Multimedia's ffmpeg backend decodes
# with no display (verified under QT_QPA_PLATFORM=offscreen), so this runs
# --headless like the drawers/metadata suites.
configure_file(scripts/e2e_video.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_video.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-video)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.video
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_video.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.video PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

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
file(MAKE_DIRECTORY ${_home}/run)
file(MAKE_DIRECTORY ${STORE_EMPTY_TARGET})
add_test(NAME scripting.e2e.store_root
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_store_root.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.store_root PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# The shipped sample scenes (the ONE data artifact Jahshaka ships): each
# bundled archive must import through the consolidated pipeline and open
# with content on a FRESH install (phase-4 archive format: catalog snapshot
# + manifest v2 + CAS objects, pins written on import). Runs ENGINE-UP (no
# --headless) since the grey-dragon regression: the gate renders every
# opened sample offscreen and asserts material fidelity at pixel probes
# (the gold dragon must be gold), plus the document-level material and
# texture-path assertions.
configure_file(scripts/e2e_samples.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_samples.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-samples)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.samples
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_samples.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.samples PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 480)

# The ONE import pipeline (phase 3): determinism record + checkConsistency +
# identical object sets on double import, in the real binary.
configure_file(scripts/e2e_import_pipeline.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_pipeline.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-import_pipeline)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.import_pipeline
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_pipeline.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.import_pipeline PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# IMAGE_PLANE_SPEC option A: scene.addImagePlane — aspect-correct plane
# (long side 1 m), camera-facing at creation, image as baseColorMap
# (roughness 1 / metallic 0, alpha images blend), dependency row. Engine up
# (no --headless): the camera-facing assertion reads editor.camera(), an
# engine verb. Fixture paths configured in.
configure_file(scripts/e2e_image_plane.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_plane.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-image_plane)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.image_plane
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_plane.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.image_plane PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# DECALS_SPEC: scene.addDecal — the projected-texture object, its box and
# material values, the image bound as a project BINDING (dependency row, no
# companion material), property round-trip, duplicate, undo and save/reopen.
# Document verbs only -> --headless; fixture paths configured in.
configure_file(scripts/e2e_decals.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_decals.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-decals)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.decals
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_decals.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.decals PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# IMAGE_PLANE_SPEC option B1: the companion image material — automatic on a
# DIRECT image add-to-project (owner call), never for dependency textures
# riding an object's closure, plus the manual materials.createFromImage verb;
# the material applies to a mesh and survives a reopen. Document verbs only
# -> --headless; fixture paths configured in.
configure_file(scripts/e2e_image_material.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_material.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-image_material)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.image_material
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_image_material.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.image_material PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

# IMAGE_PLANE_SPEC §6: pinned library assets hydrate on project REOPEN —
# add-to-project → close → reopen → material.apply by the pinned guid onto a
# cube → document material asserted. Document verbs only -> --headless;
# fixture paths configured in.
configure_file(scripts/e2e_pinned_reopen.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_pinned_reopen.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-pinned_reopen)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.pinned_reopen
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_pinned_reopen.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.pinned_reopen PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

# Post-release fix (2026-08-31): an imported textured GLB RENDERS — texture
# properties resolve to store files (no raw guid reaches Texture2D::load),
# the Mesh member carries the model bytes (scene placement has geometry),
# and the preview pixel shows the bound map. Engine up (no --headless).
configure_file(scripts/e2e_import_render.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_render.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-import_render)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.import_render
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_import_render.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.import_render PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

# VISUAL_PARITY_SPEC item 5: a saved shader GRAPH gets a real thumbnail —
# createGraph -> colour wired into Base Color -> save -> refreshThumbnail
# renders the evaluated PbrMaterial on the preview sphere, and the stored PNG's
# centre pixel is the graph's colour (assets.thumbnail reads it back). Engine up
# (no --headless): the render is the point.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-shader_thumbnail)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.shader_thumbnail
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_shader_thumbnail.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.shader_thumbnail PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 180)

# Avatar module, Part 0 (AVATAR_MODULE_SPEC S8): the Avatar page's surface
# driven from outside the app — space switch, loadPreview off disk (no project,
# no library, no database), clip display names, the pose through avatar.bones(),
# and avatar.snapshot proving all the toggle combinations at the pixel level.
# NOT --headless: the snapshot needs the engine.
configure_file(scripts/e2e_avatar.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_avatar.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-avatar)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.avatar
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_avatar.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.avatar PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 240)

# GPU_SKINNING_SPEC T10: a rigged character through the ONE import pipeline into
# a real project, reporting its rig through node.skinningMode / node.boneNames,
# with the engine viewport up. NOT --headless: the point is the production
# mirror and the production skinning path.
configure_file(scripts/e2e_skinning.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_skinning.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-skinning)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.skinning
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_skinning.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.skinning PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 240)

# World Modes (POST_CHAIN_SPEC.md §9): the scalability registry, tier
# write-through, pins that survive mode switches, and the save/open round-trip.
# Every verb is a document verb -> --headless.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-world_modes)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.world_modes
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_world_modes.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.world_modes PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 120)

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
jah_no_display(
    scripting.e2e.anim
    scripting.e2e.drawers
    scripting.e2e.asset_delete
    scripting.e2e.mesh_roundtrip
    scripting.e2e.world_reach
    scripting.e2e.ai_surface
    scripting.e2e.scene_query
    scripting.e2e.introspection
    scripting.e2e.export_web
    scripting.e2e.export_raw
    scripting.e2e.light_bindings
    scripting.e2e.metadata
    scripting.e2e.video
    scripting.e2e.store_root
    scripting.e2e.import_pipeline
    scripting.e2e.decals
    scripting.e2e.image_material
    scripting.e2e.pinned_reopen
    scripting.e2e.world_modes)
