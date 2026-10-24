# FRAMES (TESTING-SPEED-B1, 2026-10-12): every row here states JAHSHAKA_TEST_FRAMES — the frames its process(es) render,
# COUNTED on the Debug tree (an LD_PRELOAD count of Ogre::Root::_updateAllRenderTargets per row; an _asan twin = its
# twin's count; 0 = no engine frame): spikes/testing-speed-b1/frames.json, framecount/.
# The LIGHTING_FIX acceptance suites — headless, framework-free, engine only.
# Same runtime requirements as tests/engine (a reachable DISPLAY, a Vulkan
# driver or lavapipe).
#
# TWO BINARIES, deliberately. lights.falloff measures the light's own geometry
# (attenuation range, spot cone) with an ambient of zero; lights.hygiene (archived: archive/tests/lights)
# measures the shadow atlas and the shader-compile counters, which need a warm
# cache and a scene that has already rendered. Neither wants the other's setup,
# and the compile counters are process-wide.

add_executable(test_light_falloff test_light_falloff.cpp)
set_target_properties(test_light_falloff PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_light_falloff PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_light_falloff PRIVATE JahshakaEngine)
add_dependencies(test_light_falloff JahshakaEngineMedia)
jah_gpu_row(lights.falloff SHIP CLASS engine COMMAND test_light_falloff
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.falloff PROPERTIES TIMEOUT 60 LABELS "engine")
set_tests_properties(lights.falloff PROPERTIES JAHSHAKA_TEST_FRAMES 54)

