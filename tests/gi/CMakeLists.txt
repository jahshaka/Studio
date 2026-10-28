

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
    LABELS "document" TIMEOUT 60)
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
    # (TIMEOUT 60, the law's cap: every test under a minute - priced 33.4 s, 2816 frames under the sanitizer; PHOTON-7
    # stage 10's case 12: three poses, each with a fresh build as its oracle, +850 frames)
    set_tests_properties(gi.cascades_asan PROPERTIES TIMEOUT 60 LABELS "engine"
        RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN}
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    set_tests_properties(gi.cascades_asan PROPERTIES JAHSHAKA_TEST_FRAMES 2816)
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
    set_tests_properties(gi.rayquery_asan PROPERTIES TIMEOUT 60 LABELS "engine"
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
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(cloud_2d.energy PROPERTIES JAHSHAKA_TEST_FRAMES 329)

# VRAM (GATE-ADMIT-1): test_gi_voxel_coverage peaked 2.8 GB -> 3 tokens.


# gi.ray_level_fixed — ONE RAY LEVEL PER MESH (PHOTON-7 stage 6): a 4 m camera walk builds no bottom-level structure
# and re-points no near copy, every near copy is level 0, and walk vs a teleport 150 m away: every slot's near copy the
# same structure by its device address (READ_A W5; test_ray_level_fixed.cpp states the law and the base's red). 105 frames.
add_executable(test_ray_level_fixed test_ray_level_fixed.cpp)
set_target_properties(test_ray_level_fixed PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_ray_level_fixed PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_ray_level_fixed PRIVATE JahshakaEngine)
add_dependencies(test_ray_level_fixed JahshakaEngineMedia)
jah_gpu_row(gi.ray_level_fixed CLASS engine COMMAND test_ray_level_fixed
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.ray_level_fixed PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(gi.ray_level_fixed PROPERTIES JAHSHAKA_TEST_FRAMES 105)

# gi.card_sun_lift — A CARD'S SUN RAY STARTS OUTSIDE THE LEVEL IT TRACES (PHOTON-7 stage 6, READ_A finding 3): a
# convex 10.3 m cylinder cap carded at its coarse level (split 4 x 2, 2 cm texels; the level 5.4 cm inside the arc), the
# sun overhead - every texel's sun ray lifted past the captured level's measured bound, none self-shadowed
# (test_card_sun_lift.cpp states the law and the base's red). 49 frames.
# gi.sun_contact_acne — THE SUN CONTACT NEVER SHADOWS A SURFACE BY ITSELF, AND ITS LIFT IS NO SMALLER THAN THE LAW
# (PHOTON-7 stage 6, READ_A W1, READ_B F5): a two-level sphere drawn at its coarse level, whose facets' sag is 1.8x the
# level's STORED bound (a drift inside the bake's 2x law), seen where half the law's lift is under the sag and the law
# over it: the lit hemisphere with the contact job on equals it off; HALF the drawn term (rays.originCut 0.5) and none
# of it (0) both red (test_sun_contact_acne.cpp). 65 frames.
add_executable(test_card_sun_lift test_card_sun_lift.cpp)
set_target_properties(test_card_sun_lift PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_card_sun_lift PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_card_sun_lift PRIVATE JahshakaEngine)
add_dependencies(test_card_sun_lift JahshakaEngineMedia)
jah_gpu_row(gi.card_sun_lift CLASS engine COMMAND test_card_sun_lift
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_sun_lift PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(gi.card_sun_lift PROPERTIES JAHSHAKA_TEST_FRAMES 49)
# gi.card_contact_lift — A CARD'S SUN RAY STARTS ON ITS SURFACE (PHOTON-7 stage 10-3, R10 / B1): a 10-cm slab on a
# carded 16 m floor under a 12-degree sun - its whole analytic contact shadow on the floor's card, nothing past its
# edge shadowed, the planar floor's lift its depth quantum alone (test_card_contact_lift.cpp states the laws and the
# base's red: 39 of 129 umbra points lit while the lift carried a card texel).
add_executable(test_card_contact_lift test_card_contact_lift.cpp)
set_target_properties(test_card_contact_lift PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_card_contact_lift PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_card_contact_lift PRIVATE JahshakaEngine)
add_dependencies(test_card_contact_lift JahshakaEngineMedia)
jah_gpu_row(gi.card_contact_lift CLASS engine COMMAND test_card_contact_lift
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_contact_lift PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(gi.card_contact_lift PROPERTIES JAHSHAKA_TEST_FRAMES 108)
# gi.card_truth — THE TWO WORLD CACHES AGAINST THE ANALYTIC DIRECT AND A BRUTE-FORCE HEMISPHERE (PHOTON-7 stage 10-3,
# R12): a lamp-lit wall read by a small patch - each cache's answer to a gather ray against the wall's closed-form
# radiance toward it, and the patch card's gather against the same rays and a dense hemisphere traced on the CPU through
# the fixture's boxes (test_gi_card_truth.cpp states the laws, the declared red and the red before the kD format fix).
add_executable(test_gi_card_truth test_gi_card_truth.cpp)
set_target_properties(test_gi_card_truth PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_card_truth PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_card_truth PRIVATE JahshakaEngine)
add_dependencies(test_gi_card_truth JahshakaEngineMedia)
jah_gpu_row(gi.card_truth CLASS engine COMMAND test_gi_card_truth
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_truth PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(gi.card_truth PROPERTIES JAHSHAKA_TEST_FRAMES 261)
# gi.card_closed_form — THE CARDS CONVERGE TO THE SERIES' LIMIT INSIDE A SEALED ROOM, AND NOTHING REACHES THE OUTSIDE
# (PHOTON-7 stage 10-3, R12): bounce_series' emitting room with the cards on at the ray tier - the inner faces' area mean
# within q of E / (1 - a') (a' by direct integration), at 16 and 128 rays; an outer face within q of E
# (test_gi_card_closed_form.cpp states the laws and the declared red).
add_executable(test_gi_card_closed_form test_gi_card_closed_form.cpp)
set_target_properties(test_gi_card_closed_form PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_card_closed_form PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_card_closed_form PRIVATE JahshakaEngine)
add_dependencies(test_gi_card_closed_form JahshakaEngineMedia)
jah_gpu_row(gi.card_closed_form CLASS engine COMMAND test_gi_card_closed_form
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_closed_form PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(gi.card_closed_form PROPERTIES JAHSHAKA_TEST_FRAMES 219)
add_executable(test_sun_contact_acne test_sun_contact_acne.cpp)
set_target_properties(test_sun_contact_acne PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_sun_contact_acne PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_sun_contact_acne PRIVATE JahshakaEngine)
add_dependencies(test_sun_contact_acne JahshakaEngineMedia)
jah_gpu_row(gi.sun_contact_acne CLASS engine COMMAND test_sun_contact_acne
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sun_contact_acne PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(gi.sun_contact_acne PROPERTIES JAHSHAKA_TEST_FRAMES 65)

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

# gi.directional_seam (PHOTON-7 stage 1, AB A11; stage 1's read F2) — THE PACKED HALVES OF A DIRECTIONAL VOLUME DO NOT
# BLEED INTO EACH OTHER, AT ANY LEVEL: the hit read at either x face of every cascade, the first half's +x face at levels
# 0 / 0.5 / last / past it, the read past the last level, and a fractional level at a face under a roofed strip
# (test_gi_seam.cpp says the laws; (3) and (4) red on stage 1's tip). A fixture, the High chain.
add_executable(test_gi_seam test_gi_seam.cpp)
set_target_properties(test_gi_seam PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_seam PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_seam PRIVATE JahshakaEngine)
add_dependencies(test_gi_seam JahshakaEngineMedia)
jah_gpu_row(gi.directional_seam CLASS engine COMMAND test_gi_seam
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.directional_seam PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
# THE FRAMES: 24 to the chain's rest, 3 to cascade 0's rebuild under the roof (measured), 12 after it, 2 at the end
set_tests_properties(gi.directional_seam PROPERTIES JAHSHAKA_TEST_FRAMES 41)

# gi.light_order (PHOTON-7 stage 1's read F3, F4) — THE SCENE'S LIGHT ORDER IS THE WORLD'S: twenty lamps of one key (more
# than a voxel's sixteen) created in two orders leave cascade 0's direct term bit-equal (ties by the world, never by
# creation order); 132 lamps in range of both boxes count the box over its 128 light slots. A fixture.
add_executable(test_gi_light_order test_gi_light_order.cpp)
set_target_properties(test_gi_light_order PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_light_order PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_light_order PRIVATE JahshakaEngine)
add_dependencies(test_gi_light_order JahshakaEngineMedia)
jah_gpu_row(gi.light_order CLASS engine COMMAND test_gi_light_order
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.light_order PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.light_order PROPERTIES JAHSHAKA_TEST_FRAMES 45)

# gi.scroll_lag (PHOTON-7 stage 2, THE SCHEDULE) — CASCADE 0 IS NEVER QUEUED BEHIND THE CHAIN: each desktop tier's
# scroll counts (the motion slack at 1.4 m/s) against the stated table; at Epic a 1.4 m/s dolly holds cascade 0's lag
# at most 4 frames and every cascade's within its own slack, a 0.25 m/frame fly cascade 0's at most one step (the
# door: CascadeStatus::lagLast / lagMax). A fixture.
add_executable(test_gi_scroll_lag test_gi_scroll_lag.cpp)
set_target_properties(test_gi_scroll_lag PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_scroll_lag PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_scroll_lag PRIVATE JahshakaEngine)
add_dependencies(test_gi_scroll_lag JahshakaEngineMedia)
jah_gpu_row(gi.scroll_lag CLASS engine COMMAND test_gi_scroll_lag
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.scroll_lag PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.scroll_lag PROPERTIES JAHSHAKA_TEST_FRAMES 794)

# gi.changed_regions (PHOTON-7 stage 3, AB S1; L2) — ONE CHANGED-REGION LIST A FRAME SAYS WHERE THE WORLD CHANGED: a
# settled scene lists nothing for 600 frames; a lamp step lists exactly its old sphere joined with its new; a mover its
# swept box, its sun shadow (the box and the header's direction) and its lamp's sphere; a script move its old and new
# box and a material edit its wearer's box, re-voxelising only the cascades that hold it (A19 deleted); a sun change is
# the sun's overflow and a > 4x sun or sky step restarts; 129 movers merge at the cap and never overflow; a scroll's
# commit lists its injection boxes; the light passes' brick mask marks a lamp step's change only inside its spheres'
# cover (test_gi_changed_regions.cpp says the laws). A fixture.
add_executable(test_gi_changed_regions test_gi_changed_regions.cpp)
set_target_properties(test_gi_changed_regions PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_changed_regions PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_changed_regions PRIVATE JahshakaEngine)
add_dependencies(test_gi_changed_regions JahshakaEngineMedia)
jah_gpu_row(gi.changed_regions CLASS engine COMMAND test_gi_changed_regions
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.changed_regions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.changed_regions PROPERTIES JAHSHAKA_TEST_FRAMES 1071)

# gi.card_oracle (PHOTON-7 stage 5, 5-1) — THE CARDS REFINE ONLY WHAT CHANGED, AND WHAT THEY KEEP IS STILL RIGHT: a lamp
# step on a carded court against an ORACLE (the same final state solved whole: the GI torn down and built again) - every
# lit texel of every resident card within one code of the oracle, the relit and the kept alike; the step relit the
# lamp's props and not every card; the far prop's direct half, outside the lamp's range, untouched
# (test_gi_card_oracle.cpp says the laws). A fixture.
add_executable(test_gi_card_oracle test_gi_card_oracle.cpp)
set_target_properties(test_gi_card_oracle PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_card_oracle PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_card_oracle PRIVATE JahshakaEngine)
add_dependencies(test_gi_card_oracle JahshakaEngineMedia)
jah_gpu_row(gi.card_oracle CLASS engine COMMAND test_gi_card_oracle
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_oracle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.card_oracle PROPERTIES JAHSHAKA_TEST_FRAMES 355)

# gi.field_oracle (PHOTON-7 stage 5, 5-4) — THE FIELD REFINES ONLY WHAT CHANGED, AND WHAT IT KEEPS IS STILL RIGHT: a lamp
# step in the leak room, every probe against a whole re-solve (test_gi_field_oracle.cpp says the laws). A fixture.
add_executable(test_gi_field_oracle test_gi_field_oracle.cpp)
set_target_properties(test_gi_field_oracle PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_field_oracle PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_field_oracle PRIVATE JahshakaEngine)
add_dependencies(test_gi_field_oracle JahshakaEngineMedia)
# (THREE ROWS, PHOTON-7 stage 7: whole past a fixture's 10 s budget - each part its own fixtures: the leak room's step
# against its oracle (1), (2), (7); `_marks` the two sealed rooms and the open step (3), (4); `_shells` the probes outside
# cascade 0 and the cone shells (5), (6); every bar unchanged.)
jah_gpu_row(gi.field_oracle CLASS engine COMMAND test_gi_field_oracle step
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
jah_gpu_row(gi.field_oracle_marks CLASS engine COMMAND test_gi_field_oracle marks
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
jah_gpu_row(gi.field_oracle_shells CLASS engine COMMAND test_gi_field_oracle shells
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# gi.cache_level_picture (PHOTON-7 stage 10-3, the lead's ruling): (6c), the picture at cascade 0's two reachable
# placements, the camera still - the caches' camera-independent footprint level held to what reaches a reader.
jah_gpu_row(gi.cache_level_picture CLASS engine COMMAND test_gi_field_oracle picture
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.field_oracle gi.field_oracle_marks gi.field_oracle_shells gi.cache_level_picture PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.field_oracle PROPERTIES JAHSHAKA_TEST_FRAMES 780)
set_tests_properties(gi.field_oracle_marks PROPERTIES JAHSHAKA_TEST_FRAMES 1508)
set_tests_properties(gi.field_oracle_shells PROPERTIES JAHSHAKA_TEST_FRAMES 949)
set_tests_properties(gi.cache_level_picture PROPERTIES JAHSHAKA_TEST_FRAMES 666)

# gi.card_residency (PHOTON-7 stage 5, 5-2) — THE CARDS RESIDENT ARE THE ONES THE READERS READ, AND A WALK EQUALS A
# TELEPORT ON THEM: two sealed carded rooms that do not both fit the atlas, the gather's hits the readers; a walk from one
# to the other and a teleport compared at the second - its pillars carded, no outer face read, the first room's unread
# cards evicted, the read set resident in both runs (the values of the cards resident in both PRINTED, not yet a law:
# the cascades' placements are path-dependent until STORE-PLACEMENT-1), and the atlas compacting on a crowded
# room's arrivals (test_gi_card_residency.cpp says the laws). A fixture.
add_executable(test_gi_card_residency test_gi_card_residency.cpp)
set_target_properties(test_gi_card_residency PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_card_residency PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_card_residency PRIVATE JahshakaEngine)
add_dependencies(test_gi_card_residency JahshakaEngineMedia)
# ONE ROW A LAW GROUP (PHOTON-7 stage 10, the lead's ruling: each within the fixture's 10 s, every run the fewest frames
# its law needs): gi.card_residency the walk and the teleport ((1) (2) (3) (7)), _pinned the history part (6), _crowded
# the compaction (4), _late the late first reader (5), _held the band card held in and out at B (8).
foreach(_row walk pinned crowded late held)
    if(_row STREQUAL "walk")
        set(_name gi.card_residency)
    else()
        set(_name gi.card_residency_${_row})
    endif()
    jah_gpu_row(${_name} CLASS engine COMMAND test_gi_card_residency ${_row}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(${_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                         TIMEOUT 60 LABELS "engine")
endforeach()
set_tests_properties(gi.card_residency PROPERTIES JAHSHAKA_TEST_FRAMES 843)
set_tests_properties(gi.card_residency_pinned PROPERTIES JAHSHAKA_TEST_FRAMES 772)
set_tests_properties(gi.card_residency_crowded PROPERTIES JAHSHAKA_TEST_FRAMES 399)
set_tests_properties(gi.card_residency_late PROPERTIES JAHSHAKA_TEST_FRAMES 669)
set_tests_properties(gi.card_residency_held PROPERTIES JAHSHAKA_TEST_FRAMES 943)

# gi.gather_sliver (PHOTON-7 stage 10) — THE GRID ANSWERS EVERY PIXEL: a 1-px sliver of a wall seen past two black
# plates (between them, and at the view's edge) is answered by a probe on every settling frame, its noise within the
# open wall's, and held within a code of the plate-free view's; the placement's ms printed at 1080p, High and Epic
# (test_gi_gather_sliver.cpp says the laws). A fixture.
add_executable(test_gi_gather_sliver test_gi_gather_sliver.cpp)
set_target_properties(test_gi_gather_sliver PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_gather_sliver PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_gather_sliver PRIVATE JahshakaEngine)
add_dependencies(test_gi_gather_sliver JahshakaEngineMedia)
jah_gpu_row(gi.gather_sliver CLASS engine COMMAND test_gi_gather_sliver
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.gather_sliver PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.gather_sliver PROPERTIES JAHSHAKA_TEST_FRAMES 833)

# gi.card_lamp_shadow (PHOTON-7 stage 5, 5-3) — THE CARDS SEE LAMP SHADOWS, TRACED: a blocker between a lamp and a
# carded floor - the card's direct half 0 in the umbra and the lamp's closed form past it (R12), and
# the shadow following the moved MOVER (the movable channel: its LampShadow region, inside the lamp's range) within a
# code of a whole re-solve
# (test_gi_card_lamp_shadow.cpp says the laws). A fixture.
add_executable(test_gi_card_lamp_shadow test_gi_card_lamp_shadow.cpp)
set_target_properties(test_gi_card_lamp_shadow PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_card_lamp_shadow PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_card_lamp_shadow PRIVATE JahshakaEngine)
add_dependencies(test_gi_card_lamp_shadow JahshakaEngineMedia)
# (TWO ROWS, PHOTON-7 stage 7: whole past a fixture's 10 s budget - each part its own fixture: the lamp's two shadows
# (1), (2); `_area` the area light's form factor, penumbra and pixel arms (3)-(8), and the no-ray route's (9), (10);
# every bar unchanged.)
jah_gpu_row(gi.card_lamp_shadow CLASS engine COMMAND test_gi_card_lamp_shadow lamp
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
jah_gpu_row(gi.card_lamp_shadow_area CLASS engine COMMAND test_gi_card_lamp_shadow area
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.card_lamp_shadow gi.card_lamp_shadow_area PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.card_lamp_shadow PROPERTIES JAHSHAKA_TEST_FRAMES 283)
set_tests_properties(gi.card_lamp_shadow_area PROPERTIES JAHSHAKA_TEST_FRAMES 893)

# gi.leak_room (restored for PHOTON-7 stage 5, 5-3; held at 163d90223 in archive/tests/gi) — THE LEAK THROUGH A WALL,
# the field's and THE CARDS': a sealed room with a lamp inside and a red one outside, the red that reaches the inside
# is the leak (test_gi_leak_room.cpp says the laws). A fixture.
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
jah_gpu_row(gi.leak_room CLASS engine FRAMES 384 COMMAND test_gi_leak_room
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.leak_room PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.relight_credit (stage 5's read B1) — A LAMP'S RELIGHT AFTER A SHADER WAIT SPENDS ONE FRAME'S COUNT: the test door
# gi.relightShaderHold holds every cascade's relight 40 frames over a lamp step; then no frame relights more than the
# count plus the spending cascades' credit caps (test_gi_relight_credit.cpp says the laws). A fixture.
add_executable(test_gi_relight_credit test_gi_relight_credit.cpp)
set_target_properties(test_gi_relight_credit PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_relight_credit PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_relight_credit PRIVATE JahshakaEngine)
add_dependencies(test_gi_relight_credit JahshakaEngineMedia)
jah_gpu_row(gi.relight_credit CLASS engine FRAMES 410 COMMAND test_gi_relight_credit
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.relight_credit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.opaque_bindings (PHOTON-7 stage 7, 7-1) — THE OPAQUE PASSES READ ONLY SCREEN-SPACE GI (L4): the door
# (PhotonStatus.h photonPassBindings) lists every Photon texture each named colour pass binds; at every tier on a ray
# chain, a no-ray chain and a live offscreen chain the opaque passes list only the five screen products, and the refractives pass (the
# control) a world cache (test_gi_opaque_bindings.cpp says the laws). Red on the base; the ray arms green at 7-5, the
# no-ray and live arms at 7-6. A fixture.
add_executable(test_gi_opaque_bindings test_gi_opaque_bindings.cpp)
set_target_properties(test_gi_opaque_bindings PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_opaque_bindings PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_opaque_bindings PRIVATE JahshakaEngine)
add_dependencies(test_gi_opaque_bindings JahshakaEngineMedia)
jah_gpu_row(gi.opaque_bindings CLASS engine FRAMES 745 COMMAND test_gi_opaque_bindings
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.opaque_bindings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.screen_integrate (PHOTON-7 stage 7, 7-6; R12) — THE NO-RAY ROUTE'S PRODUCER MOVED, THE PHYSICS DID NOT: the screen
# integrate's picture and the voxel route the pixel ran before 7-6 (the door gi.screenIntegrateOff), both against a
# high-ray TLAS reference of the same arrangement at the ray tier, in one process at the GI's rest, High
# (test_gi_screen_integrate.cpp says the laws and the no-ray route's declared distance). A fixture.
add_executable(test_gi_screen_integrate test_gi_screen_integrate.cpp)
set_target_properties(test_gi_screen_integrate PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_screen_integrate PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_screen_integrate PRIVATE JahshakaEngine)
add_dependencies(test_gi_screen_integrate JahshakaEngineMedia)
jah_gpu_row(gi.screen_integrate CLASS engine FRAMES 637 COMMAND test_gi_screen_integrate
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.screen_integrate PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.screen_bind (PHOTON-7 stage 7, the lead's two risk fixes) — THE NO-RAY SCREEN PASSES' SHARED JOBS BIND NOTHING
# STALE (the door gi.screenBindMissing: a pass without its inputs writes nothing, every slot its own stand-in; three
# views, one destroyed), AND A SCREENSHOT LEAVES THE LIVE VIEW'S AREA HISTORY ALONE (a frame capture and a still at
# another size, the live view against a twin scene's in one process; test_gi_screen_bind.cpp says the laws). A fixture.
add_executable(test_gi_screen_bind test_gi_screen_bind.cpp)
set_target_properties(test_gi_screen_bind PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_screen_bind PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_screen_bind PRIVATE JahshakaEngine)
add_dependencies(test_gi_screen_bind JahshakaEngineMedia)
jah_gpu_row(gi.screen_bind CLASS engine FRAMES 600 COMMAND test_gi_screen_bind
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.screen_bind PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.area_contact (PHOTON-7 stage 7's read, F6; L9) — THE NO-RAY AREA VISIBILITY'S MARCH STARTS A CELL ALONG THE NORMAL:
# an area light behind a wall leaves the whole floor in front of it in shadow, the band along the wall's foot too
# (test_gi_area_contact.cpp says the law). A fixture.
add_executable(test_gi_area_contact test_gi_area_contact.cpp)
set_target_properties(test_gi_area_contact PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_area_contact PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_area_contact PRIVATE JahshakaEngine)
add_dependencies(test_gi_area_contact JahshakaEngineMedia)
jah_gpu_row(gi.area_contact CLASS engine FRAMES 300 COMMAND test_gi_area_contact
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.area_contact PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.rough_lobe_room (PHOTON-7 stage 7's read, F4; L9) — THE NO-RAY PRODUCER AND ITS PIXEL DECIDE THE ROUGH LOBE FROM
# ONE ROUGHNESS, THE FOOTPRINT'S (the widened alpha of a normal-mapped surface): a closed room's normal-mapped floor
# stays dark to a sky ten times brighter (test_gi_rough_lobe_room.cpp says the law). A fixture.
add_executable(test_gi_rough_lobe_room test_gi_rough_lobe_room.cpp)
set_target_properties(test_gi_rough_lobe_room PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_rough_lobe_room PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_rough_lobe_room PRIVATE JahshakaEngine)
add_dependencies(test_gi_rough_lobe_room JahshakaEngineMedia)
jah_gpu_row(gi.rough_lobe_room CLASS engine FRAMES 400 COMMAND test_gi_rough_lobe_room
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rough_lobe_room PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.emitter_energy (PHOTON-7 stage 10-3, R2; energy is conserved) - AN EMITTER OVER THE OLD MERGE CAP KEEPS ITS LIGHT:
# bounce_series' sealed room with every slab emitting E = 100 and 1000 (High); the emissive stores hold E and the
# room's radiance over E is the same in both (test_gi_emitter_energy.cpp says the laws). RED before: every emitter over 16
# entered the voxels at 16. A fixture.
add_executable(test_gi_emitter_energy test_gi_emitter_energy.cpp)
set_target_properties(test_gi_emitter_energy PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_emitter_energy PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_emitter_energy PRIVATE JahshakaEngine)
add_dependencies(test_gi_emitter_energy JahshakaEngineMedia)
jah_gpu_row(gi.emitter_energy CLASS engine FRAMES 110 COMMAND test_gi_emitter_energy
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.emitter_energy PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.small_emitter (PHOTON-7 stage 10-3; L9 is physics) - A SMALL OBJECT IS GEOMETRY AT EVERY CASCADE: a 0.20 m emitter
# 3 m from a mirror is in cascades 2 and 3 (their stores hold it by its coverage) and the mirror shows it as at 1.2 m
# (test_gi_small_emitter.cpp says the laws). RED before: the sub-voxel cull (rule 2) left it out of c2 and c3, and every
# ray past 1.9 m passed through it. A fixture.
add_executable(test_gi_small_emitter test_gi_small_emitter.cpp)
set_target_properties(test_gi_small_emitter PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_small_emitter PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_small_emitter PRIVATE JahshakaEngine)
add_dependencies(test_gi_small_emitter JahshakaEngineMedia)
jah_gpu_row(gi.small_emitter CLASS engine FRAMES 400 COMMAND test_gi_small_emitter
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.small_emitter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.bounce_series.<tier> (PHOTON-7 stage 4; SPECS/PHOTON_7_SPEC.md stage 4) — THE BOUNCES CONVERGE BY FEEDBACK TO THE
# SERIES' LIMIT, read in the voxel store, ONE ROW A TIER (a fixture's 10 s budget): a sealed 8 x 4 x 8 m room of emissive
# walls at albedo 0.5 - the feedback at its rest within one code of a 64-pass Jacobi over cascade 0 (the measuring door
# gi.bounceJacobi), both within 3 % of
# E / (1 - a'(1 - e)) with e the room's escape MEASURED by its sky-leak arm, and the operator's own arm (the sky held at
# E / (1 - a')) converging to it (test_gi_bounce_series.cpp says the laws). A fixture.
add_executable(test_gi_bounce_series test_gi_bounce_series.cpp)
set_target_properties(test_gi_bounce_series PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_bounce_series PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_bounce_series PRIVATE JahshakaEngine)
add_dependencies(test_gi_bounce_series JahshakaEngineMedia)
# (High and Epic in THREE ROWS each, PHOTON-7 stage 7: whole they passed the fixture's 10 s budget - the series part
# (the sky-leak, main and operator arms and the resume arm: laws (1)-(4)); `_fade` (the main room and the resume arm's
# state rebuilt, then (5)'s FADE arm and (6), which rides on the fade); `_lamp` (the same state, then (5)'s STEP arm and
# (7), the carried route from the step's end); every bar unchanged.)
foreach(_tier low medium)
    jah_gpu_row(gi.bounce_series.${_tier} CLASS engine COMMAND test_gi_bounce_series ${_tier}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.bounce_series.${_tier} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                         TIMEOUT 60 LABELS "engine")
endforeach()
foreach(_tier high epic)
    jah_gpu_row(gi.bounce_series.${_tier} CLASS engine COMMAND test_gi_bounce_series ${_tier} series
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    jah_gpu_row(gi.bounce_series.${_tier}_fade CLASS engine COMMAND test_gi_bounce_series ${_tier} fade
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    jah_gpu_row(gi.bounce_series.${_tier}_lamp CLASS engine COMMAND test_gi_bounce_series ${_tier} lamp
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # (8) THE RAY TIER (PHOTON-7 stage 10, 10-2b): the voxel sides by TLAS rays - e = 0, and the Jacobi at the ray tier
    jah_gpu_row(gi.bounce_series.${_tier}_rays CLASS engine COMMAND test_gi_bounce_series ${_tier} rays
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.bounce_series.${_tier} gi.bounce_series.${_tier}_fade gi.bounce_series.${_tier}_lamp
                         gi.bounce_series.${_tier}_rays PROPERTIES
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")
endforeach()
set_tests_properties(gi.bounce_series.low PROPERTIES JAHSHAKA_TEST_FRAMES 208)
set_tests_properties(gi.bounce_series.medium PROPERTIES JAHSHAKA_TEST_FRAMES 200)
foreach(_tier high epic)
    set_tests_properties(gi.bounce_series.${_tier} PROPERTIES JAHSHAKA_TEST_FRAMES 302)
    set_tests_properties(gi.bounce_series.${_tier}_fade PROPERTIES JAHSHAKA_TEST_FRAMES 1530)
    set_tests_properties(gi.bounce_series.${_tier}_lamp PROPERTIES JAHSHAKA_TEST_FRAMES 459)
    set_tests_properties(gi.bounce_series.${_tier}_rays PROPERTIES JAHSHAKA_TEST_FRAMES 203)
endforeach()

# gi.scroll_slab (ENGINE-UPGRADE-U5 stage 1; RESTORED AND RE-POINTED by PHOTON-7 stage 2) — A SCROLLED CASCADE IS THE
# CASCADE BUILT WHERE IT IS: three slabs in one scroll, a spread scroll (and a step cut to the scratch's layers), an edit
# in the box being built, two chained scrolls and a live-box edit, a torn shift (and its twice-failed repair) each leave
# cascade 0's store equal, float for float, to a whole build at the same placement; a commit's light (the direct as a
# delta: an unchanged voxel keeps every bit, the voxels lit = the slab's + the changed direct set, counted by the door,
# walk = teleport on the direct); the scratches' VRAM at Epic (test_gi_scroll_slab.cpp says the laws). A fixture, a
# two-cascade 64-cell chain.
add_executable(test_gi_scroll_slab test_gi_scroll_slab.cpp)
set_target_properties(test_gi_scroll_slab PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_scroll_slab PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_scroll_slab PRIVATE JahshakaEngine)
add_dependencies(test_gi_scroll_slab JahshakaEngineMedia)
jah_gpu_row(gi.scroll_slab CLASS engine COMMAND test_gi_scroll_slab
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.scroll_slab PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.scroll_slab PROPERTIES JAHSHAKA_TEST_FRAMES 272)

# gi.shell_ends (PHOTON-7 stage 1's read F1) — EVERY CASCADE MARCHES THE WORLD CHAIN'S SHELLS: at the four desktop tiers
# every cascade's shell list (GiStatus::CascadeStatus::shellEnds) equals sum 8 x cell_i over the chain (test_gi_shells.cpp
# says the law; red on the base: an inner entry summed the marching cascade's own cell). A fixture.
add_executable(test_gi_shells test_gi_shells.cpp)
set_target_properties(test_gi_shells PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_shells PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_shells PRIVATE JahshakaEngine)
add_dependencies(test_gi_shells JahshakaEngineMedia)
jah_gpu_row(gi.shell_ends CLASS engine COMMAND test_gi_shells
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.shell_ends PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.shell_ends PROPERTIES JAHSHAKA_TEST_FRAMES 27)

# gi.voxel_ray_wall (PHOTON-7 stage 10, 10-1; L9) — THE CACHES' RAY READS OCCUPANCY: a short 0.4 m wall in the outer
# cascade's cells stops a ray at 0-85 degrees off its normal, the wall-ceiling edge cell stops it, the hit reads the side
# facing the ray, and the surface a ray leaves is never its occluder (test_gi_voxel_ray.cpp says the laws; red on the
# base: the harness answered the aperture-0 march - today's ray - which passes 19-59 % through the 1 m wall at every
# angle; the edge cell held on the base, its ray reading both halves). A fixture.
add_executable(test_gi_voxel_ray test_gi_voxel_ray.cpp)
set_target_properties(test_gi_voxel_ray PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_voxel_ray PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_voxel_ray PRIVATE JahshakaEngine)
add_dependencies(test_gi_voxel_ray JahshakaEngineMedia)
jah_gpu_row(gi.voxel_ray_wall CLASS engine COMMAND test_gi_voxel_ray
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.voxel_ray_wall PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(gi.voxel_ray_wall PROPERTIES JAHSHAKA_TEST_FRAMES 93)

# gi.sealed_hall / gi.sealed_hall_noray (PHOTON-7 stage 10-3, R3; L9: a sky read with no visibility is light through a
# wall) - A SEALED HALL WIDER THAN THE FIELD'S WINDOW IS DARK INSIDE in every frame from the GI's build to rest, an area
# light over its roof and the sky outside (test_gi_sealed_hall.cpp says the laws). RED before: the ray tier's integrate
# fell back to the environment's diffuse past the field's window; the no-ray tier's first frames took the environment
# and an unshadowed area light. One row a tier. A fixture.
add_executable(test_gi_sealed_hall test_gi_sealed_hall.cpp)
set_target_properties(test_gi_sealed_hall PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_sealed_hall PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_sealed_hall PRIVATE JahshakaEngine)
add_dependencies(test_gi_sealed_hall JahshakaEngineMedia)
jah_gpu_row(gi.sealed_hall CLASS engine FRAMES 430 COMMAND test_gi_sealed_hall ray
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sealed_hall PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")
jah_gpu_row(gi.sealed_hall_noray CLASS engine FRAMES 430 COMMAND test_gi_sealed_hall noray
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.sealed_hall_noray PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.long_hall (PHOTON-7 stage 10-3, R4; L9: "ran out of distance" is not a miss) - A SEALED HALL LONGER THAN THE
# CASCADES' REACH IS DARK IN ITS SURFACE CACHE: no inner-face card texel holds one code of the roof's (the cards' rays
# run to the world's exit; test_gi_long_hall.cpp says the law). RED before: a ray that reached the outermost cascade's
# radius read the sky. A fixture.
add_executable(test_gi_long_hall test_gi_long_hall.cpp)
set_target_properties(test_gi_long_hall PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_long_hall PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_long_hall PRIVATE JahshakaEngine)
add_dependencies(test_gi_long_hall JahshakaEngineMedia)
jah_gpu_row(gi.long_hall CLASS engine FRAMES 510 COMMAND test_gi_long_hall
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.long_hall PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "engine")

# gi.sealed_sky (PHOTON-7 stage 10; L9) — THE SKY CANNOT LIGHT A SEALED ROOM, on every store that carries it: a closed box
# under a uniform sky at High and Epic, its control the roof (test_gi_sealed_sky.cpp says the laws; red on the base: the
# card relight's sixteen-cone sky share passed the walls, 10-0's P1). A fixture. 10-3 item 1: the pixel's diffuse and
# specular (two metal panels), the no-ray tier as it ships, the teleport into the box (every frame of the window read) -
# RED where a path still leaks (10-3 item 2's table).
add_executable(test_gi_sealed_sky test_gi_sealed_sky.cpp)
set_target_properties(test_gi_sealed_sky PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_sealed_sky PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_sealed_sky PRIVATE JahshakaEngine)
add_dependencies(test_gi_sealed_sky JahshakaEngineMedia)
# ONE ROW A TIER (10-3, the lead's ruling: each within the fixture's 10 s with margin): gi.sealed_sky the ray tier
# (High and Epic), gi.sealed_sky_noray the no-ray tier as it ships (the field on) and with the field off.
foreach(_row ray noray)
    if(_row STREQUAL "ray")
        set(_name gi.sealed_sky)
    else()
        set(_name gi.sealed_sky_${_row})
    endif()
    jah_gpu_row(${_name} CLASS engine COMMAND test_gi_sealed_sky ${_row}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(${_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                         TIMEOUT 60 LABELS "engine")
endforeach()
set_tests_properties(gi.sealed_sky PROPERTIES JAHSHAKA_TEST_FRAMES 409)
set_tests_properties(gi.sealed_sky_noray PROPERTIES JAHSHAKA_TEST_FRAMES 335)

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

# gi.light_is_world (PHOTON-7 stage 1, L1: LIGHT IS A WORLD FUNCTION) — the direct term of a world voxel is the same at two
# placements of its cascade (the occluders in cascade 1's and 2's shells, a cut-out partial plane, a lamp in range of one
# placement only), a pinned chain neither aborts nor moves when the eye or the pin moves, and the floor under an 8 m
# plate is dark at every tier (the march through the chain to the last cascade's exit). jahlab's `pin` and `voxels`
# doors on photon-shells and photon-roof (light_is_world.sh says the arms, the band and the base). 960x540; two sessions,
# 125 + 67 frames (LAB END, 2026-10-23).
if(TARGET jahlab)
    jah_gpu_row(gi.light_is_world CLASS engine FRAMES 192
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/light_is_world.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/light_is_world
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.light_is_world PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# gi.walk_court / gi.walk_glass (PHOTON-7 stage 0, L6: THE WALK IS THE PRODUCT) — THE WALKING ROWS, ONE A SCENE (ONE ROW
# PER PROOF, the lead's ruling 2026-10-28: the combined gi.walk's frames were already the fewest its laws need and its
# wall reached 70-87 s on a loaded box): jahlab's stillness door on photon-court's 15 m crossing and on photon-glass's
# 2.4 m walk, each a LIVE and two GI-FROZEN walks on one warm cache (a prime session first); M1 per class (live less
# frozen per pixel: mean / p99 / share over one code) and the hitch law on the wall and the GPU clock PRINTED - the bars
# are the later stages'; each asserts only its instrument's laws: frozen = frozen, the arms start equal, the classes
# cover the counted pixels (walk.sh says the laws and the base). 960x540; four sessions each: the prime's 7 frames and
# three walks of the hold + teleport + hold:1 + the 60-frame walk + the 8-frame window + 1 (LAB END: the court 231, the
# glass 671). ALL THE CARD'S SLOTS (TOKENS 8): their GPU clocks are read alone.
if(TARGET jahlab)
    jah_gpu_row(gi.walk_court CLASS engine TOKENS 8 SHIP FRAMES 700
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/walk.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/walk_court
                --court
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.walk_court PROPERTIES TIMEOUT 60 LABELS "engine")
    jah_gpu_row(gi.walk_glass CLASS engine TOKENS 8 SHIP FRAMES 2020
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/walk.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/walk_glass
                --glass
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.walk_glass PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# gi.walk_rough (PHOTON-7 stage 7, 7-3; R12) — THE ROUGH SPECULAR'S WALK LAGS ITS CONVERGED PICTURE BY NO MORE THAN ITS
# MEMORY AND ITS NOISE: rough_lag.sh on photon-rough (its four rough metal strips, the lobe alone): at walk frames 14 /
# 28 / 59 per rough pixel |walk - conv| <= (N - 1) |conv(k+1) - conv(k)| + 3 SE_walk + 1, breaking <= 1 % (the lag
# law; N the gather's history count, SE_walk its raw lobe's sigma / sqrt N). 960x540; two sessions.
if(TARGET jahlab)
    jah_gpu_row(gi.walk_rough CLASS engine TOKENS 8 FRAMES 4314
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/rough_lag.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/walk_rough
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.walk_rough PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# gi.walk_area (PHOTON-7 stage 7: the walk 7-A owed, at the stage's master check) — THE AREA LIGHT'S PENUMBRA WALK:
# walk.sh --area on photon-area (a plate's 3 m penumbra across the floor, a glossy strip through it): M1 and the live
# residual per class printed (the area visibility's history stepping while the camera moves); the walk's own laws
# asserted. 960x540; three sessions.
if(TARGET jahlab)
    jah_gpu_row(gi.walk_area CLASS engine TOKENS 8 FRAMES 1342
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/walk.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/walk_area --area
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.walk_area PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# gi.rough_lobe_quadrature / gi.rough_reference (PHOTON-7 stage 7, 7-3) — THE ROUGH SPECULAR'S TWO LAWS (rough_spec.sh):
# the split sum's own error - the shipped read against the brute-force quadrature of the same probe map, within a code -
# and the lobe at rest against the traced reflection's 4,096-sample reference, its energy within the reference's SE + 1
# code (R12; declared red - rough_spec.sh says why).
if(TARGET jahlab)
    jah_gpu_row(gi.rough_lobe_quadrature CLASS engine TOKENS 8 FRAMES 270
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/rough_spec.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/rough_quad --quadrature
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.rough_lobe_quadrature PROPERTIES TIMEOUT 60 LABELS "engine")
    jah_gpu_row(gi.rough_reference CLASS engine TOKENS 8 FRAMES 4560
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/rough_spec.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/rough_ref --reference
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.rough_reference PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# gi.low_gather (PHOTON-7 stage 7, 7-4) — LOW GAINS THE GATHER: low_gather.sh on photon-court, Low against Medium within
# their sampling noise (at Medium, its own gather row against Low's by the arms, each converged at two rest seeds), the Low
# tier's gather shape asserted, the gather's GPU rows over a 30-frame walk and its VRAM printed. 960x540; two sessions.
if(TARGET jahlab)
    jah_gpu_row(gi.low_gather CLASS engine TOKENS 8 FRAMES 1650
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/low_gather.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/low_gather
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gi.low_gather PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# gi.walk_bistro is ARCHIVED (PHOTON-7 stage 5, the lead's ruling): its laws cannot fit 60 s at Bistro's ~25 ms a frame in
# Debug - measured warm (scripts/lab/warm.sh), its hold cut to the frozen arm's minimum (the reflection held from frame
# 611: 620), the live session 28.5 s, the frozen 16.5 s, then M1 past the cap. A MEASURE run by hand once at the master
# check: walk.sh <jahlab> <work dir> --bistro <BistroExterior.fbx> <data root> under scripts/gpu-exclusive.sh, after
# scripts/lab/warm.sh --tier epic at the walk's two poses on that data root (walk.sh says them; a stamp is keyed on the tier and
# the cameras).

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

# gi.inject_fold (PHOTON-7's walk hitch, 2) — THE INJECTION's BOXES FOLD PAIR BY PAIR, NEVER INTO THEIR UNION: a two-axis
# step's 22 regions fold to the injection's 16 boxes, every input cell held, the moved faces apart, under half the cascade
# covered (the union spanned it whole: Bistro Interior's c0 / c1 commits at 100 % of 128^3). Pure geometry: no display.
add_executable(test_inject_fold test_inject_fold.cpp)
set_target_properties(test_inject_fold PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_inject_fold PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME gi.inject_fold COMMAND test_inject_fold WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.inject_fold PROPERTIES LABELS "gi" TIMEOUT 60)
set_tests_properties(gi.inject_fold PROPERTIES JAHSHAKA_TEST_FRAMES 0)
jah_no_display(gi.inject_fold)

# gi.hit_specular_mobility (PHOTON-7 11-2 (3), C2: THE HIT'S OWN SPECULAR) — A SURFACE SEEN IN A REFLECTION REFLECTS THE
# WORLD THE SAME STILL AND AS A MOVER, AND CONTINUOUSLY IN ITS METALNESS: a glossy slab under an emissive ceiling panel,
# seen in a mirror; (1) still against a mover within q + 3 SE, (2) metalness 0.49 against 0.51 within 5 % + q + 3 SE
# (test_gi_hit_specular_mobility.cpp; RED before C2: the still hit's store held the indirect specular only as its lobe's
# integral, and the metal switch decoded 0.51). Four arms, a scene each, in one process; 256x144.
add_executable(test_gi_hit_specular_mobility test_gi_hit_specular_mobility.cpp)
set_target_properties(test_gi_hit_specular_mobility PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_hit_specular_mobility PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_hit_specular_mobility PRIVATE JahshakaEngine)
add_dependencies(test_gi_hit_specular_mobility JahshakaEngineMedia)
jah_gpu_row(gi.hit_specular_mobility CLASS engine COMMAND test_gi_hit_specular_mobility
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.hit_specular_mobility PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(gi.hit_specular_mobility PROPERTIES JAHSHAKA_TEST_FRAMES 895)
