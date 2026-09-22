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
#   gi.rt_reflect_lamp_clip   L = 3.0, a GREY mirror, and the claim is the RATIO
#                             against the same pixel at L = 0.8 (3.75, not 1.25).
#                             THE LABEL `photon-target` CAME OFF HERE
#                             (VOXEL-CLIP-1, 2026-09-22) — and the row's FORM had
#                             to change with it, because an offscreen view's
#                             readback is PFG_RGBA8_UNORM and a perfect mirror at
#                             L = 3.0 reads exactly 1.0 whatever the voxels hold:
#                             the old form could never have gone green. Measured
#                             on the two engines: 1.255x unpatched, 3.745x
#                             patched (see the suite's header).
add_test(NAME gi.rt_reflect_lamp COMMAND test_rt_reflect --lamp
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rt_reflect_lamp PROPERTIES TIMEOUT 300 LABELS "engine")

add_test(NAME gi.rt_reflect_lamp_clip COMMAND test_rt_reflect --lamp --target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.rt_reflect_lamp_clip PROPERTIES
                     TIMEOUT 300 LABELS "engine")

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
# nothing. What the row DID need was a reading that survives a clock change (a GPU
# millisecond on an idle-clocked rig has been measured 20x apart for one pass):
# costMain now also holds the four arms to EACH OTHER — the filter against the
# trace it filters, and half-res against full-res — which is clock-free, and no
# bar was widened.
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
