
# engine.lod_rule_parity (a SHIP row since ATOM-5) — the quality currency's C++ half against its GLSL half,
# over 10,000 evaluations of the REAL cull shader on the device.
# ATOM stage 2 (lane ATOM-CLUSTER-1) added the FOURTH copy — the cluster cut's C++
# rule against the product GLSL piece JahClusterCut.glsl, over the shipped meshes'
# baked DAGs. That arm needs the bake (IrisGL), the mirror's hand-off, the
# cluster fixtures and the harness's device runner (tests/atom/cluster_*), and the
# harness reaches EnginePrivate.h and links Ogre, as tests/shadow's spike does.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
# The suite's own TU names a local `slots` (the GPU table's slot count); with Qt's
# headers in it now, Qt's `slots` keyword macro would eat it.
set_source_files_properties(test_lod_rule_parity.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)

# chain.hzb — the closest-depth pyramid: shape, the reduction texel by texel,
# and its GPU cost at 1920x1080 through fork 1a81f866a+1bccc3f93 (was 0027)'s timestamps.
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
jah_gpu_row(chain.hzb SHIP CLASS engine COMMAND test_hzb
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(chain.hzb PROPERTIES TIMEOUT 120 LABELS "engine")

# engine.lod_rule_parity — REVIVED BY ATOM-5 (archived at 163d90223) and PROMOTED INTO THE SHIP SET at its merge: the cut's
# C++ twin and the device's rule are held equal here (a change to one side without the other reds it — the subsystem's guard);
# the quality currency's C++ half against its GLSL half, and since ATOM-5 the cluster cut's DENSITY CAP (caps 1-4)
# in the fourth copy (the GLSL piece over the shipped meshes' DAGs) and the fifth (the product's cut job), the cut
# with the cap still ONE cut (no drawn cluster under another, every level-0 triangle covered once).
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
add_dependencies(test_lod_rule_parity JahshakaEngineMedia)
jah_gpu_row(engine.lod_rule_parity SHIP CLASS engine FRAMES 15 COMMAND test_lod_rule_parity
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.lod_rule_parity PROPERTIES TIMEOUT 60 LABELS "engine")

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "chain.hzb=68"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
