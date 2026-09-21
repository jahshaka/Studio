# Global illumination test — headless, framework-free, links JahshakaEngine ONLY.
# Proves Instant Radiosity, VCT and the VCT+PCC hybrid end to end: a red wall
# lit by a near-horizontal directional light must bounce red onto a floor the
# direct light only grazes, and turning GI off must restore the original pixels.
# Same runtime requirements as tests/engine (reachable DISPLAY, Vulkan driver
# or lavapipe).
add_executable(test_gi test_gi.cpp)
set_target_properties(test_gi PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi PRIVATE JahshakaEngine)
add_dependencies(test_gi JahshakaEngineMedia)
add_test(NAME gi.modes COMMAND test_gi
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.modes PROPERTIES TIMEOUT 120 LABELS "engine")

# The REAL PCC gate (REFLECTIONS_ADOPTION_SPEC.md §3): a closed room, a red wall
# behind the camera and a roughness-0 metal box whose reflection must contain
# that wall — plus Scene::giStatus() assertions, so buildPcc's silent
# "hybrid renders as plain VCT" bail fails the suite instead of passing it.
# Its OWN binary rather than a case in test_gi: the hybrid binds
# process-wide HlmsPbs state (sVctBindingOwner), so this scene must not share a
# process with gi.modes' scene (spec §9).
add_executable(test_gi_pcc_mirror test_gi_pcc_mirror.cpp)
set_target_properties(test_gi_pcc_mirror PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_pcc_mirror PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_pcc_mirror PRIVATE JahshakaEngine)
add_dependencies(test_gi_pcc_mirror JahshakaEngineMedia)
add_test(NAME gi.pcc_mirror COMMAND test_gi_pcc_mirror
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_mirror PROPERTIES TIMEOUT 120 LABELS "engine")

# GI BOUNDS + PROBE PLACEMENT (spec §4 / P1a). Its own binary for the same
# process-wide-binding reason as gi.pcc_mirror.
add_executable(test_gi_pcc_bounds test_gi_pcc_bounds.cpp)
set_target_properties(test_gi_pcc_bounds PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_pcc_bounds PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_pcc_bounds PRIVATE JahshakaEngine)
add_dependencies(test_gi_pcc_bounds JahshakaEngineMedia)
add_test(NAME gi.pcc_bounds COMMAND test_gi_pcc_bounds
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_bounds PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.cliff — THE GI-VOLUME CLIFF (LIGHTING_FIX fix 1) plus the engine half of
# the re-fit-on-exit contract (fix 2). Its own binary for the same
# process-wide-HlmsPbs-binding reason as its siblings.
add_executable(test_gi_cliff test_gi_cliff.cpp)
set_target_properties(test_gi_cliff PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cliff PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cliff PRIVATE JahshakaEngine)
add_dependencies(test_gi_cliff JahshakaEngineMedia)
add_test(NAME gi.cliff COMMAND test_gi_cliff
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cliff PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.ambient — THE VCT AMBIENT (LIGHTING_FIX fix 3). Own binary: process-wide
# HlmsPbs binding, same rule as its siblings.
add_executable(test_gi_ambient test_gi_ambient.cpp)
set_target_properties(test_gi_ambient PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_ambient PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_ambient PRIVATE JahshakaEngine)
add_dependencies(test_gi_ambient JahshakaEngineMedia)
add_test(NAME gi.ambient COMMAND test_gi_ambient
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.ambient PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.coalesce — the drag-a-light gate (spec §5 / P2). Links the MIRROR as well
# as the engine (like tests/mirror) because the coalescing gate is mirror-side
# and the cheap light-only path is engine-side; the contract is the pair.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_gi_coalesce test_gi_coalesce.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_gi_coalesce PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gi_coalesce PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gi_coalesce PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_coalesce PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_gi_coalesce JahshakaEngineMedia)
add_test(NAME gi.coalesce COMMAND test_gi_coalesce
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.coalesce PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")

# gi.sky_light — THE SKY LIGHT (SKY_LIGHT_SPEC.md §6). Mirror-linked for the
# same reason gi.coalesce is: the resolver and the SH scaling are mirror-side,
# the SH push, the VCT derivation and the sun disc are engine-side, and the
# contract is the pair.
add_executable(test_gi_sky_light test_gi_sky_light.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_gi_sky_light PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gi_sky_light PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gi_sky_light PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_sky_light PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_gi_sky_light JahshakaEngineMedia)
add_test(NAME gi.sky_light COMMAND test_gi_sky_light
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sky_light PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")

# gi.overlay — THE GI VOLUME OVERLAY (LIGHTING_FIX fix 9). Mirror-linked for the
# same reason gi.coalesce is: the overlay is mirror-side, the helper channel it
# rides is engine-side, and the contract is the pair.
add_executable(test_gi_overlay test_gi_overlay.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_gi_overlay PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gi_overlay PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gi_overlay PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_overlay PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_gi_overlay JahshakaEngineMedia)
add_test(NAME gi.overlay COMMAND test_gi_overlay
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")



# gi.budget — THE GI UPDATE BUDGET (FIX WAVE B1-B5). Mirror-linked for the same
# reason gi.coalesce is: the debounce and the counters are mirror-side and the
# budget, the sweep and the reuse arm are engine-side; the contract is the pair.
# Its own binary as well, like every hybrid suite here (process-wide HlmsPbs
# binding) and because it TIMES refreshes.
add_executable(test_gi_budget test_gi_budget.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_gi_budget PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gi_budget PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gi_budget PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_budget PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_gi_budget JahshakaEngineMedia)
add_test(NAME gi.budget COMMAND test_gi_budget
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.budget PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 300 LABELS "engine")

# gi.probe_inputs — THE PROBE CACHE'S INPUTS (ENGINE_CACHE_POLICY_SPEC P1 + P7).
# Reflection probes re-capture only when something they can see changes; this
# pins that a material albedo edit, a sky change, a light colour change and an
# ambient change each STALE the grid with the right reason, that the catch-up
# never exceeds the budget per frame, and that the scene idles afterwards — plus
# the pixel the old endless sweep was masking (a chrome cube's reflection
# follows an albedo edit). Mirror-linked like gi.budget: the light-parameter
# half of P7 is the mirror's GI signature.
add_executable(test_gi_probe_inputs test_gi_probe_inputs.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_gi_probe_inputs PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gi_probe_inputs PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gi_probe_inputs PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_probe_inputs PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_gi_probe_inputs JahshakaEngineMedia)
add_test(NAME gi.probe_inputs COMMAND test_gi_probe_inputs
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_inputs PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 300 LABELS "engine")

# PROBE REBUILD PARITY + SHRINK-FIT LOCALITY (2026-09-07 fix wave, defect 2).
# A CLUTTERED room — free-standing columns, which is what defeats the 1x1
# averaged-depth shrink-fit that gi.pcc_mirror's empty room never exercises —
# rebuilt six times at a fixed camera. Its own binary for the same
# process-wide-binding reason as gi.pcc_mirror.
add_executable(test_gi_pcc_stability test_gi_pcc_stability.cpp)
set_target_properties(test_gi_pcc_stability PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_pcc_stability PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_pcc_stability PRIVATE JahshakaEngine)
add_dependencies(test_gi_pcc_stability JahshakaEngineMedia)
add_test(NAME gi.pcc_stability COMMAND test_gi_pcc_stability
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_stability PROPERTIES TIMEOUT 120 LABELS "engine")

# DDGI — the irradiance-field diffuse layer (GI_UNIFIED_SPEC.md §4 P1). Its own
# binary for the same process-wide-binding reason as gi.pcc_mirror: the field
# binds to HlmsPbs globally (setIrradianceField), so it must not share a
# process with another suite's scene. It also RE-RUNS ITSELF (argv[1] == "hash")
# to prove cross-process byte determinism, which no in-process re-render can.
add_executable(test_gi_ddgi test_gi_ddgi.cpp)
set_target_properties(test_gi_ddgi PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_ddgi PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_ddgi PRIVATE JahshakaEngine)
add_dependencies(test_gi_ddgi JahshakaEngineMedia)
add_test(NAME gi.ddgi COMMAND test_gi_ddgi
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.ddgi PROPERTIES TIMEOUT 120 LABELS "engine")

# PHOTON's tier table, pins and migration (GI_UNIFIED_SPEC.md §2 / P2).
#
# The odd one out in this directory, deliberately: it links no engine of its
# own and renders nothing. Photon's phase-2 subject is RESOLUTION — which
# technique, quality and irradiance-field state a tier and its pins produce —
# and the renderer's half of that is already gated by gi.modes, gi.pcc_mirror
# and gi.ddgi, none of which this phase touches. Keeping the resolution suite
# beside them is what makes "gi.* is green" mean the whole GI surface.
#
# It compiles the registry straight in (the services layer is not a library),
# exactly like services.memory compiles the two registries it tests, and runs
# with no display at all.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_gi_tiers test_gi_tiers.cpp
    ${CMAKE_SOURCE_DIR}/src/services/worldmodes.cpp)
set_target_properties(test_gi_tiers PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_gi_tiers PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    # iris headers expose bullet types; this is IrisGL's own include path
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src
    # worldmodes.cpp reads the ENGINE's tier table (giQualityFacts, a
    # header-only function in jahshaka/engine/Types.h) so a tier's description
    # cannot drift from what the tier does — render audit A5. Header only; this
    # target links no engine.
    ${CMAKE_SOURCE_DIR}/irisgl/engine/include
)
target_link_libraries(test_gi_tiers PRIVATE IrisGL Qt6::Gui)
# A document node is an engine node (SCENEGRAPH_SPEC D2), so even this suite
# boots the NULL-render-system headless graph.
jah_document_graph(test_gi_tiers)
add_test(NAME gi.tiers COMMAND test_gi_tiers
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.tiers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "document" TIMEOUT 120)
jah_no_display(gi.tiers)

# gi.ddgi_ambient — THE PHOTON AMBIENT FIX (GI_UNIFIED_SPEC.md ADDENDUM
# CORRECTION; the upstream mechanism is in SPECS/OGRE_UPSTREAM_ISSUES.md's
# "Vct_piece_ps.any: volume-local ambient blend commented out" entry). Its own
# binary for the same process-wide-binding reason as gi.ddgi — and separate from
# gi.ddgi deliberately: that suite pins the field's BOUNCE and must keep its
# ambient at zero to do so, while this one is about the ambient and needs three
# scenes with a non-degenerate hemisphere pair.
add_executable(test_gi_ddgi_ambient test_gi_ddgi_ambient.cpp)
set_target_properties(test_gi_ddgi_ambient PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_ddgi_ambient PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_ddgi_ambient PRIVATE JahshakaEngine)
add_dependencies(test_gi_ddgi_ambient JahshakaEngineMedia)
add_test(NAME gi.ddgi_ambient COMMAND test_gi_ddgi_ambient
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.ddgi_ambient PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.hidden_volume — WHAT THE AUTOMATIC LIT VOLUME MAY CONTAIN (SMOKE_FIX S12 +
# S14): a hidden object stops bouncing light and stops defining the volume, and
# the automatic fit is capped in metres per voxel so a new project's ground
# plane cannot voxelise a square kilometre. Its own binary for the same
# process-wide-HlmsPbs-binding reason as its siblings.
add_executable(test_gi_hidden_volume test_gi_hidden_volume.cpp)
set_target_properties(test_gi_hidden_volume PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_hidden_volume PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_hidden_volume PRIVATE JahshakaEngine)
add_dependencies(test_gi_hidden_volume JahshakaEngineMedia)
add_test(NAME gi.hidden_volume COMMAND test_gi_hidden_volume
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hidden_volume PROPERTIES TIMEOUT 180 LABELS "engine")

# gi.volume_edge — THE AMBIENT MUST NOT STEP AT THE EDGE OF THE LIT VOLUME
# (ledger 177 defect A, the owner's "black rectangle"): a flat scene ambient
# reaches HlmsPbs through setAmbient's 1/pi scale and reached VctLighting
# unscaled, so the same setting was pi apart on the two sides of the volume
# boundary. Its own binary (process-wide HlmsPbs binding) like every GI suite.
add_executable(test_gi_volume_edge test_gi_volume_edge.cpp)
set_target_properties(test_gi_volume_edge PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_volume_edge PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_volume_edge PRIVATE JahshakaEngine)
add_dependencies(test_gi_volume_edge JahshakaEngineMedia)
add_test(NAME gi.volume_edge COMMAND test_gi_volume_edge
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.volume_edge PROPERTIES TIMEOUT 180 LABELS "engine")

# gi.mobility — MOVERS LEAVE THE STILL-WORLD LAYER (REALTIME_REFLECTIONS_SPEC
# §3.3, lane R2). A movable object is drawn by the per-frame layers (view,
# planar mirror, SSR) and casts shadows there in the frame it moves, and is
# absent from the reflection-probe captures, the voxel bounce and every GI
# signature — so a scene full of moving things costs the room's lighting
# nothing. Its own binary for the same process-wide-HlmsPbs-binding reason as
# its hybrid siblings.
add_executable(test_gi_mobility test_gi_mobility.cpp)
set_target_properties(test_gi_mobility PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_mobility PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_mobility PRIVATE JahshakaEngine)
add_dependencies(test_gi_mobility JahshakaEngineMedia)
add_test(NAME gi.mobility COMMAND test_gi_mobility
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.mobility PROPERTIES TIMEOUT 300 LABELS "engine")

# gi.ddgi_edge — THE IRRADIANCE FIELD'S EDGE (ENGINE_CACHE_POLICY_SPEC P9): a
# sealed box whose GI bounds top sits inside its ceiling slab, measured from
# OUTSIDE. Gates the cage clamp, the outside-the-volume fade and the fallback to
# the term the field's binding deletes (VctDisableDiffuse). Its own binary for
# the same process-wide-HlmsPbs-binding reason as its gi.ddgi siblings.
add_executable(test_gi_ddgi_edge test_gi_ddgi_edge.cpp)
set_target_properties(test_gi_ddgi_edge PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_ddgi_edge PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_ddgi_edge PRIVATE JahshakaEngine)
add_dependencies(test_gi_ddgi_edge JahshakaEngineMedia)
add_test(NAME gi.ddgi_edge COMMAND test_gi_ddgi_edge
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.ddgi_edge PROPERTIES TIMEOUT 300 LABELS "engine")

# THE MATERIAL GATE (owner 2026-09-13 Q1, ogre-patch 0028): a material with
# no specular response samples no reflection probe, and the ones beside it still
# do. Its own binary for the same process-wide-HlmsPbs-binding reason as its
# siblings.
add_executable(test_gi_probe_gate test_gi_probe_gate.cpp)
set_target_properties(test_gi_probe_gate PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_probe_gate PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_probe_gate PRIVATE JahshakaEngine)
add_dependencies(test_gi_probe_gate JahshakaEngineMedia)
add_test(NAME gi.probe_gate COMMAND test_gi_probe_gate
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_gate PROPERTIES TIMEOUT 180 LABELS "engine")

# VISIBILITY FROM THE PROBE (owner 2026-09-13 Q2, ogre-patch 0029): a surface
# occluded from a probe takes nothing from it. Synthetic room, never the
# Showroom (not open-to-open deterministic).
add_executable(test_gi_probe_visibility test_gi_probe_visibility.cpp)
set_target_properties(test_gi_probe_visibility PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_probe_visibility PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_probe_visibility PRIVATE JahshakaEngine)
add_dependencies(test_gi_probe_visibility JahshakaEngineMedia)
add_test(NAME gi.probe_visibility COMMAND test_gi_probe_visibility
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_visibility PROPERTIES TIMEOUT 180 LABELS "engine")

# NO ENCLOSURE MEANS NO PROBE GRID (owner 2026-09-13 Q3): an open scene builds
# no probes and reflects the sky; adding walls builds them.
add_executable(test_gi_probe_open test_gi_probe_open.cpp)
set_target_properties(test_gi_probe_open PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_probe_open PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_probe_open PRIVATE JahshakaEngine)
add_dependencies(test_gi_probe_open JahshakaEngineMedia)
add_test(NAME gi.probe_open COMMAND test_gi_probe_open
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_open PROPERTIES TIMEOUT 180 LABELS "engine")

# gi.pcc_second_scene — the PROCESS-WIDE probe binding against a second scene's
# own sky cubemap (lane SKY-FALLBACK-1). RUN_SERIAL is not needed: it owns both
# of its scenes and touches the HlmsPbs binding only through them.
add_executable(test_gi_pcc_second_scene test_gi_pcc_second_scene.cpp)
set_target_properties(test_gi_pcc_second_scene PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_pcc_second_scene PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_pcc_second_scene PRIVATE JahshakaEngine)
add_dependencies(test_gi_pcc_second_scene JahshakaEngineMedia)
add_test(NAME gi.pcc_second_scene COMMAND test_gi_pcc_second_scene
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_second_scene PROPERTIES TIMEOUT 180 LABELS "engine")

# PHOTON'S CAMERA-CENTRED VOXEL CASCADES (SPECS/PHOTON_SPEC.md P0): the chain
# builds and binds, a still camera re-voxelises nothing, a scroll re-centres one
# cascade per frame, the ambient reaches every cascade (upstream's 2,2,2 room),
# and a 1 km teleport is exactly N counted rebuilds with no black frame — the
# case that lost the GPU three ways on upstream's incremental path.
# Its own binary for the same process-wide-binding reason as every GI suite.
add_executable(test_gi_cascades test_gi_cascades.cpp)
set_target_properties(test_gi_cascades PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cascades PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cascades PRIVATE JahshakaEngine)
add_dependencies(test_gi_cascades JahshakaEngineMedia)
add_test(NAME gi.cascades COMMAND test_gi_cascades
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL and a wide timeout: the chain holds four voxel volumes at once
# (the VRAM-contention class, CLAUDE.md) and the teleport case renders and reads
# back a frame at a time.
set_tests_properties(gi.cascades PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# ATOM STAGE 1'S CONSUMER: WHICH MESH LOD A CASCADE VOXELISES (NANITE_SPEC §7
# stage 1's hand-off, ogre-patch 0064). A cascade hands the voxeliser the
# coarsest baked level whose error is below half its own cell — the authored
# geometry at the finest cell, a simplified one at the coarse ones — and the LOD
# BIAS, which is a dial over what is drawn, must not reach it. The A/B is in one
# process (GiParams::cascadeVoxelLod). RUN_SERIAL like every cascade suite: the
# chain holds four voxel volumes at once.
add_executable(test_gi_cascade_lod test_gi_cascade_lod.cpp)
set_target_properties(test_gi_cascade_lod PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cascade_lod PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cascade_lod PRIVATE JahshakaEngine)
add_dependencies(test_gi_cascade_lod JahshakaEngineMedia)
add_test(NAME gi.cascade_lod COMMAND test_gi_cascade_lod
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cascade_lod PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# gi.material_swap — MATERIAL-SWAP-GI-1: Scene::setNodeMaterial swaps the datablock
# on the live Item and invalidates the item's OWN box, so under the chain only
# the cascades that contain it re-voxelise; the old path (attachMesh) rebuilds
# every cascade and is the control. RUN_SERIAL like every cascade suite.
add_executable(test_gi_material_swap test_gi_material_swap.cpp)
set_target_properties(test_gi_material_swap PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_material_swap PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_material_swap PRIVATE JahshakaEngine)
add_dependencies(test_gi_material_swap JahshakaEngineMedia)
add_test(NAME gi.material_swap COMMAND test_gi_material_swap
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.material_swap PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# AN EDIT UNDER THE CHAIN COSTS AT MOST ONE CASCADE PER FRAME (PHOTON_SPEC §7 G1,
# and E2's own suite row). gi.cascades is about the camera SCROLL; this is about
# the four kinds of EDIT — geometry, material, spawn, light — each of which used
# to tear down N voxelisers and rebuild them in ONE frame. RUN_SERIAL like every
# cascade suite: the chain holds four voxel volumes at once.
add_executable(test_gi_cascade_dirty test_gi_cascade_dirty.cpp)
set_target_properties(test_gi_cascade_dirty PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cascade_dirty PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cascade_dirty PRIVATE JahshakaEngine)
add_dependencies(test_gi_cascade_dirty JahshakaEngineMedia)
add_test(NAME gi.cascade_dirty COMMAND test_gi_cascade_dirty
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cascade_dirty PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# gi.drag_cadence — WHAT A DRAG COSTS THE CACHES (DRAG-1, render audit REFLECT
# F2/F3): a dragged STILL item spends NO probe captures while it is moving (the
# staleness is recorded and the whole grid is re-captured at the settle, one per
# frame), a NON-motion input reaches the probes mid-drag anyway, and under a
# cascade chain the moving light tick never writes a second, different answer
# over the cascade the scheduler just rebuilt. RUN_SERIAL: the voxel lighting
# binds process-wide and the suite counts captures per frame.
add_executable(test_gi_drag_cadence test_gi_drag_cadence.cpp)
set_target_properties(test_gi_drag_cadence PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_drag_cadence PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_drag_cadence PRIVATE JahshakaEngine)
add_dependencies(test_gi_drag_cadence JahshakaEngineMedia)
add_test(NAME gi.drag_cadence COMMAND test_gi_drag_cadence
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.drag_cadence PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# gi.drag_mover — A DRAGGED STILL ON THE MOVER CHANNEL (MOVER-1, the render
# audit's PHOTON F3 second half). The rule is OFF by default and this suite
# measures both arms of the same gesture in one process: with it off a drag
# costs a re-voxelisation a frame, with it on a small constant; a single move is
# never a gesture; the object is on the mover channel while it is dragged and
# still world again at rest, with the DOCUMENT's own mobility untouched; and the
# picture at rest is the same picture either way (the assertion the whole trade
# rests on). RUN_SERIAL like every cascade suite.
add_executable(test_gi_drag_mover test_gi_drag_mover.cpp)
set_target_properties(test_gi_drag_mover PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_drag_mover PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_drag_mover PRIVATE JahshakaEngine)
add_dependencies(test_gi_drag_mover JahshakaEngineMedia)
add_test(NAME gi.drag_mover COMMAND test_gi_drag_mover
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.drag_mover PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# THE CHAIN'S BOUNCE IS A FUNCTION OF THE LIGHT, NOT OF ITS OWN HISTORY
# (ogre-patch 0060). The pin binds the EXTRA CASCADES' light voxels once and
# never again, while every cascade swaps its own on every bounce iteration, so
# at an ODD per-cascade count — which is what Photon's Epic tier produces,
# 1/3/7 on a four-cascade chain — the cross-cascade term integrates the previous
# injection's radiance. RUN_SERIAL like every cascade suite.
add_executable(test_gi_cascade_bounce_bindings test_gi_cascade_bounce_bindings.cpp)
set_target_properties(test_gi_cascade_bounce_bindings PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cascade_bounce_bindings PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cascade_bounce_bindings PRIVATE JahshakaEngine)
add_dependencies(test_gi_cascade_bounce_bindings JahshakaEngineMedia)
add_test(NAME gi.cascade_bounce_bindings COMMAND test_gi_cascade_bounce_bindings
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cascade_bounce_bindings PROPERTIES
    TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# THE CHAIN'S AT-REST INJECTION LANDS ON ITS FIXED POINT (LAMPREST-2). A
# re-injection of a cascade chain is one Jacobi iteration of its coupled
# radiance — each cascade reads the ones outside it and the volume it is
# injecting into — so a tick that stops short leaves the state it started from in
# the picture: measured at 4/255 in this room with two passes and at 0/255 with
# three, the engine's shipped count. The suite is a HISTORY test (the same lamp,
# the same pose, reached two ways) and it drives the engine's own
# JAHSHAKA_GI_SWEEPS diagnostic to prove the count is what makes it pass.
# RUN_SERIAL like every cascade suite.
add_executable(test_gi_chain_converge test_gi_chain_converge.cpp)
set_target_properties(test_gi_chain_converge PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_chain_converge PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_chain_converge PRIVATE JahshakaEngine)
add_dependencies(test_gi_chain_converge JahshakaEngineMedia)
add_test(NAME gi.chain_converge COMMAND test_gi_chain_converge
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.chain_converge PROPERTIES
    TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# THE SAME SCENE VOXELISES TO THE SAME PICTURE (ogre-patches 0061 and 0062): a
# cascade SCROLLED to a place renders what a cascade BUILT at it renders, two
# builds of one scene agree, and the attach order does not decide the answer.
# RUN_SERIAL like every cascade suite.
add_executable(test_gi_cascade_determinism test_gi_cascade_determinism.cpp)
set_target_properties(test_gi_cascade_determinism PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cascade_determinism PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cascade_determinism PRIVATE JahshakaEngine)
add_dependencies(test_gi_cascade_determinism JahshakaEngineMedia)
add_test(NAME gi.cascade_determinism COMMAND test_gi_cascade_determinism
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cascade_determinism PROPERTIES
    TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# THE IRRADIANCE FIELD RIDES CASCADE 0 (SPECS/PHOTON_SPEC.md E1): under a chain
# the field is built over the INNERMOST cascade's box and follows it as the
# camera walks — in the same frame the cascade moved, bound throughout, and
# converged by the end of that frame (the atlas describes another place until it
# is). Before E1 the arm refused to build a field under a chain at all.
# Its own binary for the same process-wide-binding reason as every GI suite.
add_executable(test_gi_field_follows test_gi_field_follows.cpp)
set_target_properties(test_gi_field_follows PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_field_follows PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_field_follows PRIVATE JahshakaEngine)
add_dependencies(test_gi_field_follows JahshakaEngineMedia)
add_test(NAME gi.field_follows COMMAND test_gi_field_follows
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL and a wide timeout for gi.cascades' reasons: four voxel volumes at
# once (the VRAM-contention class, CLAUDE.md) plus a 14 MB probe atlas, and the
# walk renders a frame at a time.
set_tests_properties(gi.field_follows PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# THE FIELD'S LEAK FIX SURVIVES THE MOVE ONTO A CASCADE (PHOTON_SPEC E1's bar,
# promoted from the PHOTON-S2 spike harness): a sealed room with a lamp outside
# it, four wall thicknesses, measured with the scene-fitted single volume and
# again with the chain. The leak is the only reason the irradiance field is in
# this engine, and E1 moves the field onto a volume that walks.
add_executable(test_gi_leak_room test_gi_leak_room.cpp)
set_target_properties(test_gi_leak_room PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_leak_room PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_leak_room PRIVATE JahshakaEngine)
add_dependencies(test_gi_leak_room JahshakaEngineMedia)
add_test(NAME gi.leak_room COMMAND test_gi_leak_room
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL and a wide timeout: eight full GI builds (four rooms x two arms),
# each with a shadowed room, a chain and a 14 MB probe atlas.
set_tests_properties(gi.leak_room PROPERTIES TIMEOUT 600 LABELS "engine" RUN_SERIAL ON)

# A SHADOW-ATLAS REBUILD IS NOT A GI REBUILD (PHOTON_SPEC gate G2, audit C
# finding 2): the atlas change drops only the WORKSPACES that instantiate the
# probe shadow node, never the voxels — so the 3rd casting lamp in a scene, or a
# Shadow Quality change, costs the voxel arm (and, under Photon, the whole
# cascade chain) exactly nothing.
# Its own binary for the same process-wide-binding reason as every GI suite.
add_executable(test_gi_atlas_voxels test_gi_atlas_voxels.cpp)
set_target_properties(test_gi_atlas_voxels PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_atlas_voxels PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_atlas_voxels PRIVATE JahshakaEngine)
add_dependencies(test_gi_atlas_voxels JahshakaEngineMedia)
add_test(NAME gi.atlas_keeps_voxels COMMAND test_gi_atlas_voxels
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL like gi.cascades: it holds the cascade chain AND a shadowed probe
# grid at once, which is the VRAM-contention class (CLAUDE.md).
set_tests_properties(gi.atlas_keeps_voxels PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# THE CASCADE SUITE UNDER ASan (PHOTON_SPEC G1, round-2 F1). The per-cascade
# dirty path replaces a live cascade's VOXELISER under a kept VctLighting, and
# every failure path around that swap is a candidate use-after-free: the lighting
# holds the voxeliser by raw pointer and `fillConstBufferData` walks it on every
# pass. Case 13 forces a throw on the far side of the swap on purpose; a build
# that deletes the replacement there reads freed memory on the next frame, and
# THIS is the binary that says so.
if(JAHSHAKA_ASAN AND TARGET JahshakaEngineAsan)
    add_executable(test_gi_cascades_asan test_gi_cascades.cpp)
    set_target_properties(test_gi_cascades_asan PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_gi_cascades_asan PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(test_gi_cascades_asan PRIVATE JahshakaEngineAsan)
    add_dependencies(test_gi_cascades_asan JahshakaEngineMedia)
    target_compile_options(test_gi_cascades_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_gi_cascades_asan PRIVATE -fsanitize=address)
    add_test(NAME gi.cascades_asan COMMAND test_gi_cascades_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # LeakSanitizer off for the same reason test_engine_asan turns it off (the
    # only leaks are inside libdbus via the Vulkan loader's device-select layer);
    # use-after-free detection is the point and stays on.
    set_tests_properties(gi.cascades_asan PROPERTIES TIMEOUT 600 LABELS "engine"
        RUN_SERIAL ON
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

# THE HARDWARE RAY-QUERY TIER (SPECS/PHOTON_SPEC.md §7 R1). Its own binary for
# the usual reason — an Ogre::Root per process — and because it switches the
# tier off and on again, which is device-wide state no other suite should share.
# It SKIPS (exit 0) on a device with no VK_KHR_ray_query: the no-rays path is a
# supported picture, not a failure.
add_executable(test_gi_rayquery test_gi_rayquery.cpp)
set_target_properties(test_gi_rayquery PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_rayquery PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/"
    JAH_RQ_LOG_TAG="plain")
target_link_libraries(test_gi_rayquery PRIVATE JahshakaEngine)
add_dependencies(test_gi_rayquery JahshakaEngineMedia)
add_test(NAME gi.rayquery COMMAND test_gi_rayquery
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rayquery PROPERTIES TIMEOUT 120 LABELS "engine")

# THE SAME SUITE WITH THE RAYS OFF (PHOTON_SPEC §7 R1's no-rays switch). It is
# the whole point of the switch that this GPU can render BOTH pictures, so the
# fallback is proved on every push rather than assumed. The binary takes its own
# no-rays branch and asserts the FALLBACK contract — no device extensions, no
# structures, no memory, a trace that refuses, and the scene still rendering.
add_test(NAME gi.rayquery_norays COMMAND test_gi_rayquery
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rayquery_norays PROPERTIES
    TIMEOUT 120 LABELS "engine"
    ENVIRONMENT "JAHSHAKA_NO_RAY_QUERY=1")

# THE SANITISED TWIN, WITH THE RAYS ON (PHOTON_SPEC §7 R1). The tier is the
# newest code in the engine and it hand-manages Vulkan memory, so it is exactly
# what a sanitizer is for. It runs at all only because `protect_shadow_gap=0`
# (tests/CMakeLists.txt) lets the driver create a ray-tracing device under ASan;
# the first cut of this lane booted every ASan suite with the tier OFF, which
# bought a green gate by never sanitising the new code.
if(JAHSHAKA_ASAN)
    add_executable(test_gi_rayquery_asan test_gi_rayquery.cpp)
    set_target_properties(test_gi_rayquery_asan PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_gi_rayquery_asan PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/"
        JAH_RQ_LOG_TAG="asan")
    target_link_libraries(test_gi_rayquery_asan PRIVATE JahshakaEngineAsan)
    add_dependencies(test_gi_rayquery_asan JahshakaEngineMedia)
    target_compile_options(test_gi_rayquery_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_gi_rayquery_asan PRIVATE -fsanitize=address)
    add_test(NAME gi.rayquery_asan COMMAND test_gi_rayquery_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.rayquery_asan PROPERTIES TIMEOUT 300 LABELS "engine"
        RUN_SERIAL ON
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

# GI OFF AND BACK ON AT RUNTIME (lane PATCHES-1 / GI-REENABLE-1): every other GI
# suite pushes its mode once, so the TRANSITION had no coverage at all — and it
# is what a user drives (the World panel's dropdown, `world.setGi`, a scene
# open). Two things here that no other suite does: it runs the hybrid with
# MULTIPLE BOUNCES (numBounces = 2, i.e. the bounce-inject compute job actually
# exists — ogre-patch 0057's subject), and it drains `takeLastError()` after
# every push, so an arm that throws its way out of a build fails the suite
# instead of rendering a dark picture quietly.
# Its own binary for the same process-wide-binding reason as every GI suite.
add_executable(test_gi_reenable test_gi_reenable.cpp)
set_target_properties(test_gi_reenable PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_reenable PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_reenable PRIVATE JahshakaEngine)
add_dependencies(test_gi_reenable JahshakaEngineMedia)
add_test(NAME gi.reenable COMMAND test_gi_reenable
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL and a wide timeout for the same reason as gi.cascades: the chain
# holds several voxel volumes at once (the VRAM-contention class, CLAUDE.md) and
# this suite builds the arm six times over.
set_tests_properties(gi.reenable PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# gi.chain_face — PHOTON-E3 item 0 (ii): the chain-to-world face and the faces
# inside the chain, under a REAL sky (audit B8). Its own binary like every GI
# suite (the voxel lighting binds process-wide to HlmsPbs).
add_executable(test_gi_chain_face test_gi_chain_face.cpp)
set_target_properties(test_gi_chain_face PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_chain_face PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_chain_face PRIVATE JahshakaEngine)
add_dependencies(test_gi_chain_face JahshakaEngineMedia)
add_test(NAME gi.chain_face COMMAND test_gi_chain_face
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.chain_face PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL TRUE)

# gi.boot_textures — THE ARM WAITS FOR THE ALBEDO IT IS ABOUT TO VOXELISE
# (BOOTVOX-1). The voxeliser reads a material's albedo and emissive maps, and a
# scene that binds one and pushes its GI in the same frame used to voxelise
# without it and then re-voxelise EVERY cascade when the pixels arrived — which
# is what the shipped default scene did on every boot (its ground's tile.png).
# RUN_SERIAL like every cascade suite: the chain holds four voxel volumes at
# once and the case counts rebuilds per frame.
add_executable(test_gi_boot_textures test_gi_boot_textures.cpp)
set_target_properties(test_gi_boot_textures PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_boot_textures PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_boot_textures PRIVATE JahshakaEngine)
add_dependencies(test_gi_boot_textures JahshakaEngineMedia)
add_test(NAME gi.boot_textures COMMAND test_gi_boot_textures
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.boot_textures PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# gi.stream_edit — AN EDIT THAT ARRIVES WHILE THE ARM IS STILL BEING BUILT
# (SPECS/OPEN_COVER_SPEC.md §2 A; lane OPEN-COVER-2a's fix round item 1).
#
# A world's first GI arm is built one STAGE per streaming driver frame now, so
# there is a three-to-five frame window in which the scene can be edited
# mid-build. The first cut ENDED the build there and the scene kept its cascades
# with no probe grid and no irradiance field for the rest of its life; the rule
# is that an invalidation REWINDS the machine and the scout re-fits the box.
# This is the only suite that can drive that window deterministically — it sets
# the frame pace itself, which a script cannot (editor.frame is always Complete,
# and that is the property case 4 pins).
#
# RUN_SERIAL like every cascade suite: the chain holds four voxel volumes.
add_executable(test_gi_stream_edit test_gi_stream_edit.cpp)
set_target_properties(test_gi_stream_edit PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_stream_edit PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_stream_edit PRIVATE JahshakaEngine)
add_dependencies(test_gi_stream_edit JahshakaEngineMedia)
add_test(NAME gi.stream_edit COMMAND test_gi_stream_edit
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.stream_edit PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

# SURFACE-CACHE-0 (the phase-0 design SPIKE, 2026-09-21) — a MEASUREMENT, not a
# gate. It captures one mesh instance's six axis cards through a capture
# workspace, reads them at a reflection ray's hits instead of the cascade's
# voxels, and PRINTS the three numbers SPECS/SURFACE_CACHE_ASSESSMENT.md §8
# records as unverified. Its own binary because the card read binds a descriptor
# the reflect pass otherwise never carries.
add_executable(test_gi_surface_cache_spike test_gi_surface_cache_spike.cpp)
set_target_properties(test_gi_surface_cache_spike PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_surface_cache_spike PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_surface_cache_spike PRIVATE JahshakaEngine)
add_dependencies(test_gi_surface_cache_spike JahshakaEngineMedia)
add_test(NAME gi.surface_cache_spike COMMAND test_gi_surface_cache_spike
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.surface_cache_spike PROPERTIES TIMEOUT 300 LABELS "engine")

# ---------------------------------------------------------------------------
# gi.gather — THE SCREEN-PROBE GATHER, phase 1
# (SPECS/SCREEN_PROBE_GATHER_SPEC.md section 7 phase 1).
#
# Its own binary because the gather binds descriptors and a pass texture no
# other suite's frames carry.
add_executable(test_gi_gather test_gi_gather.cpp)
set_target_properties(test_gi_gather PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_gather PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_gather PRIVATE JahshakaEngine)
add_dependencies(test_gi_gather JahshakaEngineMedia)
add_test(NAME gi.gather COMMAND test_gi_gather
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather PROPERTIES TIMEOUT 900 LABELS "engine")

# THE COST, at the resolution the spec's budget table is stated at.
#
# LABELLED `benchmark`, AND RUN_SERIAL, BECAUSE IT READS THE GPU'S CLOCK. The
# MERGE and PUSH tiers exclude that label by construction (-LE benchmark), and
# they must: this rig's GPU idles at 210 of 3105 MHz under Xvfb unless the
# clocks are locked by hand, so a GPU-time number taken inside a -j4 gate is
# not a measurement. The suite PRINTS its milliseconds and asserts only a
# catastrophe ceiling; the numbers of record are taken by hand with the clocks
# locked (spikes/gather-1a/FINDINGS.md).
add_test(NAME gi.gather_cost COMMAND test_gi_gather
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_cost PROPERTIES
                     TIMEOUT 900 LABELS "benchmark" RUN_SERIAL TRUE
                     ENVIRONMENT "JAH_GATHER_COST=1")

# ...and the picture a machine WITHOUT ray queries draws, which this lane may
# not move by one byte.
add_test(NAME gi.gather_norays COMMAND test_gi_gather
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_norays PROPERTIES
                     TIMEOUT 300 LABELS "engine"
                     ENVIRONMENT "JAHSHAKA_NO_RAY_QUERY=1")

# ---------------------------------------------------------------------------
# gi.gather_reference — THE ANALYTIC GATE (the lead's decision, ledger §937/§938)
#
# A rectangular Lambertian emitter over a diffuse plane has a CLOSED FORM for
# the irradiance it delivers (Lambert's formula for the projected solid angle of
# a polygon), and this suite measures all three of this renderer's diffuse-GI
# estimators against it at a set of floor points: the screen-probe gather, the
# voxel CONES and the irradiance FIELD. It is the first time any of them has
# been compared with a reference rather than with each other.
#
# Its own binary, and its own entry, because it is a different question from
# gi.gather's: that suite asks whether the gather behaves, this one asks whether
# it is RIGHT.
add_executable(test_gi_gather_reference test_gi_gather_reference.cpp)
set_target_properties(test_gi_gather_reference PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_gather_reference PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_gather_reference PRIVATE JahshakaEngine)
add_dependencies(test_gi_gather_reference JahshakaEngineMedia)
add_test(NAME gi.gather_reference COMMAND test_gi_gather_reference
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_reference PROPERTIES TIMEOUT 900 LABELS "engine")

# ...and the TRACE's own bar, in R5's form: a probe a metre from a lit red wall
# reads at least 0.6 of that wall's albedo times its lighting, in the wall's
# direction.
add_test(NAME gi.gather_trace COMMAND test_gi_gather_reference
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_trace PROPERTIES
                     TIMEOUT 600 LABELS "engine"
                     ENVIRONMENT "JAH_GATHER_TRACE=1")
