# ATOM P3's SUBSTRATE and the depth pyramid (SPECS/atom/A4_SUBSTRATE_CULL_DESIGN.md;
# SPECS/NANITE_SPEC.md §4.2-§4.3).
# Every suite here boots a real engine with no scene and no frame: they measure GPU
# machinery, not a picture. Same runtime requirements as tests/engine (a
# reachable DISPLAY — a Vulkan device on this pin cannot be created without one
# — and a Vulkan driver or lavapipe).

# engine.gpu_cull — ATOM P3's substrate: the cull, the compaction and the
# GPU-sized indirect chain over the GPU scene, with a CPU cull as the reference.
# (It replaces compute.indirect_dispatch, whose three proof jobs this lane
# deleted: the same claim — a dispatch sized by a count a compute shader wrote —
# is asserted here over real instances, at an empty set, an arbitrary small one
# and the whole table.)
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
# ATOM stage 2 (lane ATOM-CLUSTER-1) added the FOURTH copy — the cluster cut's C++
# rule against the product GLSL piece JahClusterCut.glsl, over the shipped meshes'
# baked DAGs. That arm needs the bake (IrisGL), the mirror's hand-off, the
# cluster fixtures and the harness's device runner (tests/atom/cluster_*), and the
# harness reaches EnginePrivate.h and links Ogre, as tests/shadow's spike does.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_lod_rule_parity test_lod_rule_parity.cpp
    ${CMAKE_SOURCE_DIR}/tests/atom/cluster_draw.cpp
    ${CMAKE_SOURCE_DIR}/tests/atom/cluster_fixtures.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_lod_rule_parity PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_lod_rule_parity PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src
    ${CMAKE_SOURCE_DIR}/tests/atom
    "${CMAKE_SOURCE_DIR}/irisgl/engine/src" "${JAHSHAKA_ENGINE_BUILD_ID_DIR}")
target_compile_definitions(test_lod_rule_parity PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/"
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CLUSTER_FIXTURE_DIR="${CMAKE_BINARY_DIR}/tests/atom/cluster-fixtures"
    CLUSTER_TEST_MEDIA_DIR="${CMAKE_SOURCE_DIR}/tests/atom/media")
target_link_libraries(test_lod_rule_parity PRIVATE IrisGL JahshakaEngine OgreNext Qt6::Gui Qt6::OpenGL)
# The suite's own TU names a local `slots` (the GPU table's slot count); with Qt's
# headers in it now, Qt's `slots` keyword macro would eat it.
set_source_files_properties(test_lod_rule_parity.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
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
