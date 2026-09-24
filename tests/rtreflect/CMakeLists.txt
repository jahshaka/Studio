# Ray-traced reflections (SPECS/PHOTON_SPEC.md §7 R5) — headless, framework-free,
# links JahshakaEngine only. Same runtime requirements as tests/ssr and
# tests/gi: a reachable DISPLAY and a Vulkan driver.
#
# TWO ENTRIES, ONE BINARY, and that is the point: `gi.rt_reflect` is the traced
# picture and `gi.rt_reflect_norays` is the picture a machine WITHOUT ray
# queries draws, proved on the same GPU through the tier's own switch. A machine
# that has no ray queries at all skips the first cleanly.
function(jahshaka_rt_reflect_test target)
    add_executable(${target} test_rt_reflect.cpp)
    set_target_properties(${target} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${target} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    add_dependencies(${target} JahshakaEngineMedia)
endfunction()

jahshaka_rt_reflect_test(test_rt_reflect)
target_link_libraries(test_rt_reflect PRIVATE JahshakaEngine)
add_test(NAME gi.rt_reflect COMMAND test_rt_reflect
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rt_reflect PROPERTIES TIMEOUT 300 LABELS "engine")

# THE LAMP ARM — A RAY-TRACED MIRROR IS A RADIOMETER (PHOTON phase A, A1
# section 1.2; lane FENCE-1). Same binary, its own fixture and its own process:
# radiance is invariant along a ray, so a perfect mirror showing an emitter of
# radiance L must read exactly L. Two rows, on the two sides of 1.0 — the
# emissive VOXEL store used to be a UNORM texture, so an emitter authored above
# 1.0 was clipped on its way into the cache (ogre-patch 0087).
#
#   gi.rt_reflect_lamp        L = 0.9, a PERFECT mirror: pixel = L.
#   gi.rt_reflect_lamp_clip   L = 3.0, the same perfect mirror: pixel = L.
#                             Both read the view's RADIANCE through
#                             View::readPixelsHdr (HDR-READBACK-1); the round in
#                             which the 8-bit readback forced this row into a grey
#                             mirror's ratio against a reference below 1.0 is over
#                             and that form is deleted.
add_test(NAME gi.rt_reflect_lamp COMMAND test_rt_reflect --lamp
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rt_reflect_lamp PROPERTIES TIMEOUT 300 LABELS "engine")

add_test(NAME gi.rt_reflect_lamp_clip COMMAND test_rt_reflect --lamp --target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rt_reflect_lamp_clip PROPERTIES
                     TIMEOUT 300 LABELS "engine")

# F1-HITRES (PHOTON-CARDS-2 part B): a perfect mirror's hit reads the hit
# surface's CARD — the reflected panel's row held to the card's closed form pixel
# by pixel, its edge a two-pixel step (the voxel read's numbers printed beside).
add_test(NAME gi.rt_reflect_hitres COMMAND test_rt_reflect --hitres
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rt_reflect_hitres PROPERTIES TIMEOUT 300 LABELS "engine")

# RAY-FMT-CHECK (PHOTON P3): the availability gate asks the device for STORAGE
# support of the tier's history formats and refuses with a log line. On
# LAVAPIPE, twice: the real check (llvmpipe stores to both formats, so it must
# not refuse) and the refusal path through the gate's test door (no device on
# this box lacks either format).
set(_rt_lvp_icd "/usr/share/vulkan/icd.d/lvp_icd.json")
if(EXISTS "${_rt_lvp_icd}")
    add_test(NAME gi.rt_reflect_format_lavapipe COMMAND test_rt_reflect --format-check
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.rt_reflect_format_lavapipe PROPERTIES TIMEOUT 300 LABELS "engine"
                         ENVIRONMENT "VK_ICD_FILENAMES=${_rt_lvp_icd}")
    add_test(NAME gi.rt_reflect_format_refused_lavapipe COMMAND test_rt_reflect --format-check
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.rt_reflect_format_refused_lavapipe PROPERTIES TIMEOUT 300
                         LABELS "engine"
                         ENVIRONMENT "VK_ICD_FILENAMES=${_rt_lvp_icd};JAHSHAKA_RAY_DENY_STORAGE_FORMAT=R32G32_SFLOAT")
endif()

# ENV-RAY-MATCH (PHOTON P3): a silver floor under the realistic sky, the
# environment lobe (rays off) against the ray tier's sky (rays on) for the same
# pixels, read as radiance. 3.25 codes at roughness 0.2 (exact at a mirror): the
# split-sum lookup along R against the VNDF lobe's off-specular lean — the fork's
# media (see envRayMatchMain). LABEL `photon-target` until that lookup moves.
add_test(NAME gi.env_ray_match COMMAND test_rt_reflect --env-ray-match
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.env_ray_match PROPERTIES TIMEOUT 300 LABELS "engine;photon-target")

add_test(NAME gi.rt_reflect_norays COMMAND test_rt_reflect
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rt_reflect_norays PROPERTIES
                     TIMEOUT 300 LABELS "engine"
                     ENVIRONMENT "JAHSHAKA_NO_RAY_QUERY=1")

# REFLECTIONS UNDER A MOVING CAMERA (PAN-SMEAR-1): the moving frame against the
# frame the renderer settles to at the same pose, counted in frames. Two entries,
# one binary, like the pair above: with the ray tier, and the march alone.
add_executable(test_reflect_motion test_reflect_motion.cpp)
set_target_properties(test_reflect_motion PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_reflect_motion PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
add_dependencies(test_reflect_motion JahshakaEngineMedia)
target_link_libraries(test_reflect_motion PRIVATE JahshakaEngine)
add_test(NAME gi.reflect_motion COMMAND test_reflect_motion
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.reflect_motion PROPERTIES TIMEOUT 300 LABELS "engine")
# THE LETTERBOX (SSR-LETTERBOX-1): both rows run a 2.4:1 "the rectangle IS the
# shot" arm and a 2.39:1 moving-vs-settled arm. The no-rays row GATES them (the
# march, the resolve and the reprojection take the shot's uv); the rays row
# prints them as `target:` lines and this row asserts them: the ray tier
# (rq_reflect.comp) reconstructs its rays from the full target's uv, so a
# letterboxed shot with rays reads ~37 codes from the shot view. LABEL
# `photon-target`; the lane that feeds the rectangle to the trace deletes it.
add_test(NAME gi.reflect_motion_letterbox_target COMMAND test_reflect_motion --letterbox-target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.reflect_motion_letterbox_target PROPERTIES
                     TIMEOUT 300 LABELS "engine;photon-target")
add_test(NAME gi.reflect_motion_norays COMMAND test_reflect_motion
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.reflect_motion_norays PROPERTIES
                     TIMEOUT 300 LABELS "engine"
                     ENVIRONMENT "JAHSHAKA_NO_RAY_QUERY=1")

# THE COST, at the resolution the bar is stated at. Same binary, same fixture
# builder, a different question (see costMain).
#
# NO RUN_SERIAL, AND THE MEASUREMENT IS THE REASON (ATOM-RESUMES-1 item 4,
# 2026-09-22). The ledger flagged the FULL-res glossy arm's 2 % margin as a
# contention red risk. Measured on this box, solo and again while a 426-suite
# gate-scope ran at -j2: 0.780 ms solo against 0.778-0.784 ms under load, with the
# other three arms identical to three decimals (0.137 / 0.072 / 0.133) — a 0.8 %
# spread on the tightest arm. Sibling Vulkan suites do not move this pass's own
# timestamp pair, so serialising the row would cost gate wall-clock and buy
# nothing.
#
# WHAT DOES MOVE IT IS THE DEVICE STATE, measured in the fix round: with the SM
# clock LOCKED at 2550 MHz (the rig's own recipe for a GPU number) the card is
# capped at 82 % of its 3105 MHz ceiling and every arm is ~7 % slower —
# 0.833-0.834 ms full-res glossy in three runs, which RED the old 0.80 ms bar
# while the ratios moved by at most 0.49 -> 0.53. So the full-res bar is now
# 0.90 ms (the worst of the two states x 1.08) and the four ratio bars are the
# worst of the two states x ~1.3; three of them are TIGHTER than the first
# version's. The arithmetic and both states' numbers are in costMain.
#
# THE FOUR MILLISECOND BARS ARE `target:` PRINTS SINCE ATOM-FARBLAS-1 (2026-09-23):
# inside a -j2 scoped gate beside two sibling lanes' gates the full-res glossy
# arm read 1.213 ms (0.90 bar) AND the filter/trace ratio 8.76x (7.5x bar); solo
# 3/3 green. The measurement above ("siblings do not move it") was one load; this
# one moved it. A ratio of two passes cancels the CLOCK, not a sibling's
# contention inside one of the two windows — so the ratio bars keep gating, and
# a red of theirs gets the SOLO-RETRY verdict (docs/TESTING_GATE.md §4) before
# anything else.
add_test(NAME gi.rt_reflect_cost COMMAND test_rt_reflect
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rt_reflect_cost PROPERTIES
                     TIMEOUT 300 LABELS "engine"
                     ENVIRONMENT "JAH_RT_REFLECT_COST=1")

if(JAHSHAKA_ASAN)
    # THE SANITISED TWIN for the new pass. `protect_shadow_gap=0` is already in
    # JAH_ASAN_ENGINE_OPTIONS and is load-bearing here: the NVIDIA driver
    # refuses vkCreateDevice with the ray-tracing feature bits enabled under
    # ASan's default shadow gap (PHOTON R1, deviation (a)) — with the gap
    # unprotected the device comes up and the 1,900-line raw-Vulkan tier gets
    # the coverage R1 had to do without.
    jahshaka_rt_reflect_test(test_rt_reflect_asan)
    target_link_libraries(test_rt_reflect_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_rt_reflect_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_rt_reflect_asan PRIVATE -fsanitize=address)
    add_test(NAME gi.rt_reflect_asan COMMAND test_rt_reflect_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.rt_reflect_asan PROPERTIES
                         TIMEOUT 600 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()
