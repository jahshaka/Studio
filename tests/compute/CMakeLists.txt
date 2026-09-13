# PHOTON SHARED INFRASTRUCTURE, proof half (SPECS/NANITE_SPEC.md §4.2-§4.3).
# Both suites boot a real engine with no scene and no frame: they measure GPU
# machinery, not a picture. Same runtime requirements as tests/engine (a
# reachable DISPLAY — a Vulkan device on this pin cannot be created without one
# — and a Vulkan driver or lavapipe).

# compute.indirect_dispatch — ogre-patch 0032: a compute job sized by the GPU.
add_executable(test_indirect_dispatch test_indirect_dispatch.cpp)
set_target_properties(test_indirect_dispatch PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_indirect_dispatch PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_indirect_dispatch PRIVATE JahshakaEngine)
add_dependencies(test_indirect_dispatch JahshakaEngineMedia)
add_test(NAME compute.indirect_dispatch COMMAND test_indirect_dispatch
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(compute.indirect_dispatch PROPERTIES TIMEOUT 120 LABELS "engine")

# chain.hzb — the closest-depth pyramid: shape, the reduction texel by texel,
# and its GPU cost at 1920x1080 through patch 0027's timestamps.
add_executable(test_hzb test_hzb.cpp)
set_target_properties(test_hzb PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_hzb PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_hzb PRIVATE JahshakaEngine)
add_dependencies(test_hzb JahshakaEngineMedia)
add_test(NAME chain.hzb COMMAND test_hzb
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(chain.hzb PROPERTIES TIMEOUT 180 LABELS "engine")
