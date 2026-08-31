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
# + manifest v2 + CAS objects, pins written on import).
configure_file(scripts/e2e_samples.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_samples.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-samples)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.samples
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_samples.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.samples PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 300)

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
