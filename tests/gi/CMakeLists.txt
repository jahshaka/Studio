# Global illumination test — headless, framework-free, links JahshakaEngine ONLY.
# Proves Instant Radiosity, VCT and the VCT+PCC hybrid end to end: a red wall
# lit by a near-horizontal directional light must bounce red onto a floor the
# direct light only grazes, and turning GI off must restore the original pixels.
# Same runtime requirements as tests/engine (reachable DISPLAY, Vulkan driver
# or lavapipe).
add_executable(test_gi test_gi.cpp)
set_target_properties(test_gi PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi PRIVATE JahshakaEngine)
add_dependencies(test_gi JahshakaEngineMedia)
add_test(NAME gi.modes COMMAND test_gi
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.modes PROPERTIES TIMEOUT 600 LABELS "engine")

# The REAL PCC gate (REFLECTIONS_ADOPTION_SPEC.md §3): a closed room, a red wall
# behind the camera and a roughness-0 metal box whose reflection must contain
# that wall — plus Scene::giStatus() assertions, so buildPcc's silent
# "hybrid renders as plain VCT" bail fails the suite instead of passing it.
# Its OWN binary rather than a case in test_gi: the hybrid binds
# process-wide HlmsPbs state (sVctBindingOwner), so this scene must not share a
# process with gi.modes' scene (spec §9).
add_executable(test_gi_pcc_mirror test_gi_pcc_mirror.cpp)
set_target_properties(test_gi_pcc_mirror PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_pcc_mirror PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_pcc_mirror PRIVATE JahshakaEngine)
add_dependencies(test_gi_pcc_mirror JahshakaEngineMedia)
add_test(NAME gi.pcc_mirror COMMAND test_gi_pcc_mirror
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_mirror PROPERTIES TIMEOUT 600 LABELS "engine")

# GI BOUNDS + PROBE PLACEMENT (spec §4 / P1a). Its own binary for the same
# process-wide-binding reason as gi.pcc_mirror.
add_executable(test_gi_pcc_bounds test_gi_pcc_bounds.cpp)
set_target_properties(test_gi_pcc_bounds PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_pcc_bounds PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_pcc_bounds PRIVATE JahshakaEngine)
add_dependencies(test_gi_pcc_bounds JahshakaEngineMedia)
add_test(NAME gi.pcc_bounds COMMAND test_gi_pcc_bounds
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.pcc_bounds PROPERTIES TIMEOUT 600 LABELS "engine")

# gi.cliff — THE GI-VOLUME CLIFF (LIGHTING_FIX fix 1) plus the engine half of
# the re-fit-on-exit contract (fix 2). Its own binary for the same
# process-wide-HlmsPbs-binding reason as its siblings.
add_executable(test_gi_cliff test_gi_cliff.cpp)
set_target_properties(test_gi_cliff PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_cliff PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_cliff PRIVATE JahshakaEngine)
add_dependencies(test_gi_cliff JahshakaEngineMedia)
add_test(NAME gi.cliff COMMAND test_gi_cliff
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.cliff PROPERTIES TIMEOUT 600 LABELS "engine")

# gi.ambient — THE VCT AMBIENT (LIGHTING_FIX fix 3). Own binary: process-wide
# HlmsPbs binding, same rule as its siblings.
add_executable(test_gi_ambient test_gi_ambient.cpp)
set_target_properties(test_gi_ambient PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gi_ambient PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_ambient PRIVATE JahshakaEngine)
add_dependencies(test_gi_ambient JahshakaEngineMedia)
add_test(NAME gi.ambient COMMAND test_gi_ambient
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.ambient PROPERTIES TIMEOUT 600 LABELS "engine")

# gi.coalesce — the drag-a-light gate (spec §5 / P2). Links the MIRROR as well
# as the engine (like tests/mirror) because the coalescing gate is mirror-side
# and the cheap light-only path is engine-side; the contract is the pair.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_gi_coalesce test_gi_coalesce.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_gi_coalesce PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gi_coalesce PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gi_coalesce PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_coalesce PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_gi_coalesce JahshakaEngineMedia)
add_test(NAME gi.coalesce COMMAND test_gi_coalesce
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.coalesce PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 600 LABELS "engine")

# gi.overlay — THE GI VOLUME OVERLAY (LIGHTING_FIX fix 9). Mirror-linked for the
# same reason gi.coalesce is: the overlay is mirror-side, the helper channel it
# rides is engine-side, and the contract is the pair.
add_executable(test_gi_overlay test_gi_overlay.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_gi_overlay PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gi_overlay PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gi_overlay PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gi_overlay PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_gi_overlay JahshakaEngineMedia)
add_test(NAME gi.overlay COMMAND test_gi_overlay
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(gi.overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 600 LABELS "engine")
