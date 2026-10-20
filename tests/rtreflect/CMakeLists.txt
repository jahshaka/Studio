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

