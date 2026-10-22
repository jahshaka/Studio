# FRAMES (TESTING-SPEED-B1, 2026-10-12): every row here states JAHSHAKA_TEST_FRAMES — the frames its process(es) render,
# COUNTED on the Debug tree (an LD_PRELOAD count of Ogre::Root::_updateAllRenderTargets per row; an _asan twin = its
# twin's count; 0 = no engine frame): spikes/testing-speed-b1/frames.json, framecount/.
# Ray-traced reflections (SPECS/PHOTON_SPEC.md §7 R5) — headless, framework-free,
# links JahshakaEngine only. Same runtime requirements as tests/ssr and
# tests/gi: a reachable DISPLAY and a Vulkan driver.
#
# TWO ENTRIES, ONE BINARY, and that is the point: `gi.rt_reflect` (archived: archive/tests/gi) is the traced
# picture and `gi.rt_reflect_norays` (archived: archive/tests/gi) is the picture a machine WITHOUT ray
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
    jah_gpu_row(gi.rt_reflect_asan SHIP CLASS engine COMMAND test_rt_reflect_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.rt_reflect_asan PROPERTIES
                         TIMEOUT 180 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    set_tests_properties(gi.rt_reflect_asan PROPERTIES JAHSHAKA_TEST_FRAMES 1435)
endif()


# rtreflect.history_detectors — THE REFLECTION HISTORY'S TWO DETECTORS (PHOTON-5 stage 4): a lamp step reaches a
# still glossy floor's reflection within 2 x kWarmupFrames frames (the radiance cap); a 0.25 m slide leaves no trail (a guard).
add_executable(test_rtreflect_history_detectors test_rtreflect_history_detectors.cpp)
set_target_properties(test_rtreflect_history_detectors PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_rtreflect_history_detectors PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_rtreflect_history_detectors PRIVATE JahshakaEngine)
add_dependencies(test_rtreflect_history_detectors JahshakaEngineMedia)
jah_gpu_row(rtreflect.history_detectors CLASS engine COMMAND test_rtreflect_history_detectors
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(rtreflect.history_detectors PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(rtreflect.history_detectors PROPERTIES JAHSHAKA_TEST_FRAMES 578)
jah_ship_row(rtreflect.history_detectors)   # THE SHIP SET: PHOTON-5's end-to-end row (the lead's promotion at the merge)

# rtreflect.still_after_stop (PHOTON-2 stage 1 and its fix round, rev 6's bars) — THE GLASS IS STILL AFTER A STOP:
# jahlab's stillness door on photon-glass after a walk inside one lattice cell and the path's teleport at equal
# placements — (b) 0 codes from the hold, (c) the mirror's estimator fixed, (d) walk vs teleport on the glossy panes,
# (e) a small lamp step held at rest against a session converged at the new light; (a) and (f) are PRINTED as pending
# R1 (stage 1's round 2), (d)'s mirror as stage 5's (still_after_stop.sh says the law and the base). 960x540; three
# jahlab sessions: 231 frames to the stop, 230 a window, the path's teleport (697 in all); 601 and 601 frames.
if(TARGET jahlab)
    jah_gpu_row(rtreflect.still_after_stop CLASS engine FRAMES 1899
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/still_after_stop.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/still_after_stop
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.still_after_stop PROPERTIES TIMEOUT 60 LABELS "engine")
endif()
