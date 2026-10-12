# Planar-reflection test — headless, framework-free, links JahshakaEngine ONLY.
# Pixel-proves that a reflector plane re-renders the scene into its own surface
# (PLANAR_REFLECTIONS_SPEC.md §8). Same runtime requirements as tests/engine
# (reachable DISPLAY, Vulkan driver or lavapipe).
#
# The ASan copy is not optional decoration here: ~PlanarReflections leaves
# HlmsPbs::mPlanarReflections stale and keeps raw Renderable pointers, so the
# teardown case at the end of the suite is exactly the shape a sanitiser catches
# and a plain run does not.
function(jahshaka_planar_test target)
    add_executable(${target} test_planar.cpp)
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

jahshaka_planar_test(test_planar)
target_link_libraries(test_planar PRIVATE JahshakaEngine)
jah_gpu_row(engine.planar CLASS engine COMMAND test_planar
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.planar PROPERTIES TIMEOUT 120 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_planar_test(test_planar_asan)
    target_link_libraries(test_planar_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_planar_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_planar_asan PRIVATE -fsanitize=address)
    jah_gpu_row(engine.planar_asan CLASS engine COMMAND test_planar_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Same reason as tests/engine: the only leaks LSan finds here are libdbus-1
    # allocations reached from the Vulkan loader's Mesa device-select layer.
    # STAGE-CLOSE (TESTING-SPEED-B3): the ASAN twin keeps its row at the stage close; engine.planar is
    # the merge tier's.
    set_tests_properties(engine.planar_asan PROPERTIES TIMEOUT 120 LABELS "engine;stage-close"

                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "engine.planar=33"
        "engine.planar_asan=33"   # its plain twin's
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
