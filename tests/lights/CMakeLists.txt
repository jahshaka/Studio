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
set_tests_properties(lights.falloff PROPERTIES TIMEOUT 120 LABELS "engine")

# lights.masks is the RUNTIME half of build-ogre.sh's
# OGRE_CONFIG_ENABLE_FINE_LIGHT_MASK_GRANULARITY guard: with the flag dropped
# the whole feature is compiled out of Ogre, both setters become no-ops, and
# only a pixel measurement notices.
add_executable(test_light_masks test_light_masks.cpp)
set_target_properties(test_light_masks PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_light_masks PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_light_masks PRIVATE JahshakaEngine)
add_dependencies(test_light_masks JahshakaEngineMedia)
add_test(NAME lights.masks COMMAND test_light_masks
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.masks PROPERTIES TIMEOUT 120 LABELS "engine")

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
set_tests_properties(lights.hygiene PROPERTIES TIMEOUT 120 LABELS "engine")

# lights.icons — the 2026-09-08 icon fix: an editor helper billboard draws in
# the overlay pass (after the post chain, no depth test) instead of being
# tonemapped with the scene, stays out of reflections, and gets a mip chain.
add_executable(test_light_icons test_light_icons.cpp)
set_target_properties(test_light_icons PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_light_icons PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_light_icons PRIVATE JahshakaEngine)
add_dependencies(test_light_icons JahshakaEngineMedia)
add_test(NAME lights.icons COMMAND test_light_icons
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.icons PROPERTIES TIMEOUT 120 LABELS "engine")

# lights.sealed_room — THE OWNER'S CASE IN PIXELS (SUN_AND_LIGHT_DEFAULTS §0):
# a lamp above the roof of a sealed room lit the floor through it, because the
# light every new scene shipped with was explicitly told not to cast. The floor
# is dark with the document's DEFAULT (soft shadows), bright when the author
# switches them off, and bright again with the roof removed (the control).
add_executable(test_sealed_room test_sealed_room.cpp)
set_target_properties(test_sealed_room PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_sealed_room PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_sealed_room PRIVATE JahshakaEngine)
add_dependencies(test_sealed_room JahshakaEngineMedia)
add_test(NAME lights.sealed_room COMMAND test_sealed_room
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.sealed_room PROPERTIES TIMEOUT 120 LABELS "engine")

# lights.secondary_directional — ONLY THE SUN CASTS (SUN_AND_LIGHT_DEFAULTS
# §3.3). Two directional lights rolled opposite ways: the side the shadow lands
# on says which one owns the node's single directional slot, and swapping the
# priority swaps the side, in one process, ten times over.
add_executable(test_secondary_directional test_secondary_directional.cpp)
set_target_properties(test_secondary_directional PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_secondary_directional PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_secondary_directional PRIVATE JahshakaEngine)
add_dependencies(test_secondary_directional JahshakaEngineMedia)
add_test(NAME lights.secondary_directional COMMAND test_secondary_directional
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.secondary_directional PROPERTIES TIMEOUT 120 LABELS "engine")
