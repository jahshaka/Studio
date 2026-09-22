# ATOM P3's SUBSTRATE and the depth pyramid (SPECS/atom/A4_SUBSTRATE_CULL_DESIGN.md;
# SPECS/NANITE_SPEC.md §4.2-§4.3).
# Every suite here boots a real engine with no scene and no frame: they measure GPU
# machinery, not a picture. Same runtime requirements as tests/engine (a
# reachable DISPLAY — a Vulkan device on this pin cannot be created without one
# — and a Vulkan driver or lavapipe).

# engine.gpu_cull — ATOM P3's substrate: the cull, the compaction and the
# GPU-sized indirect chain over the GPU scene, with a CPU cull as the reference.
# (It replaces compute.indirect_dispatch, whose three proof jobs this lane
# deleted: the same claim is asserted here with real work behind it.)
add_executable(test_gpu_cull test_gpu_cull.cpp)
set_target_properties(test_gpu_cull PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_gpu_cull PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gpu_cull PRIVATE JahshakaEngine)
add_dependencies(test_gpu_cull JahshakaEngineMedia)
add_test(NAME engine.gpu_cull COMMAND test_gpu_cull
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpu_cull PROPERTIES TIMEOUT 300 LABELS "engine")

# engine.lod_rule_parity — the quality currency's C++ half against its GLSL half,
# over 10,000 evaluations of the REAL cull shader on the device.
add_executable(test_lod_rule_parity test_lod_rule_parity.cpp)
set_target_properties(test_lod_rule_parity PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_lod_rule_parity PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_lod_rule_parity PRIVATE JahshakaEngine)
add_dependencies(test_lod_rule_parity JahshakaEngineMedia)
add_test(NAME engine.lod_rule_parity COMMAND test_lod_rule_parity
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.lod_rule_parity PROPERTIES TIMEOUT 180 LABELS "engine")

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
