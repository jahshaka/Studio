

# gi.coalesce (archived: archive/tests/gi) — the drag-a-light gate (spec §5 / P2). Links the MIRROR as well
# as the engine (like tests/mirror) because the coalescing gate is mirror-side
# and the cheap light-only path is engine-side; the contract is the pair.
find_package(Qt6 REQUIRED COMPONENTS Gui)

# PHOTON's tier table, pins and migration (GI_UNIFIED_SPEC.md §2 / P2).
#
# The odd one out in this directory, deliberately: it links no engine of its
# own and renders nothing. Photon's phase-2 subject is RESOLUTION — which
# technique, quality and irradiance-field state a tier and its pins produce —
# and the renderer's half of that is already gated by gi.modes (archived: archive/tests/gi), gi.pcc_mirror (archived: archive/tests/gi)
# and gi.ddgi (archived: archive/tests/gi), none of which this phase touches. Keeping the resolution suite
# beside them is what makes "gi.* is green" mean the whole GI surface.
#
# It compiles the registry straight in (the services layer is not a library),
# exactly like services.memory compiles the two registries it tests, and runs
# with no display at all.
find_package(Qt6 REQUIRED COMPONENTS Gui)

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
jah_ship_row(photon.tiers)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(photon.tiers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "document" TIMEOUT 120)
set_tests_properties(photon.tiers PROPERTIES JAHSHAKA_TEST_FRAMES 0)
jah_no_display(photon.tiers)

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
    jah_gpu_row(gi.cascades_asan SHIP CLASS engine COMMAND test_gi_cascades_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # LeakSanitizer off for the same reason test_engine_asan turns it off (the
    # only leaks are inside libdbus via the Vulkan loader's device-select layer);
    # use-after-free detection is the point and stays on.
    set_tests_properties(gi.cascades_asan PROPERTIES TIMEOUT 180 LABELS "engine"
        RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN}
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    set_tests_properties(gi.cascades_asan PROPERTIES JAHSHAKA_TEST_FRAMES 1966)
endif()

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
    jah_gpu_row(gi.rayquery_asan SHIP CLASS engine COMMAND test_gi_rayquery_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.rayquery_asan PROPERTIES TIMEOUT 120 LABELS "engine"
        RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN}
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    set_tests_properties(gi.rayquery_asan PROPERTIES JAHSHAKA_TEST_FRAMES 180)
endif()

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
jah_gpu_row(cloud_2d.energy SHIP CLASS engine COMMAND test_cloud_2d energy
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cloud_2d.energy PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")
set_tests_properties(cloud_2d.energy PROPERTIES JAHSHAKA_TEST_FRAMES 329)

# VRAM (GATE-ADMIT-1): test_gi_voxel_coverage peaked 2.8 GB -> 3 tokens.



# PHOTON-4 (stage 3): the revived test_gi_metal_bounce.cpp's two rows.
# gi.workflow_lobe - EVERY SURFACE'S SPECULAR LOBE REACHES THE GI, WHATEVER ITS WORKFLOW: a SpecularAsFresnel and a
# Specular plate read their metallic twin on the card, store and decode legs of a gather ray and a reflection ray
# (bar 0.4 %, the archived gi.metal_bounce_route's), and a spec-gloss dielectric carries its 0.04 lobe (its closed form).
# gi.hit_view_term - A STORE-ANSWERED HIT SENDS ITS DIFFUSE LOBE TOWARD ITS VIEWER: a matte floor seen in a mirror at
# N.V 0.15 with the sun behind the eye, the store against the decode and the Disney lobe's closed form (bar 1 %).
add_executable(test_gi_metal_bounce test_gi_metal_bounce.cpp)
set_target_properties(test_gi_metal_bounce PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_metal_bounce PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_metal_bounce PRIVATE JahshakaEngine)
add_dependencies(test_gi_metal_bounce JahshakaEngineMedia)
jah_gpu_row(gi.workflow_lobe CLASS engine COMMAND test_gi_metal_bounce --workflow
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.workflow_lobe PROPERTIES TIMEOUT 120 LABELS "engine")
set_tests_properties(gi.workflow_lobe PROPERTIES JAHSHAKA_TEST_FRAMES 816)
jah_gpu_row(gi.hit_view_term CLASS engine COMMAND test_gi_metal_bounce --view-term
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hit_view_term PROPERTIES TIMEOUT 120 LABELS "engine")
set_tests_properties(gi.hit_view_term PROPERTIES JAHSHAKA_TEST_FRAMES 100)
