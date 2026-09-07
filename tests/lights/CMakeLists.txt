# The LIGHTING_FIX acceptance suites — headless, framework-free, engine only.
# Same runtime requirements as tests/engine (a reachable DISPLAY, a Vulkan
# driver or lavapipe).
#
# TWO BINARIES, deliberately. lights.falloff measures the light's own geometry
# (attenuation range, spot cone) with an ambient of zero; lights.hygiene
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
add_test(NAME lights.falloff COMMAND test_light_falloff
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.falloff PROPERTIES TIMEOUT 600 LABELS "engine")

add_executable(test_light_hygiene test_light_hygiene.cpp)
set_target_properties(test_light_hygiene PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_light_hygiene PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_light_hygiene PRIVATE JahshakaEngine)
add_dependencies(test_light_hygiene JahshakaEngineMedia)
add_test(NAME lights.hygiene COMMAND test_light_hygiene
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.hygiene PROPERTIES TIMEOUT 600 LABELS "engine")
