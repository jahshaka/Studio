# SHADOW TOOLING phase 0 — the isolated spike (SPECS/SHADOW_TOOLING_SPEC.md §9).
#
# Unlike every other engine suite this one includes EnginePrivate.h and links
# Ogre directly: the question it answers ("can static and dynamic shadow maps
# share one atlas if the clear becomes per-map quads?") is about compositor
# mechanics that have no public verb yet, and phase 0's whole purpose is to
# answer it before a verb is designed around it.
add_executable(test_shadow_spike test_shadow_spike.cpp)
set_target_properties(test_shadow_spike PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_shadow_spike PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_include_directories(test_shadow_spike PRIVATE
    "${CMAKE_SOURCE_DIR}/irisgl/engine/src" "${JAHSHAKA_ENGINE_BUILD_ID_DIR}")
target_link_libraries(test_shadow_spike PRIVATE JahshakaEngine OgreNext)
add_dependencies(test_shadow_spike JahshakaEngineMedia)

# The arithmetic runs in the gate, and so does `cachekinds` (below); the other
# device-side modes are the phase-0 evidence and are driven by hand (and by the
# phase-1/2 suites that replace them).
add_test(NAME shadow.layout COMMAND test_shadow_spike layout
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadow.layout PROPERTIES TIMEOUT 120 LABELS "document")
jah_no_display(shadow.layout)

# THE LAMP-MAP CACHE ON THE PROBE AND REFLECT NODE INSTANCES, under the Khronos
# validation layer (ENGINE_CACHE_POLICY_SPEC §6 — E2's opening probe, kept). It
# found that an all-cached node with no sun let Ogre's barrier solver throw
# mid-pass and desync a layout: any "Validation Error" — or that solver
# message — fails the row, whatever the exit code says.
jah_gpu_row(shadow.cache_kinds CLASS engine COMMAND test_shadow_spike cachekinds
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadow.cache_kinds PROPERTIES
    TIMEOUT 120 LABELS "engine"
    ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
    FAIL_REGULAR_EXPRESSION "Validation Error;from Undefined to a read-only layout")

# THE GATE SUITE (SHADOW_TOOLING_SPEC.md §7): the budget, the derivation, the
# status readback and the rebuild churn, through the PUBLIC engine API only.
add_executable(test_shadow_maps test_shadow_maps.cpp)
set_target_properties(test_shadow_maps PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_shadow_maps PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_shadow_maps PRIVATE JahshakaEngine)
add_dependencies(test_shadow_maps JahshakaEngineMedia)
jah_gpu_row(lights.shadows CLASS engine COMMAND test_shadow_maps
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.shadows PROPERTIES TIMEOUT 120 LABELS "engine")

# shadow.atlas_rebuilds — THE HITCH COUNTER (T3x, E2 review / ledger 104/121).
# ITS OWN PROCESS on purpose: what it asserts is what an OPEN costs, and an
# open can only be seen on a VIRGIN atlas — the atlas never shrinks within a
# session (owner decision D4), so the first case to grow it takes that
# observation away from every case after it.
jah_gpu_row(shadow.atlas_rebuilds CLASS engine COMMAND test_shadow_maps t3x
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadow.atlas_rebuilds PROPERTIES TIMEOUT 120 LABELS "engine")

# shadow.atlas_binding — T3y, the other half of the same contract: nothing
# rebuilds the atlas while a world is still BINDING (no view has presented), and
# when it does come up the count and the clear strategy settle in ONE rebuild.
# Its own process for T3x's reason — a virgin atlas is the only place an open
# can be observed.
jah_gpu_row(shadow.atlas_binding CLASS engine COMMAND test_shadow_maps t3y
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadow.atlas_binding PROPERTIES TIMEOUT 120 LABELS "engine")

# AN EDITOR HELPER IS NOT A SHADOW CASTER (SHADOW-FIT-1): the shadow node's casters
# box — what fits the sun's splits — reads Ogre's caster bit under the VIEWPORT's
# mask, so a helper (a light icon, a gizmo part) that kept the bit moved the fit in
# the editor viewport and not in a screenshot. The helper's item carries no bit, the
# box is the real casters' alone, and the un-marked control proves the box reads it.
jah_gpu_row(shadow.helper_casters CLASS engine COMMAND test_shadow_spike helpercasters
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadow.helper_casters PROPERTIES TIMEOUT 120 LABELS "engine")

# CUTOUT-CASTER-1 — AN ALPHA-TESTED CASTER'S SHADOW IS ITS MAP, AND IT NEVER LOSES THE GPU.
# The fork's caster fix (HlmsPbs: the caster's root layout holds the datablock's reflection map,
# fork 5afd7a796): before it the cut-out's texture set spilled into the sampler binding and its
# shadow was solid, absent or an Xid 109 from process to process. Each row runs its script in
# TEN processes (shadow_casters.sh): one process proves nothing about the next.
#   shadow.cutout_caster    — a 3 m plane, a 4 x 4 hole checker, under the template's sun: the
#                             shadow is the checker cell by cell (the analytic projection: a
#                             horizontal caster over the floor casts a translated copy), the same
#                             10/10, and a SCROLLING cut-out's shadow holes move with the scroll.
#   shadow.two_sided_caster — the opaque plane two-sided, upright and turned over: the opaque
#                             plane's shadow (area within 10 %) 10/10.
jah_gpu_row(shadow.cutout_caster CLASS app TIER epic NEEDS photon bloom ssao smaa planar
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/shadow_casters.sh cutout
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/shadow_casters.js.in
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cutout_holes.png
                 ${CMAKE_CURRENT_BINARY_DIR}/shadow.cutout_caster 10
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(shadow.cutout_caster PROPERTIES LABELS "engine" TIMEOUT 900)
jah_gpu_row(shadow.two_sided_caster CLASS app TIER epic NEEDS photon bloom ssao smaa planar
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/shadow_casters.sh twosided
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/shadow_casters.js.in
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cutout_holes.png
                 ${CMAKE_CURRENT_BINARY_DIR}/shadow.two_sided_caster 10
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(shadow.two_sided_caster PROPERTIES LABELS "engine" TIMEOUT 900)
# gpu.cutout_soak — 5,000 frames of cut-outs (plain on a cube and a sphere, the live graph
# cut-out the hang was found on, the screenshot path, a pinned and a free clock), then 5,000 of
# the Tornado sample, under the Khronos validation layer. MINUTES OF ONE PROCESS: nightly; its
# push-time guard is shadow.cutout_caster's ten processes.
jah_gpu_row(gpu.cutout_soak CLASS app TIER epic NEEDS photon bloom ssao smaa planar
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gpu_cutout_soak.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gpu_cutout_soak.js.in
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cutout_holes.png
                 ${CMAKE_SOURCE_DIR}/scenes/Tornado.zip
                 ${CMAKE_CURRENT_BINARY_DIR}/gpu.cutout_soak
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(gpu.cutout_soak PROPERTIES
    LABELS "engine;nightly" TIMEOUT 1800
    ENVIRONMENT "VK_LOADER_LAYERS_ENABLE=VK_LAYER_KHRONOS_validation"
    PASS_REGULAR_EXPRESSION "gpu_cutout_soak: PASS"
    FAIL_REGULAR_EXPRESSION "Validation Error;DEVICE WAS LOST;assert failed")
