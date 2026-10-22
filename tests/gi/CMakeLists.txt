

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
add_executable(test_photon_tiers test_photon_tiers.cpp)
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


# gi.probe_court — A PROBE THAT PHOTOGRAPHED A FLOOR AND A WALL IS KEPT (PHOTON-5 stage 1):
# an open court keeps >= 1 probe at Medium, a bare floor printed, a sealed room its whole grid.
add_executable(test_gi_probe_court test_gi_probe_court.cpp)
set_target_properties(test_gi_probe_court PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_probe_court PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_probe_court PRIVATE JahshakaEngine)
add_dependencies(test_gi_probe_court JahshakaEngineMedia)
jah_gpu_row(gi.probe_court CLASS engine COMMAND test_gi_probe_court
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.probe_court PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.probe_court PROPERTIES JAHSHAKA_TEST_FRAMES 30)
jah_ship_row(gi.probe_court)   # THE SHIP SET: PHOTON-5's end-to-end row (the lead's promotion at the merge)

# gi.hit_list_demand (PHOTON-2 stage 2's tail, 1.6 A2; PHOTON-7 stage 0: its forced-overflow arm only) — A DROPPED HIT
# RECORD IS NO SAMPLE: jahlab on photon-room with every gather ray a record (gather.decodeHits); the overflow arm
# rq.hitListCap holding the records to a third FORCES drops and the floor stays within one code (the picture's
# quantum) of the undropped session (hit_list_demand.sh says the law and the base). 960x540; two sessions of 151 frames.
if(TARGET jahlab)
    jah_gpu_row(gi.hit_list_demand CLASS engine FRAMES 302
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/hit_list_demand.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/hit_list_demand
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.hit_list_demand PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# gi.hold_after_stop (PHOTON-2 stage 2 and its round, rev 8's H4 as rev 9 restated it) — THE GATHER SETTLES, THEN HOLDS:
# jahlab's stillness door on photon-room (a walk inside one cell; a lamp step at rest: the field's change within 36
# frames, falling over 10-frame windows, then the hold, no burst) and photon-court's 15 m crossing (the change falling
# over 10-frame windows to the hold with J5's 0.01-code tolerance, 0 codes after it, the hold <= the last input
# write + 17, the field's last write <= 40 frames, a follow frame's extra GPU <= 3 ms and its entering planes' fill
# frames stepping no more pixels than the same crossing with them inline - J1's fade) and THE FOLIAGE CLASS (K5).
# hold_after_stop.sh says the law and the base.
# 960x540; five jahlab sessions: 393, 424, 483, 483 and 483 frames.
if(TARGET jahlab)
    jah_gpu_row(gi.hold_after_stop CLASS engine FRAMES 2266
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/hold_after_stop.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/hold_after_stop
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.hold_after_stop PROPERTIES TIMEOUT 60 LABELS "engine")
endif()
