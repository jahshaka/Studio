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
jah_gpu_row(gi.modes CLASS engine COMMAND test_gi
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.modes PROPERTIES TIMEOUT 120 LABELS "engine")

# The REAL PCC gate (REFLECTIONS_ADOPTION_SPEC.md §3): a closed room, a red wall
# behind the camera and a roughness-0 metal box whose reflection must contain
# that wall — plus Scene::giStatus() assertions, so buildPcc's silent
# "hybrid renders as plain VCT" bail fails the suite instead of passing it.
# Its OWN binary rather than a case in test_gi (spec §9).
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
jah_gpu_row(gi.pcc_mirror CLASS engine COMMAND test_gi_pcc_mirror
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_mirror PROPERTIES TIMEOUT 120 LABELS "engine")

# GI BOUNDS + PROBE PLACEMENT (spec §4 / P1a). Its own binary.
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
jah_gpu_row(gi.pcc_bounds CLASS engine COMMAND test_gi_pcc_bounds
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_bounds PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.cliff — THE GI-VOLUME CLIFF (LIGHTING_FIX fix 1) plus the engine half of
# the re-fit-on-exit contract (fix 2). Its own binary.
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
jah_gpu_row(gi.cliff CLASS engine COMMAND test_gi_cliff
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cliff PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.ambient — THE VCT AMBIENT (LIGHTING_FIX fix 3). Own binary.
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
jah_gpu_row(gi.ambient CLASS engine COMMAND test_gi_ambient
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
jah_gpu_row(gi.coalesce CLASS engine COMMAND test_gi_coalesce
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
jah_gpu_row(gi.sky_light CLASS engine COMMAND test_gi_sky_light
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
jah_gpu_row(gi.overlay CLASS engine COMMAND test_gi_overlay
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")



# gi.budget — THE GI UPDATE BUDGET (FIX WAVE B1-B5). Mirror-linked for the same
# reason gi.coalesce is: the debounce and the counters are mirror-side and the
# budget, the sweep and the reuse arm are engine-side; the contract is the pair.
# Its own binary as well, because it TIMES refreshes.
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
# DEVPROCESS-1: registered through the GPU-timing lock; TIMEOUT = RUN_TIMEOUT + the lock's 900 s wait.
jah_gpu_exclusive_test(NAME gi.budget CLASS engine RUN_TIMEOUT 300 COMMAND test_gi_budget
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.budget PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     LABELS "engine")

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
jah_gpu_row(gi.probe_inputs CLASS engine COMMAND test_gi_probe_inputs
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_inputs PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")

# PROBE REBUILD PARITY + SHRINK-FIT LOCALITY (2026-09-07 fix wave, defect 2).
# A CLUTTERED room — free-standing columns, which is what defeats the 1x1
# averaged-depth shrink-fit that gi.pcc_mirror's empty room never exercises —
# rebuilt six times at a fixed camera. Its own binary.
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
jah_gpu_row(gi.pcc_stability CLASS engine COMMAND test_gi_pcc_stability
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_stability PROPERTIES TIMEOUT 120 LABELS "engine")

# DDGI — the irradiance-field diffuse layer (GI_UNIFIED_SPEC.md §4 P1). Its own
# binary and process because it RE-RUNS ITSELF (argv[1] == "hash") to prove
# cross-process byte determinism, which no in-process re-render can.
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
jah_gpu_row(gi.ddgi CLASS engine COMMAND test_gi_ddgi
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

# photon.tiers — THE SETTINGS TRUTH FOR PHOTON (D4-PHOTON-TIERS): every row x tier
# has a value, the sentences are generated from the one table, the SSR row reads
# "Traced" at a ray tier, the gather row, the tri-state resolved one way, Epic as a
# quality row, the probe grid's VRAM budget arithmetic and the declared offscreen
# picture table. Pure tables: no display.
add_executable(test_photon_tiers test_photon_tiers.cpp
    ${CMAKE_SOURCE_DIR}/src/services/worldmodes.cpp)
set_target_properties(test_photon_tiers PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_photon_tiers PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src
    ${CMAKE_SOURCE_DIR}/irisgl/engine/include)
target_link_libraries(test_photon_tiers PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_photon_tiers)
add_test(NAME photon.tiers COMMAND test_photon_tiers
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(photon.tiers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "document" TIMEOUT 120)
jah_no_display(photon.tiers)

# gi.ddgi_ambient — THE PHOTON AMBIENT FIX (GI_UNIFIED_SPEC.md ADDENDUM
# CORRECTION; the upstream mechanism is in SPECS/OGRE_UPSTREAM_ISSUES.md's
# "Vct_piece_ps.any: volume-local ambient blend commented out" entry). Its own
# binary, separate from gi.ddgi deliberately: that suite pins the field's BOUNCE and must keep its
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
jah_gpu_row(gi.ddgi_ambient CLASS engine COMMAND test_gi_ddgi_ambient
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.ddgi_ambient PROPERTIES TIMEOUT 120 LABELS "engine")
# gi.sealed_room_chain_target — SEALED-ROOM-LEAK-1: case 5's sealed room under the SHIPPED Medium
# chain with the field on, the eye 0.6 m from a wall: the sky must move no pixel more than 2/255.
# Reads 12/255 today (the room's far corner lies outside cascade 0's box - the field's volume - and
# the cone reader alone leaks 4-9/255 there at every cell size); a photon-target row until the
# reader's residual or a field over the chain closes it. Prints the cones-only table.
# LOCATED (PHOTON-PHYSICS-1, re-measured; test_gi_ddgi_ambient --sealed-classes, not a row): the
# 12/255 at the wall pose is NOT specular - the same room with the walls' F0 = 0 reads 11/255 there
# (D8's "0/255" arm drew an EMPTY room: a mesh cache keyed on a recycled Scene pointer). With the
# cone diffuse's escape zeroed it reads 7, with the specular escape zeroed too 4 (the field's
# own); the specular escape alone moves 7 -> 3 in the cones-only room. So the target waits on
# the diffuse reader's sealed-room residual beyond the field (VOXEL-5's classes) and the field's;
# spikes/photon-physics-1/ has the arms.
jah_gpu_row(gi.sealed_room_chain_target CLASS engine COMMAND test_gi_ddgi_ambient --sealed-chain
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sealed_room_chain_target PROPERTIES TIMEOUT 300 LABELS "engine;photon-target")
# gi.cone_corner_target — THE CONE'S CORNER OVER-READ (PHOTON-FIELD-ROTATE-1, F2):
# gi.ddgi_ambient's corner used to compare the field with the cone reference; it is
# anchored on the fixture's ANALYTIC now, and the cone reference - which reads a
# 2.2 m wall's foot at 93.9 % of open floor against the analytic 82 % (+11.7
# points: the cone's footprint against a wall edge) - is this row, against the same
# analytic and the same bar. LABEL `photon-target`: it runs in every scoped
# selection and PRINTS its value, and it does NOT decide a gate. The lane that
# makes the cone read a corner right deletes the label.
jah_gpu_row(gi.cone_corner_target CLASS engine COMMAND test_gi_ddgi_ambient --cone-target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cone_corner_target PROPERTIES TIMEOUT 180
                     LABELS "engine;photon-target")

# gi.hidden_volume — WHAT THE AUTOMATIC LIT VOLUME MAY CONTAIN (SMOKE_FIX S12 +
# S14): a hidden object stops bouncing light and stops defining the volume, and
# the automatic fit is capped in metres per voxel so a new project's ground
# plane cannot voxelise a square kilometre. Its own binary.
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
jah_gpu_row(gi.hidden_volume CLASS engine COMMAND test_gi_hidden_volume
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hidden_volume PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.volume_edge — THE AMBIENT MUST NOT STEP AT THE EDGE OF THE LIT VOLUME
# (ledger 177 defect A, the owner's "black rectangle"): a flat scene ambient
# reaches HlmsPbs through setAmbient's 1/pi scale and reached VctLighting
# unscaled, so the same setting was pi apart on the two sides of the volume
# boundary. Its own binary like every GI suite.
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
jah_gpu_row(gi.volume_edge CLASS engine COMMAND test_gi_volume_edge
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.volume_edge PROPERTIES TIMEOUT 120 LABELS "engine")
# gi.volume_edge_spec_target — THE SAME BINARY ON A SPECULAR SLAB (PHOTON-ENV-1,
# audit F4): the voxel specular cone's escape (the raw directional composite,
# which reads the slab's own voxels at a coarse mip) against the environment
# outside the volume steps at the face. Owned by PHOTON-PCC-1 / REFLECT-1 (three
# reflection sources make the cone's step irrelevant at the ray tiers). GREEN, and
# the `photon-target` label is off (lane D6B-GATE-SHAPE, 2026-09-27: 3/3 green on the
# rig, 0.0000 and 0.0011 against the 0.05 bar — the audit's §8 found it passing at
# 94d147c28): an ordinary gating row now.
jah_gpu_row(gi.volume_edge_spec_target CLASS engine COMMAND test_gi_volume_edge --target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.volume_edge_spec_target PROPERTIES TIMEOUT 180
                     LABELS "engine")

# gi.mobility — MOVERS LEAVE THE STILL-WORLD LAYER (REALTIME_REFLECTIONS_SPEC
# §3.3, lane R2). A movable object is drawn by the per-frame layers (view,
# planar mirror, SSR) and casts shadows there in the frame it moves, and is
# absent from the reflection-probe captures, the voxel bounce and every GI
# signature — so a scene full of moving things costs the room's lighting
# nothing. Its own binary.
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
jah_gpu_row(gi.mobility CLASS engine COMMAND test_gi_mobility
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.mobility PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.ddgi_edge — THE IRRADIANCE FIELD'S EDGE (ENGINE_CACHE_POLICY_SPEC P9): a
# sealed box whose GI bounds top sits inside its ceiling slab, measured from
# OUTSIDE. Gates the cage clamp, the outside-the-volume fade and the fallback to
# the term the field's binding deletes (VctDisableDiffuse). Its own binary.
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
jah_gpu_row(gi.ddgi_edge CLASS engine COMMAND test_gi_ddgi_edge
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.ddgi_edge PROPERTIES TIMEOUT 120 LABELS "engine")

# THE MATERIAL GATE (owner 2026-09-13 Q1, fork 36162ff37+16d8e29d4 (was 0028)): a material with
# no specular response samples no reflection probe, and the ones beside it still
# do. Its own binary.
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
jah_gpu_row(gi.probe_gate CLASS engine COMMAND test_gi_probe_gate
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_gate PROPERTIES TIMEOUT 120 LABELS "engine")

# VISIBILITY FROM THE PROBE (owner 2026-09-13 Q2, fork 4d5fbef16 (was 0029)): a surface
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
jah_gpu_row(gi.probe_visibility CLASS engine COMMAND test_gi_probe_visibility
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_visibility PROPERTIES TIMEOUT 120 LABELS "engine")

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
jah_gpu_row(gi.probe_open CLASS engine COMMAND test_gi_probe_open
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_open PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.pcc_second_scene — a second scene's mirror reads its OWN sky cubemap while
# another scene holds a probe grid, is torn down, is destroyed (lane
# SKY-FALLBACK-1; the per-pass GI binding, PHOTON-SCENE-SWITCH-1).
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
jah_gpu_row(gi.pcc_second_scene CLASS engine COMMAND test_gi_pcc_second_scene
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_second_scene PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.scene_switch — WHAT THE SHADER READS IS THE SCENE BEING DRAWN
# (PHOTON-SCENE-SWITCH-1): two rooms, each with its own voxels, probe grid, field,
# sun and caster, drawn through two offscreen views and a thumbnail-shaped one —
# A's passes first, alternating frames, B's passes first, and A destroyed — and
# every judged frame each mirror reads its own room and each floor is shadowed.
add_executable(test_gi_scene_switch test_gi_scene_switch.cpp)
set_target_properties(test_gi_scene_switch PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_scene_switch PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_scene_switch PRIVATE JahshakaEngine)
add_dependencies(test_gi_scene_switch JahshakaEngineMedia)
jah_gpu_row(gi.scene_switch CLASS engine COMMAND test_gi_scene_switch
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.scene_switch PROPERTIES TIMEOUT 120 LABELS "engine")

# PHOTON'S CAMERA-CENTRED VOXEL CASCADES (SPECS/PHOTON_SPEC.md P0): the chain
# builds and binds, a still camera re-voxelises nothing, a scroll re-centres one
# cascade per frame, the ambient reaches every cascade (upstream's 2,2,2 room),
# and a 1 km teleport is exactly N counted rebuilds with no black frame — the
# case that lost the GPU three ways on upstream's incremental path.
# Its own binary.
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
jah_gpu_row(gi.cascades CLASS engine COMMAND test_gi_cascades
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# THE gi_chain FAMILY (tests/CMakeLists.txt): one cascade-chain suite at a time,
# the rest of the gate beside it (the old VRAM reason was measured false,
# DEVPROCESS-1). A wide timeout: the teleport case renders and reads back a frame
# at a time.
set_tests_properties(gi.cascades PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# ATOM STAGE 1'S CONSUMER: WHICH MESH LOD A CASCADE VOXELISES (NANITE_SPEC §7
# stage 1's hand-off, fork ad452604a+0338ca7f2 (was 0064)). A cascade hands the voxeliser the
# coarsest baked level whose error is below half its own cell — the authored
# geometry at the finest cell, a simplified one at the coarse ones — and the LOD
# BIAS, which is a dial over what is drawn, must not reach it. The A/B is in one
# process (GiParams::cascadeVoxelLod). In the gi_chain family like every cascade
# suite.
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
jah_gpu_row(gi.cascade_lod CLASS engine COMMAND test_gi_cascade_lod
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cascade_lod PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.voxel_resident — ATOM P4: the voxeliser reads the RASTER's own vertex and
# index buffers through their device addresses, so the vertex format and the index
# width stopped being shader permutations and bucket keys (one dispatch for a
# 16-bit and a 32-bit mesh sharing a material).
add_executable(test_gi_voxel_resident test_gi_voxel_resident.cpp)
set_target_properties(test_gi_voxel_resident PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_voxel_resident PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_voxel_resident PRIVATE JahshakaEngine)
add_dependencies(test_gi_voxel_resident JahshakaEngineMedia)
jah_gpu_row(gi.voxel_resident CLASS engine COMMAND test_gi_voxel_resident
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.voxel_resident PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.material_swap — MATERIAL-SWAP-GI-1: Scene::setNodeMaterial swaps the datablock
# on the live Item and invalidates the item's OWN box, so under the chain only
# the cascades that contain it re-voxelise; the old path (attachMesh) rebuilds
# every cascade and is the control. In the gi_chain family like every cascade suite.
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
jah_gpu_row(gi.material_swap CLASS engine COMMAND test_gi_material_swap
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.material_swap PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# AN EDIT UNDER THE CHAIN COSTS AT MOST ONE CASCADE PER FRAME (PHOTON_SPEC §7 G1,
# and E2's own suite row). gi.cascades is about the camera SCROLL; this is about
# the four kinds of EDIT — geometry, material, spawn, light — each of which used
# to tear down N voxelisers and rebuild them in ONE frame. In the gi_chain
# family like every cascade suite.
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
jah_gpu_row(gi.cascade_dirty CLASS engine COMMAND test_gi_cascade_dirty
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cascade_dirty PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.drag_cadence — WHAT A DRAG COSTS THE CACHES (DRAG-1, render audit REFLECT
# F2/F3): a dragged STILL item spends NO probe captures while it is moving (the
# staleness is recorded and the whole grid is re-captured at the settle, one per
# frame), a NON-motion input reaches the probes mid-drag anyway, and under a
# cascade chain the moving light tick never writes a second, different answer
# over the cascade the scheduler just rebuilt. In the gi_chain family like
# every cascade suite (its per-frame capture counts are in-process).
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
jah_gpu_row(gi.drag_cadence CLASS engine COMMAND test_gi_drag_cadence
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.drag_cadence PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.drag_mover — A DRAGGED STILL ON THE MOVER CHANNEL (MOVER-1, the render
# audit's PHOTON F3 second half). The rule is OFF by default and this suite
# measures both arms of the same gesture in one process: with it off a drag
# costs a re-voxelisation a frame, with it on a small constant; a single move is
# never a gesture; the object is on the mover channel while it is dragged and
# still world again at rest, with the DOCUMENT's own mobility untouched; and the
# picture at rest is the same picture either way (the assertion the whole trade
# rests on). In the gi_chain family like every cascade suite.
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
jah_gpu_row(gi.drag_mover CLASS engine COMMAND test_gi_drag_mover
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.drag_mover PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# THE CHAIN'S BOUNCE IS A FUNCTION OF THE LIGHT, NOT OF ITS OWN HISTORY
# (fork ae2ed529f (was 0060)). The pin binds the EXTRA CASCADES' light voxels once and
# never again, while every cascade swaps its own on every bounce iteration, so
# at an ODD per-cascade count — which is what Photon's Epic tier produces,
# 1/3/7 on a four-cascade chain — the cross-cascade term integrates the previous
# injection's radiance. In the gi_chain family like every cascade suite.
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
jah_gpu_row(gi.cascade_bounce_bindings CLASS engine COMMAND test_gi_cascade_bounce_bindings
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cascade_bounce_bindings PROPERTIES
    TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# ONE AT-REST SWEEP IS THE CHAIN'S FIXED POINT, BYTE FOR BYTE (PHOTON-WRITER-1
# SWEEPS-3; derived in EnginePrivate.h beside kAtRestSweeps): from a perturbed
# history, a second at-rest sweep changes no byte of any cascade's light volumes
# (GiVoxelStats::lightDigest), at Medium and at High; a lamp that travelled
# leaves the bytes the same lamp jumped there leaves. (The old _target row's
# claim - one sweep within half a code of ten - is this row's assertion now.)
# In the gi_chain family like every cascade suite.
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
jah_gpu_row(gi.chain_converge CLASS engine COMMAND test_gi_chain_converge
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.chain_converge PROPERTIES
    TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# THE SAME SCENE VOXELISES TO THE SAME PICTURE (fork ad452604a+155a56bf8 (was 0061 and 0062)): a
# cascade SCROLLED to a place renders what a cascade BUILT at it renders, two
# builds of one scene agree, and the attach order does not decide the answer.
# In the gi_chain family like every cascade suite.
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
jah_gpu_row(gi.cascade_determinism CLASS engine COMMAND test_gi_cascade_determinism
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cascade_determinism PROPERTIES
    TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# THE IRRADIANCE FIELD RIDES CASCADE 0 (SPECS/PHOTON_SPEC.md E1): under a chain
# the field is built over the INNERMOST cascade's box and follows it as the
# camera walks — in the same frame the cascade moved, bound throughout, and
# converged by the end of that frame (the atlas describes another place until it
# is). Before E1 the arm refused to build a field under a chain at all.
# Its own binary.
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
jah_gpu_row(gi.field_follows CLASS engine COMMAND test_gi_field_follows
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# In the gi_chain family like every cascade suite, and a wide timeout: the walk
# renders a frame at a time.
set_tests_properties(gi.field_follows PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.field_follows.timing — the walk's frame-time bars (no frame over the tier's 16.7 ms),
# nightly (lane D6B-GATE-SHAPE; audit §5 F17 FIX-TIME): the same binary with
# JAHSHAKA_TIMING_BARS=1, quiet box, the GPU-timing lock.
jah_gpu_exclusive_test(NAME gi.field_follows.timing CLASS engine RUN_TIMEOUT 300 COMMAND test_gi_field_follows
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.field_follows.timing PROPERTIES
    ENVIRONMENT "JAHSHAKA_TIMING_BARS=1" LABELS "engine;nightly;quiet-box")
set_property(TEST gi.field_follows.timing APPEND PROPERTY RESOURCE_LOCK gi_chain)
# gi.socket_camera — THE GI FOLLOWS A SOCKETED CAMERA (GI-SOCKET-CAM-1): a View
# camera riding a node (setCameraNode, the Player's avatar camera) reports an
# identity LOCAL pose; the GI tracking and the particle camera must read its
# WORLD position. The node walks 30 m; cascade 0 re-centres on it.
add_executable(test_gi_socket_camera test_gi_socket_camera.cpp)
set_target_properties(test_gi_socket_camera PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_socket_camera PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_socket_camera PRIVATE JahshakaEngine)
add_dependencies(test_gi_socket_camera JahshakaEngineMedia)
jah_gpu_row(gi.socket_camera CLASS engine COMMAND test_gi_socket_camera
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.socket_camera PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.field_scroll — THE IRRADIANCE FIELD SCROLLS (PHOTON-WRITER-1, FIELD-SCROLL):
# a cascade-0 step keeps the probes that stay in the field's toroidal window byte
# for byte and integrates only the planes that entered; three steps out and back
# render the same picture; the step frame's GPU cost against a whole re-placement,
# in one process. RUN_SERIAL like every cascade suite.
add_executable(test_gi_field_scroll test_gi_field_scroll.cpp)
set_target_properties(test_gi_field_scroll PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_field_scroll PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_field_scroll PRIVATE JahshakaEngine)
add_dependencies(test_gi_field_scroll JahshakaEngineMedia)
# UNDER THE GPU-TIMING LOCK (POST-C-FIXES-1): its jump bar is the jump frame's GPU
# work against the tier's 16.7 ms frame and its step-frame bars are GPU ratios -
# numbers a sibling sharing the GPU moves (docs/TESTING_GATE.md section 4).
jah_gpu_exclusive_test(NAME gi.field_scroll CLASS engine RUN_TIMEOUT 300 COMMAND test_gi_field_scroll
                       WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.field_scroll PROPERTIES LABELS "engine" RUN_SERIAL ON)
# gi.field_thin_wall — THE FIELD'S ESTIMATOR ON A ONE-VOXEL EMISSIVE WALL
# (PHOTON-FIELD-ROTATE-1 part 1): a probe's reading of a one-voxel-thick
# emissive wall at four probe spacings, three lateral offsets, against the
# authored wall's analytic irradiance times what the directional coverage model
# predicts the probe reads (PHOTON-VOXEL-3). Its own binary like every
# GI suite; in the gi_chain family like every cascade suite.
add_executable(test_gi_field_thin_wall test_gi_field_thin_wall.cpp)
set_target_properties(test_gi_field_thin_wall PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_field_thin_wall PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_field_thin_wall PRIVATE JahshakaEngine)
add_dependencies(test_gi_field_thin_wall JahshakaEngineMedia)
jah_gpu_row(gi.field_thin_wall CLASS engine COMMAND test_gi_field_thin_wall wall
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.field_thin_wall PROPERTIES TIMEOUT 240 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.field_alias — ONE RAY THAT DOES NOT ALIAS (PHOTON-FIELD-ROTATE-1 part 2): the
# same fixture and binary; the noise of one integration at 1 and 2 rays (rotated)
# and at 1 ray static, the target sample count derived from it, convergence at
# budget 1 and a field that stops at rest, 1 ray against 2 converged, and the cost
# of one whole-field integration at 1 and 2 rays in one process.
jah_gpu_row(gi.field_alias CLASS engine COMMAND test_gi_field_thin_wall alias
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.field_alias PROPERTIES TIMEOUT 900 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
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
jah_gpu_row(gi.leak_room CLASS engine COMMAND test_gi_leak_room
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# In the gi_chain family, and a wide timeout: eight full GI builds (four rooms x
# two arms), each with a shadowed room, a chain and a 14 MB probe atlas.
set_tests_properties(gi.leak_room PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# A SHADOW-ATLAS REBUILD IS NOT A GI REBUILD (PHOTON_SPEC gate G2, audit C
# finding 2): the atlas change drops only the WORKSPACES that instantiate the
# probe shadow node, never the voxels — so the 3rd casting lamp in a scene, or a
# Shadow Quality change, costs the voxel arm (and, under Photon, the whole
# cascade chain) exactly nothing.
# Its own binary.
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
jah_gpu_row(gi.atlas_keeps_voxels CLASS engine COMMAND test_gi_atlas_voxels
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# In the gi_chain family like gi.cascades: it holds the cascade chain AND a
# shadowed probe grid at once.
set_tests_properties(gi.atlas_keeps_voxels PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

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
    jah_gpu_row(gi.cascades_asan CLASS engine COMMAND test_gi_cascades_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # LeakSanitizer off for the same reason test_engine_asan turns it off (the
    # only leaks are inside libdbus via the Vulkan loader's device-select layer);
    # use-after-free detection is the point and stays on.
    set_tests_properties(gi.cascades_asan PROPERTIES TIMEOUT 180 LABELS "engine"
        RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN}
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
jah_gpu_row(gi.rayquery CLASS engine COMMAND test_gi_rayquery
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rayquery PROPERTIES TIMEOUT 120 LABELS "engine")

# engine.descriptor_pools — D6-FORK-TOOLING (audit V2-D F8): every raw descriptor
# pool is planned by the same call that creates its layout and allocates its maximum
# at birth (irisgl/engine/src/VkDescriptorPools.h); the suite proves the arithmetic,
# the one-call construction (and prints the driver's answer to a pool one binding
# short — NVIDIA allocates it) and that every pool a ray-traced scene creates held
# its maximum. Links the Vulkan
# render system to reach the device, like tests/atom's negative control. It skips
# the live case on a device with no ray queries.
if(OGRE_NEXT_VULKAN_RS_LIB AND Vulkan_FOUND)
    add_executable(test_descriptor_pools test_descriptor_pools.cpp)
    set_target_properties(test_descriptor_pools PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_descriptor_pools PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_include_directories(test_descriptor_pools PRIVATE
        "${CMAKE_SOURCE_DIR}/irisgl/engine/src"
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan"
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan/include"
        ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(test_descriptor_pools PRIVATE JahshakaEngine OgreNext
        "${OGRE_NEXT_VULKAN_RS_LIB}" ${Vulkan_LIBRARIES})
    add_dependencies(test_descriptor_pools JahshakaEngineMedia)
    jah_gpu_row(engine.descriptor_pools CLASS engine COMMAND test_descriptor_pools
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.descriptor_pools PROPERTIES TIMEOUT 120 LABELS "engine")
endif()

# THE SAME SUITE WITH THE RAYS OFF (PHOTON_SPEC §7 R1's no-rays switch). It is
# the whole point of the switch that this GPU can render BOTH pictures, so the
# fallback is proved on every push rather than assumed. The binary takes its own
# no-rays branch and asserts the FALLBACK contract — no device extensions, no
# structures, no memory, a trace that refuses, and the scene still rendering.
jah_gpu_row(gi.rayquery_norays CLASS engine COMMAND test_gi_rayquery
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
    jah_gpu_row(gi.rayquery_asan CLASS engine COMMAND test_gi_rayquery_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.rayquery_asan PROPERTIES TIMEOUT 120 LABELS "engine"
        RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN}
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

# GI OFF AND BACK ON AT RUNTIME (lane PATCHES-1 / GI-REENABLE-1): every other GI
# suite pushes its mode once, so the TRANSITION had no coverage at all — and it
# is what a user drives (the World panel's dropdown, `world.gi`, a scene
# open). Two things here that no other suite does: it runs the hybrid with
# MULTIPLE BOUNCES (numBounces = 2, i.e. the bounce-inject compute job actually
# exists — fork ae2ed529f (was 0057)'s subject), and it drains `takeLastError()` after
# every push, so an arm that throws its way out of a build fails the suite
# instead of rendering a dark picture quietly.
# Its own binary.
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
jah_gpu_row(gi.reenable CLASS engine COMMAND test_gi_reenable
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# In the gi_chain family like gi.cascades, and a wide timeout: this suite builds
# the arm six times over.
set_tests_properties(gi.reenable PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.chain_face — PHOTON-E3 item 0 (ii): the chain-to-world face and the faces
# inside the chain, under a REAL sky (audit B8). Its own binary like every GI
# suite.
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
# VRAM (GATE-ADMIT-1): test_gi_chain_face peaked 1.5 GB -> 2 tokens.
jah_gpu_row(gi.chain_face CLASS engine TOKENS 2 COMMAND test_gi_chain_face
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.chain_face PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.chain_face_target — THE SAME BINARY, THE CORRECT BAR (PHOTON phase A, A1
# §0; lane FENCE-1). Every face ratio = 1.0 +- 0.05 under a real sky AND under a
# flat ambient: a face is a boundary in a data structure, not in the world.
# Today 0.859-1.234 (worst |ratio-1| 0.234) — see the binary's header for the
# three mechanisms and the derivation of the bar.
#
# LABEL `photon-target`: it RUNS in every scoped selection and PRINTS its value,
# and it does NOT decide a gate (scripts/gate-scope.py splits the label out; the
# MERGE and PUSH tiers -LE it). THE LANE THAT LANDS PHOTON P4 (ONE-READER +
# ONE-ENV + F10-TABLES) DELETES `photon-target` FROM THIS ROW and deletes the
# bracket from gi.chain_face above — that deletion IS the part's acceptance.
jah_gpu_row(gi.chain_face_target CLASS engine TOKENS 2 COMMAND test_gi_chain_face --target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.chain_face_target PROPERTIES
    TIMEOUT 300 LABELS "engine;photon-target" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.field_follows_energy — `gi.field_follows`' EXISTENCE BAR, REPLACED BY ENERGY
# (PHOTON phase A, A1 section 1.2; lane FENCE-1). A sunlit matte floor lights a
# perpendicular matte wall; the wall's radiance at three heights against the
# finite-rectangle transfer integral, with HlmsPbs BRDF_Default's own direct
# diffuse (MEASURE-1a's pbsDirect, the 1/1.51 energyFactor) as the source and
# Lambert's polygon formula as the kernel — both in double precision, and the
# closed form cross-checked against a 512x512 quadrature of its own definition
# inside the suite.
#
# ITS OWN BINARY AND PROCESS, like every GI suite.
#
# LABEL `photon-target`. GREEN AFTER: VOXEL-CLIP-1 (phase A) for the source the
# voxels hold and the field's integral in PHOTON P4 for the transport. The lane
# that lands them DELETES `photon-target` from this row.
add_executable(test_gi_field_energy test_gi_field_energy.cpp)
set_target_properties(test_gi_field_energy PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_field_energy PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_field_energy PRIVATE JahshakaEngine)
add_dependencies(test_gi_field_energy JahshakaEngineMedia)
jah_gpu_row(gi.field_follows_energy CLASS engine COMMAND test_gi_field_energy
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.field_follows_energy PROPERTIES
    TIMEOUT 300 LABELS "engine;photon-target" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.boot_textures — THE ARM WAITS FOR THE ALBEDO IT IS ABOUT TO VOXELISE
# (BOOTVOX-1). The voxeliser reads a material's albedo and emissive maps, and a
# scene that binds one and pushes its GI in the same frame used to voxelise
# without it and then re-voxelise EVERY cascade when the pixels arrived — which
# is what the shipped default scene did on every boot (its ground's tile.png).
# In the gi_chain family like every cascade suite (the case counts rebuilds
# per frame, in-process).
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
jah_gpu_row(gi.boot_textures CLASS engine COMMAND test_gi_boot_textures
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.boot_textures PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

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
# In the gi_chain family like every cascade suite.
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
jah_gpu_row(gi.stream_edit CLASS engine COMMAND test_gi_stream_edit
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.stream_edit PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# SURFACE-CACHE phase 2 (SURFACE-CACHE-1b) — the capture Component's gates.
# SEVEN ctest names (gi.card_lighting + _indirect: PHOTON-CARDS-1's lit card; gi.cone_integrator_parity, gi.card_read_parity: PHOTON-CARDS-2) out of ONE binary, selected by argv: five different scenes
# are needed, one process each — while the fixture scaffolding is the same in
# all five. The shadow case is in the gi_chain family: it renders a shadow atlas per capture.
# PHOTON-CARDS-4: a mover's shadow on the cached surfaces (the mirror umbra vs the
# raster, the slide at 0 frames' latency, rest costs nothing, a missed footprint
# relights nothing, a still crate's move recaptured). `--cost` is not a row.
# gi.region_replace — REGION-REBUILD-1: a refresh whose content moved the probe grid's
# region past its tolerance re-places the GRID alone (one placement, the chain's
# from-scratch count and cascade centres unchanged, each cascade re-voxelised at most
# once for the edit's own box), on a 400-box scene; prints the re-placement's first
# frame beside the from-scratch build it replaced.
add_executable(test_gi_region_replace test_gi_region_replace.cpp)
set_target_properties(test_gi_region_replace PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_region_replace PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_region_replace PRIVATE JahshakaEngine)
add_dependencies(test_gi_region_replace JahshakaEngineMedia)
jah_gpu_row(gi.region_replace CLASS engine COMMAND test_gi_region_replace
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.region_replace PROPERTIES TIMEOUT 120 LABELS "engine")

add_executable(test_gi_cards_mover test_gi_cards_mover.cpp)
set_target_properties(test_gi_cards_mover PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cards_mover PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cards_mover PRIVATE JahshakaEngine)
add_dependencies(test_gi_cards_mover JahshakaEngineMedia)
jah_gpu_row(gi.cards_mover_shadow CLASS engine COMMAND test_gi_cards_mover
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cards_mover_shadow PROPERTIES TIMEOUT 120 LABELS "engine")

add_executable(test_gi_cards test_gi_cards.cpp)
set_target_properties(test_gi_cards PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cards PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cards PRIVATE JahshakaEngine)
add_dependencies(test_gi_cards JahshakaEngineMedia)
jah_gpu_row(gi.card_capture CLASS engine COMMAND test_gi_cards capture
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_capture PROPERTIES TIMEOUT 120 LABELS "engine")
jah_gpu_row(gi.card_shadow CLASS engine COMMAND test_gi_cards shadow
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_shadow PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# ATOM-S3-CARDCAP: a point lamp moving re-queues no card (the capture holds the sun's
# term alone); the sun turning stales every resident card's.
jah_gpu_row(gi.card_light_trigger CLASS engine COMMAND test_gi_cards light_trigger
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_light_trigger PROPERTIES TIMEOUT 120 LABELS "engine")
jah_gpu_row(gi.card_budget CLASS engine COMMAND test_gi_cards budget
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_budget PROPERTIES TIMEOUT 120 LABELS "engine")
jah_gpu_row(gi.card_lighting CLASS engine COMMAND test_gi_cards lighting
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_lighting PROPERTIES TIMEOUT 120 LABELS "engine")
jah_gpu_row(gi.card_lighting_indirect CLASS engine COMMAND test_gi_cards lighting_indirect
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_lighting_indirect PROPERTIES TIMEOUT 120 LABELS "engine")
# CLOUDS-2D-2: the card relight's direct sun crosses the 2D cloud sheet by the
# pixel's own factor (half at shadow 0.5 under an opaque deck, none at 1).
jah_gpu_row(gi.card_clouds CLASS engine COMMAND test_gi_cards clouds
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_clouds PROPERTIES TIMEOUT 120 LABELS "engine")
# CLOUDS-2D-3: THE CLOUD LAYER'S PHYSICS — a sheet never adds light (a full deck at
# every sun height and density leaves the ground at most as bright as the clear
# sky; a half deck's tile mean likewise, its shadows brighter than a full deck's)
# and (shape) the field's blob sizes, edge ramp in km and period. Mirror-linked:
# the document's clouds block through the mirror is the product's path.
add_executable(test_cloud_2d test_cloud_2d.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_cloud_2d PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_cloud_2d PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_cloud_2d PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_cloud_2d PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_cloud_2d JahshakaEngineMedia)
jah_gpu_row(cloud_2d.energy CLASS engine COMMAND test_cloud_2d energy
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cloud_2d.energy PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")
jah_gpu_row(cloud_2d.shape CLASS engine COMMAND test_cloud_2d shape
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cloud_2d.shape PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")
# cloud_2d.budget (CLOUD-BAKE-MEMORY-1): the cloud layer's textures after two bakes — the field
# its stated budget to the texture manager's rounding (Types.h kCloudFieldBytes, every tier), the bake's intermediates gone.
jah_gpu_row(cloud_2d.budget CLASS engine COMMAND test_cloud_2d budget
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cloud_2d.budget PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")
# gi.card_blend (PHOTON-GATHER-1d fix round): a Blend material on a card subject renders, holds no card.
jah_gpu_row(gi.card_blend CLASS engine COMMAND test_gi_cards blend
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_blend PROPERTIES TIMEOUT 120 LABELS "engine")
# PHOTON-CARDS-5: a card read from a direction is the pixel seen from it — the
# ray job's card read vs the float raster, head-on and grazing, per term (sun,
# lamp x3, spot x2, ambient), GI off and on (Medium, High at the Epic trace).
jah_gpu_row(gi.card_view CLASS engine COMMAND test_gi_cards view
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_view PROPERTIES TIMEOUT 120 LABELS "engine")
# ...and a sun turn re-traces every card's still term (no capture) with every frame's
# read the card as it was or as it became (ATOM-S3-CARDCAP; it was the audit's F3 capture batch).
jah_gpu_row(gi.card_view_batch CLASS engine COMMAND test_gi_cards view_batch
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_view_batch PROPERTIES TIMEOUT 120 LABELS "engine")
# PHOTON-CARDS-2 part A: the pixel's diffuse cones and the card job's are ONE
# text (JahVoxelCones) — their answers at a point, 1 % apart at most.
jah_gpu_row(gi.cone_integrator_parity CLASS engine COMMAND test_gi_cards cone_parity
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cone_integrator_parity PROPERTIES TIMEOUT 120 LABELS "engine")
# ...THE OFF-AXIS WALL, A TARGET (HDR-READBACK-1, 2026-09-24): read in float the
# wall turned 30 degrees shows the card 1.64 % / 2.06 % below the pixel at two of
# its six points (two R11G11B10F steps, and the card below the pixel at every
# sample of both walls) — the 8-bit readback's half-code term (0.00196, ~2 % of
# these values) hid it. LABEL `photon-target`: it runs, prints `target:` and
# does not decide a gate; the lane that makes the card's off-axis cone frame
# agree with the pixel's deletes the label.
jah_gpu_row(gi.cone_integrator_parity_offaxis CLASS engine COMMAND test_gi_cards cone_parity_offaxis
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cone_integrator_parity_offaxis PROPERTIES TIMEOUT 300
                     LABELS "engine;photon-target")
# PHOTON-CARDS-2 part B: a reflection ray's hit reads the surface cache first
# (jah_rq_card.glsl); the GLSL pick = SurfaceCache::readAt's for 1,000 hits.
jah_gpu_row(gi.card_read_parity CLASS engine COMMAND test_gi_cards read_parity
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_read_parity PROPERTIES TIMEOUT 120 LABELS "engine")

# SURFACE-CACHE-1b's MEASUREMENT ARM — a TOOL, not a suite (no add_test): the
# numbers in the lane's report are taken by code that is read, and re-taking
# them later is one command rather than a reconstruction.
add_executable(sc1b_measure EXCLUDE_FROM_ALL sc1b_measure.cpp)
set_target_properties(sc1b_measure PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(sc1b_measure PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(sc1b_measure PRIVATE JahshakaEngine)
add_dependencies(sc1b_measure JahshakaEngineMedia)

# DIRECT-DIFFUSE-1's MEASUREMENT ARM (lane MEASURE-1a) — a TOOL, not a suite,
# for the same reason: the mechanism behind "the direct term renders 66-69 % of
# albedo*P/pi" is a number, and the number is taken by code that is read. It
# states HlmsPbs's own closed form before every measurement and bisects the
# light power so an 8-bit undithered picture still pins a ratio to ~0.1 %.
add_executable(direct_diffuse_measure EXCLUDE_FROM_ALL direct_diffuse_measure.cpp)
set_target_properties(direct_diffuse_measure PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(direct_diffuse_measure PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(direct_diffuse_measure PRIVATE JahshakaEngine)
add_dependencies(direct_diffuse_measure JahshakaEngineMedia)

# GA-FAR's MEASUREMENT ARM (lane MEASURE-1b) — a TOOL, not a suite: it counts
# how many of the gather's rays take rq_probe_gather.comp's else-branch and how
# many of those can get a real far answer, by reproducing the shader's own ray
# set on the CPU and tracing it through Engine::traceRays.
add_executable(gather_far_measure EXCLUDE_FROM_ALL gather_far_measure.cpp)
set_target_properties(gather_far_measure PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(gather_far_measure PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(gather_far_measure PRIVATE JahshakaEngine)
add_dependencies(gather_far_measure JahshakaEngineMedia)

# ---------------------------------------------------------------------------
# gi.far_blas — THE FAR FIELD'S GEOMETRY IN THE ONE TLAS (ATOM-FARBLAS-1, A5b §4):
# every traced object twice (a near copy over the ray rule's level, a far copy over
# the coarsest), the masks that keep the two apart, and the gather's far query.
add_executable(test_gi_far_blas test_gi_far_blas.cpp)
set_target_properties(test_gi_far_blas PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_far_blas PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_far_blas PRIVATE JahshakaEngine)
add_dependencies(test_gi_far_blas JahshakaEngineMedia)
jah_gpu_row(gi.far_blas CLASS engine COMMAND test_gi_far_blas
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.far_blas PROPERTIES TIMEOUT 120 LABELS "engine")

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
# VRAM (GATE-ADMIT-1, nvidia-smi per pid in the two-tier run): test_gi_gather peaked 3.6 GB -> 4 tokens.
jah_gpu_row(gi.gather CLASS engine TOKENS 4 COMMAND test_gi_gather
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather PROPERTIES TIMEOUT 180 LABELS "engine")

# THE COST, at the resolution the spec's budget table is stated at.
#
# LABELLED `nightly;quiet-box`, RUN_SERIAL AND IN THE GPU-TIMING LOCK, BECAUSE IT READS
# THE GPU'S CLOCK. The MERGE and PUSH tiers exclude `nightly` by construction and a
# scoped gate never runs `quiet-box`, and they must: this rig's GPU idles at 210 of 3105 MHz under Xvfb unless the
# clocks are locked by hand, so a GPU-time number taken inside a -j4 gate is
# not a measurement. The suite PRINTS its milliseconds and asserts only a
# catastrophe ceiling; the numbers of record are taken by hand with the clocks
# locked (spikes/gather-1a/FINDINGS.md).
jah_gpu_exclusive_test(NAME gi.gather_cost CLASS engine RUN_TIMEOUT 900 COMMAND test_gi_gather
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_cost PROPERTIES
                     LABELS "nightly;quiet-box" RUN_SERIAL TRUE
                     ENVIRONMENT "JAH_GATHER_COST=1")

# ...and the picture a machine WITHOUT ray queries draws, which this lane may
# not move by one byte.
jah_gpu_row(gi.gather_norays CLASS engine TOKENS 4 COMMAND test_gi_gather
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_norays PROPERTIES
                     TIMEOUT 120 LABELS "engine"
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
# VRAM (GATE-ADMIT-1): test_gi_gather_reference peaked 2.0 GB -> 2 tokens.
jah_gpu_row(gi.gather_reference CLASS engine TOKENS 2 COMMAND test_gi_gather_reference
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_reference PROPERTIES TIMEOUT 180 LABELS "engine")
# ...and the brief's 1.00 +- 0.05 at every point against the COMPLETE closed form.
# It was a TARGET row until the card read at the hit closed the residual; GREEN, and
# the `photon-target` label is off (lane D6B-GATE-SHAPE, 2026-09-27: 3/3 green on the
# rig, worst 0.969 at x = 4.60 m): an ordinary gating row now.
jah_gpu_row(gi.gather_reference_target CLASS engine TOKENS 2 COMMAND test_gi_gather_reference
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_reference_target PROPERTIES
                     TIMEOUT 900 LABELS "engine"
                     ENVIRONMENT "JAH_GATHER_REFERENCE_TARGET=1")

# ...and the TRACE's own bar, in R5's form: a probe a metre from a lit red wall
# reads at least 0.6 of that wall's albedo times its lighting, in the wall's
# direction.
jah_gpu_row(gi.gather_trace CLASS engine TOKENS 2 COMMAND test_gi_gather_reference
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_trace PROPERTIES
                     TIMEOUT 120 LABELS "engine"
                     ENVIRONMENT "JAH_GATHER_TRACE=1")

# gi.contact_occlusion — AN OBJECT STANDING ON A FLOOR DARKENS THE FLOOR BESIDE IT
# (CONTACT-OCCLUSION-1): a 2 m black wall on a matte floor under a constant sky,
# the floor's E(d)/E_open against the differential-strip configuration factor
# 1 - F(d) at d = 0.05..4 m (0.04 at Epic/High, 0.08 at Medium/Low for d >= 0.1 m),
# a white wall's bounce against a 2-D radiosity solve, the rig check's 2 m cube
# printed — at every shipped tier. JAH_CONTACT_MEASURE=1 prints the per-term table.
# The gate: arm A where the gather runs (0.04) and arm B at the tier's bounce count
# where one bounce is asked (0.03 at High and Medium) — the sky's direct term in the
# voxel and card stores (fork sky pass, CONTACT-OCCLUSION-1). The photon-target twin:
# the field alone (Low: arm A 0.08 — the probe spacing, 0.31 m across and 0.625 m in
# height: the floor reads its cage's upper layer 0.47 m above it — and arm B 0.06) and
# Epic's three bounces against the three-bounce solve (0.03). Case D gates in
# gi.contact_occlusion: the floor under a hovering 2 m MOVER at Epic within 0.02 of the
# closed form 0.446 (read with the fixture's mirror hidden — HOVER-GATHER-1), and the
# mirror's reflection of that point within 0.06 of it (the mover gate).
add_executable(test_gi_contact_occlusion test_gi_contact_occlusion.cpp)
set_target_properties(test_gi_contact_occlusion PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_contact_occlusion PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_contact_occlusion PRIVATE JahshakaEngine)
add_dependencies(test_gi_contact_occlusion JahshakaEngineMedia)
jah_gpu_row(gi.contact_occlusion CLASS engine TOKENS 2 COMMAND test_gi_contact_occlusion
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.contact_occlusion PROPERTIES TIMEOUT 300 LABELS "engine")
jah_gpu_row(gi.contact_occlusion_target CLASS engine TOKENS 2 COMMAND test_gi_contact_occlusion
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.contact_occlusion_target PROPERTIES TIMEOUT 300 LABELS "engine;photon-target"
                     ENVIRONMENT "JAH_CONTACT_TARGET=1")

# gi.voxel_coverage — THE STORE HOLDS A SURFACE'S PROJECTED AREA PER AXIS
# (PHOTON-VOXEL-3): an open emissive quad and a closed box one and four cascade-0
# cells thick, turned 0/15/45 degrees against the lattice, and a quad at opacity
# 0.5, read out of the store whole (giVoxelVolume) — the store equal to the CPU
# model of tests/support/voxelcoveragemodel.h along every axis, an open surface's
# integral equal to the authored one, the reader's mip-0 march over the store
# against the same march over the model (face-on flux, the opacity along the
# normal and along the plane), a thin wall opaque along its normal, a
# half-transparent surface half its opaque self.
add_executable(test_gi_voxel_coverage test_gi_voxel_coverage.cpp)
set_target_properties(test_gi_voxel_coverage PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_voxel_coverage PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_voxel_coverage PRIVATE JahshakaEngine)
add_dependencies(test_gi_voxel_coverage JahshakaEngineMedia)
# VRAM (GATE-ADMIT-1): test_gi_voxel_coverage peaked 2.8 GB -> 3 tokens.
jah_gpu_row(gi.voxel_coverage CLASS engine TOKENS 3 COMMAND test_gi_voxel_coverage
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.voxel_coverage PROPERTIES TIMEOUT 300 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.lamp_falloff — A LAMP'S BOUNCE FALLS OFF EXACTLY AS ITS DIRECT LIGHT (IMAGE-1): a
# point lamp's injected voxel radiance follows the pixel's own falloff (inverse square,
# windowed to the range) within 5 % from 1 m to R/2, and no voxel past the range holds any.
add_executable(test_gi_lamp_falloff test_gi_lamp_falloff.cpp)
set_target_properties(test_gi_lamp_falloff PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_lamp_falloff PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_lamp_falloff PRIVATE JahshakaEngine)
add_dependencies(test_gi_lamp_falloff JahshakaEngineMedia)
jah_gpu_row(gi.lamp_falloff CLASS engine TOKENS 2 COMMAND test_gi_lamp_falloff
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.lamp_falloff PROPERTIES TIMEOUT 300 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.area_energy — ONE AREA-LIGHT ENERGY, HELD TO A CLOSED FORM (AREA-SCALE-1): an area light
# of intensity I is a Lambertian emitter of radiance I; on its axis a floor of albedo a reflects
# a I F(h), F the point-to-rectangle form factor. The voxel store (Photon's injection), the
# accurate (LTC) pixel and the approximate area light's pixel within 5 % of it at 1, 1.5 and
# 2.5 m, each normalised by a point lamp at the same place.
add_executable(test_gi_area_energy test_gi_area_energy.cpp)
set_target_properties(test_gi_area_energy PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_area_energy PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_area_energy PRIVATE JahshakaEngine)
add_dependencies(test_gi_area_energy JahshakaEngineMedia)
jah_gpu_row(gi.area_energy CLASS engine TOKENS 2 COMMAND test_gi_area_energy
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.area_energy PROPERTIES TIMEOUT 300 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.field_edge — THE IRRADIANCE FIELD IS FLAT OVER AN OPEN FLOOR TO ITS STATED REACH
# (FIELD-EDGE-1): an 80 m matte floor under a constant sky at the Low tier (the field carries the
# diffuse), read along eight directions from the centre out past the field's stated box
# (GiStatus ifdMin/ifdMax), at seven eye heights: no sample inside it below 0.95 of the centre -
# crossing a probe layer never fades the field (FIELD-EDGE-1: the view bias's normal share drifted).
add_executable(test_gi_field_edge test_gi_field_edge.cpp)
set_target_properties(test_gi_field_edge PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_field_edge PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_field_edge PRIVATE JahshakaEngine)
add_dependencies(test_gi_field_edge JahshakaEngineMedia)
jah_gpu_row(gi.field_edge CLASS engine TOKENS 2 COMMAND test_gi_field_edge
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.field_edge PROPERTIES TIMEOUT 600 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.voxel_lab — THE VOXEL LAB (PHOTON-VOXEL-4): the reader's plane march on the CPU
# (tests/support/voxel_lab.h) over analytic stores - gi.ddgi_ambient's sealed shell per
# surface class and its open scene per cone set, on the cone-trace reference and the
# hemisphere's truth. No engine, no GPU: the record the reader's rules were chosen on,
# every candidate the lab measured beside the shipped reader. `validate DIR` reads the
# stores a suite dumped (JAH_VOXEL_DUMP, tests/support/voxeldump.h).
add_executable(test_gi_voxel_lab test_gi_voxel_lab.cpp)
set_target_properties(test_gi_voxel_lab PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME gi.voxel_lab COMMAND test_gi_voxel_lab
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.voxel_lab PROPERTIES TIMEOUT 120 LABELS "engine")
# gi.voxel_emissive — AN EMITTER AUTHORED AT RADIANCE L IS STORED AS L (PHOTON
# phase A, SPECS/photon/A2_VOXEL_CLIP_DESIGN.md section 2; lane VOXEL-CLIP-1,
# fork ad452604a+155a56bf8 (was 0087)). The emissive VOXEL store was RGBA8_UNORM, so every emitter
# above 1.0 entered the injection, the bounces, the field, the cones and the ray
# hits clipped — read in the bytes (`giVoxelStats().peakEmissive`), because a
# clipped emitter draws a picture that is merely dimmer.
#
# In the gi_chain family like every cascade suite: it drives a full chain and
# downloads whole volumes.
add_executable(test_gi_voxel_emissive test_gi_voxel_emissive.cpp)
set_target_properties(test_gi_voxel_emissive PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_voxel_emissive PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_voxel_emissive PRIVATE JahshakaEngine)
add_dependencies(test_gi_voxel_emissive JahshakaEngineMedia)
jah_gpu_row(gi.voxel_emissive CLASS engine COMMAND test_gi_voxel_emissive
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.voxel_emissive PROPERTIES
    TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.cone_emitter — AN EMITTER IN A SPECULAR LOBE READS ITS RADIANCE (PHOTON-PHYSICS-1 item 1,
# CONE-EMITTER-1): a 1 m panel of radiance 5 over a white metal plate at roughness 0.05 / 0.2 / 0.5,
# the plate's reflection read as radiance against the same plate under a uniform sky (the split
# sum's DFG measured, not computed), with the fixture in cascade 0 and in cascade 1; a covered
# lobe reads >= 0.9 L (the gate: roughness 0.05, a covered lobe); and (c) the panel's voxels hold
# L x their opacity. The 0.2 and 0.5 lobes against the GGX coverage are NOT gated: CONE-LOBE-1 was
# declined on cost (PHOTON-II-1 part 1; the cone reads 1.48x / 1.58x), the target row below. The chain family: it drives a full chain and downloads volumes.
add_executable(test_gi_cone_emitter test_gi_cone_emitter.cpp)
set_target_properties(test_gi_cone_emitter PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cone_emitter PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cone_emitter PRIVATE JahshakaEngine)
add_dependencies(test_gi_cone_emitter JahshakaEngineMedia)
jah_gpu_row(gi.cone_emitter CLASS engine COMMAND test_gi_cone_emitter
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cone_emitter PROPERTIES
    TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.cone_emitter_lobe_target — the same fixture's roughness-0.5 lobe against the GGX coverage of
# the panel (bar 10 %): the cone over-reads a partly covered wide lobe (the aperture and the
# footprint kernel, the test's header). LABEL photon-target: it prints and does not gate; the
# lane that fits the reader's lobe to GGX deletes the label.
jah_gpu_row(gi.cone_emitter_lobe_target CLASS engine COMMAND test_gi_cone_emitter --lobe-target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cone_emitter_lobe_target PROPERTIES
    TIMEOUT 120 LABELS "engine;photon-target" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.voxel_feed — THE VOXELISER IS FED ON THE DEVICE (ATOM P4b, SPECS/atom/
# A5b_VOXELISER_FEED_AND_FAR_BLAS_DESIGN.md §2-§3). The gather jobs' own counts
# (attach set, enclosed set, one record per partition that reaches a cascade's
# box, triangles, levels, overflow) held to a CPU reference computed from the
# fixture; a hide leaves the next gather by itself; and a from-scratch rebuild of
# a still scene copies nothing into the GPU scene's tables.
#
# In the gi_chain family like every cascade suite.
add_executable(test_gi_voxel_feed test_gi_voxel_feed.cpp)
set_target_properties(test_gi_voxel_feed PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_voxel_feed PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_voxel_feed PRIVATE JahshakaEngine)
add_dependencies(test_gi_voxel_feed JahshakaEngineMedia)
jah_gpu_row(gi.voxel_feed CLASS engine COMMAND test_gi_voxel_feed
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.voxel_feed PROPERTIES
    TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# gi.env_cone — THE ONE ENVIRONMENT'S CONE LOOKUP, MEASURED (PHOTON-ENV-1).
# jahEnvCone reads the GGX-prefiltered sky chain as equal-solid-angle cells (7, or
# 19 for a wide cone, CONE-ENV-EDGE-1); this suite measures it against a
# 1024-direction integral of the cube over the same cone on the shipped
# (analytic) sky at the renderer's four apertures, and sweeps the aperture for a
# step (Engine::environmentCones: one job built from the shared file). Its own
# binary: one engine per process.
add_executable(test_gi_env_cone test_gi_env_cone.cpp)
set_target_properties(test_gi_env_cone PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_env_cone PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_env_cone PRIVATE JahshakaEngine)
add_dependencies(test_gi_env_cone JahshakaEngineMedia)
jah_gpu_row(gi.env_cone CLASS engine COMMAND test_gi_env_cone
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.env_cone PROPERTIES TIMEOUT 120 LABELS "engine")
# gi.hit_voxel — A RAY'S HIT ON A STATIC SURFACE, SHADED FROM THE VOXELS (jahVoxelRadiance), IS
# THAT SURFACE'S RADIANCE (PHOTON-VOXEL-5 item (iv); HIT-SHADE-1's F-A): a static matte crate in a
# perfect mirror against its raster from the reflected camera.
add_executable(test_gi_hit_voxel test_gi_hit_voxel.cpp)
set_target_properties(test_gi_hit_voxel PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_hit_voxel PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_hit_voxel PRIVATE JahshakaEngine)
add_dependencies(test_gi_hit_voxel JahshakaEngineMedia)
jah_gpu_row(gi.hit_voxel CLASS engine COMMAND test_gi_hit_voxel
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hit_voxel PROPERTIES TIMEOUT 120 LABELS "engine")
# VOXEL-LEAK-TRACE (PHOTON-VOXEL-5 item (iii)) — a TOOL, not a suite: gi.gather's leak room,
# the pixel's four cones from the -Z wall's inner face by the engine's own reader, per cone
# colour / alpha / cascade, lamp on and off, and the store dumped (JAH_VOXEL_DUMP).
add_executable(voxel_leak_trace EXCLUDE_FROM_ALL voxel_leak_trace.cpp)
set_target_properties(voxel_leak_trace PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(voxel_leak_trace PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(voxel_leak_trace PRIVATE JahshakaEngine)
add_dependencies(voxel_leak_trace JahshakaEngineMedia)

# voxel_store_room_trace — a TOOL (PHOTON-VOXEL-5 item (ii)): scripting.e2e.gi_voxel_store's room -
# the cascade-0 voxels holding a face that looks into the room whose direct light is zero.
add_executable(voxel_store_room_trace EXCLUDE_FROM_ALL voxel_store_room_trace.cpp)
set_target_properties(voxel_store_room_trace PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(voxel_store_room_trace PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(voxel_store_room_trace PRIVATE JahshakaEngine)
add_dependencies(voxel_store_room_trace JahshakaEngineMedia)

# gi.env_one — ONE ENVIRONMENT, FOUR READERS, ONE NUMBER (PHOTON-ENV-1): a matte
# box under a uniform sky reads rho L energyFactor(1) outside any volume, through
# the voxel cones' escape, through the irradiance field and through the gather's
# ray misses; no Sky Light is no environment for any of them; the SH lookup is
# camera-independent; a sun disc in the probes reaches neither the SH nor the
# cube. Its own binary.
add_executable(test_gi_env_one test_gi_env_one.cpp)
set_target_properties(test_gi_env_one PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_env_one PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_env_one PRIVATE JahshakaEngine)
add_dependencies(test_gi_env_one JahshakaEngineMedia)
jah_gpu_row(gi.env_one CLASS engine COMMAND test_gi_env_one
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.env_one PROPERTIES TIMEOUT 120 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# ---------------------------------------------------------------------------
# THE GATHER'S FOUR CORRECTNESS FINDINGS (PHOTON-GATHER-1b item 1; C2 design
# section 1.1) — one binary, the mode on the command line: GA-GLASS (a blended
# fragment reads no probe irradiance), GA-NEST (the registration is the PASS's,
# not the scene's), GA-SKY (ONE-ENV's miss: the open floor's probe irradiance is
# the disc-free sky's SH irradiance), gi.gather_plane_weight (the gather's plane
# weight at a 2 cm fence's edge, its leak printed; the field's own crushed-cage
# guard is unreachable while the gather runs — the suite's header says why).
add_executable(test_gi_gather_findings test_gi_gather_findings.cpp)
set_target_properties(test_gi_gather_findings PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_gather_findings PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_gather_findings PRIVATE JahshakaEngine)
add_dependencies(test_gi_gather_findings JahshakaEngineMedia)
# ...and gi.gather_letterbox (PHOTON-GATHER-1d): the gather's eye basis
# expanded to the target under a constrained-aspect camera (the shared helper);
# gi.gather_edge (the fix round, GA-EDGE): a silhouette pixel belongs to its surface.
foreach(_finding glass nest sky plane_weight letterbox edge)
    jah_gpu_row(gi.gather_${_finding} CLASS engine COMMAND test_gi_gather_findings ${_finding}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.gather_${_finding} PROPERTIES TIMEOUT 600 LABELS "engine")
endforeach()

# THE GATHER'S PHASE-2 ACCEPTANCE (PHOTON-GATHER-1b item 5; C2 design section
# 1.4): gi.gather_plane (no bounce crosses a plane edge; the floor two strides
# out at the closed form) and gi.gather_deterministic (three cold 60-frame
# stills, the probeIrradiance readback byte-identical).
add_executable(test_gi_gather_phase2 test_gi_gather_phase2.cpp)
set_target_properties(test_gi_gather_phase2 PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_gather_phase2 PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_gather_phase2 PRIVATE JahshakaEngine)
add_dependencies(test_gi_gather_phase2 JahshakaEngineMedia)
# ...and gi.gather_cards (PHOTON-GATHER-1d, GA-1e): the hit lit from the card —
# the plane fixture on carded meshes, the voxel / card / ungated arms paired.
foreach(_arm plane deterministic cards)
    jah_gpu_row(gi.gather_${_arm} CLASS engine COMMAND test_gi_gather_phase2 ${_arm}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.gather_${_arm} PROPERTIES TIMEOUT 600 LABELS "engine")
endforeach()
# THE GATHER'S HISTORIES (PHOTON-GATHER-1c; C2 design section 2): gi.gather_stable
# (a still Showroom-2-shaped room moves < 2/255 frame to frame after the warm-up,
# and the arm "gather.temporal" = 0 in the same process shows the history is
# what stills it) and gi.gather_motion (a truck and a yaw against the frames they
# settle to, gi.reflect_motion's method).
add_executable(test_gi_gather_temporal test_gi_gather_temporal.cpp)
set_target_properties(test_gi_gather_temporal PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_gather_temporal PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_gather_temporal PRIVATE JahshakaEngine)
add_dependencies(test_gi_gather_temporal JahshakaEngineMedia)
foreach(_arm stable motion)
    jah_gpu_row(gi.gather_${_arm} CLASS engine COMMAND test_gi_gather_temporal ${_arm}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.gather_${_arm} PROPERTIES TIMEOUT 600 LABELS "engine")
endforeach()

# gi.flythrough_noise.Showroom2 (GATHER-NOISE-1): the rig's flythrough of a
# Showroom-2-shaped room at Epic, the gather's own answer read back at ten poses;
# the moving floor's noise (two flights of one path) against the pre-lane estimator
# in the same process (-40 %), and its mean against a converged reference (no bias).
add_executable(test_gi_flythrough_noise test_gi_flythrough_noise.cpp)
set_target_properties(test_gi_flythrough_noise PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_flythrough_noise PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_flythrough_noise PRIVATE JahshakaEngine)
add_dependencies(test_gi_flythrough_noise JahshakaEngineMedia)
# ...and gi.flythrough_noise.Open, the control: the same blocks under the open sky
# (most rays escape to it) must be no noisier and keep their mean; .Leak: a thin
# partition, the camera turning onto its dark side — no light through the wall.
foreach(_arm Showroom2 Open Leak)
    string(TOLOWER ${_arm} _lower)
    jah_gpu_row(gi.flythrough_noise.${_arm} CLASS engine COMMAND test_gi_flythrough_noise ${_lower}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.flythrough_noise.${_arm} PROPERTIES TIMEOUT 900 LABELS "engine")
endforeach()

# gi.gather_adaptive_fair — THE ADAPTIVE BUDGET STARVES NO CELL (PHOTON-II-1 F3): with the cap at
# a quarter of the demand, every frame spends the budget and every asking cell gets its second
# probe within 32 frames (the placement's ranking rotates across tiles and inside them).
add_executable(test_gi_gather_adaptive test_gi_gather_adaptive.cpp)
set_target_properties(test_gi_gather_adaptive PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_gather_adaptive PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_gather_adaptive PRIVATE JahshakaEngine)
add_dependencies(test_gi_gather_adaptive JahshakaEngineMedia)
jah_gpu_row(gi.gather_adaptive_fair CLASS engine COMMAND test_gi_gather_adaptive
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_adaptive_fair PROPERTIES TIMEOUT 300 LABELS "engine")

# ...and the plane fixture's COMPLETE closed form at the brief's 5 %. It was a TARGET
# row until the card read at the hit (GA-1e) closed the voxel-read residual; GREEN,
# and the `photon-target` label is off (lane D6B-GATE-SHAPE, 2026-09-27: 3/3 green on
# the rig at 1.000): an ordinary gating row now.
jah_gpu_row(gi.gather_plane_target CLASS engine COMMAND test_gi_gather_phase2 plane --target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_plane_target PROPERTIES TIMEOUT 600
                     LABELS "engine")

# gi.settle_partial — THE OWED SETTLE INJECTS ONLY THE STALE CASCADES
# (PHOTON-GATHER-1b item 6; WRITER-1's debt, audit F5): a cascade re-voxelised in
# the frame of an at-rest tick owes the cascades INSIDE it (k, ..., 0), not the
# chain's whole sweep — and the partial settle is the whole sweep's fixed point
# (light voxels byte-identical, picture 0/255). Counted on the render monitor's
# `vct.light.settle` rows.
add_executable(test_gi_settle_partial test_gi_settle_partial.cpp)
set_target_properties(test_gi_settle_partial PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_settle_partial PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_settle_partial PRIVATE JahshakaEngine)
add_dependencies(test_gi_settle_partial JahshakaEngineMedia)
jah_gpu_row(gi.settle_partial CLASS engine COMMAND test_gi_settle_partial
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.settle_partial PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.sun_contact — HARD SUN CONTACT SHADOWS (PHOTON P5, RY-R3; PHOTON-RAYS-1)
#
# A crate on a floor under a 15-degree sun: the shadow map's contact leak is
# measured (and must exist), then closed to one texel of the ray job at full and
# at half resolution; the shadow beyond the 2 m range is the map's, unchanged;
# the sunlit floor is untouched (no acne). The no-rays row renders the row-on
# picture byte for byte as the row-off one. `test_gi_sun_contact --cost` is the
# paired-arm cost measurement (by hand, under scripts/gpu-exclusive.sh).
add_executable(test_gi_sun_contact test_gi_sun_contact.cpp)
set_target_properties(test_gi_sun_contact PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_sun_contact PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_sun_contact PRIVATE JahshakaEngine)
add_dependencies(test_gi_sun_contact JahshakaEngineMedia)
jah_gpu_row(gi.sun_contact CLASS engine COMMAND test_gi_sun_contact
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sun_contact PROPERTIES TIMEOUT 120 LABELS "engine")
jah_gpu_row(gi.sun_contact_norays CLASS engine COMMAND test_gi_sun_contact
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sun_contact_norays PROPERTIES
                     TIMEOUT 120 LABELS "engine"
                     ENVIRONMENT "JAHSHAKA_NO_RAY_QUERY=1")
# ...and the CURVED casters as the process's SECOND scene, drawn beside the
# board's (PHOTON-SCENE-SWITCH-1 — they used to need a process of their own, and
# the cause was the mixed shadow-VAO list, not the second scene): a sphere
# lattice with a three-level chain at 25.5 m under a 10-degree sun — the map
# shadows every foot's contact and has no acne, the ray has none either at full
# and half resolution, and a glass pane over the contact keeps the map's answer.
jah_gpu_row(gi.sun_contact_both CLASS engine COMMAND test_gi_sun_contact --both
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sun_contact_both PROPERTIES TIMEOUT 120 LABELS "engine")
# ...and THE GAP THE MAP LEAVES BY CONSTRUCTION (PHOTON-SCENE-SWITCH-2): a 1 m
# board 2 cm thick on 5 cm legs under a 10-degree sun with a 1024 px atlas — the
# receiver's normal-offset lift (>= 0.136 m) exceeds the board's 0.07 m top, so the
# map shadows none of the board's shadow; the contact ray closes all of it. Its no-rays
# claim (row on == row off, byte for byte) runs inside gi.sun_contact_norays as the
# process's second scene (lane D6B-GATE-SHAPE; audit verdict MERGE — it was the row
# gi.sun_contact_legs_norays).
jah_gpu_row(gi.sun_contact_legs CLASS engine COMMAND test_gi_sun_contact --legs
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sun_contact_legs PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.hit_shade — A RAY HIT IS A PIXEL OF A SECOND VISIBILITY BUFFER
# (PHOTON-HIT-SHADE-1; SPECS/atom/D2_HIT_SHADING_DESIGN.md)
#
# A hit no cache can shade (a mover, a rigged item, a static hit neither card
# nor cascade answers, the gather's far copies) is appended to the hit list and
# shaded by HlmsAtom's decode: (a) a MOVER crate's reflection in a perfect
# mirror covers the static control's fraction of its silhouette in its raster
# colour; (b) the gather's bounce off a MOVER emitter meets the closed form;
# (d) a hit outside the frustum takes its lamp from the world light list; (e) the sun at a hit is
# the shadow ray. The rigged arm (c) is gi.skin_rays_mirror's.
# `test_gi_hit_shade --cost` is the paired-arm cost measurement (by hand, under
# scripts/gpu-exclusive.sh).
add_executable(test_gi_hit_shade test_gi_hit_shade.cpp)
set_target_properties(test_gi_hit_shade PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_hit_shade PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_hit_shade PRIVATE JahshakaEngine)
add_dependencies(test_gi_hit_shade JahshakaEngineMedia)
# VRAM (GATE-ADMIT-1): test_gi_hit_shade peaked 1.8 GB -> 2 tokens.
jah_gpu_row(gi.hit_shade CLASS engine TOKENS 2 COMMAND test_gi_hit_shade
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hit_shade PROPERTIES TIMEOUT 180 LABELS "engine")
# gi.speckle — THE GATHER'S HITS ARE DIFFUSE-ONLY ON EVERY ROUTE (SPECKLE-FIX-1;
# the owner's smoke: "speckly circular reflections flashing when the silver sphere
# moves"; spikes/speckle-1). A gather hit answered by the decode was the full PBS
# lobe (the sun's GGX highlight), by the caches the diffuse alone; a moved object's
# cards re-allocate, the route alternated, the floor flashed. Arms: (a) the
# controls (still, a matte sphere moved: 0 bursts on the floor band), (b) the
# silver sphere moved: 0 bursts, (c) the same sphere MOVABLE (every hit decoded):
# 0 bursts, (e) the reflection keeps the full lobe (the sphere in a mirror floor),
# (d) ROUTE AGREEMENT: a static box's bounce gathered through the caches and through
# the decode (the arm "gather.decodeHits") within the cards' 3 %.
# JAH_SPECKLE_DUMP=1 writes each moving arm's worst frame pair as PPMs.
add_executable(test_gi_speckle test_gi_speckle.cpp)
set_target_properties(test_gi_speckle PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_speckle PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_speckle PRIVATE JahshakaEngine)
add_dependencies(test_gi_speckle JahshakaEngineMedia)
jah_gpu_row(gi.speckle CLASS engine TOKENS 2 COMMAND test_gi_speckle
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.speckle PROPERTIES TIMEOUT 180 LABELS "engine")
# gi.hit_offscreen_lights (D3-HIT-SHADE-2): THE WORLD LIGHT LIST — a point lamp and
# a spot whose ranges touch no frustum light a mover's hit seen in a mirror as its
# raster shows them (2 %), and with the list switched off in the same process the
# hit falls to the lamps-off raster (the picture before the lane).
jah_gpu_row(gi.hit_offscreen_lights CLASS engine TOKENS 2 COMMAND test_gi_hit_shade --offscreen-lights
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hit_offscreen_lights PROPERTIES TIMEOUT 120 LABELS "engine")
# gi.hit_planar (D3-HIT-SHADE-2): a PLANAR mirror seen in a traced mirror decodes
# (its own material with the voxel cone's specular) — the error against the true
# (planar raster) picture bounded (5 %, and no further than the screen's own cone);
# the caches' answer (black) shown.
jah_gpu_row(gi.hit_planar CLASS engine TOKENS 2 COMMAND test_gi_hit_shade --planar
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hit_planar PROPERTIES TIMEOUT 120 LABELS "engine")
# gi.hit_voxel_view (D3-HIT-SHADE-2): the view term on the SAME grazing hits — the
# card's answer carries it (the true picture), the voxels' is the view mean (no light
# direction is stored); each held to its own closed form.
jah_gpu_row(gi.hit_voxel_view CLASS engine TOKENS 2 COMMAND test_gi_hit_shade --voxel-view
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hit_voxel_view PROPERTIES TIMEOUT 120 LABELS "engine")

# gi.skin_rays — THE GPU SKIN CACHE AND THE SKINNED BLAS (PHOTON P5, RY-R4;
# PHOTON-SKIN-1)
#
# A two-bone column bent by a clip: rays down meet the POSED arm (analytic to a
# millimetre) one frame after the pose push, never the bind pose; still frames
# and a walk re-skin nothing; a contact ray (the sun-contact job's mask and
# range) is occluded by the posed arm; the GPU scene names the row override; a
# removed character gives its cache back. `--mirror` is the reflection against
# the raster; the no-rays row asserts nothing of the tier exists.
# `test_gi_skin_rays --cost [--chars N --rings R]` is the paired-arm cost
# measurement (by hand, under scripts/gpu-exclusive.sh).
add_executable(test_gi_skin_rays test_gi_skin_rays.cpp)
set_target_properties(test_gi_skin_rays PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_skin_rays PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_skin_rays PRIVATE JahshakaEngine)
add_dependencies(test_gi_skin_rays JahshakaEngineMedia)
jah_gpu_row(gi.skin_rays CLASS engine COMMAND test_gi_skin_rays
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.skin_rays PROPERTIES TIMEOUT 120 LABELS "engine")
# ...and THE MIRROR, its own process: the rigged column bent by its clip, a
# static control and a static MOVER, each reflected in a perfect mirror against
# its raster through the reflected camera. Asserted: no T-pose ghost, the static
# baseline within 3 px, and the rigged and the MOVER arms SHADED over at least the
# control's fraction of their silhouettes (the hit decode, PHOTON-HIT-SHADE-1 —
# gi.hit_shade's arm (c)).
jah_gpu_row(gi.skin_rays_mirror CLASS engine COMMAND test_gi_skin_rays --mirror
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.skin_rays_mirror PROPERTIES TIMEOUT 120 LABELS "engine")
jah_gpu_row(gi.skin_rays_norays CLASS engine COMMAND test_gi_skin_rays
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.skin_rays_norays PROPERTIES
                     TIMEOUT 120 LABELS "engine"
                     ENVIRONMENT "JAHSHAKA_NO_RAY_QUERY=1")

# ---------------------------------------------------------------------------
# photon.view — THE PHOTON VIEW (lane PHOTON-VIEW-1): world.setPhotonView's seven
# debug pictures. On the default scene every mode that can paint changes pixels and
# 'off' after each is the off picture, pixel for pixel; on Showroom 2 at the High
# Photon tier each of the seven repaints more than 20 % of the frame; every refusal
# names its reason (no field, no cards, no rays, the Low mode's passthrough viewport,
# GI off); the thumbnail's shot parks the view. Every shot is the "scene" grade at
# 160 x 90 with every pixel probed. An app run on the rig's display, like atom.view.
set(_photon_view_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-photon_view)
file(MAKE_DIRECTORY ${_photon_view_home}/run)
set(PHOTON_VIEW_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/scripts/photon_view.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/photon_view.js @ONLY)
# ...and arm (f), the COMBINATION with the Atom view (VIEWS-DEPTH-1), whose second
# half is the kernel's: like every GPU row since TEST-SELECTOR-1 H4, the admission
# (scripts/vram_tokens.py) reds the row on any NVRM Xid line from its process tree since
# the launch (scripts/kernel_xid.py reads the journal, no sudo).
jah_gpu_row(photon.view CLASS app TIER epic NEEDS photon bloom ssao smaa planar
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/photon_view.js
         WORKING_DIRECTORY ${_photon_view_home}/run)
set_tests_properties(photon.view PROPERTIES
    TIMEOUT 400 LABELS "scripting"
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}HOME=${_photon_view_home};JAHSHAKA_DATA_ROOT=${_photon_view_home}/.local/share/Jahshaka")

# gi.probe_nan — PROBE-NAN-1 (D4-PHOTON-TIERS): a point light of intensity 20 at 0.3 m over
# a 100 m ground leaves every irradiance-field texel FINITE (tested on the bits) and never
# darkens the field against intensity 2 — the defect turned every probe black at once.
add_executable(test_gi_probe_nan test_gi_probe_nan.cpp)
set_target_properties(test_gi_probe_nan PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_probe_nan PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_probe_nan PRIVATE JahshakaEngine)
add_dependencies(test_gi_probe_nan JahshakaEngineMedia)
jah_gpu_row(gi.probe_nan CLASS engine COMMAND test_gi_probe_nan
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_nan PROPERTIES LABELS "engine" TIMEOUT 120 RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})
# gi.cone_skip — THE PIXEL'S VOXEL CONES RUN ONLY WHERE THEIR ANSWER IS READ (SPEED-GPU,
# perf audit PH-2 / PH-1): the diffuse set only where the field's fallback weight is
# above zero, the specular cone only where the reflection's confidence is below one;
# each skip's arm on and off renders the same floats, in one process. In the gi_chain
# family like every cascade suite.
add_executable(test_gi_cone_skip test_gi_cone_skip.cpp)
set_target_properties(test_gi_cone_skip PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cone_skip PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cone_skip PRIVATE JahshakaEngine)
add_dependencies(test_gi_cone_skip JahshakaEngineMedia)
jah_gpu_row(gi.cone_skip CLASS engine COMMAND test_gi_cone_skip
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cone_skip PROPERTIES TIMEOUT 180 LABELS "engine" RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, the nightly) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST photon.view)
    jah_fresh_home_fixture(photon.view.fresh_home photonViewFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-photon_view" WARM LABELS "scripting")
    set_property(TEST photon.view APPEND PROPERTY FIXTURES_REQUIRED photonViewFreshHome)
endif()
